#include "config/CommandLine.h"

#include <argparse/argparse.hpp>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

#include "Version.h"
#include "config/ConfigFile.h"
#include "util/namespace-stuffs.h"

namespace creatures {

std::unique_ptr<Configuration> parseCommandLine(int argc, char* argv[]) {
    argparse::ArgumentParser program(
        "creature-listener",
        fmt::format("{}.{}.{}", CREATURE_LISTENER_VERSION_MAJOR,
                    CREATURE_LISTENER_VERSION_MINOR,
                    CREATURE_LISTENER_VERSION_PATCH));

    program.add_description("Wake word conversational interface for April's Creature Workshop");

    program.add_argument("--config-path")
        .help("Path to YAML configuration file")
        .default_value(std::string(""));

    // These few flags are useful without a config file (dev/testing)
    program.add_argument("--list-devices")
        .help("List available audio devices and exit")
        .default_value(false)
        .implicit_value(true);

    program.add_argument("--verbose")
        .help("Enable verbose/debug logging")
        .default_value(false)
        .implicit_value(true);

    program.add_argument("--audio-device")
        .help("Override audio device index")
        .default_value(-1)
        .scan<'i', int>();

    // Allow overriding creature-id and server URL from CLI for quick testing
    program.add_argument("--creature-id")
        .help("Override creature UUID");

    program.add_argument("--creature-server-url")
        .help("Override creature-server base URL");

    program.add_argument("--llm-host")
        .help("Override llama-server hostname");

    program.add_argument("--llm-port")
        .help("Override llama-server port")
        .scan<'i', int>();

    program.add_argument("--whisper-model")
        .help("Override whisper model path");

    program.add_argument("--honeycomb-api-key")
        .help("Override Honeycomb API key (or set HONEYCOMB_API_KEY env var)");

    try {
        program.parse_args(argc, argv);
    } catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
        std::cerr << program;
        return nullptr;
    }

    auto config = std::make_unique<Configuration>();

    // --list-devices works without any config
    config->listDevices = program.get<bool>("--list-devices");
    if (config->listDevices) {
        return config;
    }

    config->verbose = program.get<bool>("--verbose");

    // Load config file if provided
    auto configPath = program.get<std::string>("--config-path");
    if (!configPath.empty()) {
        if (!loadConfigFile(configPath, *config)) {
            return nullptr;
        }
    }

    // CLI overrides (applied after config file)
    auto audioDevice = program.get<int>("--audio-device");
    if (audioDevice >= 0) {
        config->audioDeviceIndex = audioDevice;
    }

    if (auto val = program.present("--creature-id")) {
        config->creatureId = *val;
    }
    if (auto val = program.present("--creature-server-url")) {
        config->creatureServerUrl = *val;
    }
    if (auto val = program.present("--llm-host")) {
        config->llmHost = *val;
    }
    if (auto val = program.present<int>("--llm-port")) {
        config->llmPort = *val;
    }
    if (auto val = program.present("--whisper-model")) {
        config->whisperModelPath = *val;
    }

    // Honeycomb: CLI > config file > env var
    if (auto val = program.present("--honeycomb-api-key")) {
        config->honeycombApiKey = *val;
    } else if (config->honeycombApiKey.empty()) {
        if (const char* envKey = std::getenv("HONEYCOMB_API_KEY")) {
            config->honeycombApiKey = envKey;
        }
    }

    // Validate required fields
    if (config->creatureId.empty()) {
        std::cerr << "creatureId is required (set in config file or via --creature-id)" << std::endl;
        std::cerr << program;
        return nullptr;
    }
    if (config->llmSystemPrompt.empty()) {
        std::cerr << "llmSystemPrompt is required (set in config file)" << std::endl;
        return nullptr;
    }

    return config;
}

}  // namespace creatures
