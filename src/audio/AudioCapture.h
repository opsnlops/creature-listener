#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <vector>

#include <portaudio.h>

namespace creatures {

/// Callback invoked with each audio frame at 16kHz mono.
using AudioFrameCallback = std::function<void(const int16_t* samples, int numSamples)>;

/// PortAudio wrapper that captures mono audio and delivers 16kHz frames via callbacks.
/// If the device doesn't support 16kHz natively (e.g. USB devices that only do 48kHz),
/// the stream opens at the device's native rate and downsamples to 16kHz in the callback.
class AudioCapture {
public:
    static constexpr int kTargetRate = 16000;
    static constexpr int kFrameSize = 512;  // 32ms at 16kHz — matches Porcupine requirement
    static constexpr int kChannels = 1;

    AudioCapture();
    ~AudioCapture();

    // Non-copyable
    AudioCapture(const AudioCapture&) = delete;
    AudioCapture& operator=(const AudioCapture&) = delete;

    /// List available audio devices to stdout.
    static void listDevices();

    /// Initialize PortAudio and open a stream on the given device.
    /// deviceIndex of -1 means use default input device.
    bool init(int deviceIndex);

    /// Start capturing audio.
    bool start();

    /// Stop capturing audio.
    void stop();

    /// Register a callback to receive audio frames.
    void setFrameCallback(AudioFrameCallback callback);

    /// Get the recording buffer (accumulated frames since startRecording()).
    std::vector<float> getRecordingBuffer();

    /// Get the current recording buffer size in samples (cheap — no copy).
    size_t getRecordingSampleCount() const;

    /// Start accumulating frames into the recording buffer.
    void startRecording();

    /// Stop accumulating frames.
    void stopRecording();

    /// Returns true if currently recording.
    bool isRecording() const { return recording_.load(); }

private:
    static int paCallback(const void* input, void* output,
                          unsigned long frameCount,
                          const PaStreamCallbackTimeInfo* timeInfo,
                          PaStreamCallbackFlags statusFlags,
                          void* userData);

    void processFrame(const int16_t* samples, int numSamples);

    PaStream* stream_ = nullptr;
    AudioFrameCallback frameCallback_;
    std::mutex callbackMutex_;

    // Resampling: if the device doesn't support 16kHz, we open at the native
    // rate and downsample. decimationFactor_ = deviceRate / 16000.
    int deviceSampleRate_ = kTargetRate;
    int decimationFactor_ = 1;  // 1 = no resampling, 3 = 48kHz→16kHz

    // Accumulator for delivering fixed-size frames after resampling
    std::vector<int16_t> frameAccumulator_;

    // Recording buffer: stores float samples for whisper (at 16kHz)
    std::atomic<bool> recording_{false};
    mutable std::mutex recordingMutex_;
    std::vector<float> recordingBuffer_;
};

}  // namespace creatures
