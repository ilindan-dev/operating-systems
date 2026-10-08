cmake_minimum_required(VERSION 3.16)

project(LocalChat VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(BUILD_GUI "Build host_* and client_* (requires Qt 6 Widgets)" ON)
option(BUILD_TESTS "Build unit and component tests (requires GoogleTest)" ON)

# Applied per target, so they do not hit third-party code (gtest).
set(CHAT_WARNINGS -Wall -Wextra -Werror)

find_package(Threads REQUIRED)
if(BUILD_GUI)
    find_package(Qt6 REQUIRED COMPONENTS Widgets)
endif()

add_subdirectory(src)

if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
