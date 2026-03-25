#include "audio/VoiceActivityDetector.h"

#include <algorithm>
#include <cmath>
#include <numeric>

#include "util/namespace-stuffs.h"

namespace creatures {

VoiceActivityDetector::VoiceActivityDetector() = default;
VoiceActivityDetector::~VoiceActivityDetector() = default;

bool VoiceActivityDetector::init([[maybe_unused]] const std::string& modelPath, float threshold) {
    threshold_ = threshold;

    // Energy-based VAD doesn't need a model file, but we accept the path
    // for future Silero ONNX integration.
    // TODO: Integrate Silero ONNX model via onnxruntime C API when available on ARM64
    info("VAD initialized (energy-based, threshold: {:.2f})", threshold_);
    info("  Model path noted for future ONNX integration: {}", modelPath);

    reset();
    return true;
}

float VoiceActivityDetector::process(const int16_t* samples, int numSamples) {
    if (numSamples <= 0) {
        lastProbability_ = 0.0f;
        return lastProbability_;
    }

    // Compute RMS energy of the frame
    double sumSquares = 0.0;
    for (int i = 0; i < numSamples; i++) {
        double sample = static_cast<double>(samples[i]);
        sumSquares += sample * sample;
    }
    float rms = static_cast<float>(std::sqrt(sumSquares / numSamples));

    frameCount_++;

    // Calibration phase: learn the noise floor from the first ~1 second
    if (frameCount_ <= kCalibrationFrames) {
        if (frameCount_ == 1) {
            energyFloor_ = rms;
            energyCeiling_ = rms;
        } else {
            energyFloor_ = std::min(energyFloor_, rms);
            energyCeiling_ = std::max(energyCeiling_, rms);
        }

        if (frameCount_ == kCalibrationFrames) {
            // Set a reasonable floor — don't let it be zero
            energyFloor_ = std::max(energyFloor_, 50.0f);
            debug("VAD calibration complete: floor={:.1f}, ceiling={:.1f}",
                  energyFloor_, energyCeiling_);
        }

        lastProbability_ = 0.0f;
        return lastProbability_;
    }

    // Adaptive noise floor: slowly track upward if we see consistently low energy
    energyFloor_ = energyFloor_ * 0.995f + std::min(rms, energyFloor_ * 2.0f) * 0.005f;

    // Speech probability based on energy relative to floor
    float speechThreshold = energyFloor_ * kEnergyMultiplier;
    if (rms > speechThreshold) {
        // Map energy above threshold to probability [0.5, 1.0]
        float ratio = (rms - speechThreshold) / (speechThreshold * 2.0f);
        lastProbability_ = 0.5f + std::min(ratio, 1.0f) * 0.5f;
    } else {
        // Below threshold: map to [0.0, 0.5]
        lastProbability_ = (rms / speechThreshold) * 0.5f;
    }

    return lastProbability_;
}

void VoiceActivityDetector::reset() {
    lastProbability_ = 0.0f;
    frameCount_ = 0;
    energyFloor_ = 0.0f;
    energyCeiling_ = 0.0f;
}

}  // namespace creatures
