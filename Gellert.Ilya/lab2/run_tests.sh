#!/usr/bin/env bash
# Runs all tests in Docker, so that test processes, signals, FIFOs and semaphores
# never touch the host system. Extra arguments go to ctest, e.g.:
#   ./run_tests.sh -L unit          # only unit tests
#   ./run_tests.sh -R fifo          # only the FIFO transport
set -euo pipefail

cd "$(dirname "$0")"
IMAGE=local-chat-tests

docker build -f tests/Dockerfile -t "$IMAGE" .
docker run --rm --init "$IMAGE" ctest --test-dir build --output-on-failure "$@"
