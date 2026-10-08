# Shared code of all transports (everything here except conn_*.cpp).
file(GLOB CHAT_CHANNEL_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
list(FILTER CHAT_CHANNEL_SOURCES EXCLUDE REGEX "/conn_[^/]*\\.cpp$")

add_library(chat_channel STATIC ${CHAT_CHANNEL_SOURCES})
target_include_directories(chat_channel PUBLIC ${PROJECT_SOURCE_DIR}/src/common)
target_compile_options(chat_channel PRIVATE ${CHAT_WARNINGS})
target_link_libraries(chat_channel PUBLIC chat_posix chat_proto)

# Just the Conn interface, for code that works with "some" transport (host/client models).
add_library(chat_conn_api INTERFACE)
target_include_directories(chat_conn_api INTERFACE ${PROJECT_SOURCE_DIR}/src/common)

# One library per conn_<type>.cpp; src/CMakeLists.txt makes host_<type>/client_<type> from it.
file(GLOB CHAT_CONN_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/conn_*.cpp)
set(types "")
foreach(source IN LISTS CHAT_CONN_SOURCES)
    get_filename_component(name ${source} NAME_WE)
    string(REPLACE "conn_" "" type ${name})
    add_library(conn_${type} STATIC ${source})
    target_compile_options(conn_${type} PRIVATE ${CHAT_WARNINGS})
    target_link_libraries(conn_${type} PUBLIC chat_conn_api chat_channel)
    list(APPEND types ${type})
endforeach()
set_property(GLOBAL PROPERTY CHAT_TRANSPORTS ${types})
