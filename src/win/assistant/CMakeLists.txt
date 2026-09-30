cmake_minimum_required(VERSION 3.20)

project(pm-win32-mini
    VERSION 0.1.0
    DESCRIPTION "Minimal Win32 sandbox (no vips / pm-image deps)"
    LANGUAGES CXX
)

if(NOT WIN32)
    message(FATAL_ERROR "pm-win32-mini is Windows-only.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/utf-8)
endif()

# Output next to pm-image: packages/media/cpp/dist/
get_filename_component(_PM_CPP_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(_PM_DIST "${_PM_CPP_ROOT}/dist")

add_executable(pm-win32-mini WIN32
    main.cpp
    AppDefaults.cpp
    filmstrip/FilmStrip.cpp
    filmstrip/FilmStripThumb.cpp
)
target_compile_definitions(pm-win32-mini PRIVATE
    UNICODE
    _UNICODE
    _WIN32_WINNT=0x0A00
    NOMINMAX
)
target_link_libraries(pm-win32-mini PRIVATE
    user32
    gdi32
    dwmapi
    gdiplus
    d2d1
    windowscodecs
    dwrite
    ole32
    advapi32
)

set_target_properties(pm-win32-mini PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${_PM_DIST}"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${_PM_DIST}"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${_PM_DIST}"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${_PM_DIST}"
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${_PM_DIST}"
)
