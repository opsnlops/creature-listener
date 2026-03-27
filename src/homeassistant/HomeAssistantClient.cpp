#include "homeassistant/HomeAssistantClient.h"

#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include "util/namespace-stuffs.h"

using json = nlohmann::json;

namespace creatures {

HomeAssistantClient::HomeAssistantClient(const std::string& url,
                                          const std::string& apiKey)
    : url_(url), apiKey_(apiKey) {
    // Strip trailing slash
    if (!url_.empty() && url_.back() == '/') {
        url_.pop_back();
    }
    info("Home Assistant client: {}", url_);
}

namespace {

size_t writeCallback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* response = static_cast<std::string*>(userdata);
    response->append(ptr, size * nmemb);
    return size * nmemb;
}

}  // anonymous namespace

std::string HomeAssistantClient::getEntityState(const std::string& entityId) {
    std::string requestUrl = url_ + "/api/states/" + entityId;

    CURL* curl = curl_easy_init();
    if (!curl) {
        error("Failed to initialize libcurl for HA request");
        return "";
    }

    std::string response;
    std::string authHeader = "Authorization: Bearer " + apiKey_;

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, authHeader.c_str());
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, requestUrl.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);

    CURLcode res = curl_easy_perform(curl);

    long httpCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        error("HA request failed for {}: {}", entityId, curl_easy_strerror(res));
        return "";
    }

    if (httpCode != 200) {
        error("HA returned status {} for {}: {}", httpCode, entityId, response);
        return "";
    }

    try {
        auto j = json::parse(response);
        std::string state = j.value("state", "unknown");
        std::string unit = j.value("attributes", json::object()).value("unit_of_measurement", "");
        std::string friendlyName = j.value("attributes", json::object()).value("friendly_name", entityId);

        std::string result = state;
        if (!unit.empty()) {
            result += " " + unit;
        }

        debug("HA entity {} ({}): {}", entityId, friendlyName, result);
        return result;
    } catch (const json::exception& e) {
        error("Failed to parse HA response for {}: {}", entityId, e.what());
        return "";
    }
}

}  // namespace creatures
