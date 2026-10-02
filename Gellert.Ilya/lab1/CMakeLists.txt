cmake_minimum_required(VERSION 3.14)
project(DiskMonitor VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# -Wall -Werror are set per target in src/ so that they do not apply to third-party code (gtest).
# ON so that IDEs (CLion) see the tests; build.sh turns it off.
option(BUILD_TESTS "Build unit and component tests (requires GoogleTest)" ON)

add_subdirectory(src)

if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
