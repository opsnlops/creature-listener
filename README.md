# creature-listener

Wake word conversational interface for Beaky the animatronic parrot.

Say "Hey Beaky" to wake her up, speak a question or comment, and Beaky responds conversationally via the creature-server TTS + animation pipeline.

## Architecture

Runs on the same Pi 5 as creature-controller. Lightweight state machine:

1. **LISTENING** — LOWWI wake word detection (openWakeWord ONNX models, <5% CPU)
2. **ACK** — Trigger acknowledgment animation
3. **RECORDING** — Capture speech, VAD detects end-of-speech
4. **STT** — whisper.cpp transcribes audio
5. **LLM** — Stream response from llama-server (Mistral Nemo 12B on M1 Mac)
6. **SPEAKING** — Sentences flow to creature-server streaming session → TTS → animation → playback

## Building

### Prerequisites

- CMake 3.25+
- libcurl development headers
- ALSA development headers (Linux)

All other dependencies (LOWWI, ONNX Runtime, PortAudio, whisper.cpp, spdlog, etc.) are fetched via FetchContent.

### Build

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

### Build Debian package

```bash
./build_deb.sh
```

## Running

### List audio devices

```bash
./creature-listener --list-devices
```

### Basic usage (keyboard trigger)

```bash
./creature-listener \
    --creature-id "YOUR-CREATURE-UUID" \
    --whisper-model ../data/ggml-tiny.en.bin
```

### Full usage with wake word

```bash
./creature-listener \
    --creature-id "YOUR-CREATURE-UUID" \
    --wake-word-model /path/to/hey_beaky.onnx \
    --mel-model /path/to/melspectrogram.onnx \
    --embedding-model /path/to/embedding_model.onnx \
    --whisper-model /path/to/ggml-tiny.en.bin \
    --llm-host 10.69.66.4 \
    --llm-port 1234 \
    --creature-server-url https://server.prod.chirpchirp.dev
```

## Model Files

Run `./download_models.sh` or let CMake download them during configure:

| File | Size | Purpose |
|------|------|---------|
| ggml-tiny.en.bin | ~75MB | Whisper STT model |
| silero_vad.onnx | ~2MB | Voice activity detection |
| melspectrogram.onnx | ~1MB | LOWWI shared audio preprocessing |
| embedding_model.onnx | ~1.3MB | LOWWI shared feature extraction |
| hey_beaky.onnx | ~200KB | Wake word classifier (train with openwakeword-trainer) |

### Training a custom wake word

Use [openwakeword-trainer](https://github.com/lgpearson1771/openwakeword-trainer) to train a "Hey Beaky" classifier. Two-word phrases produce better models than single words. The output `.onnx` file is your `--wake-word-model`.
