#include "stt/SpeechToText.h"

#include <chrono>
#include <cmath>

#include "util/namespace-stuffs.h"

namespace creatures {

SpeechToText::SpeechToText() = default;

SpeechToText::~SpeechToText() {
    if (ctx_) {
        whisper_free(ctx_);
        ctx_ = nullptr;
    }
}

bool SpeechToText::init(const std::string& modelPath) {
    info("Loading whisper model: {}", modelPath);

    // Redirect whisper's internal logging through spdlog
    whisper_log_set([](enum ggml_log_level level, const char* text, void*) {
        // Strip trailing newline that whisper adds
        std::string msg(text);
        while (!msg.empty() && (msg.back() == '\n' || msg.back() == '\r')) {
            msg.pop_back();
        }
        if (msg.empty()) return;

        switch (level) {
            case GGML_LOG_LEVEL_ERROR: error("whisper: {}", msg); break;
            case GGML_LOG_LEVEL_WARN:  warn("whisper: {}", msg); break;
            case GGML_LOG_LEVEL_INFO:  debug("whisper: {}", msg); break;
            default:                   trace("whisper: {}", msg); break;
        }
    }, nullptr);

    struct whisper_context_params cparams = whisper_context_default_params();
    cparams.use_gpu = false;  // CPU only on Pi

    ctx_ = whisper_init_from_file_with_params(modelPath.c_str(), cparams);
    if (!ctx_) {
        error("Failed to load whisper model from: {}", modelPath);
        return false;
    }

    info("Whisper model loaded successfully");
    return true;
}

std::string SpeechToText::transcribe(const std::vector<float>& samples) {
    if (!ctx_) {
        error("Whisper not initialized");
        return "";
    }

    if (samples.empty()) {
        warn("Empty audio buffer, nothing to transcribe");
        return "";
    }

    // Trim leading and trailing silence to reduce whisper processing time.
    // Speech on Pi 5 is CPU-bound, so shorter audio = faster transcription.
    auto trimmed = trimSilence(samples);
    if (trimmed.empty()) {
        warn("Audio is all silence after trimming");
        return "";
    }

    auto startTime = std::chrono::steady_clock::now();

    float originalSec = static_cast<float>(samples.size()) / 16000.0f;
    float trimmedSec = static_cast<float>(trimmed.size()) / 16000.0f;
    info("Transcribing {:.1f}s of audio (trimmed from {:.1f}s, {} samples)...",
         trimmedSec, originalSec, trimmed.size());

    struct whisper_full_params wparams = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
    wparams.print_realtime = false;
    wparams.print_progress = false;
    wparams.print_timestamps = false;
    wparams.print_special = false;
    wparams.no_context = true;
    wparams.single_segment = true;
    wparams.suppress_blank = true;
    wparams.suppress_nst = true;
    wparams.language = "en";
    wparams.n_threads = 4;  // Use 4 of the Pi 5's cores

    int result = whisper_full(ctx_, wparams, trimmed.data(),
                              static_cast<int>(trimmed.size()));
    if (result != 0) {
        error("Whisper transcription failed: error code {}", result);
        return "";
    }

    // Collect all segments into a single string
    std::string text;
    int numSegments = whisper_full_n_segments(ctx_);
    for (int i = 0; i < numSegments; i++) {
        const char* segmentText = whisper_full_get_segment_text(ctx_, i);
        if (segmentText) {
            if (!text.empty()) text += " ";
            text += segmentText;
        }
    }

    auto endTime = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        endTime - startTime);

    // Trim leading/trailing whitespace
    auto start = text.find_first_not_of(" \t\n\r");
    auto end = text.find_last_not_of(" \t\n\r");
    if (start != std::string::npos) {
        text = text.substr(start, end - start + 1);
    } else {
        text.clear();
    }

    info("Transcription complete in {}ms: \"{}\"", elapsed.count(), text);

    // Filter whisper hallucinations — the tiny model commonly produces
    // these patterns when transcribing silence or background noise
    if (isHallucination(text)) {
        warn("Filtered whisper hallucination: \"{}\"", text);
        return "";
    }

    return text;
}

std::vector<float> SpeechToText::trimSilence(const std::vector<float>& samples) {
    // Compute RMS energy in 10ms windows (160 samples at 16kHz).
    // Find the first and last windows above a threshold to trim silence.
    static constexpr int kWindowSize = 160;
    static constexpr float kSilenceThreshold = 0.005f;  // ~-46 dB
    static constexpr int kPaddingSamples = 1600;  // 100ms padding to keep

    if (static_cast<int>(samples.size()) < kWindowSize) return samples;

    // Find first non-silent window
    int firstSpeech = -1;
    int lastSpeech = -1;
    int numWindows = static_cast<int>(samples.size()) / kWindowSize;

    for (int w = 0; w < numWindows; w++) {
        float energy = 0.0f;
        int offset = w * kWindowSize;
        for (int i = 0; i < kWindowSize; i++) {
            energy += samples[offset + i] * samples[offset + i];
        }
        float rms = std::sqrt(energy / kWindowSize);

        if (rms > kSilenceThreshold) {
            if (firstSpeech < 0) firstSpeech = offset;
            lastSpeech = offset + kWindowSize;
        }
    }

    if (firstSpeech < 0) return {};  // All silence

    // Add padding around the speech
    int start = std::max(0, firstSpeech - kPaddingSamples);
    int end = std::min(static_cast<int>(samples.size()), lastSpeech + kPaddingSamples);

    return std::vector<float>(samples.begin() + start, samples.begin() + end);
}

bool SpeechToText::isHallucination(const std::string& text) {
    if (text.empty()) return true;

    // Very short transcriptions are almost always hallucinations
    if (text.size() < 3) return true;

    // Common whisper tiny hallucination patterns on silence/noise
    static const std::vector<std::string> hallucinations = {
        "Thank you.",
        "Thanks for watching.",
        "Thank you for watching.",
        "you",
        "You",
        "Bye.",
        "Bye!",
        "I'm sorry.",
        "Oh.",
        "Hmm.",
        "...",
        "Ah.",
        "So,",
        "The end.",
    };

    for (const auto& h : hallucinations) {
        if (text == h) return true;
    }

    // Bracketed annotations like [MUSIC], [BLANK_AUDIO], [SILENCE], etc.
    if (text.front() == '[' && text.back() == ']') return true;

    // Parenthesized annotations like (upbeat music), (silence)
    if (text.front() == '(' && text.back() == ')') return true;

    // Repeated single words/tokens are hallucinations
    // e.g. "you you you you" or "the the the the"
    if (text.size() > 8) {
        // Check if the text is just the same short word repeated
        auto firstSpace = text.find(' ');
        if (firstSpace != std::string::npos && firstSpace < 6) {
            std::string word = text.substr(0, firstSpace);
            std::string repeated;
            while (repeated.size() < text.size() + word.size()) {
                if (!repeated.empty()) repeated += " ";
                repeated += word;
            }
            if (text == repeated.substr(0, text.size())) return true;
        }
    }

    return false;
}

}  // namespace creatures
