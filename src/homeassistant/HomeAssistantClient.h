#pragma once

#include <string>

namespace creatures {

/// Simple HTTP client for Home Assistant REST API.
/// Queries entity states using a long-lived access token.
class HomeAssistantClient {
public:
    HomeAssistantClient(const std::string& url, const std::string& apiKey);

    /// Get the state of an entity. Returns a human-readable string like
    /// "72.5 °F" or "locked". Returns empty string on failure.
    std::string getEntityState(const std::string& entityId);

private:
    std::string url_;
    std::string apiKey_;
};

}  // namespace creatures
