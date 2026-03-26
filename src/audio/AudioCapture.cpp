#include "audio/AudioCapture.h"

#include <cmath>
#include <iostream>

#include "util/namespace-stuffs.h"

namespace creatures {

AudioCapture::AudioCapture() {
    Pa_Initialize();
}

AudioCapture::~AudioCapture() {
    stop();
    Pa_Terminate();
}

void AudioCapture::listDevices() {
    Pa_Initialize();

    int numDevices = Pa_GetDeviceCount();
    if (numDevices < 0) {
        std::cerr << "PortAudio error: " << Pa_GetErrorText(numDevices) << std::endl;
        Pa_Terminate();
        return;
    }

    std::cout << "Available audio input devices:" << std::endl;
    int defaultInput = Pa_GetDefaultInputDevice();

    for (int i = 0; i < numDevices; i++) {
        const PaDeviceInfo* deviceInfo = Pa_GetDeviceInfo(i);
        if (deviceInfo->maxInputChannels > 0) {
            std::cout << "  [" << i << "] " << deviceInfo->name
                      << " (inputs: " << deviceInfo->maxInputChannels
                      << ", rate: " << deviceInfo->defaultSampleRate << " Hz)";
            if (i == defaultInput) {
                std::cout << " [DEFAULT]";
            }
            std::cout << std::endl;
        }
    }

    Pa_Terminate();
}

bool AudioCapture::init(int deviceIndex) {
    PaStreamParameters inputParams;
    inputParams.device = (deviceIndex >= 0) ? deviceIndex : Pa_GetDefaultInputDevice();

    if (inputParams.device == paNoDevice) {
        error("No default input device found");
        return false;
    }

    const PaDeviceInfo* deviceInfo = Pa_GetDeviceInfo(inputParams.device);
    if (!deviceInfo) {
        error("Could not get device info for device {}", inputParams.device);
        return false;
    }

    info("Using audio device: {} (index {})", deviceInfo->name, inputParams.device);

    // Try 16kHz first; if the device doesn't support it, fall back to
    // its native rate and downsample.
    deviceSampleRate_ = kTargetRate;
    decimationFactor_ = 1;

    inputParams.channelCount = kChannels;
    inputParams.sampleFormat = paInt16;
    inputParams.suggestedLatency = deviceInfo->defaultLowInputLatency;
    inputParams.hostApiSpecificStreamInfo = nullptr;

    PaError err = Pa_IsFormatSupported(&inputParams, nullptr, kTargetRate);
    if (err != paFormatIsSupported) {
        // 16kHz not supported — try the device's default rate
        int nativeRate = static_cast<int>(deviceInfo->defaultSampleRate);
        if (nativeRate % kTargetRate != 0) {
            // Try common rates that are integer multiples of 16kHz
            static const int fallbackRates[] = {48000, 32000, 96000};
            bool found = false;
            for (int rate : fallbackRates) {
                err = Pa_IsFormatSupported(&inputParams, nullptr, rate);
                if (err == paFormatIsSupported) {
                    nativeRate = rate;
                    found = true;
                    break;
                }
            }
            if (!found) {
                error("Device doesn't support 16kHz or any compatible sample rate");
                return false;
            }
        }

        deviceSampleRate_ = nativeRate;
        decimationFactor_ = nativeRate / kTargetRate;
        info("Device doesn't support 16kHz — opening at {}Hz ({}:1 decimation)",
             deviceSampleRate_, decimationFactor_);
    }

    // Calculate the frame size at the device rate to produce kFrameSize
    // samples at 16kHz after decimation
    int deviceFrameSize = kFrameSize * decimationFactor_;

    err = Pa_OpenStream(
        &stream_,
        &inputParams,
        nullptr,  // no output
        deviceSampleRate_,
        deviceFrameSize,
        paClipOff,
        paCallback,
        this);

    if (err != paNoError) {
        error("Failed to open audio stream: {}", Pa_GetErrorText(err));
        return false;
    }

    if (decimationFactor_ > 1) {
        info("Audio stream opened: {}Hz (decimating to {}Hz), {} device samples/frame, mono",
             deviceSampleRate_, kTargetRate, deviceFrameSize);
    } else {
        info("Audio stream opened: {}Hz, {} samples/frame, mono",
             kTargetRate, kFrameSize);
    }
    return true;
}

bool AudioCapture::start() {
    if (!stream_) {
        error("Audio stream not initialized");
        return false;
    }

    PaError err = Pa_StartStream(stream_);
    if (err != paNoError) {
        error("Failed to start audio stream: {}", Pa_GetErrorText(err));
        return false;
    }

    info("Audio capture started");
    return true;
}

void AudioCapture::stop() {
    if (stream_) {
        Pa_StopStream(stream_);
        Pa_CloseStream(stream_);
        stream_ = nullptr;
        info("Audio capture stopped");
    }
}

void AudioCapture::setFrameCallback(AudioFrameCallback callback) {
    std::lock_guard<std::mutex> lock(callbackMutex_);
    frameCallback_ = std::move(callback);
}

void AudioCapture::startRecording() {
    std::lock_guard<std::mutex> lock(recordingMutex_);
    recordingBuffer_.clear();
    recording_.store(true);
    debug("Recording buffer started");
}

void AudioCapture::stopRecording() {
    recording_.store(false);
    debug("Recording buffer stopped ({} samples)",
          recordingBuffer_.size());
}

std::vector<float> AudioCapture::getRecordingBuffer() {
    std::lock_guard<std::mutex> lock(recordingMutex_);
    return recordingBuffer_;
}

size_t AudioCapture::getRecordingSampleCount() const {
    std::lock_guard<std::mutex> lock(recordingMutex_);
    return recordingBuffer_.size();
}

int AudioCapture::paCallback(const void* input, [[maybe_unused]] void* output,
                              unsigned long frameCount,
                              [[maybe_unused]] const PaStreamCallbackTimeInfo* timeInfo,
                              [[maybe_unused]] PaStreamCallbackFlags statusFlags,
                              void* userData) {
    auto* self = static_cast<AudioCapture*>(userData);
    const auto* samples = static_cast<const int16_t*>(input);

    if (self->decimationFactor_ <= 1) {
        // No resampling needed — deliver directly
        self->processFrame(samples, static_cast<int>(frameCount));
    } else {
        // Downsample: pick every Nth sample (simple decimation).
        // For speech this is fine — we're going from 48kHz to 16kHz,
        // and the content is well below 8kHz (Nyquist for 16kHz).
        int decimated = static_cast<int>(frameCount) / self->decimationFactor_;
        auto& acc = self->frameAccumulator_;

        for (int i = 0; i < decimated; i++) {
            acc.push_back(samples[i * self->decimationFactor_]);
        }

        // Deliver full frames
        while (static_cast<int>(acc.size()) >= kFrameSize) {
            self->processFrame(acc.data(), kFrameSize);
            acc.erase(acc.begin(), acc.begin() + kFrameSize);
        }
    }

    return paContinue;
}

void AudioCapture::processFrame(const int16_t* samples, int numSamples) {
    // Deliver to frame callback (wake word detector, VAD)
    {
        std::lock_guard<std::mutex> lock(callbackMutex_);
        if (frameCallback_) {
            frameCallback_(samples, numSamples);
        }
    }

    // Accumulate into recording buffer as float [-1.0, 1.0] for whisper
    if (recording_.load()) {
        std::lock_guard<std::mutex> lock(recordingMutex_);
        for (int i = 0; i < numSamples; i++) {
            recordingBuffer_.push_back(static_cast<float>(samples[i]) / 32768.0f);
        }
    }
}

}  // namespace creatures
