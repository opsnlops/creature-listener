#pragma once

#include <functional>
#include <string>
#include <vector>

#include "llm/ConversationHistory.h"

namespace creatures {

/// Callback invoked with each complete sentence from the LLM stream.
using SentenceCallback = std::function<void(const std::string& sentence, int sentenceIndex)>;

/// HTTP streaming client for llama-server (OpenAI-compatible API).
/// Sends chat completions with `stream: true`, parses SSE events,
/// accumulates tokens, and yields complete sentences via callback.
class LLMClient {
public:
    LLMClient(const std::string& host, int port, const std::string& model,
              const std::string& systemPrompt, float temperature, int maxTokens,
              int minSentenceChars, ConversationHistory& history);

    /// Send a prompt to the LLM and stream sentences back via callback.
    /// Returns the full response text, or empty string on failure.
    /// The callback is invoked on the calling thread for each complete sentence.
    std::string respondStreaming(const std::string& prompt,
                                SentenceCallback onSentence);

private:
    /// Find a sentence boundary in the buffer.
    /// Returns the index of the punctuation mark, or -1 if none found.
    int sentenceBoundaryIndex(const std::string& buffer) const;

    /// Strip <think>...</think> tags from text.
    static std::string stripThinkTags(const std::string& text);

    std::string host_;
    int port_;
    std::string model_;
    std::string systemPrompt_;
    float temperature_;
    int maxTokens_;
    int minSentenceChars_;
    ConversationHistory& history_;
};

}  // namespace creatures
