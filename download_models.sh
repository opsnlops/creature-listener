#!/bin/bash
#
# Download model files for creature-listener.
# Run this once before building, or let CMake download them automatically.
#

set -e

DATA_DIR="${1:-./data}"
mkdir -p "$DATA_DIR"

echo "Downloading models to $DATA_DIR..."

# Whisper tiny.en model (~75MB)
WHISPER_MODEL="$DATA_DIR/ggml-tiny.en.bin"
if [ ! -f "$WHISPER_MODEL" ]; then
    echo "Downloading whisper tiny.en model (~75MB)..."
    curl -L -o "$WHISPER_MODEL" \
        "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-tiny.en.bin"
    echo "Done: $WHISPER_MODEL"
else
    echo "Already exists: $WHISPER_MODEL"
fi

# Silero VAD model (~2MB)
SILERO_MODEL="$DATA_DIR/silero_vad.onnx"
if [ ! -f "$SILERO_MODEL" ]; then
    echo "Downloading Silero VAD model (~2MB)..."
    curl -L -o "$SILERO_MODEL" \
        "https://github.com/snakers4/silero-vad/raw/master/src/silero_vad/data/silero_vad.onnx"
    echo "Done: $SILERO_MODEL"
else
    echo "Already exists: $SILERO_MODEL"
fi

echo ""
echo "All models downloaded to $DATA_DIR"
echo ""
echo "Porcupine wake word model must be trained separately:"
echo "  1. Go to https://console.picovoice.ai/"
echo "  2. Train a custom wake word for your creature"
echo "  3. Download the .ppn file for Raspberry Pi (ARM Cortex-A76)"
echo "  4. Also download porcupine_params.pv from Picovoice releases"
