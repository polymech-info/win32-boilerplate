# Declare the standalone imgui project (used when building this dependency).
project(imgui VERSION 1.76)

# Collect the official ImGui distribution sources.  Consumers compile these into
# a static library that mirrors upstream's default build configuration.
set(_imgui_Sources
    imconfig.h
    imgui.cpp
    imgui.h
    imgui.natvis
    imgui_demo.cpp
    imgui_draw.cpp
    imgui_internal.h
    imgui_widgets.cpp
    imgui_tables.cpp
    imstb_rectpack.h
    imstb_textedit.h
    imstb_truetype.h
    LICENSE.txt
)

# Build imgui as a static library so that downstream targets can link against
# it without relying on bundled prebuilt binaries.
add_library(${PROJECT_NAME} STATIC ${_imgui_Sources})

# Expose headers so downstream targets can simply `#include "imgui.h"`.
target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# Disable deprecated API branches to keep builds warning-free unless a project
# opts back in.
target_compile_definitions(${PROJECT_NAME} PUBLIC IMGUI_DISABLE_OBSOLETE_FUNCTIONS)

# Organize files in IDEs that support virtual folders.
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${_imgui_Sources})

# Group the target under an "external" folder when seen inside tools like
# Visual Studio's Solution Explorer.
set_property(TARGET ${PROJECT_NAME} PROPERTY FOLDER "external")

