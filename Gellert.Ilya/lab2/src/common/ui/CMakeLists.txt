file(GLOB CHAT_UI_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)

# The chat window shared by host_* and client_*.
add_library(chat_ui STATIC ${CHAT_UI_SOURCES})
set_target_properties(chat_ui PROPERTIES AUTOMOC ON)
target_include_directories(chat_ui PUBLIC ${PROJECT_SOURCE_DIR}/src/common)
target_compile_options(chat_ui PRIVATE ${CHAT_WARNINGS})
target_link_libraries(chat_ui PUBLIC chat_posix chat_proto Qt6::Widgets)
