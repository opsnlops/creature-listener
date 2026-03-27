#include "llm/LLMClient.h"

#include <cctype>
#include <regex>
#include <string>

#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include "util/namespace-stuffs.h"
#include "util/utf8.h"

using json = nlohmann::json;

namespace creatures {

LLMClient::LLMClient(const std::string& host, int port, const std::string& model,
                     const std::string& systemPrompt, float temperature, int maxTokens,
                     int minSentenceChars, ConversationHistory& history,
                     std::shared_ptr<HomeAssistantClient> haClient,
                     std::vector<HomeAssistantEntity> haEntities)
    : host_(host),
      port_(port),
      model_(model),
      systemPrompt_(systemPrompt),
      temperature_(temperature),
      maxTokens_(maxTokens),
      minSentenceChars_(minSentenceChars),
      history_(history),
      haClient_(std::move(haClient)),
      haEntities_(std::move(haEntities)) {}

json LLMClient::buildToolsJson() const {
    if (!haClient_ || haEntities_.empty()) return json::array();

    // Build the enum of available entity IDs and a description string
    json entityEnum = json::array();
    std::string entityDescriptions;
    for (const auto& entity : haEntities_) {
        entityEnum.push_back(entity.entityId);
        if (!entityDescriptions.empty()) entityDescriptions += ", ";
        entityDescriptions += entity.description + " (" + entity.entityId + ")";
    }

    json tools = json::array();
    tools.push_back({
        {"type", "function"},
        {"function", {
            {"name", "get_home_state"},
            {"description", "Get the current state of something in the house. "
                            "Available: " + entityDescriptions},
            {"parameters", {
                {"type", "object"},
                {"properties", {
                    {"entity_id", {
                        {"type", "string"},
                        {"description", "The Home Assistant entity ID to query"},
                        {"enum", entityEnum}
                    }}
                }},
                {"required", json::array({"entity_id"})}
            }}
        }}
    });

    return tools;
}

namespace {

/// libcurl write callback — accumulates data into a string.
size_t simpleWriteCallback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* response = static_cast<std::string*>(userdata);
    response->append(ptr, size * nmemb);
    return size * nmemb;
}

/// Shared curl request setup
struct CurlRequest {
    std::string url;
    std::string body;
    struct curl_slist* headers = nullptr;

    CurlRequest(const std::string& url, const std::string& body)
        : url(url), body(body) {
        headers = curl_slist_append(headers, "Content-Type: application/json");
    }
    ~CurlRequest() { curl_slist_free_all(headers); }
};

}  // anonymous namespace

std::string LLMClient::doRequest(const json& messages, bool includeTools) {
    std::string url = fmt::format("http://{}:{}/v1/chat/completions", host_, port_);

    json body = {
        {"model", model_},
        {"messages", messages},
        {"temperature", temperature_},
        {"max_tokens", maxTokens_},
        {"stop", json::array({"\n\n\n", "\n\n"})},
        {"stream", false}
    };

    auto tools = buildToolsJson();
    if (includeTools && !tools.empty()) {
        body["tools"] = tools;
    }

    std::string bodyStr = body.dump();

    CURL* curl = curl_easy_init();
    if (!curl) return "";

    std::string response;
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, bodyStr.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, simpleWriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        error("LLM request failed: {}", curl_easy_strerror(res));
        return "";
    }

    return response;
}

LLMClient::StreamResult LLMClient::doStreamingRequest(
    const json& messages, bool includeTools, SentenceCallback onSentence) {

    std::string url = fmt::format("http://{}:{}/v1/chat/completions", host_, port_);

    json body = {
        {"model", model_},
        {"messages", messages},
        {"temperature", temperature_},
        {"max_tokens", maxTokens_},
        {"stop", json::array({"\n\n\n", "\n\n"})},
        {"stream", true}
    };

    auto tools = buildToolsJson();
    if (includeTools && !tools.empty()) {
        body["tools"] = tools;
    }

    std::string bodyStr = body.dump();
    debug("LLM request: {} messages, model={}, tools={}", messages.size(), model_,
          includeTools && !tools.empty() ? "yes" : "no");

    CURL* curl = curl_easy_init();
    if (!curl) {
        error("Failed to initialize libcurl");
        return {};
    }

    // State for SSE streaming
    StreamResult result;
    std::string sentenceBuffer;
    std::string lineBuffer;
    bool insideThinkTag = false;

    // Tool call accumulation
    std::string toolCallName;
    std::string toolCallArgs;
    std::string toolCallId;
    bool isToolCall = false;

    // Write callback as a lambda via a wrapper struct
    struct StreamCtx {
        std::string* sentenceBuffer;
        std::string* fullResponse;
        std::string lineBuffer;
        SentenceCallback onSentence;
        int sentenceCount = 0;
        int minSentenceChars;
        bool insideThinkTag = false;
        // Tool call state
        bool isToolCall = false;
        std::string toolCallName;
        std::string toolCallArgs;
        std::string toolCallId;
    };

    StreamCtx ctx;
    ctx.sentenceBuffer = &sentenceBuffer;
    ctx.fullResponse = &result.fullResponse;
    ctx.onSentence = onSentence;
    ctx.minSentenceChars = minSentenceChars_;

    auto writeCallback = [](char* ptr, size_t size, size_t nmemb, void* userdata) -> size_t {
        auto* ctx = static_cast<StreamCtx*>(userdata);
        size_t totalBytes = size * nmemb;
        ctx->lineBuffer.append(ptr, totalBytes);

        size_t pos;
        while ((pos = ctx->lineBuffer.find('\n')) != std::string::npos) {
            std::string line = ctx->lineBuffer.substr(0, pos);
            ctx->lineBuffer.erase(0, pos + 1);

            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.substr(0, 6) != "data: ") continue;

            std::string jsonStr = line.substr(6);
            if (jsonStr == "[DONE]") break;

            try {
                auto j = json::parse(jsonStr);
                if (!j.contains("choices") || j["choices"].empty()) continue;

                auto& choice = j["choices"][0];
                auto& delta = choice["delta"];

                // Check for tool calls
                if (delta.contains("tool_calls")) {
                    ctx->isToolCall = true;
                    auto& tc = delta["tool_calls"][0];
                    if (tc.contains("id") && !tc["id"].is_null()) {
                        ctx->toolCallId = tc["id"].get<std::string>();
                    }
                    if (tc.contains("function")) {
                        if (tc["function"].contains("name") && !tc["function"]["name"].is_null()) {
                            ctx->toolCallName += tc["function"]["name"].get<std::string>();
                        }
                        if (tc["function"].contains("arguments") && !tc["function"]["arguments"].is_null()) {
                            ctx->toolCallArgs += tc["function"]["arguments"].get<std::string>();
                        }
                    }
                    continue;
                }

                // Regular content
                if (!delta.contains("content") || delta["content"].is_null()) continue;
                std::string content = delta["content"].get<std::string>();

                for (char ch : content) {
                    if (ctx->insideThinkTag) {
                        ctx->sentenceBuffer->push_back(ch);
                        if (ctx->sentenceBuffer->ends_with("</think>")) {
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

                    if (ctx->sentenceBuffer->ends_with("<think>")) {
                        ctx->insideThinkTag = true;
                        continue;
                    }

                    // Sentence boundary detection
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

                            auto trimStart = sentence.find_first_not_of(" \t\"'\xE2\x80\x9C\xE2\x80\x9D");
                            auto trimEnd = sentence.find_last_not_of(" \t\"'\xE2\x80\x9C\xE2\x80\x9D");
                            if (trimStart != std::string::npos) {
                                sentence = sentence.substr(trimStart, trimEnd - trimStart + 1);
                            }

                            if (!sentence.empty()) {
                                if (static_cast<int>(sentence.size()) >= ctx->minSentenceChars) {
                                    ctx->sentenceCount++;
                                    *ctx->fullResponse += sentence + " ";
                                    if (ctx->onSentence) {
                                        ctx->onSentence(sentence, ctx->sentenceCount);
                                    }
                                    *ctx->sentenceBuffer = remainder;
                                }
                            } else {
                                *ctx->sentenceBuffer = remainder;
                            }
                        }
                    }
                }
            } catch (const json::exception& e) {
                // Silently skip unparseable SSE chunks
            }
        }

        return totalBytes;
    };

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, bodyStr.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,
        static_cast<size_t(*)(char*, size_t, size_t, void*)>(writeCallback));
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        error("LLM request failed: {}", curl_easy_strerror(res));
        return {};
    }

    // Flush remaining content
    if (!ctx.isToolCall) {
        auto trimStart = sentenceBuffer.find_first_not_of(" \t\n\r\"'\xE2\x80\x9C\xE2\x80\x9D");
        auto trimEnd = sentenceBuffer.find_last_not_of(" \t\n\r\"'\xE2\x80\x9C\xE2\x80\x9D");
        if (trimStart != std::string::npos) {
            std::string remaining = sentenceBuffer.substr(trimStart, trimEnd - trimStart + 1);
            if (!remaining.empty()) {
                ctx.sentenceCount++;
                result.fullResponse += remaining;
                if (onSentence) {
                    onSentence(remaining, ctx.sentenceCount);
                }
            }
        }
    }

    result.sentenceCount = ctx.sentenceCount;
    result.toolCallName = ctx.toolCallName;
    result.toolCallArgs = ctx.toolCallArgs;
    result.toolCallId = ctx.toolCallId;

    return result;
}

std::string LLMClient::respondStreaming(const std::string& prompt,
                                         SentenceCallback onSentence) {
    // Build messages array: system + history + user
    json messages = json::array();
    messages.push_back({{"role", "system"}, {"content", sanitizeUtf8(systemPrompt_)}});

    auto historyMessages = history_.allMessages();
    for (const auto& msg : historyMessages) {
        messages.push_back({{"role", msg.role}, {"content", sanitizeUtf8(msg.content)}});
    }
    messages.push_back({{"role", "user"}, {"content", sanitizeUtf8(prompt)}});

    if (!historyMessages.empty()) {
        info("Conversation history: {} previous messages included", historyMessages.size());
    }

    bool hasTools = haClient_ && !haEntities_.empty();

    // First request — streaming, with tools if available
    auto result = doStreamingRequest(messages, hasTools, onSentence);

    // Handle tool calls — execute and re-prompt (up to 3 rounds)
    int toolRounds = 0;
    while (!result.toolCallName.empty() && toolRounds < 3) {
        toolRounds++;
        info("LLM tool call: {}({})", result.toolCallName, result.toolCallArgs);

        std::string toolResult;
        if (result.toolCallName == "get_home_state" && haClient_) {
            try {
                auto args = json::parse(result.toolCallArgs);
                std::string entityId = args.value("entity_id", "");
                if (!entityId.empty()) {
                    toolResult = haClient_->getEntityState(entityId);
                    if (toolResult.empty()) {
                        toolResult = "Error: could not get state for " + entityId;
                    }
                    info("Tool result for {}: {}", entityId, toolResult);
                } else {
                    toolResult = "Error: no entity_id provided";
                }
            } catch (const json::exception& e) {
                toolResult = "Error: invalid tool arguments";
                error("Failed to parse tool call args: {}", e.what());
            }
        } else {
            toolResult = "Error: unknown tool " + result.toolCallName;
        }

        // Add the assistant's tool call and the tool result to messages.
        // Format the tool result as a regular assistant+user exchange that
        // llama-server can understand, since not all models handle the
        // OpenAI tool_calls/tool message format correctly.
        std::string toolSummary = fmt::format(
            "[Used get_home_state for {}. Result: {}]",
            result.toolCallArgs, toolResult);
        messages.push_back({{"role", "assistant"}, {"content", toolSummary}});
        messages.push_back({{"role", "user"},
            {"content", "Now respond to the original question using that information. "
                        "Be conversational and natural."}});

        debug("Re-prompting LLM with tool result: {}", toolSummary);

        // Re-prompt — stream the final answer (no tools to prevent loops)
        result = doStreamingRequest(messages, false, onSentence);
    }

    info("LLM streaming complete: {} sentences, {} chars{}",
         result.sentenceCount, result.fullResponse.size(),
         toolRounds > 0 ? fmt::format(" ({} tool calls)", toolRounds) : "");

    // Save to conversation history
    std::string cleanResponse = stripThinkTags(result.fullResponse);
    auto cStart = cleanResponse.find_first_not_of(" \t\n\r");
    auto cEnd = cleanResponse.find_last_not_of(" \t\n\r");
    if (cStart != std::string::npos) {
        cleanResponse = cleanResponse.substr(cStart, cEnd - cStart + 1);
    }
    if (!cleanResponse.empty()) {
        history_.addExchange(sanitizeUtf8(prompt), sanitizeUtf8(cleanResponse));
        info("Saved to history — user: \"{}...\" assistant: \"{}...\"",
             prompt.substr(0, 50), cleanResponse.substr(0, 50));
    } else {
        warn("Empty response — not saving to conversation history");
    }

    return result.fullResponse;
}

std::string LLMClient::stripThinkTags(const std::string& text) {
    std::regex thinkPattern("<think>[\\s\\S]*?</think>");
    return std::regex_replace(text, thinkPattern, "");
}

}  // namespace creatures
