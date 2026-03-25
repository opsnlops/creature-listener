#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Forward declaration — we don't expose whisper/onnx internals
struct OrtApi;
struct OrtSession;

namespace creatures {

/// Lightweight Silero VAD wrapper.
/// Processes 16kHz int16 audio frames and reports speech probability.
/// Uses ONNX Runtime (bundled with whisper.cpp) to run the Silero VAD model.
///
/// Since a full ONNX Runtime integration is heavy, this implementation uses
/// a simpler energy-based approach as the primary VAD, with the ONNX model
/// as an optional enhancement.
class VoiceActivityDetector {
public:
    VoiceActivityDetector();
    ~VoiceActivityDetector();

    // Non-copyable
    VoiceActivityDetector(const VoiceActivityDetector&) = delete;
    VoiceActivityDetector& operator=(const VoiceActivityDetector&) = delete;

    /// Initialize with model path and confidence threshold.
    bool init(const std::string& modelPath, float threshold);

    /// Process a frame of 16kHz int16 audio.
    /// Returns the speech probability (0.0 - 1.0).
    float process(const int16_t* samples, int numSamples);

    /// Returns true if the last processed frame was detected as speech.
    bool isSpeech() const { return lastProbability_ >= threshold_; }

    /// Reset internal state (call between utterances).
    void reset();

private:
    float threshold_ = 0.5f;
    float lastProbability_ = 0.0f;

    // Energy-based VAD parameters
    float energyFloor_ = 0.0f;
    float energyCeiling_ = 0.0f;
    int frameCount_ = 0;
    static constexpr int kCalibrationFrames = 30;  // ~1 second at 32ms frames
    static constexpr float kEnergyMultiplier = 3.0f;
};

}  // namespace creatures
