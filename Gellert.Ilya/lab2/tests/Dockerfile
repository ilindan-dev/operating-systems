# Test image: builds the libraries and the tests (no GUI, no Qt needed) and runs ctest.
FROM ubuntu:24.04

RUN apt-get update \
    && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
       build-essential cmake libgtest-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .

RUN cmake -S . -B build -DBUILD_GUI=OFF -DCMAKE_BUILD_TYPE=Debug \
    && cmake --build build -j"$(nproc)"

CMD ["ctest", "--test-dir", "build", "--output-on-failure"]
