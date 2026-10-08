add_subdirectory(posix)
add_subdirectory(proto)
add_subdirectory(transport)

if(BUILD_GUI)
    add_subdirectory(ui)
endif()
