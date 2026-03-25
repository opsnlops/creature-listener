#include "server/CreatureServerClient.h"

#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include "util/namespace-stuffs.h"

using json = nlohmann::json;

namespace creatures {

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
                                        const std::string& jsonBody) {
    std::string url = baseUrl_ + path;

    CURL* curl = curl_easy_init();
    if (!curl) {
        error("Failed to initialize libcurl");
        return "";
    }

    std::string response;

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");

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
                                                bool resumePlaylist) {
    json body = {
        {"creature_id", creatureId},
        {"resume_playlist", resumePlaylist}
    };

    info("Starting streaming session for creature {}", creatureId);

    std::string response = post("/api/v1/animation/ad-hoc-stream/start", body.dump());
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
                                    const std::string& text) {
    json body = {
        {"session_id", sessionId},
        {"text", text}
    };

    debug("Sending text to session {}: \"{}\"", sessionId, text);

    std::string response = post("/api/v1/animation/ad-hoc-stream/text", body.dump());
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

bool CreatureServerClient::finishSession(const std::string& sessionId) {
    json body = {
        {"session_id", sessionId}
    };

    info("Finishing streaming session {}", sessionId);

    std::string response = post("/api/v1/animation/ad-hoc-stream/finish", body.dump());
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

}  // namespace creatures
