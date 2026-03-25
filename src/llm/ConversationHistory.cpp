#include "llm/ConversationHistory.h"

#include "util/namespace-stuffs.h"

namespace creatures {

ConversationHistory::ConversationHistory(int maxExchanges)
    : maxExchanges_(maxExchanges) {}

void ConversationHistory::addExchange(const std::string& userMessage,
                                       const std::string& assistantMessage) {
    std::lock_guard<std::mutex> lock(mutex_);

    messages_.push_back({"user", userMessage});
    messages_.push_back({"assistant", assistantMessage});

    // Each exchange is 2 messages. Trim from the front if over limit.
    int maxMessages = maxExchanges_ * 2;
    while (static_cast<int>(messages_.size()) > maxMessages) {
        // Remove the oldest exchange (2 messages)
        messages_.erase(messages_.begin(), messages_.begin() + 2);
    }

    debug("Conversation history: {} exchanges ({} messages)",
          messages_.size() / 2, messages_.size());
}

std::vector<ConversationMessage> ConversationHistory::allMessages() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return messages_;
}

void ConversationHistory::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    messages_.clear();
    info("Conversation history cleared");
}

int ConversationHistory::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return static_cast<int>(messages_.size()) / 2;
}

}  // namespace creatures
