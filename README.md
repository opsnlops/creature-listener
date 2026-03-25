# creature-listener

Wake word conversational interface for Beaky the animatronic parrot.

Say "Hey Beaky" to wake her up, speak a question or comment, and Beaky responds conversationally — LLM generates the reply, creature-server synthesizes speech with ElevenLabs, builds an animation, and plays it back on the real hardware.

## How It Works

```
Mic → Wake Word → Record Speech → Whisper STT → LLM → creature-server → Beaky speaks
```

State machine running on the Pi 5 alongside creature-controller:

1. **LISTENING** — LOWWI wake word detection (openWakeWord ONNX models, <5% CPU)
2. **ACK** — Acknowledge wake word (user feedback that Beaky heard them)
3. **RECORDING** — Capture speech with energy-based VAD; stop on silence
4. **STT** — whisper.cpp (tiny.en) transcribes the audio (~150ms on M3 Max, ~3-9s on Pi 5)
5. **LLM** — Stream response from llama-server with sentence splitting
6. **SPEAKING** — Each sentence streams to creature-server's ad-hoc session API → ElevenLabs TTS → animation → playback

Conversation history is maintained in memory so Beaky remembers context across turns.

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

No wake word model needed. Press Enter to simulate "Hey Beaky", then talk into your Mac's mic:

```bash
cd build
./creature-listener \
    --creature-id "4754fc0e-1706-11ef-931d-bbb95a696e2e" \
    --whisper-model ./data/ggml-tiny.en.bin \
    --vad-model ./data/silero_vad.onnx \
    --creature-server-url https://server.prod.chirpchirp.dev \
    --llm-host 10.69.66.4 \
    --llm-port 1234 \
    --verbose
```

What happens:
1. creature-listener starts and prints `Press Enter to simulate wake word detection`
2. Press Enter — it starts recording from your mic
3. Speak — you'll see `Speech detected — recording...`
4. Stop talking — after 1.5s of silence it transcribes, calls the LLM, and sends to creature-server
5. Beaky speaks the response and it returns to listening
6. Press Enter again for another turn (conversation history is preserved)

Add `--silence-duration-ms 2000` if it cuts you off too early, or `--max-record-seconds 20` for longer recordings.

### List audio devices

```bash
./creature-listener --list-devices
```

Pick a device index and pass it with `--audio-device N`.

### Production — Pi 5 with wake word

```bash
./creature-listener \
    --creature-id "4754fc0e-1706-11ef-931d-bbb95a696e2e" \
    --wake-word-model /path/to/hey_beaky.onnx \
    --mel-model /path/to/melspectrogram.onnx \
    --embedding-model /path/to/embedding_model.onnx \
    --whisper-model /usr/share/creature-listener/data/ggml-tiny.en.bin \
    --vad-model /usr/share/creature-listener/data/silero_vad.onnx \
    --creature-server-url https://server.prod.chirpchirp.dev \
    --llm-host 10.69.66.4 \
    --llm-port 1234
```

### Debian package (systemd)

After installing the `.deb`:

```bash
# Copy and edit the environment file
sudo cp /etc/default/creature-listener.env.example /etc/default/creature-listener
sudo nano /etc/default/creature-listener
# Fill in CREATURE_ID, and optionally HONEYCOMB_API_KEY

# Enable and start
sudo systemctl enable --now creature-listener
sudo journalctl -u creature-listener -f
```

## Tracing

creature-listener supports OpenTelemetry distributed tracing. Set `HONEYCOMB_API_KEY` (env var or `--honeycomb-api-key`) and every conversation turn produces a trace with spans for STT, LLM, and server communication. The `traceparent` header is propagated to creature-server, so server-side spans (TTS, audio encoding, animation) appear in the same trace.

```bash
HONEYCOMB_API_KEY=hcaik_your_key_here ./creature-listener \
    --creature-id "..." \
    --whisper-model ./data/ggml-tiny.en.bin \
    ...
```

Spans created per conversation turn:
- `conversation.turn` — root span covering the entire interaction
  - `stt.transcribe` — whisper.cpp transcription
  - `server.streaming_session` — creature-server session lifecycle
  - `llm.respond` — LLM streaming response
  - (creature-server adds its own child spans: TTS, audio encoding, animation, etc.)

## Configuration Reference

| Flag | Default | Description |
|------|---------|-------------|
| `--creature-id` | (required) | Creature UUID |
| `--creature-server-url` | `https://server.prod.chirpchirp.dev` | creature-server base URL |
| `--llm-host` | `10.69.66.4` | llama-server hostname |
| `--llm-port` | `1234` | llama-server port |
| `--llm-model` | `mistral-nemo` | Model name |
| `--llm-temperature` | `1.2` | LLM temperature |
| `--llm-max-tokens` | `256` | Max response tokens |
| `--llm-system-prompt` | (built-in Beaky prompt) | System prompt text or path to file |
| `--whisper-model` | `/usr/share/creature-listener/data/ggml-tiny.en.bin` | Whisper model path |
| `--vad-model` | `/usr/share/creature-listener/data/silero_vad.onnx` | Silero VAD model path |
| `--vad-threshold` | `0.5` | VAD confidence threshold |
| `--silence-duration-ms` | `1500` | Silence before ending recording |
| `--max-record-seconds` | `15` | Maximum recording length |
| `--min-sentence-chars` | `50` | Minimum chars before yielding sentence to TTS |
| `--conversation-history` | `10` | Max conversation exchanges to remember |
| `--wake-word-model` | (none) | Wake word classifier .onnx path |
| `--mel-model` | (none) | melspectrogram.onnx path |
| `--embedding-model` | (none) | embedding_model.onnx path |
| `--wake-word-threshold` | `0.5` | Wake word confidence threshold |
| `--audio-device` | `-1` (default) | PortAudio device index |
| `--honeycomb-api-key` | (none) | Honeycomb API key (or `HONEYCOMB_API_KEY` env var) |
| `--honeycomb-dataset` | `creature-listener` | Honeycomb dataset name |
| `--verbose` | off | Enable debug logging |
| `--list-devices` | | List audio devices and exit |
| `--no-resume-playlist` | | Don't resume playlist after speaking |

## Model Files

CMake downloads these during configure, or run `./download_models.sh` manually:

| File | Size | Source |
|------|------|--------|
| `ggml-tiny.en.bin` | ~75MB | Whisper STT (English-only, fast on Pi 5) |
| `silero_vad.onnx` | ~2MB | Voice activity detection |

Wake word models (for production with "Hey Beaky" trigger):

| File | Size | Source |
|------|------|--------|
| `melspectrogram.onnx` | ~1MB | Ships with LOWWI (`build/_deps/lowwi-src/models/`) |
| `embedding_model.onnx` | ~1.3MB | Ships with LOWWI (`build/_deps/lowwi-src/models/`) |
| `hey_beaky.onnx` | ~200KB | Train with [openwakeword-trainer](https://github.com/lgpearson1771/openwakeword-trainer) or [openwakeword.com](https://openwakeword.com/) |

### Training a custom wake word

You don't need to record your voice. The trainer generates thousands of synthetic speech clips using Piper TTS with varied voices, augments them with noise, and trains a small classifier. Use "Hey Beaky" (two words work much better than one). Options:

1. **[openwakeword.com](https://openwakeword.com/)** — web-based, no setup
2. **[Google Colab notebook](https://github.com/dscripka/openWakeWord/blob/main/notebooks/automatic_model_training.ipynb)** — free GPU, ~45 minutes
3. **[openwakeword-trainer](https://github.com/lgpearson1771/openwakeword-trainer)** (Docker) — local, needs Linux or Docker

The output `.onnx` file is your `--wake-word-model`.
