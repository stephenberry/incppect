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

target_link_libraries(incppect_incppect INTERFACE uWS)
