#!/usr/bin/env bash
# Builds host_<type> and client_<type> for every src/common/transport/conn_<type>.cpp,
# puts them next to this script and removes everything CMake generated.
#
# Qt is looked up in $QT_DIR (default: ~/Qt/6.10.2/gcc_64, the online installer's layout),
# otherwise in the system (qt6-base-dev).
set -euo pipefail

cd "$(dirname "$0")"
BUILD_DIR=build
QT_DIR="${QT_DIR:-$HOME/Qt/6.10.2/gcc_64}"

cleanup() {
    rm -rf "$BUILD_DIR"
}
trap cleanup EXIT

cmake_args=(-S . -B "$BUILD_DIR" -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=OFF)
if [[ -d "$QT_DIR" ]]; then
    cmake_args+=("-DCMAKE_PREFIX_PATH=$QT_DIR")
fi

cmake "${cmake_args[@]}"
cmake --build "$BUILD_DIR" -j"$(nproc)"
cp "$BUILD_DIR"/bin/host_* "$BUILD_DIR"/bin/client_* .

echo "Built:" host_* client_*
