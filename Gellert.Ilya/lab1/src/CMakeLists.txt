add_library(monitor_core STATIC
    config/config.cpp
    daemon/daemon.cpp
    daemon/pid_manager.cpp
    worker/worker.cpp
)

target_include_directories(monitor_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(monitor_core PRIVATE -Wall -Werror)

add_executable(disk_monitor main.cpp)
target_compile_options(disk_monitor PRIVATE -Wall -Werror)
target_link_libraries(disk_monitor PRIVATE monitor_core)
