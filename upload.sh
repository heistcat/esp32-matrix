#!/bin/bash
set -e

FQBN="esp32:esp32:nologo_esp32c3_super_mini"
PROPS="PartitionScheme=huge_app"
PORT="${1:-/dev/ttyACM0}"
SKETCH_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

echo "=== Compiling esp32c3_fastled ($PROPS) ==="
arduino-cli compile --fqbn "$FQBN" --board-options "$PROPS" "$SKETCH_DIR"

echo "=== Uploading to $PORT ==="
arduino-cli upload -p "$PORT" --fqbn "$FQBN" --board-options "$PROPS" "$SKETCH_DIR"

echo "=== Done! ==="
