cmake_minimum_required (VERSION 3.10)
project(incppect)

#[[ 
  Suppress Warning for compatibility with older versions of CMake.
  The option is clearing the variable 'INCPPECT_DEBUG'.
#]]
cmake_policy(SET CMP0077 NEW)

set(CMAKE_EXPORT_COMPILE_COMMANDS "on")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

if(NOT PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
   include("./cmake/uwebsockets.cmake")
endif()

find_uwebsockets()

if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "RelWithDebInfo")
endif ()

option(INCPPECT_DEBUG   "Enable debug messages in the incppect service" OFF)
option(INCPPECT_NO_SSL  "Disable SSL support" ON)

find_package(Threads REQUIRED)
find_package(ZLIB REQUIRED)

if (APPLE)
    include(./cmake/FindLibUV.cmake)
    find_package(libuv REQUIRED)
endif()

include_directories("${CMAKE_CURRENT_SOURCE_DIR}/third-party/incppect/include")

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/js/incppect.js" src_incppect_js)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/include/incppect/common.h.in ${CMAKE_CURRENT_SOURCE_DIR}/include/incppect/common.h @ONLY)

add_library(incppect_incppect INTERFACE)
add_library(incppect::incppect ALIAS incppect_incppect)

set_property(TARGET incppect_incppect PROPERTY EXPORT_NAME incppect)

target_include_directories(
    incppect_incppect
    INTERFACE 
    "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>"
    "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/../imgui/imgui>"
    "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/../imgui/imgui-extra>"
)

target_include_directories(incppect_incppect INTERFACE)
target_link_libraries(incppect_incppect INTERFACE uWS)

#if (CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    add_subdirectory(examples)
#endif ()
