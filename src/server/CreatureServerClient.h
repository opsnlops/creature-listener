#pragma once

#include <string>

namespace creatures {

/// HTTP client for creature-server streaming ad-hoc session endpoints.
/// Sends sentences from the LLM to creature-server for TTS + animation + playback.
class CreatureServerClient {
public:
    /// Construct with base URL (e.g. "https://server.prod.chirpchirp.dev").
    explicit CreatureServerClient(const std::string& baseUrl);

    /// Start a streaming ad-hoc session for the given creature.
    /// Returns the session_id on success, or empty string on failure.
    std::string startSession(const std::string& creatureId, bool resumePlaylist);

    /// Send a sentence to an active streaming session.
    /// Returns true on success.
    bool addText(const std::string& sessionId, const std::string& text);

    /// Finish a streaming session (triggers TTS + animation + playback).
    /// Returns true on success.
    bool finishSession(const std::string& sessionId);

private:
    /// Perform a POST request and return the response body.
    /// Returns empty string on failure.
    std::string post(const std::string& path, const std::string& jsonBody);

    std::string baseUrl_;
};

}  // namespace creatures
