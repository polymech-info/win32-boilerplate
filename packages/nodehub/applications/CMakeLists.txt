# Set up vcpkg toolchain
if(DEFINED CMAKE_TOOLCHAIN_FILE AND EXISTS "${CMAKE_TOOLCHAIN_FILE}")
    include(${CMAKE_TOOLCHAIN_FILE})
endif()

#------------------------------------------------------------------------------
# ImGui Node Editor examples build configuration.
# - `get_filename_component(... ABSOLUTE CACHE)` resolves the project root to
#   an absolute path and stores it in CMake's cache so re-configures reuse it.
# - `USE_FOLDERS` enables logical IDE folders for generated projects.
# - `BUILD_CONSOLE_VARIANTS` toggles additional Win32 console builds.
# - `USE_CONSOLE_AS_STARTUP` selects which target Visual Studio launches.
# - `EXAMPLES_NO_WARNING_FLAGS` holds warning overrides (set to `/W0` on MSVC).
#------------------------------------------------------------------------------
cmake_minimum_required(VERSION 3.5)

# Root project that pulls in the node editor and builds the sample apps.
project(imgui-node-editor)

# Enable testing capabilities with CTest
enable_testing()

# Fetch Google Test framework
include(FetchContent)
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/03597a01ee50ed33e9dfd640b249b4be3799d395.zip
)

# Enforce the same compiler settings for gtest as for our project
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(googletest)

# Option to enable/disable protobuf support (default: ON)
option(ENABLE_PROTOBUF "Enable Protocol Buffers support" ON)

# Protocol Buffers support (optional, via vcpkg)
if (ENABLE_PROTOBUF)
    # Try CONFIG mode first (modern protobuf CMake, works with vcpkg)
    find_package(protobuf CONFIG QUIET)
    if (protobuf_FOUND)
        message(STATUS "Protocol Buffers: ENABLED (found via CONFIG mode)")
        # Create an interface library to propagate protobuf settings
        add_library(protobuf_interface INTERFACE)
        target_link_libraries(protobuf_interface INTERFACE 
            protobuf::libprotobuf
            protobuf::libprotoc
        )
        target_compile_definitions(protobuf_interface INTERFACE ENABLE_PROTOBUF=1)
    else()
        # Fallback to MODULE mode (legacy FindProtobuf)
        find_package(Protobuf QUIET)
        if (Protobuf_FOUND)
            message(STATUS "Protocol Buffers: ENABLED (found via MODULE mode)")
            # Create an interface library to propagate protobuf settings
            add_library(protobuf_interface INTERFACE)
            target_link_libraries(protobuf_interface INTERFACE ${Protobuf_LIBRARIES})
            target_include_directories(protobuf_interface INTERFACE ${Protobuf_INCLUDE_DIRS})
            target_compile_definitions(protobuf_interface INTERFACE ENABLE_PROTOBUF=1)
        else()
            message(WARNING "Protocol Buffers: ENABLED but not found. Install via 'vcpkg install protobuf[zlib] protobuf[zlib]:x64-windows' or set ENABLE_PROTOBUF=OFF")
            message(WARNING "  To use vcpkg, configure CMake with: -DCMAKE_TOOLCHAIN_FILE=[path to vcpkg]/scripts/buildsystems/vcpkg.cmake")
        endif()
    endif()
else()
    message(STATUS "Protocol Buffers: DISABLED")
endif()

# Define IMGUI_NODE_EDITOR_ROOT_DIR pointing to project root directory
get_filename_component(IMGUI_NODE_EDITOR_ROOT_DIR ${CMAKE_SOURCE_DIR}/.. ABSOLUTE CACHE)

# Define IMGUI_NODE_EDITOR_ROOT_DIR pointing to project root directory
get_filename_component(ROOT_DIR ${CMAKE_SOURCE_DIR}/.. ABSOLUTE CACHE)

# Ensure IDE generators expose logical folder groupings instead of a flat list.
# Enable solution folders in Visual Studio and Folders in Xcode
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# Extend the module search path so `find_package(imgui_node_editor)` locates the
# custom `Findimgui_node_editor.cmake` bundled with this repository.
# Point CMake where to look for module files.
list(APPEND CMAKE_MODULE_PATH ${IMGUI_NODE_EDITOR_ROOT_DIR}/misc/cmake-modules)

# Central place to tweak warning configuration for all example targets.
if (MSVC)
    set(EXAMPLES_NO_WARNING_FLAGS "/W0" CACHE STRING "Compiler warning overrides applied to all example targets" FORCE)
elseif (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    set(EXAMPLES_NO_WARNING_FLAGS "-w" CACHE STRING "Compiler warning overrides applied to all example targets" FORCE)
else()
    set(EXAMPLES_NO_WARNING_FLAGS "" CACHE STRING "Compiler warning overrides applied to all example targets" FORCE)
endif()

# Examples use C++17 features (structured bindings, custom unordered_map hash).
# Node editor use C++17 (required for structured bindings and unordered_map with custom hash)
set(CMAKE_CXX_STANDARD            17)
set(CMAKE_CXX_STANDARD_REQUIRED   YES)

# Make it easy to toggle console builds without editing the file.
# Option to also build console variants on Windows
# option(BUILD_CONSOLE_VARIANTS "Also build console variants of applications on Windows" ON)

# Let developers choose which target Visual Studio launches by default.
# Option to set console variant as default VS startup project
# option(USE_CONSOLE_AS_STARTUP "Set console variant as Visual Studio startup project" ON)


# Macro that will configure an example application
macro(add_example_executable name)
    # Start a nested project for each example so properties such as versioning
    # remain isolated.
    project(${name})

    # Collect all sources provided by the caller.
    set(_Example_Sources
        ${ARGN}
    )
    
    # Add entry_point.cpp to sources (not in application library due to _CONSOLE conditional compilation)
    list(APPEND _Example_Sources ${APPLICATION_ENTRY_POINT_SOURCE})

    #source_group("" FILES ${_Example_Sources})
    # Group example sources under their tree
    source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${ARGN})
    # Group entry_point.cpp separately since it's from application directory
    source_group("base" FILES ${APPLICATION_ENTRY_POINT_SOURCE})

    file(GLOB _Nodehub_CommonResources CONFIGURE_DEPENDS "${IMGUI_NODE_EDITOR_ROOT_DIR}/applications/nodehub/data/*")
    file(GLOB _Example_CommonResources CONFIGURE_DEPENDS "${IMGUI_NODE_EDITOR_ROOT_DIR}/applications/base/data/*")
    file(GLOB _Example_Resources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/data/*")
    #message(FATAL_ERROR "_Example_Resources = ${_Example_Resources}")

    # Choose bundle/executable type depending on platform.
    set(_Example_Type)
    if (WIN32)
        set(_Example_Type WIN32)

        set(ApplicationIcon ${IMGUI_NODE_EDITOR_ROOT_DIR}/applications/base/support/Icon.ico)
        file(TO_NATIVE_PATH "${ApplicationIcon}" ApplicationIcon)
        string(REPLACE "\\" "\\\\" ApplicationIcon "${ApplicationIcon}")
        configure_file(
            ${IMGUI_NODE_EDITOR_ROOT_DIR}/applications/base/support/Resource.rc.in
            ${CMAKE_CURRENT_BINARY_DIR}/Resource.rc
        )
        source_group(TREE "${IMGUI_NODE_EDITOR_ROOT_DIR}/applications/base" FILES ${_Example_CommonResources})
        source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${_Example_Resources})
        list(APPEND _Example_Resources
            ${CMAKE_CURRENT_BINARY_DIR}/Resource.rc
            ${_Example_CommonResources}
        )
        source_group("resources" FILES ${CMAKE_CURRENT_BINARY_DIR}/Resource.rc)
    elseif (APPLE)
        set(_Example_Type MACOSX_BUNDLE)

        set_source_files_properties(${_Example_Resources} ${_Example_CommonResources} PROPERTIES
            MACOSX_PACKAGE_LOCATION "Resources/data"
        )
        set(_Example_Icon "${IMGUI_NODE_EDITOR_ROOT_DIR}/applications/base/support/Icon.icns")
        list(APPEND _Example_Resources ${_Example_Icon})
        set_source_files_properties(${_Example_Icon} PROPERTIES
            MACOSX_PACKAGE_LOCATION "Resources"
        )
    endif()

    add_executable(${name} ${_Example_Type} ${_Example_Sources} ${_Example_Resources} ${_Example_CommonResources})

    if (EXAMPLES_NO_WARNING_FLAGS)
        target_compile_options(${name} PRIVATE ${EXAMPLES_NO_WARNING_FLAGS})
    endif()

    # Add /FS flag for MSVC to prevent PDB locking issues during parallel builds
    if (WIN32 AND MSVC)
        target_compile_options(${name} PRIVATE /FS)
    endif()

    find_package(imgui REQUIRED)
    find_package(imgui_node_editor REQUIRED)
    target_link_libraries(${name} PRIVATE imgui imgui_node_editor base)
    
    # Link protobuf if available
    if (ENABLE_PROTOBUF AND TARGET protobuf_interface)
        target_link_libraries(${name} PRIVATE protobuf_interface)
    endif()

    set(_ExampleBinDir ${CMAKE_BINARY_DIR}/bin)

    set_target_properties(${name} PROPERTIES
        FOLDER "applications"
        RUNTIME_OUTPUT_DIRECTORY                "${_ExampleBinDir}"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG          "${_ExampleBinDir}"
        RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${_ExampleBinDir}"
        RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL     "${_ExampleBinDir}"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE        "${_ExampleBinDir}"
        DEBUG_POSTFIX                           _d
        RELWITHDEBINGO_POSTFIX                  _rd
        MINSIZEREL_POSTFIX                      _r
        VS_DEBUGGER_WORKING_DIRECTORY           ${_ExampleBinDir}
        MACOSX_BUNDLE_INFO_PLIST                "${IMGUI_NODE_EDITOR_ROOT_DIR}/applications/base/support/Info.plist.in"
        MACOSX_BUNDLE_BUNDLE_NAME               "${PACKAGE_NAME}"
        MACOSX_BUNDLE_GUI_IDENTIFIER            "com.sandbox.collisions"
        MACOSX_BUNDLE_LONG_VERSION_STRING       "${PACKAGE_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING      "${PACKAGE_VERSION_MAJOR}.${PACKAGE_VERSION_MINOR}"
        MACOSX_BUNDLE_ICON_FILE                 Icon.icns
    )

    # Ensure the runtime data directory exists before copying anything in.
    add_custom_command(
        TARGET ${name}
        PRE_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory ARGS ${_ExampleBinDir}/data
    )

    set(_ResourceRoot ${CMAKE_CURRENT_SOURCE_DIR})
    # Copy both shared and example-specific data into the runtime folder while
    # preserving relative paths.
    foreach(_Resource ROOT "${IMGUI_NODE_EDITOR_ROOT_DIR}/applications/base/data" ${_Example_CommonResources} ROOT "${CMAKE_CURRENT_SOURCE_DIR}/data" ${_Example_Resources})
        if (_Resource STREQUAL ROOT)
            set(_ResourceRoot FALSE)
            continue()
        elseif(NOT _ResourceRoot)
            set(_ResourceRoot ${_Resource})
            continue()
        endif()

        if ("${_Resource}" MATCHES "\.DS_Store$")
            list(REMOVE_ITEM _Example_Resources ${_Resource})
            list(REMOVE_ITEM _Example_CommonResources ${_Resource})
            continue()
        endif()

        file(RELATIVE_PATH _RelResource ${_ResourceRoot} ${_Resource})

        add_custom_command(
            TARGET ${name}
            PRE_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ARGS ${_Resource} ${_ExampleBinDir}/data/${_RelResource}
        )
    endforeach()

    # On Windows, optionally create a console variant
    if (WIN32 AND BUILD_CONSOLE_VARIANTS)
        set(_ConsoleTargetName ${name}-console)
        
        # Create console executable (without WIN32 flag)
        add_executable(${_ConsoleTargetName} ${_Example_Sources} ${_Example_Resources} ${_Example_CommonResources})
        
        if (EXAMPLES_NO_WARNING_FLAGS)
            target_compile_options(${_ConsoleTargetName} PRIVATE ${EXAMPLES_NO_WARNING_FLAGS})
        endif()
        
        # Define _CONSOLE to use main() instead of WinMain()
        target_compile_definitions(${_ConsoleTargetName} PRIVATE _CONSOLE)
        
        # Add /FS flag for MSVC to prevent PDB locking issues during parallel builds
        if (MSVC)
            target_compile_options(${_ConsoleTargetName} PRIVATE /FS)
        endif()
        
        # Link the same libraries
        find_package(imgui REQUIRED)
        find_package(imgui_node_editor REQUIRED)
        target_link_libraries(${_ConsoleTargetName} PRIVATE imgui imgui_node_editor application)
        
        # Link protobuf if available
        if (ENABLE_PROTOBUF AND TARGET protobuf_interface)
            target_link_libraries(${_ConsoleTargetName} PRIVATE protobuf_interface)
        endif()
        
        # Set the same properties as the GUI version
        set_target_properties(${_ConsoleTargetName} PROPERTIES
            FOLDER "examples"
            RUNTIME_OUTPUT_DIRECTORY                "${_ExampleBinDir}"
            RUNTIME_OUTPUT_DIRECTORY_DEBUG          "${_ExampleBinDir}"
            RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${_ExampleBinDir}"
            RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL     "${_ExampleBinDir}"
            RUNTIME_OUTPUT_DIRECTORY_RELEASE        "${_ExampleBinDir}"
            DEBUG_POSTFIX                           _d
            RELWITHDEBINGO_POSTFIX                  _rd
            MINSIZEREL_POSTFIX                      _r
            VS_DEBUGGER_WORKING_DIRECTORY           ${_ExampleBinDir}
        )
        
        # Add custom commands for resource copying (same as GUI version)
        add_custom_command(
            TARGET ${_ConsoleTargetName}
            PRE_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory ARGS ${_ExampleBinDir}/data
        )
        
        foreach(_Resource ROOT "${IMGUI_NODE_EDITOR_ROOT_DIR}/applications/base/data" ${_Example_CommonResources} ROOT "${CMAKE_CURRENT_SOURCE_DIR}/data" ${_Example_Resources})
            if (_Resource STREQUAL ROOT)
                set(_ResourceRoot FALSE)
                continue()
            elseif(NOT _ResourceRoot)
                set(_ResourceRoot ${_Resource})
                continue()
            endif()

            if ("${_Resource}" MATCHES "\.DS_Store$")
                continue()
            endif()

            file(RELATIVE_PATH _RelResource ${_ResourceRoot} ${_Resource})

            # Reuse the resource copying logic for the console binary.
            add_custom_command(
                TARGET ${_ConsoleTargetName}
                PRE_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different ARGS ${_Resource} ${_ExampleBinDir}/data/${_RelResource}
            )
        endforeach()
    endif()

endmacro()

# Build the shared application support code and individual examples.
add_subdirectory(base)
add_subdirectory(nodehub)
add_subdirectory(tests)

# Set the default Visual Studio startup project
if (WIN32 AND BUILD_CONSOLE_VARIANTS AND USE_CONSOLE_AS_STARTUP)
    set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT nodehub-console)
    message(STATUS "Visual Studio startup project: nodehub-console")
else()
    set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT nodehub)
    message(STATUS "Visual Studio startup project: nodehub")
endif()
