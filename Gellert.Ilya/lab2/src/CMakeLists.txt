add_subdirectory(common)
add_subdirectory(host)
add_subdirectory(client)

if(NOT BUILD_GUI)
    return()
endif()

# For every transport conn_<type>.cpp: host_<type> and client_<type>.
get_property(CHAT_TRANSPORTS GLOBAL PROPERTY CHAT_TRANSPORTS)
foreach(type IN LISTS CHAT_TRANSPORTS)
    add_executable(host_${type} host/host.cpp)
    # chat_*_model must come before conn_<type>: the model uses Conn, conn_<type> defines it.
    target_link_libraries(host_${type} PRIVATE chat_host_view chat_host_model conn_${type})

    add_executable(client_${type} client/client.cpp)
    target_link_libraries(client_${type} PRIVATE chat_client_view chat_client_model conn_${type})

    foreach(target host_${type} client_${type})
        target_compile_options(${target} PRIVATE ${CHAT_WARNINGS})
        set_target_properties(${target} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin)
    endforeach()
endforeach()
