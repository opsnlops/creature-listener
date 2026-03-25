#pragma once

#include <mutex>
#include <string>
#include <vector>

namespace creatures {

struct ConversationMessage {
    std::string role;     // "user" or "assistant"
    std::string content;
};

/// Thread-safe conversation history with a configurable maximum number of exchanges.
/// An "exchange" is a user message + assistant response pair.
class ConversationHistory {
public:
    explicit ConversationHistory(int maxExchanges = 10);

    /// Add a user + assistant exchange.
    void addExchange(const std::string& userMessage,
                     const std::string& assistantMessage);

    /// Get all messages in chronological order.
    std::vector<ConversationMessage> allMessages() const;

    /// Clear all history.
    void clear();

    /// Get the current number of exchanges.
    int size() const;

private:
    mutable std::mutex mutex_;
    std::vector<ConversationMessage> messages_;
    int maxExchanges_;
};

}  // namespace creatures
