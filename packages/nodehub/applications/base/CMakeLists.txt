cmake_minimum_required(VERSION 3.5)
project(base)

set(_base_Sources
    include/base.h
    source/base.cpp
    source/entry_point.cpp
    source/imgui_extra_keys.h
    source/config.h.in
    source/setup.h
    source/platform.h
    source/platform_win32.cpp
    source/platform_glfw.cpp
    source/renderer.h
    source/renderer_dx11.cpp
    source/renderer_ogl3.cpp
)

# entry_point.cpp is not included in the library because it has
# conditional compilation for WinMain() vs main() based on _CONSOLE define.
# It will be added directly to each executable target instead.
set(base_ENTRY_POINT_SOURCE
    ${CMAKE_CURRENT_SOURCE_DIR}/source/entry_point.cpp
    CACHE INTERNAL ""
)

add_library(base STATIC)

target_include_directories(base PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

find_package(imgui REQUIRED)
find_package(stb_image REQUIRED)
find_package(ScopeGuard REQUIRED)
target_link_libraries(base PUBLIC imgui)
target_link_libraries(base PRIVATE stb_image ScopeGuard)

if (WIN32)
    list(APPEND _base_Sources
        source/imgui_impl_dx11.cpp
        source/imgui_impl_dx11.h
        source/imgui_impl_win32.cpp
        source/imgui_impl_win32.h
    )

    set(_DXSDK_Dir  ${IMGUI_NODE_EDITOR_ROOT_DIR}/external/DXSDK)
    set(_DXSDK_Arch x86)
    if (${CMAKE_SIZEOF_VOID_P} EQUAL 8)
        set(_DXSDK_Arch x64)
    endif()

    add_library(dxerr STATIC ${_DXSDK_Dir}/src/dxerr.cpp)
    target_include_directories(dxerr PUBLIC "${_DXSDK_Dir}/include")
    set_property(TARGET dxerr PROPERTY FOLDER "external")

    add_library(d3dx11 UNKNOWN IMPORTED)
    set_target_properties(d3dx11 PROPERTIES
        IMPORTED_LOCATION               "${_DXSDK_Dir}/lib/${_DXSDK_Arch}/d3dx11.lib"
        IMPORTED_LOCATION_DEBUG         "${_DXSDK_Dir}/lib/${_DXSDK_Arch}/d3dx11d.lib"
        INTERFACE_INCLUDE_DIRECTORIES   "${_DXSDK_Dir}/include"
        INTERFACE_LINK_LIBRARIES        "$<$<CONFIG:Debug>:dxerr>"
    )

    target_link_libraries(base PRIVATE d3d11.lib d3dcompiler.lib d3dx11)
else()
    find_package(OpenGL REQUIRED)
    find_package(glfw3 3 REQUIRED)

    if (APPLE)
        target_link_libraries(base PRIVATE
            "-framework CoreFoundation"
            "-framework Cocoa"
            "-framework IOKit"
            "-framework CoreVideo"
        )
    endif()
endif()

if (OpenGL_FOUND)
    set(HAVE_OPENGL YES)

    target_include_directories(base PRIVATE ${OPENGL_INCLUDE_DIR})
    target_link_libraries(base PRIVATE ${OPENGL_gl_LIBRARY})
    list(APPEND _base_Sources
        source/imgui_impl_opengl3.cpp
        source/imgui_impl_opengl3.h
        source/imgui_impl_opengl3_loader.h
    )
endif()

if (glfw3_FOUND)
    set(HAVE_GLFW3 YES)

    list(APPEND _base_Sources
        source/imgui_impl_glfw.cpp
        source/imgui_impl_glfw.h
    )
    target_link_libraries(base PRIVATE
        glfw
    )
endif()

configure_file(
    source/config.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/source/config.h
)

target_compile_definitions(base PRIVATE
    #BACKEND_CONFIG=IMGUI_GLFW
    #RENDERER_CONFIG=IMGUI_OGL3
)

# Make config.h include directory PUBLIC since entry_point.cpp (which needs it) is now compiled in executables
target_include_directories(base PUBLIC ${CMAKE_CURRENT_BINARY_DIR}/source)

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${_base_Sources})

target_sources(base PRIVATE ${_base_Sources})

set_property(TARGET base PROPERTY FOLDER "applications")