cmake_minimum_required(VERSION 3.20)
project(html_pkg CXX C)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# ── lexbor: HTML / DOM parsing for html::parse / html::select / html2md ──
FetchContent_Declare(
    lexbor
    GIT_REPOSITORY https://github.com/lexbor/lexbor.git
    GIT_TAG        v2.4.0
    GIT_SHALLOW    TRUE
)
set(LEXBOR_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(LEXBOR_BUILD_STATIC ON  CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(lexbor)

# ── md4c: tiny CommonMark + GFM (tables, strikethrough, task lists) ─────────
FetchContent_Declare(
    md4c
    GIT_REPOSITORY https://github.com/mity/md4c.git
    GIT_TAG        release-0.5.2
    GIT_SHALLOW    TRUE
)
set(MD4C_USE_UTF8        ON  CACHE BOOL "" FORCE)
set(BUILD_MD2HTML_EXECUTABLE OFF CACHE BOOL "" FORCE)  # we just want the lib
set(BUILD_SHARED_LIBS    OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(md4c)

add_library(html STATIC
    src/html.cpp
    src/html2md.cpp
    src/md.cpp
    src/md_terminal.cpp
    src/table.cpp
)

# MSVC: treat source and execution charset as UTF-8
# (fixes \u200b zero-width-space mismatch in html2md tests)
if(MSVC)
    target_compile_options(html PRIVATE /utf-8)
endif()

target_include_directories(html
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(html
    PUBLIC lexbor_static
    PRIVATE md4c-html md4c
)
# md4c targets don't expose their include dir publicly — point md.cpp at it.
target_include_directories(html PRIVATE
    "${md4c_SOURCE_DIR}/src"
)

# ── Optional standalone test target (Catch2) ────────────────────────────────
# Enable with -DHTML_BUILD_TESTS=ON; runs as `ctest` or the produced executable.
option(HTML_BUILD_TESTS "Build the html package's Catch2 unit tests" OFF)
if(HTML_BUILD_TESTS)
    FetchContent_Declare(
        catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG        v3.6.0
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(catch2)

    # The canonical tests live in <repo>/packages/media/cpp/tests/unit/test_html.cpp
    # so it can be picked up by the umbrella test runner too.
    set(_html_test_src "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/unit/test_html.cpp")
    if(EXISTS "${_html_test_src}")
        add_executable(html_tests "${_html_test_src}")
        if(MSVC)
            target_compile_options(html_tests PRIVATE /utf-8)
        endif()
        target_link_libraries(html_tests PRIVATE html Catch2::Catch2WithMain)
        include(CTest)
        include(Catch)
        catch_discover_tests(html_tests)
    endif()
endif()
