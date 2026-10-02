#!/bin/bash
# Builds a Docker image with the project and runs all tests inside a container,
# so nothing touches the host system (daemons, signals, /tmp, /dev/log).
#
#   ./run_tests.sh                 # all tests
#   ./run_tests.sh -L unit         # only unit tests
#   ./run_tests.sh -L component    # only component tests
#   ./run_tests.sh -R Sighup       # tests whose name matches a regex
set -euo pipefail

cd "$(dirname "$0")"

IMAGE="disk-monitor-tests"

docker build -f tests/Dockerfile -t "$IMAGE" .

# --rm: delete the container afterwards; --init: a tiny init as PID 1 that reaps orphans.
docker run --rm --init "$IMAGE" ctest --test-dir build --output-on-failure "$@"
