#pragma once

#include <string>
#include <vector>

#include "whisper.h"

namespace creatures {

/// whisper.cpp wrapper — loads the model once and transcribes float audio buffers.
class SpeechToText {
public:
    SpeechToText();
    ~SpeechToText();

    // Non-copyable
    SpeechToText(const SpeechToText&) = delete;
    SpeechToText& operator=(const SpeechToText&) = delete;

    /// Load the whisper model from the given path.
    bool init(const std::string& modelPath);

    /// Transcribe a buffer of float samples (16kHz mono, range [-1.0, 1.0]).
    /// Returns the transcribed text, or empty string on failure.
    std::string transcribe(const std::vector<float>& samples);

private:
    /// Check if a transcription is a known whisper hallucination pattern.
    static bool isHallucination(const std::string& text);

    struct whisper_context* ctx_ = nullptr;
};

}  // namespace creatures
