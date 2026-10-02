# GoogleTest: the system package if installed (libgtest-dev in Docker), otherwise download it.
find_package(GTest QUIET)
if(NOT GTest_FOUND)
    include(FetchContent)
    FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
endif()

include(GoogleTest)

# ---------- Unit tests: classes in isolation, only temporary directories ----------
add_executable(unit_tests
    main_test.cpp
    ut/config_test.cpp
    ut/daemon_test.cpp
    ut/pid_manager_test.cpp
    ut/worker_test.cpp
)
target_include_directories(unit_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(unit_tests PRIVATE -Wall -Werror)
target_link_libraries(unit_tests PRIVATE monitor_core GTest::gtest)
gtest_discover_tests(unit_tests
    TEST_PREFIX "unit."
    PROPERTIES LABELS unit TIMEOUT 30
)

# ---------- Component tests: the real disk_monitor binary as a black box ----------
# They start daemons, send signals, use /tmp/disk_monitor.pid and replace /dev/log,
# so they only run inside the Docker container (see run_tests.sh).
add_executable(component_tests
    ct/component_test.cpp
    ct/daemon_runner.cpp
    ct/syslog_collector.cpp
)
target_include_directories(component_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(component_tests PRIVATE -Wall -Werror)
target_compile_definitions(component_tests PRIVATE
    DISK_MONITOR_BIN="$<TARGET_FILE:disk_monitor>"
)
target_link_libraries(component_tests PRIVATE monitor_core GTest::gtest_main)
add_dependencies(component_tests disk_monitor)
gtest_discover_tests(component_tests
    TEST_PREFIX "component."
    PROPERTIES LABELS component TIMEOUT 60 RUN_SERIAL TRUE
)
