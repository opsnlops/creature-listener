# creature-listener

Wake word conversational interface for [April's Creature Workshop](https://github.com/opsnlops/creature-server).

Say a creature's wake word, speak a question or comment, and the creature responds conversationally — LLM generates the reply, creature-server synthesizes speech with ElevenLabs, builds an animation, and plays it back on the real hardware.

## How It Works

```
Mic → Wake Word → Record Speech → STT → LLM → creature-server → Creature speaks
```

State machine running on the Pi 5 alongside creature-controller:

1. **LISTENING** — LOWWI wake word detection (openWakeWord ONNX models, <5% CPU)
2. **ACK** — Acknowledge wake word (user feedback that the creature heard them)
3. **RECORDING** — Capture speech with energy-based VAD; stop on silence
4. **STT** — Transcription via creature-server (Ryzen 9 + whisper base.en), or local whisper as fallback
5. **LLM** — Stream response from llama-server with sentence splitting
6. **SPEAKING** — Each sentence streams to creature-server's ad-hoc session API → ElevenLabs TTS → animation → playback

Conversation history is maintained in memory so the creature remembers context across turns.

## Building

### Prerequisites

- CMake 3.25+
- C++20 compiler (GCC 12+ or Clang 15+)
- libcurl development headers (`libcurl4-openssl-dev` on Debian)
- ALSA development headers (`libasound2-dev` on Linux)

Everything else (whisper.cpp, LOWWI, ONNX Runtime, PortAudio, spdlog, fmt, yaml-cpp, OpenTelemetry, nlohmann_json, argparse) is fetched automatically via CMake FetchContent.

### Build from source

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

The first build downloads model files and compiles all dependencies. Subsequent builds are fast.

### Build Debian package

```bash
./build_deb.sh
```

Or use Docker to build a Linux .deb on macOS:

```bash
docker build -f Dockerfile . --target package -t creature-listener-pkg
docker create --name tmp creature-listener-pkg
docker cp tmp:/package/ .
docker rm tmp
```

## Running

### Quick start — development / testing on macOS

No wake word model needed. Press Enter to simulate a wake word, then talk into your Mac's mic:

```bash
cd build
./creature-listener \
    --config-path /path/to/creature-listener.yaml \
    --verbose
```

What happens:
1. creature-listener starts and prints `Press Enter to simulate wake word detection`
2. Press Enter — it starts recording from your mic
3. Speak — you'll see `Speech detected — recording...`
4. Stop talking — after silence it transcribes, calls the LLM, and sends to creature-server
5. The creature speaks the response and it returns to listening
6. Press Enter again for another turn (conversation history is preserved)

### List audio devices

```bash
./creature-listener --list-devices
```

Pick a device index and pass it with `--audio-device N`. USB audio devices that don't support 16kHz natively (e.g. Sound Blaster Play! 3) are automatically opened at their native rate and decimated to 16kHz.

### Production — Pi 5 with wake word

```bash
./creature-listener --config-path /etc/creature-listener.yaml
```

### Debian package (systemd)

After installing the `.deb`:

```bash
# Copy the sample config and edit — set creatureId, system prompt, and wake word model
sudo cp /etc/creature-listener.yaml.sample /etc/creature-listener.yaml
sudo vim /etc/creature-listener.yaml

# Optionally set Honeycomb API key for tracing
sudo vim /etc/default/creature-listener

# Enable and start
sudo systemctl enable --now creature-listener
sudo journalctl -u creature-listener -f
```

## Configuration

All settings live in a YAML config file. See [`debian/creature-listener.yaml`](debian/creature-listener.yaml) for the full template.

Key fields:

```yaml
creatureId: <uuid>
creatureServerUrl: https://server.prod.chirpchirp.dev

llmHost: 10.69.66.4
llmPort: 1234
llmModel: mistral-nemo
llmSystemPrompt: |
  Your creature-specific system prompt here.
  This defines the creature's personality.

# STT: use creature-server for transcription (default, fast).
# Set to false to run whisper locally on the Pi (slow but offline).
useServerStt: true

# Wake word detection — only wakeWordModel needs to be set per creature.
# melModel and embeddingModel are shared and ship in the .deb package.
wakeWordModel: /usr/share/creature-listener/data/<wakeword>.onnx
melModel: /usr/share/creature-listener/data/melspectrogram.onnx
embeddingModel: /usr/share/creature-listener/data/embedding_model.onnx
```

CLI flags (`--creature-id`, `--llm-host`, etc.) override config file values for quick testing.

## Tracing

creature-listener supports OpenTelemetry distributed tracing. Set `honeycombApiKey` in the config (or `HONEYCOMB_API_KEY` env var, or `--honeycomb-api-key` CLI) and every conversation turn produces a trace with spans for STT, LLM, and server communication. The `traceparent` header is propagated to creature-server, so server-side spans (STT, TTS, audio encoding, animation) appear in the same trace.

Spans created per conversation turn:
- `conversation.turn` — root span covering the entire interaction
  - `stt.transcribe` — speech-to-text (server-side or local)
  - `server.streaming_session` — creature-server session lifecycle
  - `llm.respond` — LLM streaming response
  - (creature-server adds its own child spans: whisper STT, ElevenLabs TTS, Opus encoding, animation build, etc.)

## Model Files

The `.deb` package includes all required models. For local builds, CMake downloads them during configure.

### Included in the package

| File | Size | Purpose |
|------|------|---------|
| `ggml-tiny.en-q5_1.bin` | ~31MB | Whisper STT quantized model (local fallback) |
| `ggml-tiny.en.bin` | ~75MB | Whisper STT full model (local fallback) |
| `silero_vad.onnx` | ~2MB | Voice activity detection |
| `melspectrogram.onnx` | ~1MB | LOWWI shared audio preprocessing |
| `embedding_model.onnx` | ~1.3MB | LOWWI shared feature extraction |

### Per-creature (you provide)

| File | Size | Source |
|------|------|--------|
| `<wakeword>.onnx` | ~200KB | Train with [openwakeword-trainer](https://github.com/lgpearson1771/openwakeword-trainer) or [openwakeword.com](https://openwakeword.com/) |

### Training a custom wake word

You don't need to record your voice. The trainer generates thousands of synthetic speech clips using Piper TTS with varied voices, augments them with noise, and trains a small classifier. Two-word phrases (e.g. "Hey Beaky") work much better than single words. Options:

1. **[openwakeword.com](https://openwakeword.com/)** — web-based, no setup
2. **[Google Colab notebook](https://github.com/dscripka/openWakeWord/blob/main/notebooks/automatic_model_training.ipynb)** — free GPU, ~45 minutes
3. **[openwakeword-trainer](https://github.com/lgpearson1771/openwakeword-trainer)** (Docker) — local, needs Linux or Docker

The output `.onnx` file is your `wakeWordModel` config value. Copy it to `/usr/share/creature-listener/data/` on the Pi.
