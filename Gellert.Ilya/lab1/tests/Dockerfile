# Builds the project with tests and runs them in an isolated container.
# The component tests start real daemons, send signals, use /tmp/disk_monitor.pid and replace
# /dev/log, so they must never run directly on the host.
#
# Usage (from the lab1 directory):  ./run_tests.sh
FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update \
    && apt-get install -y --no-install-recommends build-essential cmake libgtest-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /lab1
COPY . .

# The same flags as build.sh (-Wall -Werror come from CMakeLists), plus the tests.
RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTS=ON \
    && cmake --build build --parallel "$(nproc)"

# Allows the component tests to run (they skip themselves without this variable).
ENV DISK_MONITOR_IN_CONTAINER=1

CMD ["ctest", "--test-dir", "build", "--output-on-failure"]
