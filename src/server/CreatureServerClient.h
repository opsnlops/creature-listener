#pragma once

#include <string>
#include <vector>

namespace creatures {

/// HTTP client for creature-server.
/// Handles streaming ad-hoc sessions (TTS + animation + playback)
/// and server-side speech-to-text transcription.
class CreatureServerClient {
public:
    /// Construct with base URL (e.g. "https://server.prod.chirpchirp.dev").
    explicit CreatureServerClient(const std::string& baseUrl);

    /// Start a streaming ad-hoc session for the given creature.
    /// Returns the session_id on success, or empty string on failure.
    /// If traceparent is provided, it's sent as a W3C traceparent header.
    std::string startSession(const std::string& creatureId, bool resumePlaylist,
                             const std::string& traceparent = "");

    /// Send a sentence to an active streaming session.
    /// Returns true on success.
    bool addText(const std::string& sessionId, const std::string& text,
                 const std::string& traceparent = "");

    /// Finish a streaming session (triggers TTS + animation + playback).
    /// Returns true on success.
    bool finishSession(const std::string& sessionId,
                       const std::string& traceparent = "");

    /// Transcribe audio on the server using whisper.cpp.
    /// Sends raw 16kHz mono float32 PCM audio and returns the transcribed text.
    /// Returns empty string on failure.
    std::string transcribe(const std::vector<float>& audioData,
                           const std::string& traceparent = "");

private:
    /// Perform a POST request and return the response body.
    /// Returns empty string on failure.
    std::string post(const std::string& path, const std::string& jsonBody,
                     const std::string& traceparent = "");

    std::string baseUrl_;
};

}  // namespace creatures
