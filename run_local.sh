#!/bin/bash
#
# Run creature-listener locally for development / testing.
# Uses the build directory's models and a local test config.
#

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"

if [ ! -f "${BUILD_DIR}/creature-listener" ]; then
    echo "Build first: mkdir build && cd build && cmake .. -DCMAKE_BUILD_TYPE=Release && make -j$(nproc)"
    exit 1
fi

# Set up models/ symlinks so LOWWI can find its hardcoded paths
mkdir -p "${BUILD_DIR}/models"
ln -sf "${BUILD_DIR}/_deps/lowwi-src/models/melspectrogram.onnx" "${BUILD_DIR}/models/"
ln -sf "${BUILD_DIR}/_deps/lowwi-src/models/embedding_model.onnx" "${BUILD_DIR}/models/"

cd "${BUILD_DIR}"
exec ./creature-listener --config-path "${1:-./test-config.yaml}" --verbose "${@:2}"
