#include "server/CreatureServerClient.h"

#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include "util/namespace-stuffs.h"

using json = nlohmann::json;

namespace creatures {

namespace {

/// Strip invalid UTF-8 bytes from a string.
/// The LLM sometimes outputs partial emoji sequences that break JSON serialization.
std::string sanitizeUtf8(const std::string& input) {
    std::string result;
    result.reserve(input.size());

    size_t i = 0;
    while (i < input.size()) {
        unsigned char c = static_cast<unsigned char>(input[i]);

        int expectedLen = 0;
        if (c < 0x80) {
            expectedLen = 1;
        } else if ((c & 0xE0) == 0xC0) {
            expectedLen = 2;
        } else if ((c & 0xF0) == 0xE0) {
            expectedLen = 3;
        } else if ((c & 0xF8) == 0xF0) {
            expectedLen = 4;
        } else {
            // Invalid leading byte — skip it
            i++;
            continue;
        }

        // Check that we have enough continuation bytes
        if (i + expectedLen > input.size()) {
            break;  // Truncated sequence at end
        }

        bool valid = true;
        for (int j = 1; j < expectedLen; j++) {
            if ((static_cast<unsigned char>(input[i + j]) & 0xC0) != 0x80) {
                valid = false;
                break;
            }
        }

        if (valid) {
            result.append(input, i, expectedLen);
            i += expectedLen;
        } else {
            i++;  // Skip invalid byte
        }
    }

    return result;
}

}  // anonymous namespace

CreatureServerClient::CreatureServerClient(const std::string& baseUrl)
    : baseUrl_(baseUrl) {
    // Strip trailing slash if present
    if (!baseUrl_.empty() && baseUrl_.back() == '/') {
        baseUrl_.pop_back();
    }
    info("Creature server client: {}", baseUrl_);
}

namespace {

size_t writeCallback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* response = static_cast<std::string*>(userdata);
    response->append(ptr, size * nmemb);
    return size * nmemb;
}

}  // anonymous namespace

std::string CreatureServerClient::post(const std::string& path,
                                        const std::string& jsonBody,
                                        const std::string& traceparent) {
    std::string url = baseUrl_ + path;

    CURL* curl = curl_easy_init();
    if (!curl) {
        error("Failed to initialize libcurl");
        return "";
    }

    std::string response;

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    if (!traceparent.empty()) {
        std::string tpHeader = "traceparent: " + traceparent;
        headers = curl_slist_append(headers, tpHeader.c_str());
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, jsonBody.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);

    CURLcode res = curl_easy_perform(curl);

    long httpCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        error("HTTP POST {} failed: {}", path, curl_easy_strerror(res));
        return "";
    }

    if (httpCode < 200 || httpCode >= 300) {
        error("HTTP POST {} returned status {}: {}", path, httpCode, response);
        return "";
    }

    return response;
}

std::string CreatureServerClient::startSession(const std::string& creatureId,
                                                bool resumePlaylist,
                                                const std::string& traceparent) {
    json body = {
        {"creature_id", creatureId},
        {"resume_playlist", resumePlaylist}
    };

    info("Starting streaming session for creature {}", creatureId);

    std::string response = post("/api/v1/animation/ad-hoc-stream/start",
                                body.dump(), traceparent);
    if (response.empty()) {
        return "";
    }

    try {
        auto j = json::parse(response);
        std::string sessionId = j["session_id"].get<std::string>();
        std::string message = j.value("message", "");
        info("Streaming session started: {} — {}", sessionId, message);
        return sessionId;
    } catch (const json::exception& e) {
        error("Failed to parse start session response: {}", e.what());
        return "";
    }
}

bool CreatureServerClient::addText(const std::string& sessionId,
                                    const std::string& text,
                                    const std::string& traceparent) {
    auto cleanText = sanitizeUtf8(text);

    json body = {
        {"session_id", sessionId},
        {"text", cleanText}
    };

    debug("Sending text to session {}: \"{}\"", sessionId, cleanText);

    std::string response = post("/api/v1/animation/ad-hoc-stream/text",
                                body.dump(), traceparent);
    if (response.empty()) {
        return false;
    }

    try {
        auto j = json::parse(response);
        int chunks = j.value("chunks_received", 0);
        debug("Session {} — {} chunks received", sessionId, chunks);
        return true;
    } catch (const json::exception& e) {
        error("Failed to parse add text response: {}", e.what());
        return false;
    }
}

bool CreatureServerClient::finishSession(const std::string& sessionId,
                                          const std::string& traceparent) {
    json body = {
        {"session_id", sessionId}
    };

    info("Finishing streaming session {}", sessionId);

    std::string response = post("/api/v1/animation/ad-hoc-stream/finish",
                                body.dump(), traceparent);
    if (response.empty()) {
        return false;
    }

    try {
        auto j = json::parse(response);
        std::string message = j.value("message", "");
        bool playbackTriggered = j.value("playback_triggered", false);
        info("Session {} finished — {} (playback: {})",
             sessionId, message, playbackTriggered);
        return true;
    } catch (const json::exception& e) {
        error("Failed to parse finish session response: {}", e.what());
        return false;
    }
}

std::string CreatureServerClient::transcribe(const std::vector<float>& audioData,
                                              const std::string& traceparent) {
    std::string url = baseUrl_ + "/api/v1/stt/transcribe";

    float durationSec = static_cast<float>(audioData.size()) / 16000.0f;
    info("Sending {:.1f}s of audio to server for transcription ({} samples)...",
         durationSec, audioData.size());

    CURL* curl = curl_easy_init();
    if (!curl) {
        error("Failed to initialize libcurl for STT");
        return "";
    }

    std::string response;

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/octet-stream");
    if (!traceparent.empty()) {
        std::string tpHeader = "traceparent: " + traceparent;
        headers = curl_slist_append(headers, tpHeader.c_str());
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, audioData.data());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE,
                     static_cast<long>(audioData.size() * sizeof(float)));
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);

    CURLcode res = curl_easy_perform(curl);

    long httpCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        error("STT request failed: {}", curl_easy_strerror(res));
        return "";
    }

    if (httpCode < 200 || httpCode >= 300) {
        error("STT returned status {}: {}", httpCode, response);
        return "";
    }

    try {
        auto j = json::parse(response);
        std::string transcript = j.value("transcript", "");
        double timeMs = j.value("transcription_time_ms", 0.0);
        info("Server transcription in {:.0f}ms: \"{}\"", timeMs, transcript);
        return transcript;
    } catch (const json::exception& e) {
        error("Failed to parse STT response: {}", e.what());
        return "";
    }
}

}  // namespace creatures
