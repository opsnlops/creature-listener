//
// creature-listener — Wake word conversational interface for April's Creature Workshop
//

#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

#include "spdlog/sinks/stdout_color_sinks.h"
#include "spdlog/spdlog.h"

#include "Version.h"
#include "audio/AudioCapture.h"
#include "audio/VoiceActivityDetector.h"
#include "config/CommandLine.h"
#include "config/Configuration.h"
#include "llm/ConversationHistory.h"
#include "llm/LLMClient.h"
#include "server/CreatureServerClient.h"
#include "stt/SpeechToText.h"
#include "trace/Trace.h"
#include "wakeword/WakeWordDetector.h"

#include "util/namespace-stuffs.h"

namespace creatures {

/// State machine for the conversation loop
enum class ListenerState {
    Listening,    // Waiting for wake word
    Ack,          // Wake word detected, triggering acknowledgment
    Recording,    // Recording speech, VAD monitoring
    Transcribing, // whisper.cpp STT
    Thinking,     // LLM streaming response
    Speaking,     // Sentences flowing to creature-server
    Error         // Recoverable error, return to Listening
};

std::string stateToString(ListenerState state) {
    switch (state) {
        case ListenerState::Listening:    return "LISTENING";
        case ListenerState::Ack:          return "ACK";
        case ListenerState::Recording:    return "RECORDING";
        case ListenerState::Transcribing: return "TRANSCRIBING";
        case ListenerState::Thinking:     return "THINKING";
        case ListenerState::Speaking:     return "SPEAKING";
        case ListenerState::Error:        return "ERROR";
    }
    return "UNKNOWN";
}

std::atomic<bool> running{true};

void signalHandler(int signal) {
    info("Caught signal {}, shutting down...", signal);
    running.store(false);
}

}  // namespace creatures

using namespace creatures;

int main(int argc, char* argv[]) {
    // Set up signal handlers
    std::signal(SIGINT, creatures::signalHandler);
    std::signal(SIGTERM, creatures::signalHandler);

    // Set up logging
    auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    auto logger = std::make_shared<spdlog::logger>("listener", console_sink);
    logger->set_level(spdlog::level::info);
    spdlog::set_default_logger(logger);

    info("creature-listener v{}.{}.{}",
         CREATURE_LISTENER_VERSION_MAJOR,
         CREATURE_LISTENER_VERSION_MINOR,
         CREATURE_LISTENER_VERSION_PATCH);

    // Parse command line
    auto config = parseCommandLine(argc, argv);
    if (!config) {
        return 1;
    }

    // Set log level based on verbose flag
    if (config->verbose) {
        logger->set_level(spdlog::level::debug);
        debug("Verbose logging enabled");
    }

    // List audio devices and exit if requested
    if (config->listDevices) {
        AudioCapture::listDevices();
        return 0;
    }

    // --- Initialize tracing ---
    creatures::tracer = std::make_shared<Tracer>();
    if (!config->honeycombApiKey.empty()) {
        creatures::tracer->initialize("creature-listener",
            fmt::format("{}.{}.{}", CREATURE_LISTENER_VERSION_MAJOR,
                        CREATURE_LISTENER_VERSION_MINOR,
                        CREATURE_LISTENER_VERSION_PATCH),
            config->honeycombApiKey, config->honeycombDataset);
    } else {
        info("No Honeycomb API key — tracing disabled (set HONEYCOMB_API_KEY or --honeycomb-api-key)");
    }

    // --- Initialize components ---

    // Audio capture
    AudioCapture audioCapture;
    if (!audioCapture.init(config->audioDeviceIndex)) {
        error("Failed to initialize audio capture");
        return 1;
    }

    // Voice activity detector
    VoiceActivityDetector vad;
    if (!vad.init(config->vadModelPath, config->vadThreshold)) {
        error("Failed to initialize VAD");
        return 1;
    }

    // Wake word detector (LOWWI / openWakeWord)
    WakeWordDetector wakeWord;
    bool haveWakeWord = false;
    if (!config->wakeWordModelPath.empty() && !config->melModelPath.empty()
        && !config->embeddingModelPath.empty()) {
        if (wakeWord.init(config->wakeWordModelPath, config->melModelPath,
                          config->embeddingModelPath, config->wakeWordThreshold)) {
            haveWakeWord = true;
        } else {
            warn("Wake word detector failed to initialize — using keyboard trigger");
        }
    } else {
        info("No wake word models configured — using keyboard trigger (press Enter)");
    }

    // Speech-to-text
    SpeechToText stt;
    if (!stt.init(config->whisperModelPath)) {
        error("Failed to initialize speech-to-text");
        return 1;
    }

    // Conversation history
    ConversationHistory history(config->maxConversationExchanges);

    // LLM client
    LLMClient llm(config->llmHost, config->llmPort, config->llmModel,
                  config->llmSystemPrompt, config->llmTemperature,
                  config->llmMaxTokens, config->minSentenceChars, history);

    // Creature server client
    CreatureServerClient server(config->creatureServerUrl);

    // --- State machine ---

    ListenerState state = ListenerState::Listening;
    std::atomic<bool> wakeWordDetected{false};

    // Silence tracking for VAD
    auto lastSpeechTime = std::chrono::steady_clock::now();
    bool speechDetected = false;
    size_t maxRecordingSamples = static_cast<size_t>(config->maxRecordSeconds)
                                * AudioCapture::kSampleRate;

    // Audio frame callback — routes frames to wake word and VAD
    audioCapture.setFrameCallback(
        [&](const int16_t* samples, int numSamples) {
            if (state == ListenerState::Listening && haveWakeWord) {
                // LOWWI needs float samples — convert from int16
                std::vector<float> floatSamples(numSamples);
                for (int i = 0; i < numSamples; i++) {
                    floatSamples[i] = static_cast<float>(samples[i]) / 32768.0f;
                }
                wakeWord.processFloat(floatSamples.data(), numSamples);
            }

            if (state == ListenerState::Recording) {
                vad.process(samples, numSamples);
            }
        });

    // Start audio capture
    if (!audioCapture.start()) {
        error("Failed to start audio capture");
        return 1;
    }

    info("=== creature-listener ready ===");
    if (haveWakeWord) {
        info("Say the wake word to start a conversation");
    } else {
        info("Press Enter to simulate wake word detection");
    }

    // Keyboard trigger thread (when no wake word detector)
    std::thread keyboardThread;
    if (!haveWakeWord) {
        keyboardThread = std::thread([&]() {
            while (running.load()) {
                std::string line;
                if (!std::getline(std::cin, line)) {
                    // EOF on stdin — no more keyboard input possible
                    debug("stdin closed, keyboard trigger disabled");
                    break;
                }
                if (state == ListenerState::Listening) {
                    info("Keyboard trigger: wake word simulated");
                    wakeWordDetected.store(true);
                }
            }
        });
        keyboardThread.detach();
    }

    // Main state machine loop
    while (running.load()) {
        switch (state) {

        case ListenerState::Listening: {
            // Wait for wake word detection
            if ((haveWakeWord && wakeWord.detected())
                || wakeWordDetected.exchange(false)) {
                state = ListenerState::Ack;
                info("State: {} -> {}", stateToString(ListenerState::Listening),
                     stateToString(state));
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            break;
        }

        case ListenerState::Ack: {
            // Acknowledge wake word — start recording immediately
            // TODO: Trigger a short "listening" animation on creature-server
            info("Wake word acknowledged — listening for speech...");

            // Reset VAD and start recording
            vad.reset();
            audioCapture.startRecording();
            lastSpeechTime = std::chrono::steady_clock::now();
            speechDetected = false;

            state = ListenerState::Recording;
            info("State: {} -> {}", stateToString(ListenerState::Ack),
                 stateToString(state));
            break;
        }

        case ListenerState::Recording: {
            auto now = std::chrono::steady_clock::now();

            // Check if VAD detects speech
            if (vad.isSpeech()) {
                if (!speechDetected) {
                    info("Speech detected — recording...");
                    speechDetected = true;
                }
                lastSpeechTime = now;
            }

            // Only start the silence countdown after speech has been detected
            if (speechDetected) {
                auto silenceMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - lastSpeechTime).count();

                if (silenceMs >= config->silenceDurationMs) {
                    audioCapture.stopRecording();
                    auto buffer = audioCapture.getRecordingBuffer();
                    float durationSec = static_cast<float>(buffer.size()) / 16000.0f;
                    info("Recording complete: {:.1f}s ({} samples), silence: {}ms",
                         durationSec, buffer.size(), silenceMs);

                    if (buffer.size() < 8000) {
                        // Less than 0.5 seconds of actual audio — probably just noise
                        warn("Recording too short ({:.1f}s), ignoring", durationSec);
                        state = ListenerState::Listening;
                    } else {
                        state = ListenerState::Transcribing;
                    }
                    info("State: RECORDING -> {}", stateToString(state));
                    break;
                }
            }

            // Check for max recording length (using sample count from AudioCapture)
            auto recordingSamples = audioCapture.getRecordingSampleCount();
            if (recordingSamples >= maxRecordingSamples) {
                audioCapture.stopRecording();
                info("Max recording length reached ({}s)", config->maxRecordSeconds);
                state = ListenerState::Transcribing;
                info("State: RECORDING -> {}", stateToString(state));
                break;
            }

            // Also timeout if no speech detected within a few seconds
            if (!speechDetected) {
                auto waitMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - lastSpeechTime).count();
                if (waitMs >= 5000) {
                    audioCapture.stopRecording();
                    warn("No speech detected within 5s, returning to listening");
                    state = ListenerState::Listening;
                    info("State: RECORDING -> {}", stateToString(state));
                    break;
                }
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            break;
        }

        case ListenerState::Transcribing: {
            // Create a root span for the entire conversation turn
            auto turnSpan = creatures::tracer
                ? creatures::tracer->startSpan("conversation.turn")
                : nullptr;

            auto buffer = audioCapture.getRecordingBuffer();
            if (turnSpan) {
                turnSpan->setAttribute("audio.samples", static_cast<int64_t>(buffer.size()));
                turnSpan->setAttribute("audio.duration_sec",
                    static_cast<double>(buffer.size()) / 16000.0);
                turnSpan->setAttribute("creature.id", config->creatureId);
            }

            // STT span
            auto sttSpan = creatures::tracer
                ? creatures::tracer->startChildSpan("stt.transcribe", turnSpan)
                : nullptr;

            std::string transcript = stt.transcribe(buffer);

            if (sttSpan) {
                sttSpan->setAttribute("stt.transcript", transcript);
                sttSpan->setAttribute("stt.transcript_length",
                    static_cast<int64_t>(transcript.size()));
                if (transcript.empty()) sttSpan->setError("Empty transcription");
                else sttSpan->setSuccess();
            }

            if (transcript.empty()) {
                warn("Empty transcription, returning to listening");
                state = ListenerState::Listening;
                if (turnSpan) turnSpan->setError("Empty transcription");
            } else {
                info("Transcript: \"{}\"", transcript);
                state = ListenerState::Thinking;
            }
            info("State: TRANSCRIBING -> {}", stateToString(state));

            if (state == ListenerState::Thinking) {
                // Generate traceparent from the turn span for server propagation
                std::string tp = turnSpan ? turnSpan->traceparent() : "";

                // Start streaming session with creature-server
                auto sessionSpan = creatures::tracer
                    ? creatures::tracer->startChildSpan("server.streaming_session", turnSpan)
                    : nullptr;

                std::string sessionId = server.startSession(
                    config->creatureId, config->resumePlaylist, tp);

                if (sessionId.empty()) {
                    error("Failed to start streaming session");
                    if (sessionSpan) sessionSpan->setError("Failed to start session");
                    if (turnSpan) turnSpan->setError("Server session failed");
                    state = ListenerState::Error;
                    break;
                }

                if (sessionSpan) {
                    sessionSpan->setAttribute("session.id", sessionId);
                }

                info("State: THINKING -> SPEAKING (streaming)");
                state = ListenerState::Speaking;

                // LLM span
                auto llmSpan = creatures::tracer
                    ? creatures::tracer->startChildSpan("llm.respond", turnSpan)
                    : nullptr;

                // Stream LLM response, sending each sentence to creature-server
                int sentenceCount = 0;
                std::string fullResponse = llm.respondStreaming(
                    transcript,
                    [&](const std::string& sentence, [[maybe_unused]] int idx) {
                        server.addText(sessionId, sentence, tp);
                        sentenceCount++;
                    });

                if (llmSpan) {
                    llmSpan->setAttribute("llm.response_length",
                        static_cast<int64_t>(fullResponse.size()));
                    llmSpan->setAttribute("llm.sentence_count",
                        static_cast<int64_t>(sentenceCount));
                    if (fullResponse.empty()) llmSpan->setError("Empty LLM response");
                    else llmSpan->setSuccess();
                }

                if (fullResponse.empty()) {
                    error("LLM returned empty response");
                }

                // Finish the session — triggers TTS + animation + playback
                server.finishSession(sessionId, tp);

                if (sessionSpan) sessionSpan->setSuccess();
                if (turnSpan) {
                    turnSpan->setAttribute("response.sentences",
                        static_cast<int64_t>(sentenceCount));
                    turnSpan->setSuccess();
                }

                info("Conversation turn complete, returning to listening");
                state = ListenerState::Listening;
                info("State: SPEAKING -> {}", stateToString(state));
            }
            break;
        }

        case ListenerState::Thinking:
        case ListenerState::Speaking:
            // These states are handled inline in Transcribing above
            // (LLM streaming and server communication happen synchronously)
            break;

        case ListenerState::Error: {
            warn("Error state — returning to listening after brief pause");
            std::this_thread::sleep_for(std::chrono::seconds(1));
            state = ListenerState::Listening;
            break;
        }

        }  // switch
    }

    // Cleanup
    info("Shutting down...");
    audioCapture.stop();

    info("creature-listener stopped");
    return 0;
}
