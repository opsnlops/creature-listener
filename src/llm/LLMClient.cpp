#include "llm/LLMClient.h"

#include <cctype>
#include <regex>
#include <sstream>
#include <string>

#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include "util/namespace-stuffs.h"

using json = nlohmann::json;

namespace creatures {

LLMClient::LLMClient(const std::string& host, int port, const std::string& model,
                     const std::string& systemPrompt, float temperature, int maxTokens,
                     int minSentenceChars, ConversationHistory& history)
    : host_(host),
      port_(port),
      model_(model),
      systemPrompt_(systemPrompt),
      temperature_(temperature),
      maxTokens_(maxTokens),
      minSentenceChars_(minSentenceChars),
      history_(history) {}

namespace {

/// libcurl write callback — accumulates data into a string.
struct CurlStreamContext {
    std::string* sentenceBuffer;
    std::string* fullResponse;
    std::string lineBuffer;
    SentenceCallback onSentence;
    const LLMClient* client;
    int sentenceCount;
    int minSentenceChars;
    bool insideThinkTag;
};

size_t streamWriteCallback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* ctx = static_cast<CurlStreamContext*>(userdata);
    size_t totalBytes = size * nmemb;

    ctx->lineBuffer.append(ptr, totalBytes);

    // Process complete lines
    size_t pos;
    while ((pos = ctx->lineBuffer.find('\n')) != std::string::npos) {
        std::string line = ctx->lineBuffer.substr(0, pos);
        ctx->lineBuffer.erase(0, pos + 1);

        // Remove trailing \r
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        // SSE format: lines starting with "data: "
        if (line.substr(0, 6) != "data: ") {
            continue;
        }

        std::string jsonStr = line.substr(6);

        // End of stream
        if (jsonStr == "[DONE]") {
            break;
        }

        // Parse the delta content
        try {
            auto j = json::parse(jsonStr);
            if (!j.contains("choices") || j["choices"].empty()) continue;

            auto& delta = j["choices"][0]["delta"];
            if (!delta.contains("content") || delta["content"].is_null()) continue;

            std::string content = delta["content"].get<std::string>();

            // Process each character
            for (char ch : content) {
                if (ctx->insideThinkTag) {
                    ctx->sentenceBuffer->push_back(ch);
                    if (ctx->sentenceBuffer->ends_with("</think>")) {
                        // Remove the entire think block
                        auto thinkStart = ctx->sentenceBuffer->find("<think>");
                        if (thinkStart != std::string::npos) {
                            *ctx->sentenceBuffer = ctx->sentenceBuffer->substr(0, thinkStart);
                        } else {
                            ctx->sentenceBuffer->clear();
                        }
                        ctx->insideThinkTag = false;
                    }
                    continue;
                }

                ctx->sentenceBuffer->push_back(ch);

                // Detect <think> tag
                if (ctx->sentenceBuffer->ends_with("<think>")) {
                    ctx->insideThinkTag = true;
                    continue;
                }

                // Check for sentence boundary (need at least 2 chars)
                if (ctx->sentenceBuffer->size() >= 2) {
                    size_t len = ctx->sentenceBuffer->size();
                    char last = (*ctx->sentenceBuffer)[len - 1];
                    char penult = (*ctx->sentenceBuffer)[len - 2];

                    auto isPunct = [](char c) {
                        return c == '.' || c == '!' || c == '?';
                    };
                    auto isNewSentenceStart = [](char c) {
                        return c == ' ' || std::isupper(static_cast<unsigned char>(c))
                               || c == '"';
                    };

                    bool boundary = false;
                    size_t splitAt = 0;

                    if (isPunct(penult) && isNewSentenceStart(last)) {
                        boundary = true;
                        splitAt = len - 2;
                    } else if (len >= 3) {
                        char threeBack = (*ctx->sentenceBuffer)[len - 3];
                        if (isPunct(threeBack) && (penult == '"' || penult == '\'')
                            && isNewSentenceStart(last)) {
                            boundary = true;
                            splitAt = len - 2;
                        }
                    }

                    if (boundary) {
                        std::string sentence = ctx->sentenceBuffer->substr(0, splitAt + 1);
                        std::string remainder = ctx->sentenceBuffer->substr(splitAt + 1);

                        // Trim whitespace and wrapping quotes
                        auto trimStart = sentence.find_first_not_of(" \t\"'\xE2\x80\x9C\xE2\x80\x9D");
                        auto trimEnd = sentence.find_last_not_of(" \t\"'\xE2\x80\x9C\xE2\x80\x9D");
                        if (trimStart != std::string::npos) {
                            sentence = sentence.substr(trimStart, trimEnd - trimStart + 1);
                        }

                        if (!sentence.empty()) {
                            if (static_cast<int>(sentence.size()) >= ctx->minSentenceChars) {
                                ctx->sentenceCount++;
                                *ctx->fullResponse += sentence + " ";
                                info("LLM sentence {}: \"{}\" ({} chars)",
                                     ctx->sentenceCount, sentence, sentence.size());
                                if (ctx->onSentence) {
                                    ctx->onSentence(sentence, ctx->sentenceCount);
                                }
                                *ctx->sentenceBuffer = remainder;
                            }
                            // else: too short, keep in buffer to merge with next
                        } else {
                            *ctx->sentenceBuffer = remainder;
                        }
                    }
                }
            }
        } catch (const json::exception& e) {
            debug("Failed to parse SSE JSON: {}", e.what());
        }
    }

    return totalBytes;
}

}  // anonymous namespace

std::string LLMClient::respondStreaming(const std::string& prompt,
                                         SentenceCallback onSentence) {
    std::string url = fmt::format("http://{}:{}/v1/chat/completions", host_, port_);

    // Build messages array: system + history + user
    json messages = json::array();
    messages.push_back({{"role", "system"}, {"content", systemPrompt_}});

    auto historyMessages = history_.allMessages();
    for (const auto& msg : historyMessages) {
        messages.push_back({{"role", msg.role}, {"content", msg.content}});
    }
    messages.push_back({{"role", "user"}, {"content", prompt}});

    json body = {
        {"model", model_},
        {"messages", messages},
        {"temperature", temperature_},
        {"max_tokens", maxTokens_},
        {"stop", json::array({"\n\n\n", "\n\n"})},
        {"stream", true}
    };

    std::string bodyStr = body.dump();
    debug("LLM request: {} messages, model={}", messages.size(), model_);

    CURL* curl = curl_easy_init();
    if (!curl) {
        error("Failed to initialize libcurl");
        return "";
    }

    std::string sentenceBuffer;
    std::string fullResponse;

    CurlStreamContext ctx;
    ctx.sentenceBuffer = &sentenceBuffer;
    ctx.fullResponse = &fullResponse;
    ctx.onSentence = std::move(onSentence);
    ctx.client = this;
    ctx.sentenceCount = 0;
    ctx.minSentenceChars = minSentenceChars_;
    ctx.insideThinkTag = false;

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, bodyStr.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, streamWriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);

    CURLcode res = curl_easy_perform(curl);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        error("LLM request failed: {}", curl_easy_strerror(res));
        return "";
    }

    // Flush remaining text in the buffer
    auto trimStart = sentenceBuffer.find_first_not_of(" \t\n\r\"'\xE2\x80\x9C\xE2\x80\x9D");
    auto trimEnd = sentenceBuffer.find_last_not_of(" \t\n\r\"'\xE2\x80\x9C\xE2\x80\x9D");
    if (trimStart != std::string::npos) {
        std::string remaining = sentenceBuffer.substr(trimStart, trimEnd - trimStart + 1);
        if (!remaining.empty()) {
            ctx.sentenceCount++;
            fullResponse += remaining;
            info("LLM sentence {} (final): \"{}\" ({} chars)",
                 ctx.sentenceCount, remaining, remaining.size());
            if (ctx.onSentence) {
                ctx.onSentence(remaining, ctx.sentenceCount);
            }
        }
    }

    info("LLM streaming complete: {} sentences, {} chars",
         ctx.sentenceCount, fullResponse.size());

    // Save to conversation history
    std::string cleanResponse = stripThinkTags(fullResponse);
    // Trim
    auto cStart = cleanResponse.find_first_not_of(" \t\n\r");
    auto cEnd = cleanResponse.find_last_not_of(" \t\n\r");
    if (cStart != std::string::npos) {
        cleanResponse = cleanResponse.substr(cStart, cEnd - cStart + 1);
    }
    if (!cleanResponse.empty()) {
        history_.addExchange(prompt, cleanResponse);
    }

    return fullResponse;
}

std::string LLMClient::stripThinkTags(const std::string& text) {
    std::regex thinkPattern("<think>[\\s\\S]*?</think>");
    return std::regex_replace(text, thinkPattern, "");
}

}  // namespace creatures
