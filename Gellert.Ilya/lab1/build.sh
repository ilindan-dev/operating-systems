#!/bin/bash
# Builds ./disk_monitor from sources and removes all intermediate files.
set -euo pipefail

cd "$(dirname "$0")"

BUILD_DIR="build"

# Intermediate files are removed on any exit, including a failed build.
cleanup() {
    rm -rf "$BUILD_DIR"
}
trap cleanup EXIT

echo "Building DiskMonitor via CMake..."

cmake -S . -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=OFF
cmake --build "$BUILD_DIR" --parallel "$(nproc)"

cp "$BUILD_DIR/src/disk_monitor" ./disk_monitor

echo "Done! Program compiled: ./disk_monitor"
