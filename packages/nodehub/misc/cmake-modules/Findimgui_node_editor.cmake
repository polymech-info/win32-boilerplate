# Prevent re-defining the target when this module is included multiple times.
if (TARGET imgui_node_editor)
    return()
endif()

# ImGui node editor depends on dear imgui. Require the package so we can link
# against the imgui target that it provides.
find_package(imgui REQUIRED)

# List every translation unit that belongs to the node editor library.  The
# consuming project must pass `IMGUI_NODE_EDITOR_ROOT_DIR` when calling
# `find_package(imgui_node_editor)` so that each path resolves correctly.
# Duplicates are harmless but can be cleaned up later.
set(_imgui_node_editor_Sources
    ${IMGUI_NODE_EDITOR_ROOT_DIR}/external/crude_json.cpp
    ${IMGUI_NODE_EDITOR_ROOT_DIR}/external/crude_json.h
    ${IMGUI_NODE_EDITOR_ROOT_DIR}/external/imgui_node/imgui_bezier_math.h
    ${IMGUI_NODE_EDITOR_ROOT_DIR}/external/imgui_node/imgui_bezier_math.inl
    ${IMGUI_NODE_EDITOR_ROOT_DIR}/external/imgui_node/imgui_canvas.cpp
    ${IMGUI_NODE_EDITOR_ROOT_DIR}/external/imgui_node/imgui_canvas.h
    ${IMGUI_NODE_EDITOR_ROOT_DIR}/external/imgui_node/imgui_extra_math.h
    ${IMGUI_NODE_EDITOR_ROOT_DIR}/external/imgui_node/imgui_extra_math.inl
    ${IMGUI_NODE_EDITOR_ROOT_DIR}/external/imgui_node/imgui_node_editor_api.cpp
    ${IMGUI_NODE_EDITOR_ROOT_DIR}/external/imgui_node/imgui_node_editor_animation.cpp
    ${IMGUI_NODE_EDITOR_ROOT_DIR}/external/imgui_node/imgui_node_editor_store.cpp
    ${IMGUI_NODE_EDITOR_ROOT_DIR}/external/imgui_node/EditorContext.cpp
    ${IMGUI_NODE_EDITOR_ROOT_DIR}/external/imgui_node/EditorContext.h
    ${IMGUI_NODE_EDITOR_ROOT_DIR}/external/imgui_node/imgui_node_editor_internal.h
    ${IMGUI_NODE_EDITOR_ROOT_DIR}/external/imgui_node/imgui_node_editor_internal.inl
    ${IMGUI_NODE_EDITOR_ROOT_DIR}/external/imgui_node/imgui_node_editor.cpp
    ${IMGUI_NODE_EDITOR_ROOT_DIR}/external/imgui_node/imgui_node_editor.h
    ${IMGUI_NODE_EDITOR_ROOT_DIR}/external/imgui_node/links-guided.cpp
    ${IMGUI_NODE_EDITOR_ROOT_DIR}/external/imgui_node/links-guided.h
    ${IMGUI_NODE_EDITOR_ROOT_DIR}/misc/imgui_node_editor.natvis
)

# Build the editor implementation as a static library that downstream projects
# can link against.
add_library(imgui_node_editor STATIC
    ${_imgui_node_editor_Sources}
)

# Expose the root directory so that depending targets can include headers with
# simple relative paths (e.g. `#include "imgui_node_editor.h"`).
target_include_directories(imgui_node_editor PUBLIC
    ${IMGUI_NODE_EDITOR_ROOT_DIR}/external
    ${IMGUI_NODE_EDITOR_ROOT_DIR}/external/imgui_node
)

# Link against dear imgui provided by the earlier `find_package` call.
target_link_libraries(imgui_node_editor PUBLIC imgui)

# Arrange sources into folders in IDEs that support virtual source groups.
source_group(TREE ${IMGUI_NODE_EDITOR_ROOT_DIR} FILES ${_imgui_node_editor_Sources})

# Use CMake's helper to generate the usual `imgui_node_editor` package result:
#   - `FOUND` variable
#   - Optional diagnostic messages when variables are missing.
include(${CMAKE_ROOT}/Modules/FindPackageHandleStandardArgs.cmake)

find_package_handle_standard_args(
    imgui_node_editor
    REQUIRED_VARS
        IMGUI_NODE_EDITOR_ROOT_DIR
)