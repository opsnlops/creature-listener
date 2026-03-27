#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace creatures {

struct HomeAssistantEntity {
    std::string entityId;     // e.g. "sensor.outside_temperature"
    std::string description;  // e.g. "Outside temperature"
};

struct HomeAssistantConfig {
    std::string url;     // e.g. "http://homeassistant.local:8123"
    std::string apiKey;  // Long-lived access token
    std::vector<HomeAssistantEntity> entities;
};

struct Configuration {
    // General
    bool verbose = false;

    // Audio
    int audioDeviceIndex = -1;  // -1 = default device
    bool listDevices = false;

    // Wake word (LOWWI / openWakeWord)
    std::string wakeWordModelPath;   // Path to wake word classifier .onnx
    std::string melModelPath;        // Path to melspectrogram.onnx
    std::string embeddingModelPath;  // Path to embedding_model.onnx
    float wakeWordThreshold = 0.5f;

    // STT — if useServerStt is true, audio is sent to creature-server for
    // transcription (fast). Otherwise, whisper runs locally on the Pi (slow).
    bool useServerStt = true;
    std::string whisperModelPath = "/usr/share/creature-listener/data/ggml-tiny.en-q5_1.bin";

    // VAD
    std::string vadModelPath = "/usr/share/creature-listener/data/silero_vad.onnx";
    float vadThreshold = 0.5f;
    int silenceDurationMs = 1500;
    int maxRecordSeconds = 15;

    // LLM (llama-server)
    std::string llmHost = "10.69.66.4";
    int llmPort = 1234;
    std::string llmModel = "mistral-nemo";
    std::string llmSystemPrompt;  // No default — must be set in config file
    float llmTemperature = 1.2f;
    int llmMaxTokens = 256;
    int minSentenceChars = 50;

    // Conversation history
    int maxConversationExchanges = 10;

    // Tracing (OpenTelemetry)
    std::string honeycombApiKey;
    std::string honeycombDataset = "creature-listener";

    // Creature server
    std::string creatureServerUrl = "https://server.prod.chirpchirp.dev";
    std::string creatureId;
    bool resumePlaylist = true;

    // Home Assistant
    HomeAssistantConfig homeAssistant;
};

}  // namespace creatures
