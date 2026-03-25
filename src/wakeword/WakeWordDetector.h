#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>

// Forward declare LOWWI types to avoid leaking the header
namespace CLFML::LOWWI {
class Lowwi;
}

namespace creatures {

/// Callback invoked when the wake word is detected.
using WakeWordCallback = std::function<void()>;

/// Wake word detector using LOWWI (openWakeWord ONNX models).
/// Fully offline, no API key required. Apache 2.0 licensed.
///
/// Requires three ONNX model files:
///   - melspectrogram.onnx   (shared audio preprocessing)
///   - embedding_model.onnx  (shared feature extraction)
///   - <wakeword>.onnx       (per-word classifier, trained for your creature)
class WakeWordDetector {
public:
    WakeWordDetector();
    ~WakeWordDetector();

    // Non-copyable
    WakeWordDetector(const WakeWordDetector&) = delete;
    WakeWordDetector& operator=(const WakeWordDetector&) = delete;

    /// Initialize with paths to the three ONNX models and a confidence threshold.
    bool init(const std::string& wakeWordModelPath,
              const std::string& melModelPath,
              const std::string& embeddingModelPath,
              float threshold);

    /// Process audio samples (16kHz mono float, range [-1.0, 1.0]).
    /// LOWWI handles its own internal buffering and windowing.
    void processFloat(const float* samples, int numSamples);

    /// Check if the wake word was detected (and clear the flag).
    bool detected();

    /// Set the callback for wake word detection.
    void setCallback(WakeWordCallback callback);

private:
    WakeWordCallback callback_;
    std::atomic<bool> detected_{false};
    std::unique_ptr<CLFML::LOWWI::Lowwi> lowwi_;
};

}  // namespace creatures
