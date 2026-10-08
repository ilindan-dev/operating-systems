file(GLOB CHAT_PROTO_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)

add_library(chat_proto STATIC ${CHAT_PROTO_SOURCES})
target_include_directories(chat_proto PUBLIC ${PROJECT_SOURCE_DIR}/src/common)
target_compile_options(chat_proto PRIVATE ${CHAT_WARNINGS})
