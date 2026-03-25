# creature-listener

Wake word conversational interface for [April's Creature Workshop](https://github.com/opsnlops/creature-server).

Say a creature's wake word, speak a question or comment, and the creature responds conversationally — LLM generates the reply, creature-server synthesizes speech with ElevenLabs, builds an animation, and plays it back on the real hardware.

## How It Works

```
Mic → Wake Word → Record Speech → Whisper STT → LLM → creature-server → Creature speaks
```

State machine running on the Pi 5 alongside creature-controller:

1. **LISTENING** — LOWWI wake word detection (openWakeWord ONNX models, <5% CPU)
2. **ACK** — Acknowledge wake word (user feedback that the creature heard them)
3. **RECORDING** — Capture speech with energy-based VAD; stop on silence
4. **STT** — whisper.cpp (tiny.en) transcribes the audio (~150ms on M3 Max, ~3-9s on Pi 5)
5. **LLM** — Stream response from llama-server with sentence splitting
6. **SPEAKING** — Each sentence streams to creature-server's ad-hoc session API → ElevenLabs TTS → animation → playback

Conversation history is maintained in memory so the creature remembers context across turns.

## Building

### Prerequisites

- CMake 3.25+
- C++20 compiler (GCC 12+ or Clang 15+)
- libcurl development headers (`libcurl4-openssl-dev` on Debian)
- ALSA development headers (`libasound2-dev` on Linux)

Everything else (whisper.cpp, LOWWI, ONNX Runtime, PortAudio, spdlog, fmt, OpenTelemetry, nlohmann_json, argparse) is fetched automatically via CMake FetchContent.

### Build from source

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

The first build downloads model files (~75MB whisper model, ~2MB Silero VAD) and compiles all dependencies. Subsequent builds are fast.

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

Pick a device index and pass it with `--audio-device N`.

### Production — Pi 5 with wake word

```bash
./creature-listener --config-path /etc/creature-listener.yaml
```

### Debian package (systemd)

After installing the `.deb`:

```bash
# Copy the sample config and edit — set creatureId and system prompt
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

whisperModel: /usr/share/creature-listener/data/ggml-tiny.en.bin
```

CLI flags (`--creature-id`, `--llm-host`, etc.) override config file values for quick testing.

## Tracing

creature-listener supports OpenTelemetry distributed tracing. Set `HONEYCOMB_API_KEY` (env var or `--honeycomb-api-key`) and every conversation turn produces a trace with spans for STT, LLM, and server communication. The `traceparent` header is propagated to creature-server, so server-side spans (TTS, audio encoding, animation) appear in the same trace.

Spans created per conversation turn:
- `conversation.turn` — root span covering the entire interaction
  - `stt.transcribe` — whisper.cpp transcription
  - `server.streaming_session` — creature-server session lifecycle
  - `llm.respond` — LLM streaming response

## Model Files

CMake downloads these during configure, or run `./download_models.sh` manually:

| File | Size | Source |
|------|------|--------|
| `ggml-tiny.en.bin` | ~75MB | Whisper STT (English-only, fast on Pi 5) |
| `silero_vad.onnx` | ~2MB | Voice activity detection |

Wake word models (optional — for hands-free activation):

| File | Size | Source |
|------|------|--------|
| `melspectrogram.onnx` | ~1MB | Ships with LOWWI (`build/_deps/lowwi-src/models/`) |
| `embedding_model.onnx` | ~1.3MB | Ships with LOWWI (`build/_deps/lowwi-src/models/`) |
| `<wakeword>.onnx` | ~200KB | Train with [openwakeword-trainer](https://github.com/lgpearson1771/openwakeword-trainer) or [openwakeword.com](https://openwakeword.com/) |

### Training a custom wake word

You don't need to record your voice. The trainer generates thousands of synthetic speech clips using Piper TTS with varied voices, augments them with noise, and trains a small classifier. Two-word phrases (e.g. "Hey Beaky") work much better than single words. Options:

1. **[openwakeword.com](https://openwakeword.com/)** — web-based, no setup
2. **[Google Colab notebook](https://github.com/dscripka/openWakeWord/blob/main/notebooks/automatic_model_training.ipynb)** — free GPU, ~45 minutes
3. **[openwakeword-trainer](https://github.com/lgpearson1771/openwakeword-trainer)** (Docker) — local, needs Linux or Docker

The output `.onnx` file is your `wakeWordModel` config value.
