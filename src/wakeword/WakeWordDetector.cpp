#include "wakeword/WakeWordDetector.h"

#include <filesystem>

#include "lowwi.hpp"

#include "util/namespace-stuffs.h"

namespace creatures {

WakeWordDetector::WakeWordDetector() = default;

WakeWordDetector::~WakeWordDetector() = default;

bool WakeWordDetector::init(const std::string& wakeWordModelPath,
                            const std::string& melModelPath,
                            const std::string& embeddingModelPath,
                            float threshold) {
    // Verify model files exist
    if (!std::filesystem::exists(wakeWordModelPath)) {
        error("Wake word model not found: {}", wakeWordModelPath);
        return false;
    }
    if (!std::filesystem::exists(melModelPath)) {
        error("Mel-spectrogram model not found: {}", melModelPath);
        return false;
    }
    if (!std::filesystem::exists(embeddingModelPath)) {
        error("Embedding model not found: {}", embeddingModelPath);
        return false;
    }

    try {
        // LOWWI hardcodes "models/melspectrogram.onnx" and "models/embedding_model.onnx"
        // relative to the working directory. The systemd service sets
        // WorkingDirectory=/var/lib/creature-listener, and the .deb package
        // creates symlinks: /var/lib/creature-listener/models/ -> data dir.
        lowwi_ = std::make_unique<CLFML::LOWWI::Lowwi>();

        // Configure the wake word
        CLFML::LOWWI::Lowwi_word_t word;
        word.phrase = "wakeword";
        word.model_path = wakeWordModelPath;
        word.threshold = threshold;
        word.min_activations = 5;
        word.refractory = 20;
        word.debug = false;

        // Capture a pointer to our atomic flag for the callback
        auto detectedFlag = std::make_shared<std::atomic<bool>*>(&detected_);
        auto callbackPtr = std::make_shared<WakeWordCallback*>(&callback_);

        word.cbfunc = [detectedFlag, callbackPtr](
                          CLFML::LOWWI::Lowwi_ctx_t ctx,
                          [[maybe_unused]] std::shared_ptr<void> arg) {
            info("Wake word detected: \"{}\" (confidence: {:.3f})", ctx.phrase, ctx.confidence);
            (*detectedFlag)->store(true);
            if (**callbackPtr) {
                (**callbackPtr)();
            }
        };

        lowwi_->add_wakeword(word);

        info("LOWWI wake word detector initialized");
        info("  Wake word model: {}", wakeWordModelPath);
        info("  Threshold: {:.2f}, min activations: 5", threshold);
        return true;

    } catch (const std::exception& e) {
        error("Failed to initialize LOWWI: {}", e.what());
        lowwi_.reset();
        return false;
    }
}

void WakeWordDetector::processFloat(const float* samples, int numSamples) {
    if (!lowwi_) return;

    // LOWWI expects std::vector<float>
    std::vector<float> audioChunk(samples, samples + numSamples);
    lowwi_->run(audioChunk);
}

bool WakeWordDetector::detected() {
    return detected_.exchange(false);
}

void WakeWordDetector::setCallback(WakeWordCallback callback) {
    callback_ = std::move(callback);
}

}  // namespace creatures
