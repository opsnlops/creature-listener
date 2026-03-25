#include "config/CommandLine.h"

#include <argparse/argparse.hpp>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>

#include "Version.h"
#include "util/namespace-stuffs.h"

namespace creatures {

std::unique_ptr<Configuration> parseCommandLine(int argc, char* argv[]) {
    argparse::ArgumentParser program(
        "creature-listener",
        fmt::format("{}.{}.{}", CREATURE_LISTENER_VERSION_MAJOR,
                    CREATURE_LISTENER_VERSION_MINOR,
                    CREATURE_LISTENER_VERSION_PATCH));

    program.add_description("Wake word conversational interface for Beaky");

    // Audio
    program.add_argument("--audio-device")
        .help("PortAudio device index (-1 for default)")
        .default_value(-1)
        .scan<'i', int>();

    program.add_argument("--list-devices")
        .help("List available audio devices and exit")
        .default_value(false)
        .implicit_value(true);

    program.add_argument("--verbose")
        .help("Enable verbose/debug logging")
        .default_value(false)
        .implicit_value(true);

    // Wake word (LOWWI / openWakeWord)
    program.add_argument("--wake-word-model")
        .help("Path to wake word classifier .onnx model (e.g. hey_beaky.onnx)");

    program.add_argument("--mel-model")
        .help("Path to melspectrogram.onnx (shared preprocessing model)");

    program.add_argument("--embedding-model")
        .help("Path to embedding_model.onnx (shared feature extraction model)");

    program.add_argument("--wake-word-threshold")
        .help("Wake word detection confidence threshold (0.0 - 1.0)")
        .default_value(0.5f)
        .scan<'g', float>();

    // Whisper
    program.add_argument("--whisper-model")
        .help("Path to whisper ggml model file")
        .default_value(std::string("/usr/share/creature-listener/data/ggml-tiny.en.bin"));

    // VAD
    program.add_argument("--vad-model")
        .help("Path to Silero VAD ONNX model")
        .default_value(std::string("/usr/share/creature-listener/data/silero_vad.onnx"));

    program.add_argument("--vad-threshold")
        .help("Silero VAD confidence threshold (0.0 - 1.0)")
        .default_value(0.5f)
        .scan<'g', float>();

    program.add_argument("--silence-duration-ms")
        .help("Milliseconds of silence before ending recording")
        .default_value(1500)
        .scan<'i', int>();

    program.add_argument("--max-record-seconds")
        .help("Maximum recording length in seconds")
        .default_value(15)
        .scan<'i', int>();

    // LLM
    program.add_argument("--llm-host")
        .help("llama-server hostname")
        .default_value(std::string("10.69.66.4"));

    program.add_argument("--llm-port")
        .help("llama-server port")
        .default_value(1234)
        .scan<'i', int>();

    program.add_argument("--llm-model")
        .help("Model name for llama-server")
        .default_value(std::string("mistral-nemo"));

    program.add_argument("--llm-system-prompt")
        .help("System prompt text or path to a file containing it");

    program.add_argument("--llm-temperature")
        .help("LLM temperature")
        .default_value(1.2f)
        .scan<'g', float>();

    program.add_argument("--llm-max-tokens")
        .help("Maximum tokens in LLM response")
        .default_value(256)
        .scan<'i', int>();

    program.add_argument("--min-sentence-chars")
        .help("Minimum characters before yielding a sentence to TTS")
        .default_value(50)
        .scan<'i', int>();

    // Conversation
    program.add_argument("--conversation-history")
        .help("Maximum conversation exchanges to keep")
        .default_value(10)
        .scan<'i', int>();

    // Creature server
    program.add_argument("--creature-server-url")
        .help("creature-server base URL")
        .default_value(std::string("https://server.prod.chirpchirp.dev"));

    program.add_argument("--creature-id")
        .help("Creature UUID for Beaky")
        .default_value(std::string(""));

    program.add_argument("--no-resume-playlist")
        .help("Don't resume playlist after speaking")
        .default_value(false)
        .implicit_value(true);

    try {
        program.parse_args(argc, argv);
    } catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
        std::cerr << program;
        return nullptr;
    }

    auto config = std::make_unique<Configuration>();

    config->verbose = program.get<bool>("--verbose");
    config->audioDeviceIndex = program.get<int>("--audio-device");
    config->listDevices = program.get<bool>("--list-devices");

    // Validate creature-id is provided unless just listing devices
    if (!config->listDevices) {
        auto creatureId = program.get<std::string>("--creature-id");
        if (creatureId.empty()) {
            std::cerr << "--creature-id is required" << std::endl;
            std::cerr << program;
            return nullptr;
        }
    }

    if (auto val = program.present("--wake-word-model")) {
        config->wakeWordModelPath = *val;
    }
    if (auto val = program.present("--mel-model")) {
        config->melModelPath = *val;
    }
    if (auto val = program.present("--embedding-model")) {
        config->embeddingModelPath = *val;
    }
    config->wakeWordThreshold = program.get<float>("--wake-word-threshold");

    config->whisperModelPath = program.get<std::string>("--whisper-model");

    config->vadModelPath = program.get<std::string>("--vad-model");
    config->vadThreshold = program.get<float>("--vad-threshold");
    config->silenceDurationMs = program.get<int>("--silence-duration-ms");
    config->maxRecordSeconds = program.get<int>("--max-record-seconds");

    config->llmHost = program.get<std::string>("--llm-host");
    config->llmPort = program.get<int>("--llm-port");
    config->llmModel = program.get<std::string>("--llm-model");
    config->llmTemperature = program.get<float>("--llm-temperature");
    config->llmMaxTokens = program.get<int>("--llm-max-tokens");
    config->minSentenceChars = program.get<int>("--min-sentence-chars");

    // System prompt: if the value is a readable file path, load its contents
    if (auto val = program.present("--llm-system-prompt")) {
        std::ifstream file(*val);
        if (file.is_open()) {
            std::string contents((std::istreambuf_iterator<char>(file)),
                                 std::istreambuf_iterator<char>());
            config->llmSystemPrompt = contents;
            info("Loaded system prompt from file: {}", *val);
        } else {
            config->llmSystemPrompt = *val;
        }
    }

    config->maxConversationExchanges = program.get<int>("--conversation-history");

    config->creatureServerUrl = program.get<std::string>("--creature-server-url");
    config->creatureId = program.get<std::string>("--creature-id");
    config->resumePlaylist = !program.get<bool>("--no-resume-playlist");

    return config;
}

}  // namespace creatures
