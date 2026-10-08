file(GLOB CHAT_HOST_MODEL_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/model/*.cpp)

# The host's logic: no Qt, links against "some" Conn (the transport is chosen per binary).
add_library(chat_host_model STATIC ${CHAT_HOST_MODEL_SOURCES})
target_include_directories(chat_host_model PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_compile_options(chat_host_model PRIVATE ${CHAT_WARNINGS})
target_link_libraries(chat_host_model PUBLIC chat_conn_api chat_proto chat_posix)

if(BUILD_GUI)
    file(GLOB CHAT_HOST_VIEW_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/view/*.cpp)
    add_library(chat_host_view STATIC ${CHAT_HOST_VIEW_SOURCES})
    set_target_properties(chat_host_view PROPERTIES AUTOMOC ON)
    target_compile_options(chat_host_view PRIVATE ${CHAT_WARNINGS})
    target_link_libraries(chat_host_view PUBLIC chat_host_model chat_ui Qt6::Widgets)
endif()
