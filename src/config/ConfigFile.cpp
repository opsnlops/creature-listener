#include "config/ConfigFile.h"

#include <yaml-cpp/yaml.h>

#include "util/namespace-stuffs.h"

namespace creatures {

bool loadConfigFile(const std::string& path, Configuration& config) {
    YAML::Node root;
    try {
        root = YAML::LoadFile(path);
    } catch (const YAML::Exception& e) {
        error("Failed to load config file {}: {}", path, e.what());
        return false;
    }

    info("Loading configuration from {}", path);

    // Creature
    if (root["creatureId"]) config.creatureId = root["creatureId"].as<std::string>();
    if (root["creatureServerUrl"]) config.creatureServerUrl = root["creatureServerUrl"].as<std::string>();
    if (root["resumePlaylist"]) config.resumePlaylist = root["resumePlaylist"].as<bool>();

    // LLM
    if (root["llmHost"]) config.llmHost = root["llmHost"].as<std::string>();
    if (root["llmPort"]) config.llmPort = root["llmPort"].as<int>();
    if (root["llmModel"]) config.llmModel = root["llmModel"].as<std::string>();
    if (root["llmTemperature"]) config.llmTemperature = root["llmTemperature"].as<float>();
    if (root["llmMaxTokens"]) config.llmMaxTokens = root["llmMaxTokens"].as<int>();
    if (root["llmSystemPrompt"]) config.llmSystemPrompt = root["llmSystemPrompt"].as<std::string>();
    if (root["minSentenceChars"]) config.minSentenceChars = root["minSentenceChars"].as<int>();
    if (root["conversationHistorySize"]) config.maxConversationExchanges = root["conversationHistorySize"].as<int>();

    // Audio
    if (root["audioDevice"]) config.audioDeviceIndex = root["audioDevice"].as<int>();

    // Wake word
    if (root["wakeWordModel"]) config.wakeWordModelPath = root["wakeWordModel"].as<std::string>();
    if (root["melModel"]) config.melModelPath = root["melModel"].as<std::string>();
    if (root["embeddingModel"]) config.embeddingModelPath = root["embeddingModel"].as<std::string>();
    if (root["wakeWordThreshold"]) config.wakeWordThreshold = root["wakeWordThreshold"].as<float>();

    // STT
    if (root["whisperModel"]) config.whisperModelPath = root["whisperModel"].as<std::string>();

    // VAD
    if (root["vadModel"]) config.vadModelPath = root["vadModel"].as<std::string>();
    if (root["vadThreshold"]) config.vadThreshold = root["vadThreshold"].as<float>();
    if (root["silenceDurationMs"]) config.silenceDurationMs = root["silenceDurationMs"].as<int>();
    if (root["maxRecordSeconds"]) config.maxRecordSeconds = root["maxRecordSeconds"].as<int>();

    // Tracing
    if (root["honeycombApiKey"]) config.honeycombApiKey = root["honeycombApiKey"].as<std::string>();
    if (root["honeycombDataset"]) config.honeycombDataset = root["honeycombDataset"].as<std::string>();

    return true;
}

}  // namespace creatures
