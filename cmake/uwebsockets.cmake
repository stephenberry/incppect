if (SHOW_MODULE_IMPORTS)
    get_filename_component(CURRENT_MODULE_NAME "${CMAKE_CURRENT_LIST_FILE}" NAME)
    message(STATUS "  Importing ${CURRENT_MODULE_NAME}.")
endif()

macro(make_uwebsockets)
   set(CMAKE_CXX_STANDARD 20)
   set(CMAKE_CXX_STANDARD_REQUIRED ON)
   set(CMAKE_CXX_EXTENSIONS OFF)

   if (MSVC)
    # Suppress warning C4996 (deprecated declarations) for Visual Studio
    add_compile_options(/wd4996)
    add_compile_options(/wd4100)

   elseif (CMAKE_COMPILER_IS_GNUCC OR CMAKE_COMPILER_IS_CLANG)
    # Suppress warnings about deprecated OpenSSL functions for GCC and Clang
    add_compile_options(-Wno-deprecated-declarations)
    add_compile_options(-Wunused-parameter)

   endif()


   if (NOT TARGET uWS)
   
      if (NOT INCPPECT_NO_SSL)
         find_package(OpenSSL REQUIRED)
      else ()
         unset(OPENSSL_LIBRARIES)
         unset(OPENSSL_INCLUDE_DIR)
      endif ()
   
      set(uSockets_SRC_DIR "${CMAKE_BINARY_DIR}/_deps/uwebsockets-src/uSockets/src")
      set(uWebSockets_SRC_DIR "${CMAKE_BINARY_DIR}/_deps/uwebsockets-src")

      if (WIN32)
         message("Warning: uWebSockets is not supported on Windows.")
         
      elseif (APPLE)

         # TODO: APPLE is untested...
         
         add_library(uWS SHARED
             ${uSockets_SRC_DIR}/context.c
             ${uSockets_SRC_DIR}/loop.c
             ${uSockets_SRC_DIR}/socket.c
             ${uSockets_SRC_DIR}/crypto/openssl.c
             ${uSockets_SRC_DIR}/eventing/libuv.c
         )
         target_include_directories(uWS PRIVATE "${uSockets_SRC_DIR}" "${LIBUV_INCLUDE_DIR}" "${OPENSSL_INCLUDE_DIR}")
         target_link_libraries(uWS PRIVATE "${LIBUV_LIBRARIES}")
         target_compile_definitions(uWS PRIVATE LIBUS_USE_LIBUV=1)
         
      elseif(UNIX)
         add_library(uWS SHARED
             ${uSockets_SRC_DIR}/bsd.c
             ${uSockets_SRC_DIR}/context.c
             ${uSockets_SRC_DIR}/loop.c
             ${uSockets_SRC_DIR}/socket.c
             ${uSockets_SRC_DIR}/crypto/sni_tree.cpp
             ${uSockets_SRC_DIR}/crypto/openssl.c
             ${uSockets_SRC_DIR}/eventing/epoll_kqueue.c
             ${uSockets_SRC_DIR}/eventing/gcd.c
         )
         target_include_directories(uWS PRIVATE "${uSockets_SRC_DIR}" "${OPENSSL_INCLUDE_DIR}")
      else()
         message("Warning/TODO: ${CMAKE_SYSTEM_NAME} CMake support has not implemented for uWebSockets (ln:${CMAKE_CURRENT_LIST_LINE}).")
      endif()

      target_include_directories(uWS INTERFACE "${uSockets_SRC_DIR}" "${uWebSockets_SRC_DIR}")
      target_link_libraries(uWS PUBLIC ${OPENSSL_LIBRARIES} "${ZLIB_LIBRARIES}" "${CMAKE_THREAD_LIBS_INIT}")

      if (INCPPECT_NO_SSL)
         target_compile_options(uWS PRIVATE -DLIBUS_NO_SSL=1)
      else()
         target_compile_options(uWS PRIVATE -DLIBUS_USE_OPENSSL=1)
      endif()
   endif()
endmacro()

macro(find_uwebsockets)
   include(FetchContent)

   if (WIN32)
      message("Warning: uWebSockets is not supported on Windows.")
   else()
      if (NOT TARGET uWebSockets)
         FetchContent_Declare(
            uwebsockets
            GIT_REPOSITORY https://github.com/uNetworking/uWebSockets
            GIT_TAG v20.62.0
            GIT_SHALLOW ON
            GIT_SUBMODULES_RECURSE YES # Get uSockets
         )
         FetchContent_MakeAvailable(uwebsockets)

         find_package(ZLIB REQUIRED)
         add_library(uWebSockets INTERFACE)
         target_include_directories(uWebSockets INTERFACE ${uwebsockets_SOURCE_DIR}/src/)
         target_link_libraries(uWebSockets INTERFACE uSockets ${ZLIB_LIBRARIES})
         target_compile_options(uWebSockets INTERFACE -Wno-deprecated-declarations)

         set(uwebsockets_FOUND TRUE)
         set(uwebsockets_INCLUDE_DIR ${uwebsockets_SOURCE_DIR}/src)
      else()
         set(uwebsockets_FOUND TRUE)
         get_target_property(uWebSockets_INCLUDE_DIR uWebSockets INTERFACE_INCLUDE_DIRECTORIES)
      endif()

      make_uwebsockets()
           
      if (NOT EXISTS "${CMAKE_BINARY_DIR}/_deps/uwebsockets-src/src/App.h" OR (NOT EXISTS "${CMAKE_BINARY_DIR}/_deps/uwebsockets-src/uSockets/src/libusockets.h"))
         message(FATAL_ERROR "Required library 'https://github.com/uNetworking/uWebSockets' is missing!.")
      endif()

      include_directories("${CMAKE_BINARY_DIR}/_deps/uwebsockets-src/src")
      include_directories("${CMAKE_BINARY_DIR}/_deps/uwebsockets-src/uSockets/src")
        
   endif(WIN32)

   if (NOT TARGET uWS)
      install(TARGETS uWS)
   endif()

endmacro()