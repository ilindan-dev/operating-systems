file(GLOB CHAT_CLIENT_MODEL_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/model/*.cpp)

# The client's logic: no Qt, links against "some" Conn (the transport is chosen per binary).
add_library(chat_client_model STATIC ${CHAT_CLIENT_MODEL_SOURCES})
target_include_directories(chat_client_model PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_compile_options(chat_client_model PRIVATE ${CHAT_WARNINGS})
target_link_libraries(chat_client_model PUBLIC chat_conn_api chat_proto chat_posix)

if(BUILD_GUI)
    file(GLOB CHAT_CLIENT_VIEW_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/view/*.cpp)
    add_library(chat_client_view STATIC ${CHAT_CLIENT_VIEW_SOURCES})
    set_target_properties(chat_client_view PROPERTIES AUTOMOC ON)
    target_compile_options(chat_client_view PRIVATE ${CHAT_WARNINGS})
    target_link_libraries(chat_client_view PUBLIC chat_client_model chat_ui Qt6::Widgets)
endif()
