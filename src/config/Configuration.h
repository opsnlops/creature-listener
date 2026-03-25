#pragma once

#include <cstdint>
#include <string>

namespace creatures {

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

    // Whisper STT
    std::string whisperModelPath = "/usr/share/creature-listener/data/ggml-tiny.en.bin";

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
};

}  // namespace creatures
