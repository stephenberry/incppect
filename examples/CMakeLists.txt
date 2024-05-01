find_uwebsockets()

# incppect directory:
get_filename_component(PARENT_DIR "${CMAKE_CURRENT_SOURCE_DIR}" DIRECTORY)
include_directories("${PARENT_DIR}/include")

#add_subdirectory(hello-browser)
#add_subdirectory(hello-browser-ssl)
#add_subdirectory(client-info)
add_subdirectory(balls2d)
#add_subdirectory(balls3d)
#add_subdirectory(send)
