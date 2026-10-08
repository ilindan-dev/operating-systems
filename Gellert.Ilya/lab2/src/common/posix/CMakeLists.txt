file(GLOB CHAT_POSIX_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)

add_library(chat_posix STATIC ${CHAT_POSIX_SOURCES})
target_include_directories(chat_posix PUBLIC ${PROJECT_SOURCE_DIR}/src/common)
target_compile_options(chat_posix PRIVATE ${CHAT_WARNINGS})
target_link_libraries(chat_posix PUBLIC Threads::Threads)
