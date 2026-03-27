#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json_fwd.hpp>

#include "config/Configuration.h"
#include "homeassistant/HomeAssistantClient.h"
#include "llm/ConversationHistory.h"

namespace creatures {

/// Callback invoked with each complete sentence from the LLM stream.
using SentenceCallback = std::function<void(const std::string& sentence, int sentenceIndex)>;

/// HTTP streaming client for llama-server (OpenAI-compatible API).
/// Sends chat completions with `stream: true`, parses SSE events,
/// accumulates tokens, and yields complete sentences via callback.
/// Supports tool use (function calling) for Home Assistant integration.
class LLMClient {
public:
    LLMClient(const std::string& host, int port, const std::string& model,
              const std::string& systemPrompt, float temperature, int maxTokens,
              int minSentenceChars, ConversationHistory& history,
              std::shared_ptr<HomeAssistantClient> haClient = nullptr,
              std::vector<HomeAssistantEntity> haEntities = {});

    /// Send a prompt to the LLM and stream sentences back via callback.
    /// If the LLM requests a tool call, executes it and re-prompts.
    /// Returns the full response text, or empty string on failure.
    std::string respondStreaming(const std::string& prompt,
                                SentenceCallback onSentence);

private:
    /// Strip <think>...</think> tags from text.
    static std::string stripThinkTags(const std::string& text);

    /// Build the tools JSON array for the LLM request.
    nlohmann::json buildToolsJson() const;

    /// Execute a non-streaming request and return the full response.
    /// Used for tool call follow-up (tool results → final answer).
    std::string doRequest(const nlohmann::json& messages,
                          bool includeTools);

    /// Stream a request and yield sentences via callback.
    /// Returns {fullResponse, toolCallJson} — toolCallJson is empty if
    /// the response was regular content, non-empty if the LLM wants a tool call.
    struct StreamResult {
        std::string fullResponse;
        std::string toolCallName;
        std::string toolCallArgs;
        std::string toolCallId;
        int sentenceCount = 0;
    };
    StreamResult doStreamingRequest(const nlohmann::json& messages,
                                     bool includeTools,
                                     SentenceCallback onSentence);

    std::string host_;
    int port_;
    std::string model_;
    std::string systemPrompt_;
    float temperature_;
    int maxTokens_;
    int minSentenceChars_;
    ConversationHistory& history_;
    std::shared_ptr<HomeAssistantClient> haClient_;
    std::vector<HomeAssistantEntity> haEntities_;
};

}  // namespace creatures
