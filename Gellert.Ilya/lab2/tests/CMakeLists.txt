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

# ---------- Unit tests: posix wrappers and the message format ----------
file(GLOB CHAT_UNIT_TEST_SOURCES CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/ut/posix/*.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/ut/proto/*.cpp)
add_executable(unit_tests ${CHAT_UNIT_TEST_SOURCES})
target_include_directories(unit_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(unit_tests PRIVATE ${CHAT_WARNINGS})
target_link_libraries(unit_tests PRIVATE chat_posix chat_proto GTest::gtest_main)
gtest_discover_tests(unit_tests TEST_PREFIX "unit." PROPERTIES LABELS unit TIMEOUT 30)

# ---------- Per transport: the same tests against conn_fifo, conn_sock, conn_pipe ----------
get_property(CHAT_TRANSPORTS GLOBAL PROPERTY CHAT_TRANSPORTS)
foreach(type IN LISTS CHAT_TRANSPORTS)
    # Conn on its own: both ends in one process.
    add_executable(conn_tests_${type} ut/transport/conn_test.cpp)
    target_include_directories(conn_tests_${type} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_options(conn_tests_${type} PRIVATE ${CHAT_WARNINGS})
    target_compile_definitions(conn_tests_${type} PRIVATE CHAT_TRANSPORT="${type}")
    target_link_libraries(conn_tests_${type} PRIVATE conn_${type} GTest::gtest_main)
    gtest_discover_tests(conn_tests_${type}
            TEST_PREFIX "unit.${type}."
            PROPERTIES LABELS unit TIMEOUT 30)

    # The whole chat without GUI: Hub in the test process, clients in forked processes.
    add_executable(chat_tests_${type} ct/chat_test.cpp)
    target_include_directories(chat_tests_${type} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_options(chat_tests_${type} PRIVATE ${CHAT_WARNINGS})
    target_link_libraries(chat_tests_${type} PRIVATE
            chat_host_model chat_client_model conn_${type} GTest::gtest_main)
    gtest_discover_tests(chat_tests_${type}
            TEST_PREFIX "component.${type}."
            PROPERTIES LABELS component TIMEOUT 60 RUN_SERIAL TRUE)
endforeach()
