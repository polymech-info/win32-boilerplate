cmake_minimum_required(VERSION 3.20)

project(media-image-service
    VERSION 0.1.0
    DESCRIPTION "Polymech image resize service (CLI, REST, IPC) — libvips"
    LANGUAGES CXX
)

include(GNUInstallDirs)

# Binaries and co-located runtime assets: dist/win-x64/ on Windows,
# dist/ on Linux, dist-osx/ on macOS.
if(APPLE)
    set(PM_DIST_ROOT_DIR "${CMAKE_SOURCE_DIR}/dist-osx")
    set(PM_DIST_DIR "${CMAKE_SOURCE_DIR}/dist-osx")
elseif(WIN32)
    set(PM_DIST_ROOT_DIR "${CMAKE_SOURCE_DIR}/dist")
    set(PM_DIST_DIR "${PM_DIST_ROOT_DIR}/win-x64")
else()
    set(PM_DIST_ROOT_DIR "${CMAKE_SOURCE_DIR}/dist")
    set(PM_DIST_DIR "${CMAKE_SOURCE_DIR}/dist")
endif()
set(PM_DIST_SHARED_DIR "${CMAKE_SOURCE_DIR}/dist/shared")
set(PM_DIST_VENDOR_DIR "${CMAKE_SOURCE_DIR}/dist/vendor")

set(PM_BRANDING_ASSET_DIR "${PM_DIST_ROOT_DIR}/branding")
if(EXISTS "${PM_BRANDING_ASSET_DIR}/pm_image.ico")
  set(PM_HAS_BRANDING_ICON 1)
  file(TO_CMAKE_PATH "${PM_BRANDING_ASSET_DIR}/pm_image.ico" PM_BRANDING_ICON_RC_PATH)
  set(PM_APP_ICON_RC "${CMAKE_CURRENT_BINARY_DIR}/pm_app_icon.rc")
  file(WRITE "${PM_APP_ICON_RC}"
    "#pragma code_page(65001)\n"
    "#define IDW_MAIN 41\n"
    "IDW_MAIN ICON \"${PM_BRANDING_ICON_RC_PATH}\"\n")
else()
  set(PM_HAS_BRANDING_ICON 0)
endif()
if(EXISTS "${PM_BRANDING_ASSET_DIR}/logo.png" AND EXISTS "${PM_BRANDING_ASSET_DIR}/splash-1.png")
  set(PM_HAS_BRANDING_SPLASH 1)
else()
  set(PM_HAS_BRANDING_SPLASH 0)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PM_DIST_DIR}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG "${PM_DIST_DIR}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE "${PM_DIST_DIR}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${PM_DIST_DIR}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${PM_DIST_DIR}")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# MSVC: UTF-8 source/execution charset so wide string literals (L"...") match UTF-8 in .cpp files.
# RC /utf-8: locale .rc files store UTF-8 (ñ, …, etc.). rc.exe does not interpret \\uXXXX like cl.exe.
if(MSVC)
    add_compile_options(/utf-8)
    string(APPEND CMAKE_RC_FLAGS " /utf-8")
endif()

# Dev-only: clang-tidy (POLYMECH_CLANG_TIDY_BUILD=ON in clang-tidy CMake preset) applies to first-party
# targets at the end of this file — not to FetchContent dependencies.
option(POLYMECH_CLANG_TIDY_BUILD "When ON, run clang-tidy on pm-media, kbot, and pm-image targets" OFF)
# Dev-only: MSVC /fsanitize=address (cl + link; use the msvc-asan preset). See npm run msvc:address-sanitizer.
option(POLYMECH_MSVC_ADDRESS_SANITIZER "MSVC: build with Address Sanitizer" OFF)
if(POLYMECH_MSVC_ADDRESS_SANITIZER)
  if(NOT MSVC)
    message(FATAL_ERROR "POLYMECH_MSVC_ADDRESS_SANITIZER requires the MSVC toolset. Use the msvc-asan preset on Windows with Visual Studio.")
  endif()
  add_compile_options(/fsanitize=address)
  add_link_options(/INCREMENTAL:NO /fsanitize=address)
endif()

# MSVC: compile multiple .cpp files in parallel within a target (/MP). Omits process count so cl uses all logical
# processors (high RAM). Skipped with Address Sanitizer (/MP + /fsanitize=address is a poor default).
if(MSVC AND NOT POLYMECH_MSVC_ADDRESS_SANITIZER)
  add_compile_options($<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/MP>)
endif()

# macOS only: build PM-Image.app (AppKit) alongside the CLI. Default OFF; see docs/osx-ui.md and osx/README.md
option(PM_BUILD_MACOS_APP "Build AppKit .app (target pm-image-gui; macOS only)" OFF)
if(PM_BUILD_MACOS_APP AND NOT APPLE)
  message(FATAL_ERROR "PM_BUILD_MACOS_APP is only supported on macOS.")
endif()

# Build vendored CodeEdit (Swift) via xcodebuild; copies app to dist-osx and stages pm-media for linking from Xcode later.
option(PM_BUILD_CODEEDIT "Build PixelWiz.app under osx/CodeEdit via xcodebuild (CMake target pm-codeedit; macOS only)" OFF)
if(PM_BUILD_CODEEDIT AND NOT APPLE)
  message(FATAL_ERROR "PM_BUILD_CODEEDIT is only supported on macOS.")
endif()

# Linux only: build GTK4/libadwaita GUI (pm-image-gui) alongside the CLI. Default OFF; see linux/README.md
option(PM_BUILD_LINUX_GUI "Build GTK4/libadwaita GUI (target pm-image-gui; Linux only)" OFF)
if(PM_BUILD_LINUX_GUI AND NOT UNIX)
  message(FATAL_ERROR "PM_BUILD_LINUX_GUI is only supported on Linux.")
endif()
option(LINUX_USE_WEBKIT_GTK "Linux GUI: enable WebKitGTK web panel + host bridge" ON)
option(LINUX_USE_LIBPANEL "Linux GUI: enable libpanel docking (experimental; requires GTK4/libadwaita+libpanel)" OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(Branding)
configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/src/pm_branding_config.hpp.in"
  "${CMAKE_CURRENT_BINARY_DIR}/include/pm_branding_config.hpp"
  @ONLY
)
file(MAKE_DIRECTORY "${PM_DIST_DIR}")
configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/dist/branding/menu.json.in"
  "${PM_DIST_DIR}/menu.json"
  @ONLY
)

# CMP0169: FetchContent_Populate(name) with declared details is deprecated in CMake 4.x.
# picosha2 / pranav_glob / laserpants_dotenv use the old populate-then-define-targets pattern;
# keep OLD behaviour until those are migrated to FetchContent_MakeAvailable.
if(POLICY CMP0169)
  cmake_policy(SET CMP0169 OLD)
endif()

# Windows: official libvips dev tree — vips-dev-* under third_party/.
# If the folder is missing, download the pre-built MSVC bundle automatically
# (same action as scripts/fetch-vips-windows.ps1).
if(WIN32)
  file(GLOB _MEDIA_VIPS_SDK
    "${PM_DIST_ROOT_DIR}/vendor/vips-dev-*"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/vips-dev-*")
  if(NOT _MEDIA_VIPS_SDK)
    set(_VIPS_VERSION "8.18.2")
    set(_VIPS_ZIP_NAME "vips-dev-x64-all-${_VIPS_VERSION}.zip")
    set(_VIPS_URL "https://github.com/libvips/build-win64-mxe/releases/download/v${_VIPS_VERSION}/${_VIPS_ZIP_NAME}")
    set(_VIPS_ZIP "${CMAKE_CURRENT_SOURCE_DIR}/third_party/${_VIPS_ZIP_NAME}")
    message(STATUS "libvips SDK not found in third_party/ — downloading ${_VIPS_URL}")
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/third_party")
    file(DOWNLOAD "${_VIPS_URL}" "${_VIPS_ZIP}"
         SHOW_PROGRESS
         STATUS _VIPS_DL_STATUS
         TLS_VERIFY ON)
    list(GET _VIPS_DL_STATUS 0 _VIPS_DL_CODE)
    if(NOT _VIPS_DL_CODE EQUAL 0)
      message(FATAL_ERROR "Failed to download libvips SDK: ${_VIPS_DL_STATUS}\n"
              "Run scripts/fetch-vips-windows.ps1 manually or set VIPS_ROOT.")
    endif()
    message(STATUS "Extracting ${_VIPS_ZIP_NAME} ...")
    execute_process(
      COMMAND "${CMAKE_COMMAND}" -E tar xf "${_VIPS_ZIP}"
      WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/third_party"
      RESULT_VARIABLE _VIPS_EXTRACT_RESULT
    )
    if(NOT _VIPS_EXTRACT_RESULT EQUAL 0)
      message(FATAL_ERROR "Failed to extract ${_VIPS_ZIP_NAME}.")
    endif()
    file(GLOB _MEDIA_VIPS_SDK
      "${PM_DIST_ROOT_DIR}/vendor/vips-dev-*"
      "${CMAKE_CURRENT_SOURCE_DIR}/third_party/vips-dev-*")
  endif()
  if(_MEDIA_VIPS_SDK)
    list(GET _MEDIA_VIPS_SDK 0 _MEDIA_VIPS_SDK_FIRST)
    if(MSVC AND EXISTS "${_MEDIA_VIPS_SDK_FIRST}/lib/libvips.def" AND NOT EXISTS "${_MEDIA_VIPS_SDK_FIRST}/lib/libvips.lib")
      file(GLOB _MEDIA_VIPS_DEF_FILES "${_MEDIA_VIPS_SDK_FIRST}/lib/*.def")
      foreach(_MEDIA_VIPS_DEF IN LISTS _MEDIA_VIPS_DEF_FILES)
        get_filename_component(_MEDIA_VIPS_IMPORT_STEM "${_MEDIA_VIPS_DEF}" NAME_WE)
        set(_MEDIA_VIPS_IMPORT_LIB "${_MEDIA_VIPS_SDK_FIRST}/lib/${_MEDIA_VIPS_IMPORT_STEM}.lib")
        if(NOT EXISTS "${_MEDIA_VIPS_IMPORT_LIB}")
          execute_process(
            COMMAND "${CMAKE_AR}" /nologo /machine:x64 "/def:${_MEDIA_VIPS_DEF}" "/out:${_MEDIA_VIPS_IMPORT_LIB}"
            RESULT_VARIABLE _MEDIA_VIPS_LIB_RESULT
            OUTPUT_QUIET
            ERROR_VARIABLE _MEDIA_VIPS_LIB_ERROR
          )
          if(NOT _MEDIA_VIPS_LIB_RESULT EQUAL 0)
            message(FATAL_ERROR "Failed to generate ${_MEDIA_VIPS_IMPORT_LIB} from ${_MEDIA_VIPS_DEF}: ${_MEDIA_VIPS_LIB_ERROR}")
          endif()
        endif()
      endforeach()
    endif()
    list(APPEND CMAKE_PREFIX_PATH ${_MEDIA_VIPS_SDK})
  endif()
endif()

include(FetchContent)

set(JSON_BuildTests OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    cli11
    GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
    GIT_TAG v2.4.2
    GIT_SHALLOW TRUE
)

FetchContent_Declare(
    asio
    GIT_REPOSITORY https://github.com/chriskohlhoff/asio.git
    GIT_TAG asio-1-28-0
    GIT_SHALLOW TRUE
)

FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
    GIT_SHALLOW TRUE
)

FetchContent_Declare(
    cpp_httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG v0.16.3
    GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(cli11 asio nlohmann_json cpp_httplib)

FetchContent_Declare(
    muparser
    GIT_REPOSITORY https://github.com/beltoforion/muparser.git
    GIT_TAG master
    GIT_SHALLOW TRUE
)

set(_pm_muparser_restore_BUILD_SHARED_LIBS FALSE)
set(_pm_muparser_restore_ENABLE_OPENMP FALSE)
set(_pm_muparser_restore_ENABLE_SAMPLES FALSE)
if(DEFINED BUILD_SHARED_LIBS)
    set(_pm_muparser_restore_BUILD_SHARED_LIBS TRUE)
    set(_pm_muparser_prev_BUILD_SHARED_LIBS "${BUILD_SHARED_LIBS}")
endif()
if(DEFINED ENABLE_OPENMP)
    set(_pm_muparser_restore_ENABLE_OPENMP TRUE)
    set(_pm_muparser_prev_ENABLE_OPENMP "${ENABLE_OPENMP}")
endif()
if(DEFINED ENABLE_SAMPLES)
    set(_pm_muparser_restore_ENABLE_SAMPLES TRUE)
    set(_pm_muparser_prev_ENABLE_SAMPLES "${ENABLE_SAMPLES}")
endif()
set(ENABLE_SAMPLES OFF CACHE BOOL "" FORCE)
set(ENABLE_OPENMP OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(muparser)
if(_pm_muparser_restore_BUILD_SHARED_LIBS)
    set(BUILD_SHARED_LIBS "${_pm_muparser_prev_BUILD_SHARED_LIBS}" CACHE BOOL "" FORCE)
else()
    unset(BUILD_SHARED_LIBS CACHE)
    unset(BUILD_SHARED_LIBS)
endif()
if(_pm_muparser_restore_ENABLE_OPENMP)
    set(ENABLE_OPENMP "${_pm_muparser_prev_ENABLE_OPENMP}" CACHE BOOL "" FORCE)
else()
    unset(ENABLE_OPENMP CACHE)
    unset(ENABLE_OPENMP)
endif()
if(_pm_muparser_restore_ENABLE_SAMPLES)
    set(ENABLE_SAMPLES "${_pm_muparser_prev_ENABLE_SAMPLES}" CACHE BOOL "" FORCE)
else()
    unset(ENABLE_SAMPLES CACHE)
    unset(ENABLE_SAMPLES)
endif()

# libcurl — HTTP(S) URL inputs (minimal static build; SChannel on Windows, OpenSSL on Linux).
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "" FORCE)
FetchContent_Declare(
    CURL
    URL https://github.com/curl/curl/releases/download/curl-8_12_1/curl-8.12.1.tar.xz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
set(BUILD_CURL_EXE OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
if(WIN32)
    set(CURL_USE_OPENSSL OFF CACHE BOOL "" FORCE)
    set(CURL_USE_SCHANNEL ON CACHE BOOL "" FORCE)
else()
    set(CURL_USE_SCHANNEL OFF CACHE BOOL "" FORCE)
    set(CURL_USE_OPENSSL ON CACHE BOOL "" FORCE)
endif()
set(CURL_ZLIB OFF CACHE BOOL "" FORCE)
set(CURL_BROTLI OFF CACHE BOOL "" FORCE)
set(CURL_ZSTD OFF CACHE BOOL "" FORCE)
set(USE_NGHTTP2 OFF CACHE BOOL "" FORCE)
set(CURL_USE_LIBSSH2 OFF CACHE BOOL "" FORCE)
set(CURL_USE_LIBPSL OFF CACHE BOOL "" FORCE)
set(CURL_DISABLE_LDAP ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_LDAPS ON CACHE BOOL "" FORCE)
# WebSocket support (curl 7.86+): used by FEATURE_STT ElevenLabs realtime STT client.
set(ENABLE_WEBSOCKETS ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(CURL)

# PicoSHA2 — header-only SHA256 for cache keys.
FetchContent_Declare(
    picosha2
    GIT_REPOSITORY https://github.com/okdshin/PicoSHA2.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
)
FetchContent_GetProperties(picosha2)
if(NOT picosha2_POPULATED)
    FetchContent_Populate(picosha2)
endif()

# p-ranav/glob — same as packages/kbot/cpp (glob / rglob for **).
FetchContent_Declare(
    pranav_glob
    GIT_REPOSITORY https://github.com/p-ranav/glob.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
)
FetchContent_GetProperties(pranav_glob)
if(NOT pranav_glob_POPULATED)
    FetchContent_Populate(pranav_glob)
endif()

option(FEATURE_MCP_SERVER "Embedded MCP Streamable HTTP server (--mcp / --mcp-port)" ON)
option(FEATURE_MCP_CLIENT "MCP client bridge/probe for agent mcp.json tools" ON)
option(FEATURE_HOME_LLM_TOOLS "Home/agent LLM tools bundle: built-in path tools and MCP tools" ON)
option(FEATURE_HOME_LLM_SKILLS "Home/agent LLM skills discovery and prompt injection" ON)
option(FEATURE_AGENT_COMPUTER_USE "Agent computer-use tools: app inspect/find/screenshot/click" ON)
option(FEATURE_CUSTOM_COMMANDS "User editable custom command/ribbon presets (commands.json)" ON)
option(FEATURE_XBLOX "XBlox block-flow runtime, CLI command, and Windows viewer" ON)
option(FEATURE_MODBUS "Modbus TCP/RTU client/server wrapper library (libmodbus)" ON)
option(FEATURE_MODBUS_XBLOX "XBlox Modbus blocks (requires FEATURE_XBLOX and FEATURE_MODBUS)" ON)
message(STATUS "FEATURE_MCP_SERVER: ${FEATURE_MCP_SERVER}")
message(STATUS "FEATURE_MCP_CLIENT: ${FEATURE_MCP_CLIENT}")
message(STATUS "FEATURE_AGENT_COMPUTER_USE: ${FEATURE_AGENT_COMPUTER_USE}")
message(STATUS "FEATURE_HOME_LLM_TOOLS: ${FEATURE_HOME_LLM_TOOLS}")
message(STATUS "FEATURE_HOME_LLM_SKILLS: ${FEATURE_HOME_LLM_SKILLS}")
message(STATUS "FEATURE_CUSTOM_COMMANDS: ${FEATURE_CUSTOM_COMMANDS}")
message(STATUS "FEATURE_XBLOX: ${FEATURE_XBLOX}")
if(FEATURE_MODBUS_XBLOX AND NOT FEATURE_XBLOX)
    message(WARNING "FEATURE_MODBUS_XBLOX=ON requires FEATURE_XBLOX=ON — disabling Modbus xblox blocks")
    set(FEATURE_MODBUS_XBLOX OFF CACHE BOOL "XBlox Modbus blocks (requires FEATURE_XBLOX and FEATURE_MODBUS)" FORCE)
endif()
if(FEATURE_MODBUS_XBLOX AND NOT FEATURE_MODBUS)
    message(WARNING "FEATURE_MODBUS_XBLOX=ON requires FEATURE_MODBUS=ON — disabling Modbus xblox blocks")
    set(FEATURE_MODBUS_XBLOX OFF CACHE BOOL "XBlox Modbus blocks (requires FEATURE_XBLOX and FEATURE_MODBUS)" FORCE)
endif()
message(STATUS "FEATURE_MODBUS: ${FEATURE_MODBUS}")
message(STATUS "FEATURE_MODBUS_XBLOX: ${FEATURE_MODBUS_XBLOX}")

if(FEATURE_MODBUS)
    FetchContent_Declare(
        libmodbus
        GIT_REPOSITORY https://github.com/stephane/libmodbus.git
        GIT_TAG        v3.1.12
        GIT_SHALLOW    TRUE
    )
    FetchContent_GetProperties(libmodbus)
    if(NOT libmodbus_POPULATED)
        FetchContent_Populate(libmodbus)
    endif()

    set(_pm_libmodbus_generated_include "${CMAKE_CURRENT_BINARY_DIR}/include/libmodbus")
    file(MAKE_DIRECTORY "${_pm_libmodbus_generated_include}")
    file(WRITE "${_pm_libmodbus_generated_include}/modbus-version.h" [=[
/*
 * Generated by pm-image CMake for static libmodbus consumption.
 */
#ifndef MODBUS_VERSION_H
#define MODBUS_VERSION_H
#define LIBMODBUS_VERSION_MAJOR (3)
#define LIBMODBUS_VERSION_MINOR (1)
#define LIBMODBUS_VERSION_MICRO (12)
#define LIBMODBUS_VERSION 3.1.12
#define LIBMODBUS_VERSION_STRING "3.1.12"
#define LIBMODBUS_VERSION_HEX \
    ((LIBMODBUS_VERSION_MAJOR << 16) | (LIBMODBUS_VERSION_MINOR << 8) | \
     (LIBMODBUS_VERSION_MICRO << 0))
#define LIBMODBUS_VERSION_CHECK(major, minor, micro) \
    (LIBMODBUS_VERSION_MAJOR > (major) || \
     (LIBMODBUS_VERSION_MAJOR == (major) && LIBMODBUS_VERSION_MINOR > (minor)) || \
     (LIBMODBUS_VERSION_MAJOR == (major) && LIBMODBUS_VERSION_MINOR == (minor) && \
      LIBMODBUS_VERSION_MICRO >= (micro)))
#endif
]=])
    file(WRITE "${_pm_libmodbus_generated_include}/config.h" [=[
/*
 * Minimal generated config.h for static libmodbus consumption inside pm-image.
 */
#define HAVE_ERRNO_H 1
#define HAVE_FCNTL_H 1
#define HAVE_INTTYPES_H 1
#define HAVE_LIMITS_H 1
#define HAVE_MEMORY_H 1
#define HAVE_MEMSET 1
#define HAVE_STDINT_H 1
#define HAVE_STDLIB_H 1
#define HAVE_STRERROR 1
#define HAVE_STRING_H 1
#define HAVE_SYS_STAT_H 1
#define HAVE_SYS_TYPES_H 1
#define HAVE_TIME_H 1
#if defined(_WIN32)
#define HAVE_WINSOCK2_H 1
#endif
#define PACKAGE "libmodbus"
#define PACKAGE_BUGREPORT "https://github.com/stephane/libmodbus/issues"
#define PACKAGE_NAME "libmodbus"
#define PACKAGE_STRING "libmodbus 3.1.12"
#define PACKAGE_TARNAME "libmodbus"
#define PACKAGE_URL ""
#define PACKAGE_VERSION "3.1.12"
#define STDC_HEADERS 1
#define VERSION "3.1.12"
]=])

    add_library(pm-libmodbus STATIC
        "${libmodbus_SOURCE_DIR}/src/modbus.c"
        "${libmodbus_SOURCE_DIR}/src/modbus-data.c"
        "${libmodbus_SOURCE_DIR}/src/modbus-rtu.c"
        "${libmodbus_SOURCE_DIR}/src/modbus-tcp.c"
    )
    if(MSVC)
        set_property(TARGET pm-libmodbus PROPERTY
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
    endif()
    target_include_directories(pm-libmodbus PUBLIC
        "${libmodbus_SOURCE_DIR}/src"
        "${_pm_libmodbus_generated_include}"
    )
    target_compile_definitions(pm-libmodbus PUBLIC DLLBUILD)
    if(MSVC)
        target_compile_definitions(pm-libmodbus PRIVATE _CRT_SECURE_NO_WARNINGS)
        target_compile_options(pm-libmodbus PRIVATE
            $<$<CONFIG:Debug>:/MDd>
            $<$<NOT:$<CONFIG:Debug>>:/MD>
            /wd4133)
    endif()
    if(WIN32)
        target_compile_definitions(pm-libmodbus PRIVATE FD_SETSIZE=16384)
        target_link_libraries(pm-libmodbus PUBLIC ws2_32)
    endif()
endif()
add_library(pranav_glob STATIC ${pranav_glob_SOURCE_DIR}/source/glob.cpp)
target_include_directories(pranav_glob PUBLIC ${pranav_glob_SOURCE_DIR}/include)
target_compile_features(pranav_glob PUBLIC cxx_std_17)
if(MSVC)
    target_compile_options(pranav_glob PRIVATE /permissive-)
endif()

# taskflow — referenced by packages/kbot's CMakeLists PUBLIC include path.
# Only the headers are needed here (kbot.cpp / llm_client.cpp / source_files.cpp
# do not actually #include <taskflow/…>; cmd_kbot_uds.cpp does, but we don't
# build that file in pm-image — it's the UDS daemon worker).
FetchContent_Declare(
    taskflow
    GIT_REPOSITORY https://github.com/taskflow/taskflow.git
    GIT_TAG        v3.7.0
    GIT_SHALLOW    TRUE
)
set(TF_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(TF_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(TF_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(TF_BUILD_CUDA       OFF CACHE BOOL "" FORCE)
FetchContent_GetProperties(taskflow)
if(NOT taskflow_POPULATED)
    FetchContent_Populate(taskflow)
endif()

# tomlplusplus — single-header TOML parser used by src/cmd_kbot.cpp to
# read the optional `config/postgres.toml` API-key fallback.
FetchContent_Declare(
    tomlplusplus
    GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
    GIT_TAG        v3.4.0
    GIT_SHALLOW    TRUE
)
FetchContent_GetProperties(tomlplusplus)
if(NOT tomlplusplus_POPULATED)
    FetchContent_Populate(tomlplusplus)
endif()

# kainjow/Mustache — header-only (Mustache spec: sections, partials, escape); v4.1+ has stable API.
FetchContent_Declare(
    kainjow_mustache
    GIT_REPOSITORY https://github.com/kainjow/Mustache.git
    GIT_TAG        v4.1
    GIT_SHALLOW    TRUE
)
FetchContent_GetProperties(kainjow_mustache)
if(NOT kainjow_mustache_POPULATED)
    FetchContent_Populate(kainjow_mustache)
endif()

add_library(tomlplusplus_hdr INTERFACE)
target_include_directories(tomlplusplus_hdr INTERFACE ${tomlplusplus_SOURCE_DIR}/include)

# laserpants/dotenv-cpp — load .env (same pattern as packages/kbot/cpp).
FetchContent_Declare(
    laserpants_dotenv
    GIT_REPOSITORY https://github.com/laserpants/dotenv-cpp.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
)
FetchContent_GetProperties(laserpants_dotenv)
if(NOT laserpants_dotenv_POPULATED)
    FetchContent_Populate(laserpants_dotenv)
endif()
add_library(laserpants_dotenv INTERFACE)
target_include_directories(laserpants_dotenv INTERFACE ${laserpants_dotenv_SOURCE_DIR}/include)
add_library(laserpants::dotenv ALIAS laserpants_dotenv)

# libsodium — encrypted settings (secretbox; Windows protects the local key with DPAPI).
set(SODIUM_DISABLE_TESTS ON CACHE BOOL "" FORCE)
FetchContent_Declare(
  libsodium_cmake
  GIT_REPOSITORY https://github.com/robinlinden/libsodium-cmake.git
  GIT_TAG cfebfd3da486d5a86c644c8b47067e5411c7599c
)
FetchContent_MakeAvailable(libsodium_cmake)

# With a Meson static install: set VIPS_ROOT + PKG_CONFIG_PATH, then -DPOLYMECH_VIPS_PREFER_STATIC=ON.
option(POLYMECH_VIPS_PREFER_STATIC "Use pkg-config --static for vips (WSL/Linux meson install)" OFF)
if(POLYMECH_VIPS_PREFER_STATIC)
  set(Vips_PREFER_STATIC ON CACHE BOOL "From POLYMECH_VIPS_PREFER_STATIC" FORCE)
endif()

find_package(Vips REQUIRED)

# ── FEATURE_PNG_COMPRESSOR dependencies ─────────────────────────────────────
# libimagequant 2.17.0 — last pure-C version (3.x+ requires Rust).
# libpng 1.6.x   — PNG encoding; kept separate from the vips-dev DLL.
# zopfli          — optional ultra-DEFLATE (FEATURE_PNG_ZOPFLI).

option(FEATURE_PNG_COMPRESSOR
    "PNG optimisation pipeline: libimagequant + libpng (palette quantisation + high-compression encode)"
    OFF)
option(FEATURE_PNG_ZOPFLI
    "Add zopfli ultra-DEFLATE to the PNG compressor (lossless, 5-20 % smaller, much slower)"
    OFF)

if(FEATURE_PNG_COMPRESSOR)
    message(STATUS "FEATURE_PNG_COMPRESSOR: ON")

    # ── libimagequant 2.17.0 (C-only, last before Rust rewrite) ─────────────
    FetchContent_Declare(
        libimagequant
        GIT_REPOSITORY https://github.com/ImageOptim/libimagequant.git
        GIT_TAG        v2.17.0
        GIT_SHALLOW    TRUE
    )
    # Disable the libimagequant test/tool builds.
    set(LIQ_NO_MAIN     ON CACHE BOOL "" FORCE)
    set(LIQ_ENABLE_SSE  OFF CACHE BOOL "" FORCE)   # safer default for cross-arch
    FetchContent_MakeAvailable(libimagequant)

    # ── libpng 1.6.44 ───────────────────────────────────────────────────────
    set(PNG_SHARED   OFF CACHE BOOL "" FORCE)
    set(PNG_STATIC   ON  CACHE BOOL "" FORCE)
    set(PNG_TESTS    OFF CACHE BOOL "" FORCE)
    set(PNG_TOOLS    OFF CACHE BOOL "" FORCE)
    set(PNG_EXECUTABLES OFF CACHE BOOL "" FORCE)
    set(SKIP_INSTALL_ALL ON CACHE BOOL "" FORCE)
    FetchContent_Declare(
        libpng
        GIT_REPOSITORY https://github.com/pnggroup/libpng.git
        GIT_TAG        v1.6.44
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(libpng)

    if(FEATURE_PNG_ZOPFLI)
        message(STATUS "FEATURE_PNG_ZOPFLI: ON")
        set(ZOPFLI_BUILD_SHARED OFF CACHE BOOL "" FORCE)
        set(ZOPFLI_BUILD_TESTS  OFF CACHE BOOL "" FORCE)
        FetchContent_Declare(
            zopfli
            GIT_REPOSITORY https://github.com/google/zopfli.git
            GIT_TAG        zopfli-1.0.3
            GIT_SHALLOW    TRUE
        )
        FetchContent_MakeAvailable(zopfli)
    else()
        message(STATUS "FEATURE_PNG_ZOPFLI: OFF")
    endif()
endif()

# ── FTXUI (vendored) ─────────────────────────────────────────────────
if(NOT TARGET ftxui::ftxui)
    set(FTXUI_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
    set(FTXUI_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(FTXUI_BUILD_MODULES  OFF CACHE BOOL "" FORCE)
    set(FTXUI_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
    set(FTXUI_BUILD_TESTS_FUZZER OFF CACHE BOOL "" FORCE)
    set(FTXUI_CLANG_TIDY     OFF CACHE BOOL "" FORCE)
    set(FTXUI_DEV_WARNINGS   OFF CACHE BOOL "" FORCE)
    set(FTXUI_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
    add_subdirectory(packages/FTXUI EXCLUDE_FROM_ALL)
endif()

# ── In-tree polymech subpackages (logger, json, oai, kbot) ──────────
# The kbot lib (polymech::kbot::LLMClient, built on liboai) powers `llm agent`,
# duplicates meta+LLM compare, and other in-process OpenAI-compatible calls.
if(NOT TARGET logger)
    add_subdirectory(packages/logger)
endif()
if(NOT TARGET json)
    add_subdirectory(packages/json)
endif()
if(NOT TARGET oai)
    # Use the inner CMakeLists.txt — the outer one assumes vcpkg (ZLIB, etc.).
    # The inner project accepts the existing nlohmann_json::nlohmann_json and
    # CURL::libcurl targets we already brought in via FetchContent above.
    add_subdirectory(packages/liboai/liboai)
endif()
if(NOT TARGET kbot)
    add_subdirectory(packages/kbot)
endif()

# ── pm-media library (resize + transform API) ───────────────────────
option(POLYMECH_MEDIA_SHARED "Build pm-media as a shared library (DLL/so)" OFF)

set(_pm_media_sources
    src/core/cli_cancel.cpp
    src/core/batch_queue.cpp
    src/core/cache.cpp
    src/core/command_variables.cpp
    src/core/compress.cpp
    src/core/find.cpp
    src/core/search.cpp
    src/core/duplicates.cpp
    src/core/glob_paths.cpp
    src/core/input_selection.cpp
    src/core/path_sanitizer.cpp
    src/core/mustache_render.cpp
    src/core/string_sanitizers.cpp
    src/core/llm_jpeg_from_path.cpp
    src/core/meta.cpp
    src/core/output_path.cpp
    src/core/resize.cpp
    src/core/transform.cpp
    src/core/url_fetch.cpp
    src/core/app_image_provider.cpp
    src/core/settings_store.cpp
    src/core/settings_archive.cpp
    src/core/settings_portable.cpp
    src/core/settings_runtime.cpp
    src/core/app_exe_directory.cpp
    src/core/replicate_provider_models_cli.cpp
    src/core/openrouter_provider_models_cli.cpp
    src/core/openai_models_cli.cpp
    src/core/pixlwiz_provider_models_cli.cpp
    src/core/pixlwiz_user_budget.cpp
    src/lib/provider_oauth.cpp
    src/llm/agent_tools.cpp
    src/llm/agent_commons.cpp
    src/llm/agent_context.cpp
    src/llm/agent_feature.cpp
    src/llm/agent_factory.cpp
    src/llm/agent_skills.cpp
    src/llm/agent.cpp
    src/llm/agent_memory.cpp
    src/llm/agent_scheduler.cpp
    src/llm/tool_catalog.cpp
    src/llm/tool_executor.cpp
    src/llm/llm_fs_guard.cpp
    src/llm/llm_glob_filter.cpp
    src/llm/llm_image_tool_defaults.cpp
    src/llm/path_tool_catalog.cpp
    src/llm/path_tool_executor.cpp
    src/llm/tools/computer_use/Tool_ComputerUse.cpp
    src/llm/tools/run/RunTool.cpp
    src/llm/tools/run/RunValidators.cpp
)
if(FEATURE_MCP_CLIENT)
    list(APPEND _pm_media_sources
        src/llm/mcp_stream_client.cpp
        src/llm/mcp_stdio_client.cpp
        src/llm/agent_mcp_bridge.cpp
        src/llm/mcp_probe.cpp
    )
endif()
if(FEATURE_MODBUS)
    list(APPEND _pm_media_sources
        src/modbus/modbus_client.cpp
    )
endif()
if(FEATURE_XBLOX)
    list(APPEND _pm_media_sources
        src/xblox/xblox_commands.cpp
        src/xblox/blocks/app_blocks.cpp
        src/xblox/blocks/builtin_blocks.cpp
        src/xblox/blocks/data_blocks.cpp
        $<$<BOOL:${FEATURE_MODBUS_XBLOX}>:src/xblox/blocks/modbus_blocks.cpp>
        src/xblox/blocks/network_blocks.cpp
        src/xblox/blocks/shell_blocks.cpp
        src/xblox/utils/conv.cpp
    )
endif()
if(FEATURE_PNG_COMPRESSOR)
    list(APPEND _pm_media_sources src/core/png_compress.cpp)
endif()

# ── FEATURE_ENCRYPT_SESSIONS — PME1-encrypt sessions.json (off by default) ────
option(FEATURE_ENCRYPT_SESSIONS
    "Encrypt sessions.json with PME1 (libsodium secretbox + DPAPI key)"
    OFF)
if(FEATURE_ENCRYPT_SESSIONS)
    message(STATUS "FEATURE_ENCRYPT_SESSIONS: ON")
else()
    message(STATUS "FEATURE_ENCRYPT_SESSIONS: OFF (sessions.json stored as plain JSON)")
endif()

if(POLYMECH_MEDIA_SHARED)
    add_library(pm-media SHARED ${_pm_media_sources})
    target_compile_definitions(pm-media PRIVATE POLYMECH_BUILDING_LIBRARY)
else()
    add_library(pm-media STATIC ${_pm_media_sources})
    target_compile_definitions(pm-media PRIVATE POLYMECH_STATIC_BUILD=1)
    target_compile_definitions(pm-media INTERFACE POLYMECH_STATIC_BUILD=1)
endif()

target_include_directories(pm-media PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core
)
if(FEATURE_XBLOX)
    target_include_directories(pm-media PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/xblox)
    target_link_libraries(pm-media PRIVATE muparser)
endif()
if(FEATURE_MODBUS)
    target_link_libraries(pm-media PRIVATE pm-libmodbus)
endif()
target_include_directories(pm-media PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_BINARY_DIR}/include
    ${picosha2_SOURCE_DIR}
    ${kainjow_mustache_SOURCE_DIR}
)
target_compile_definitions(pm-media PUBLIC
    "FEATURE_MCP_CLIENT=$<BOOL:${FEATURE_MCP_CLIENT}>"
    "FEATURE_HOME_LLM_TOOLS=$<BOOL:${FEATURE_HOME_LLM_TOOLS}>"
    "FEATURE_HOME_LLM_SKILLS=$<BOOL:${FEATURE_HOME_LLM_SKILLS}>"
    "FEATURE_AGENT_COMPUTER_USE=$<BOOL:${FEATURE_AGENT_COMPUTER_USE}>"
    "FEATURE_XBLOX=$<BOOL:${FEATURE_XBLOX}>"
    "FEATURE_MODBUS=$<BOOL:${FEATURE_MODBUS}>"
    "FEATURE_MODBUS_XBLOX=$<BOOL:${FEATURE_MODBUS_XBLOX}>"
    "FEATURE_ASSISTANT=$<BOOL:${FEATURE_ASSISTANT}>"
)
target_link_libraries(pm-media PUBLIC
    nlohmann_json::nlohmann_json
    CURL::libcurl
    Vips::vips
    pranav_glob
)
target_link_libraries(pm-media PRIVATE logger kbot sodium)

# Compile-time floor for `pm::log::*_lazy` (see docs/logging.md §12). Numeric PM_COMPILE_LOG_LEVEL:
# 0=TRACE … 5=OFF. PUBLIC so linkers of pm-media (e.g. pm-image) get the same floor in their TUs.
# Empty cache → TRACE(0) for all configs; set PM_LOG_COMPILED_MIN_SEVERITY=INFO (etc.) to strip lazy logs in Release.
set(PM_LOG_COMPILED_MIN_SEVERITY "" CACHE STRING
    "Compile-time log floor for pm::log *_lazy: TRACE|DEBUG|INFO|WARN|ERROR|OFF. Empty = TRACE (0) for all configs.")
if(PM_LOG_COMPILED_MIN_SEVERITY STREQUAL "")
    target_compile_definitions(pm-media PUBLIC PM_COMPILE_LOG_LEVEL=0)
else()
    string(TOUPPER "${PM_LOG_COMPILED_MIN_SEVERITY}" _pm_log_sev_u)
    if(_pm_log_sev_u STREQUAL "TRACE")
        set(_pm_log_sev_num 0)
    elseif(_pm_log_sev_u STREQUAL "DEBUG")
        set(_pm_log_sev_num 1)
    elseif(_pm_log_sev_u STREQUAL "INFO")
        set(_pm_log_sev_num 2)
    elseif(_pm_log_sev_u STREQUAL "WARN")
        set(_pm_log_sev_num 3)
    elseif(_pm_log_sev_u STREQUAL "ERROR")
        set(_pm_log_sev_num 4)
    elseif(_pm_log_sev_u STREQUAL "OFF")
        set(_pm_log_sev_num 5)
    else()
        message(FATAL_ERROR "PM_LOG_COMPILED_MIN_SEVERITY=\"${PM_LOG_COMPILED_MIN_SEVERITY}\" must be TRACE, DEBUG, INFO, WARN, ERROR, or OFF")
    endif()
    target_compile_definitions(pm-media PUBLIC "PM_COMPILE_LOG_LEVEL=${_pm_log_sev_num}")
endif()

if(FEATURE_PNG_COMPRESSOR)
    target_compile_definitions(pm-media PUBLIC FEATURE_PNG_COMPRESSOR=1)
    # libimagequant — the FetchContent target is named 'imagequant' in v2.x
    if(TARGET imagequant)
        target_link_libraries(pm-media PRIVATE imagequant)
    elseif(TARGET imagequant_static)
        target_link_libraries(pm-media PRIVATE imagequant_static)
    else()
        target_link_libraries(pm-media PRIVATE libimagequant)
    endif()
    # libpng static target is 'png_static' from pnggroup/libpng CMakeLists
    target_link_libraries(pm-media PRIVATE png_static)
    target_include_directories(pm-media PRIVATE
        ${libimagequant_SOURCE_DIR}
        ${libpng_SOURCE_DIR}
        ${libpng_BINARY_DIR}   # for pnglibconf.h (generated header)
    )
    if(FEATURE_PNG_ZOPFLI)
        target_compile_definitions(pm-media PUBLIC FEATURE_PNG_ZOPFLI=1)
        target_link_libraries(pm-media PRIVATE libzopflipng)
        target_include_directories(pm-media PRIVATE ${zopfli_SOURCE_DIR}/src)
    endif()
endif()

if(WIN32)
    target_compile_definitions(pm-media PRIVATE _WIN32_WINNT=0x0A00 NOMINMAX)
    file(GLOB _MEDIA_VIPS_SDK_LIST_LIB
        "${PM_DIST_ROOT_DIR}/vendor/vips-dev-*"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/vips-dev-*")
    if(_MEDIA_VIPS_SDK_LIST_LIB)
        list(GET _MEDIA_VIPS_SDK_LIST_LIB 0 _MEDIA_VIPS_SDK_LIB)
        if(EXISTS "${_MEDIA_VIPS_SDK_LIB}/lib/libgobject-2.0.lib")
            target_link_libraries(pm-media PRIVATE
                "${_MEDIA_VIPS_SDK_LIB}/lib/libgobject-2.0.lib"
                "${_MEDIA_VIPS_SDK_LIB}/lib/libglib-2.0.lib"
            )
        endif()
    endif()
endif()

if(POLYMECH_MEDIA_SHARED)
    set_target_properties(pm-media PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${PM_DIST_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${PM_DIST_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${PM_DIST_DIR}"
        LIBRARY_OUTPUT_DIRECTORY "${PM_DIST_DIR}"
        ARCHIVE_OUTPUT_DIRECTORY "${PM_DIST_DIR}"
    )
endif()

install(TARGETS pm-media
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)
install(FILES
    src/core/polymech_export.h
    src/core/resize.hpp
    src/core/transform.hpp
    src/core/app_image_provider.hpp
    src/core/meta.hpp
    src/core/find.hpp
    src/core/search.hpp
    src/core/duplicates.hpp
    DESTINATION include/polymech
)
if(FEATURE_XBLOX)
    install(FILES
        src/xblox/xblox_commands.hpp
        DESTINATION include/polymech
    )
endif()
install(FILES
    src/llm/tool_catalog.hpp
    src/llm/tool_executor.hpp
    src/llm/llm_fs_guard.hpp
    src/llm/llm_glob_filter.hpp
    src/llm/llm_fs_write_blocklist.h
    src/llm/path_tool_catalog.hpp
    src/llm/path_tool_executor.hpp
    src/llm/tools/computer_use/Tool_ComputerUse.hpp
    src/llm/tools/run/RunTool.hpp
    src/llm/tools/run/RunValidators.hpp
    DESTINATION include/polymech/llm
)

# OpenAPI 3 YAML — embedded at configure time; served by `pm-image serve` at GET /openapi.yaml.
set(PM_OPENAPI_YAML_SRC "${CMAKE_CURRENT_SOURCE_DIR}/docs/public/openapi.yaml")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${PM_OPENAPI_YAML_SRC}")
if(NOT EXISTS "${PM_OPENAPI_YAML_SRC}")
    message(FATAL_ERROR "OpenAPI spec missing: ${PM_OPENAPI_YAML_SRC}")
endif()
file(READ "${PM_OPENAPI_YAML_SRC}" PM_OPENAPI_YAML_BODY)
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/src/http/openapi_embed.cpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/openapi_embed.cpp"
    @ONLY
)
set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/openapi_embed.cpp" PROPERTIES GENERATED TRUE)

# ── pm-image executable ─────────────────────────────────────────────
add_executable(${MEDIA_TARGET_PM_IMAGE}
    src/cli/pm_image_cli_helpers.cpp
    src/cli/cli_tty.cpp
    src/cli/pm_image_register_cli.cpp
    src/cli/pm_image_settings.cpp
    src/cli/pm_image_dispatch.cpp
    src/cli/pm_image_dispatch_remaining.cpp
    src/cli/pm_image_cmd_resize.cpp
    src/cli/pm_image_cmd_find.cpp
    src/cli/pm_image_cmd_search.cpp
    src/cli/pm_image_cmd_dup.cpp
    src/cli/pm_image_cmd_meta.cpp
    src/cli/pm_image_cmd_transform.cpp
    src/cli/pm_image_cmd_create.cpp
    src/cli/pm_image_cmd_compress.cpp
    src/cli/pm_image_cmd_provider_models.cpp
    src/cli/pm_image_cmd_daemon.cpp
    src/cli/pm_image_cmd_assistant.cpp
    src/cli/pm_image_cmd_llm_list.cpp
    src/cli/pm_image_cmd_llm_call.cpp
    src/cli/pm_image_cmd_llm_agent.cpp
    src/cli/pm_image_cmd_llm_info.cpp
    src/cli/pm_image_cmd_info.cpp
    src/cli/pm_image_cmd_register_explorer.cpp
    src/cli/pm_image_cmd_register_startmenu.cpp
    src/cli/pm_image_cmd_installer.cpp
    src/cli/pm_image_cmd_app.cpp
    src/cli/pm_image_cmd_batch.cpp
    src/cli/pm_image_cmd_service.cpp
    src/lib/pm_zitadel_oauth.cpp
    src/lib/pm_service_upload.cpp
    src/lib/pm_service_posts.cpp
    src/cli/pm_image_cmd_print_help.cpp
    src/cli/pm_image_run.cpp
    src/cli/pm_image_entry.cpp
    src/cmd_kbot.cpp
)
target_compile_definitions(${MEDIA_TARGET_PM_IMAGE} PRIVATE
    "FEATURE_MCP_SERVER=$<BOOL:${FEATURE_MCP_SERVER}>"
    "FEATURE_HOME_LLM_TOOLS=$<BOOL:${FEATURE_HOME_LLM_TOOLS}>"
    "FEATURE_HOME_LLM_SKILLS=$<BOOL:${FEATURE_HOME_LLM_SKILLS}>"
    "FEATURE_AGENT_COMPUTER_USE=$<BOOL:${FEATURE_AGENT_COMPUTER_USE}>"
    "FEATURE_CUSTOM_COMMANDS=$<BOOL:${FEATURE_CUSTOM_COMMANDS}>"
    "FEATURE_XBLOX=$<BOOL:${FEATURE_XBLOX}>"
    "FEATURE_MODBUS=$<BOOL:${FEATURE_MODBUS}>"
    "FEATURE_MODBUS_XBLOX=$<BOOL:${FEATURE_MODBUS_XBLOX}>"
)
if(FEATURE_MCP_SERVER)
    target_sources(${MEDIA_TARGET_PM_IMAGE} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src/cli/pm_image_mcp_embed.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/cli/pm_image_mcp_streamable.cpp"
    )
endif()
# ── FEATURE_SERVE — HTTP REST API server (src/http/serve.cpp + OpenAPI embed) ──
option(FEATURE_SERVE "HTTP REST server subcommand (pm-image serve …)" ON)
if(FEATURE_SERVE)
    message(STATUS "FEATURE_SERVE: ON")
    target_compile_definitions(${MEDIA_TARGET_PM_IMAGE} PRIVATE FEATURE_SERVE=1)
    target_sources(${MEDIA_TARGET_PM_IMAGE} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src/cli/pm_image_cmd_serve.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/http/serve.cpp"
        "${CMAKE_CURRENT_BINARY_DIR}/openapi_embed.cpp"
    )
else()
    message(STATUS "FEATURE_SERVE: OFF")
endif()
# ── FEATURE_IPC — JSON-line IPC server (src/ipc/ipc_serve.cpp) ──────────────
option(FEATURE_IPC "JSON-line IPC server subcommand (pm-image ipc …)" ON)
if(FEATURE_IPC)
    message(STATUS "FEATURE_IPC: ON")
    target_compile_definitions(${MEDIA_TARGET_PM_IMAGE} PRIVATE FEATURE_IPC=1)
    target_sources(${MEDIA_TARGET_PM_IMAGE} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src/cli/pm_image_cmd_ipc.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/ipc/ipc_serve.cpp"
    )
else()
    message(STATUS "FEATURE_IPC: OFF")
endif()

# ── FEATURE_COMMAND_* — CLI command enable flags ──────────────────────────────
# All commands default to ON (via CMakePresets.json base preset). Disable specific
# commands to build minimal CLI variants (e.g., headless servers without UI commands).
message(STATUS "--- Command Features ---")

option(FEATURE_COMMAND_RESIZE "CLI: resize command" ON)
option(FEATURE_COMMAND_COMPRESS "CLI: compress command" ON)
option(FEATURE_COMMAND_TRANSFORM "CLI: transform command" ON)
option(FEATURE_COMMAND_CREATE "CLI: create command" ON)
option(FEATURE_COMMAND_META "CLI: meta command" ON)
option(FEATURE_COMMAND_FIND "CLI: find command" ON)
option(FEATURE_COMMAND_SEARCH "CLI: search command" ON)
option(FEATURE_COMMAND_DUPLICATES "CLI: duplicates command" ON)
option(FEATURE_COMMAND_PROVIDER_MODELS "CLI: provider models command" ON)
option(FEATURE_COMMAND_LLM "CLI: llm command" ON)
option(FEATURE_REGISTER_EXPLORER "CLI: register-explorer command" ON)
option(FEATURE_COMMAND_APP "CLI: app command" ON)
option(FEATURE_COMMAND_SERVICE "CLI: service command" ON)
option(FEATURE_COMMAND_BATCH "CLI: batch command" ON)
option(FEATURE_COMMAND_SETTINGS "CLI: settings command" ON)
option(FEATURE_COMMAND_TEST "CLI: test command (Win32 dev harness)" ON)
option(FEATURE_COMMAND_LICENSE "CLI: license command (Win32 offline activation)" ON)
option(FEATURE_COMMAND_STATUS "CLI: status command (Pixlwiz credits plus optional license/trial status)" ON)
option(FEATURE_COMMAND_COMMANDS "CLI: commands command (list registered commands)" ON)
option(FEATURE_COMMAND_INFO "CLI: info command (write generated agent skill references)" ON)
option(FEATURE_COMMAND_REPLAY "CLI: replay command (Win32 session replay)" ON)
option(FEATURE_COMMAND_SERVE "CLI: serve command (HTTP server mode)" ON)
option(FEATURE_COMMAND_IPC "CLI: ipc command (inter-process communication)" ON)
option(FEATURE_COMMAND_AUDIO "CLI: audio command (requires FEATURE_STT)" ON)
option(FEATURE_COMMAND_VIDEO "CLI: video command (requires FEATURE_VIDEO)" ON)
option(FEATURE_COMMAND_XBLOX "CLI: xblox block-tree command runner" ON)
option(FEATURE_DAEMON "Global shortcut daemon (Win32 hotkeys; cross-platform CLI surface)" ON)
option(FEATURE_ASSISTANT "UIAutomation assistant spy PoC — auto spy command (Win32 only)" ON)

# Collect all command feature definitions into a single compile definition block
# ── Command feature dependencies ───────────────────────────────────────────────
# Some commands require base features to be enabled (especially on Windows)
if(WIN32)
    # License command requires FEATURE_LICENSE_FILE
    if(FEATURE_COMMAND_LICENSE AND NOT FEATURE_LICENSE_FILE)
        message(WARNING "FEATURE_COMMAND_LICENSE=ON requires FEATURE_LICENSE_FILE=ON — disabling license command")
        set(FEATURE_COMMAND_LICENSE OFF)
    endif()
endif()
if(FEATURE_COMMAND_SERVE AND NOT FEATURE_SERVE)
    message(WARNING "FEATURE_COMMAND_SERVE=ON requires FEATURE_SERVE=ON — disabling serve command")
    set(FEATURE_COMMAND_SERVE OFF)
endif()
if(FEATURE_COMMAND_IPC AND NOT FEATURE_IPC)
    message(WARNING "FEATURE_COMMAND_IPC=ON requires FEATURE_IPC=ON — disabling ipc command")
    set(FEATURE_COMMAND_IPC OFF)
endif()
if(FEATURE_COMMAND_AUDIO AND DEFINED FEATURE_STT AND NOT FEATURE_STT)
    message(WARNING "FEATURE_COMMAND_AUDIO=ON requires FEATURE_STT=ON — disabling audio command")
    set(FEATURE_COMMAND_AUDIO OFF)
endif()
if(FEATURE_COMMAND_VIDEO AND DEFINED FEATURE_VIDEO AND NOT FEATURE_VIDEO)
    message(WARNING "FEATURE_COMMAND_VIDEO=ON requires FEATURE_VIDEO=ON — disabling video command")
    set(FEATURE_COMMAND_VIDEO OFF)
endif()
if(FEATURE_COMMAND_XBLOX AND NOT FEATURE_XBLOX)
    message(WARNING "FEATURE_COMMAND_XBLOX=ON requires FEATURE_XBLOX=ON — disabling xblox command")
    set(FEATURE_COMMAND_XBLOX OFF)
endif()

set(_PM_COMMAND_DEFS "")
foreach(_cmd RESIZE COMPRESS TRANSFORM CREATE META FIND SEARCH DUPLICATES
             PROVIDER_MODELS LLM APP SERVICE BATCH SETTINGS
             TEST LICENSE STATUS COMMANDS INFO REPLAY SERVE IPC AUDIO VIDEO XBLOX SESSION_PERSISTENCE LOG_VIEW QUEUE_VIEW UI_COMMAND_CONTROL)
    string(TOLOWER "${_cmd}" _cmd_lower)
    if(FEATURE_COMMAND_${_cmd})
        list(APPEND _PM_COMMAND_DEFS "FEATURE_COMMAND_${_cmd}=1")
        message(STATUS "FEATURE_COMMAND_${_cmd}: ON")
    else()
        list(APPEND _PM_COMMAND_DEFS "FEATURE_COMMAND_${_cmd}=0")
        message(STATUS "FEATURE_COMMAND_${_cmd}: OFF")
    endif()
endforeach()
message(STATUS "FEATURE_REGISTER_EXPLORER: ${FEATURE_REGISTER_EXPLORER}")
message(STATUS "FEATURE_DAEMON: ${FEATURE_DAEMON}")
message(STATUS "FEATURE_ASSISTANT: ${FEATURE_ASSISTANT}")
target_compile_definitions(${MEDIA_TARGET_PM_IMAGE} PRIVATE ${_PM_COMMAND_DEFS})
target_compile_definitions(${MEDIA_TARGET_PM_IMAGE} PRIVATE
    "FEATURE_REGISTER_EXPLORER=$<BOOL:${FEATURE_REGISTER_EXPLORER}>"
    "FEATURE_DAEMON=$<BOOL:${FEATURE_DAEMON}>"
    "FEATURE_ASSISTANT=$<BOOL:${FEATURE_ASSISTANT}>"
)

if(FEATURE_COMMAND_STATUS)
    target_sources(${MEDIA_TARGET_PM_IMAGE} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src/cli/pm_image_cmd_status.cpp")
endif()

# ── FEATURE_ASSISTANT — UIAutomation spy PoC (Win32 only) ────────────────────
if(WIN32 AND FEATURE_ASSISTANT)
    target_sources(${MEDIA_TARGET_PM_IMAGE} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src/win/assistant/app_targets.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/win/assistant/uia_spy.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/win/assistant/assistant_bar.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/win/assistant/app_inspect.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/win/assistant/app_use.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/win/assistant/app_batch.cpp"
    )
    target_link_libraries(${MEDIA_TARGET_PM_IMAGE} PRIVATE uiautomationcore.lib windowscodecs.lib gdi32 shlwapi)
endif()
if(FEATURE_COMMAND_XBLOX)
    target_sources(${MEDIA_TARGET_PM_IMAGE} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src/cli/pm_image_cmd_xblox.cpp"
    )
endif()

# ── FEATURE_EXPLORER_INTEGRATION ──────────────────────────────────────────────
# Base feature to enable/disable Windows Explorer context menu integration
option(FEATURE_EXPLORER_INTEGRATION "Windows Explorer context menu integration (IExecute/IExplorerCommand)" ON)
if(FEATURE_EXPLORER_INTEGRATION)
    message(STATUS "FEATURE_EXPLORER_INTEGRATION: ON")
    target_compile_definitions(${MEDIA_TARGET_PM_IMAGE} PRIVATE FEATURE_EXPLORER_INTEGRATION=1)
else()
    message(STATUS "FEATURE_EXPLORER_INTEGRATION: OFF")
    target_compile_definitions(${MEDIA_TARGET_PM_IMAGE} PRIVATE FEATURE_EXPLORER_INTEGRATION=0)
endif()

# Shared translation units use `#if FEATURE_PIXLWIZ_*`. Linux CLI has no Pixlwiz shell; Windows + macOS
# enable ZITADEL login/logout and (where applicable) service posts CLI.
if(NOT WIN32 AND NOT APPLE)
    target_compile_definitions(${MEDIA_TARGET_PM_IMAGE} PRIVATE FEATURE_PIXLWIZ_AUTH=0 FEATURE_PIXLWIZ_SHARE=0)
endif()
# macOS: same ZITADEL / `service posts create` flags as Windows (no Win32-only UI sources).
if(APPLE)
    option(FEATURE_PIXLWIZ_AUTH "Pixlwiz ZITADEL: CLI login/logout (tokens beside PixelWiz settings.json)" ON)
    option(FEATURE_PIXLWIZ_SHARE "Pixlwiz Share: service posts create CLI (no Explorer shell on macOS)" ON)
    if(FEATURE_PIXLWIZ_SHARE AND NOT FEATURE_PIXLWIZ_AUTH)
        message(WARNING
            "FEATURE_PIXLWIZ_SHARE=ON without FEATURE_PIXLWIZ_AUTH: there is no `pm-image login` in this build; "
            "Bearer tokens must still exist in zitadel-oauth.json for posts/upload.")
    endif()
    if(FEATURE_PIXLWIZ_AUTH)
        target_compile_definitions(${MEDIA_TARGET_PM_IMAGE} PRIVATE FEATURE_PIXLWIZ_AUTH=1)
        target_sources(${MEDIA_TARGET_PM_IMAGE} PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/src/cli/pm_image_cmd_login.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/cli/pm_image_cmd_logout.cpp")
        message(STATUS "FEATURE_PIXLWIZ_AUTH: ON (macOS)")
    else()
        target_compile_definitions(${MEDIA_TARGET_PM_IMAGE} PRIVATE FEATURE_PIXLWIZ_AUTH=0)
        message(STATUS "FEATURE_PIXLWIZ_AUTH: OFF — no login/logout CLI (macOS)")
    endif()
    if(FEATURE_PIXLWIZ_SHARE)
        target_compile_definitions(${MEDIA_TARGET_PM_IMAGE} PRIVATE FEATURE_PIXLWIZ_SHARE=1)
        message(STATUS "FEATURE_PIXLWIZ_SHARE: ON (macOS)")
    else()
        target_compile_definitions(${MEDIA_TARGET_PM_IMAGE} PRIVATE FEATURE_PIXLWIZ_SHARE=0)
        message(STATUS "FEATURE_PIXLWIZ_SHARE: OFF — no service posts create CLI (macOS)")
    endif()
endif()
if(WIN32)
    # Pixlwiz ZITADEL CLI + menu login/logout (optional).
    option(FEATURE_PIXLWIZ_AUTH "Pixlwiz ZITADEL: CLI login/logout, menu login/logout, web chat auth bar" ON)
    # Pixlwiz Share: in-app share, Explorer shell verb, service posts create (+ Win32 dialogs / job UI).
    option(FEATURE_PIXLWIZ_SHARE "Pixlwiz Share: menu, Explorer verb, service posts create, share dialogs" ON)
    if(FEATURE_PIXLWIZ_SHARE AND NOT FEATURE_PIXLWIZ_AUTH)
        message(WARNING
            "FEATURE_PIXLWIZ_SHARE=ON without FEATURE_PIXLWIZ_AUTH: there is no `pm-image login` in this build; "
            "Bearer tokens must still exist in zitadel-oauth.json for posts/upload.")
    endif()
    if(FEATURE_PIXLWIZ_AUTH)
        target_compile_definitions(${MEDIA_TARGET_PM_IMAGE} PRIVATE FEATURE_PIXLWIZ_AUTH=1)
        target_sources(${MEDIA_TARGET_PM_IMAGE} PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/src/cli/pm_image_cmd_login.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/cli/pm_image_cmd_logout.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/win/pixlwiz_auth_payload.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/win/pixlwiz_login_spawn.cpp")
        message(STATUS "FEATURE_PIXLWIZ_AUTH: ON")
    else()
        target_compile_definitions(${MEDIA_TARGET_PM_IMAGE} PRIVATE FEATURE_PIXLWIZ_AUTH=0)
        message(STATUS "FEATURE_PIXLWIZ_AUTH: OFF — no login/logout CLI, menu, or chat auth bridge")
    endif()
    if(FEATURE_PIXLWIZ_SHARE)
        target_compile_definitions(${MEDIA_TARGET_PM_IMAGE} PRIVATE FEATURE_PIXLWIZ_SHARE=1)
        message(STATUS "FEATURE_PIXLWIZ_SHARE: ON")
    else()
        target_compile_definitions(${MEDIA_TARGET_PM_IMAGE} PRIVATE FEATURE_PIXLWIZ_SHARE=0)
        message(STATUS "FEATURE_PIXLWIZ_SHARE: OFF — no Share menu/verb, no posts-create UI")
    endif()

    target_sources(${MEDIA_TARGET_PM_IMAGE} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src/win/app_commands.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/win/ribbon_commands.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/win/session_replay/session_replay.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/win/session_replay/session_input_hooks.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/win/register_explorer.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/win/register_startmenu.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/win/shell/pm_iexecute_reg.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/win/resize_ui.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/win/explorer_job_ui.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/win/screenshot.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/win/ui_singleton.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/win/LayoutStore.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/win/settings_store.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/win/machine_fingerprint.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/win/media-img-win.manifest"
    )

    # ── FEATURE_TRIAL_CHECK — trial protection (only when ON) ───────────────────
    if(FEATURE_TRIAL_CHECK)
        target_sources(${MEDIA_TARGET_PM_IMAGE} PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/src/win/trial_protection.cpp"
        )
    endif()

    # ── ui_next (Win32++ ribbon + dock UI) ──────────────────────────────
    set(_UI_NEXT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/win/ui_next")

    # Own-ribbon UI uses hand-maintained `RibbonUI.h` + `RibbonUI.rc` (string + bitmap ids for the
    # toolbar strip). The old `Ribbon.xml` → uicc → Ribbon.bml pipeline is no longer used.
    add_custom_target(pm_image_ribbon COMMENT "pm-image: no uicc step; RibbonUI.h/rc are source files")

    target_sources(${MEDIA_TARGET_PM_IMAGE} PRIVATE
        "${_UI_NEXT_DIR}/App.cpp"
        "${_UI_NEXT_DIR}/ChatProviderDlg.cpp"
        "${_UI_NEXT_DIR}/FileViewer.cpp"
        "${_UI_NEXT_DIR}/FileViewerPanel.cpp"
        "${_UI_NEXT_DIR}/FileViewerImageCropTool.cpp"
        "${_UI_NEXT_DIR}/FileViewerImagePillToolbar.cpp"
        "${_UI_NEXT_DIR}/ViewerTabsHost.cpp"
        "${_UI_NEXT_DIR}/PreviewCoordinator.cpp"
        "${_UI_NEXT_DIR}/ExplorerSelectionRouter.cpp"
        "${_UI_NEXT_DIR}/FileViewer_Text.cpp"
        "${_UI_NEXT_DIR}/FileViewer_Markdown.cpp"
        "${_UI_NEXT_DIR}/FileViewer_PDF.cpp"
        "${_UI_NEXT_DIR}/FileViewer_Video.cpp"
        "${_UI_NEXT_DIR}/FileViewer_BrowserImage.cpp"
        "${_UI_NEXT_DIR}/FileViewer_HTML.cpp"
        "${_UI_NEXT_DIR}/FileViewer_3D.cpp"
        "${_UI_NEXT_DIR}/FileViewer_OpenSCAD.cpp"
        "${_UI_NEXT_DIR}/FileViewer_Images.cpp"
        "${_UI_NEXT_DIR}/AppSettingsDlg.cpp"
        "${_UI_NEXT_DIR}/FileQueue.cpp"
        "${_UI_NEXT_DIR}/queue_path_enumeration.cpp"
        "${_UI_NEXT_DIR}/FindPanel.cpp"
        "${_UI_NEXT_DIR}/DuplicatePanel.cpp"
        "${_UI_NEXT_DIR}/helpers/theme.cpp"
        "${_UI_NEXT_DIR}/helpers/ui_font.cpp"
        "${_UI_NEXT_DIR}/helpers/ui_dialog_relayout.cpp"
        "${_UI_NEXT_DIR}/helpers/ui_language.cpp"
        "${_UI_NEXT_DIR}/helpers/app_settings_i18n.cpp"
        "${_UI_NEXT_DIR}/helpers/provider_dlg_i18n.cpp"
        "${_UI_NEXT_DIR}/helpers/dock_chrome_i18n.cpp"
        "${_UI_NEXT_DIR}/helpers/dock_panel_toolstrip.cpp"
        "${_UI_NEXT_DIR}/helpers/chat_provider_dlg_i18n.cpp"
        "${_UI_NEXT_DIR}/helpers/settings_panel_i18n.cpp"
        "${_UI_NEXT_DIR}/helpers/settings_panel_modes_i18n.cpp"
        "${_UI_NEXT_DIR}/helpers/default_shell.cpp"
        "${_UI_NEXT_DIR}/LogPanel.cpp"
        "${_UI_NEXT_DIR}/log_sink.cpp"
        "${_UI_NEXT_DIR}/ui_log_file.cpp"
        "${_UI_NEXT_DIR}/helpers/win_initial_show.cpp"
        "${_UI_NEXT_DIR}/helpers/preview_file_info.cpp"
        "${_UI_NEXT_DIR}/helpers/markdown_preview_base.cpp"
        "${_UI_NEXT_DIR}/SettingsPanel_core.cpp"
        "${_UI_NEXT_DIR}/SettingsPanel_Resize.cpp"
        "${_UI_NEXT_DIR}/SettingsPanel_Compress.cpp"
        "${_UI_NEXT_DIR}/SettingsPanel_Duplicates.cpp"
        "${_UI_NEXT_DIR}/SettingsPanel_Transform.cpp"
        "${_UI_NEXT_DIR}/SettingsPanel_Meta.cpp"
        "${_UI_NEXT_DIR}/SettingsPanel_Find.cpp"
        "${_UI_NEXT_DIR}/SettingsPanel_read.cpp"
        "${_UI_NEXT_DIR}/SettingsPanel_dock.cpp"
        "${_UI_NEXT_DIR}/ProviderDlg.cpp"
        "${_UI_NEXT_DIR}/ProviderModelRegistry.cpp"
        "${_UI_NEXT_DIR}/ReplicateSelectorController.cpp"
        "${_UI_NEXT_DIR}/OpenRouterSelectorController.cpp"
        "${_UI_NEXT_DIR}/PixlWizSelectorController.cpp"
        "${_UI_NEXT_DIR}/OpenAISelectorController.cpp"
        "${_UI_NEXT_DIR}/AudioSelectorController.cpp"
        "${_UI_NEXT_DIR}/ElevenLabsSelectorController.cpp"
        "${_UI_NEXT_DIR}/widgets/SearchableCombo.cpp"
        "${_UI_NEXT_DIR}/NodesPanel.cpp"
        "${_UI_NEXT_DIR}/CSettingsWebView.cpp"
        "${_UI_NEXT_DIR}/Mainfrm.cpp"
        "${_UI_NEXT_DIR}/Mainfrm_Web.cpp"
        "${_UI_NEXT_DIR}/workbench/DefaultMainWorkbench.cpp"
        "${_UI_NEXT_DIR}/workbench/ChatSimpleWorkbench.cpp"
        "${_UI_NEXT_DIR}/workbench/ViewerSimpleWorkbench.cpp"
        "${_UI_NEXT_DIR}/pm_win32_dock_json.cpp"
        "${_UI_NEXT_DIR}/Mainfrm_layout.cpp"
        "${_UI_NEXT_DIR}/Mainfrm_commands.cpp"
        "${_UI_NEXT_DIR}/Mainfrm_queue.cpp"
        "${_UI_NEXT_DIR}/Mainfrm_presets.cpp"
        "${_UI_NEXT_DIR}/launch_ui_next.cpp"
        "${_UI_NEXT_DIR}/Resource.rc"
    )
    if(FEATURE_XBLOX)
        target_sources(${MEDIA_TARGET_PM_IMAGE} PRIVATE "${_UI_NEXT_DIR}/CBlockView.cpp")
    endif()
    if(PM_HAS_BRANDING_ICON)
        target_sources(${MEDIA_TARGET_PM_IMAGE} PRIVATE "${PM_APP_ICON_RC}")
    endif()
    add_dependencies(${MEDIA_TARGET_PM_IMAGE} pm_image_ribbon)
    # helpers/ is a subdirectory of _UI_NEXT_DIR — included as "helpers/xxx.hpp"
    target_include_directories(${MEDIA_TARGET_PM_IMAGE} PRIVATE "${_UI_NEXT_DIR}")

    if(FEATURE_PIXLWIZ_SHARE)
        target_sources(${MEDIA_TARGET_PM_IMAGE} PRIVATE "${_UI_NEXT_DIR}/PixlwizSharePostDlg.cpp")
    endif()
    # Pixlwiz menu/dialog `#if`s in Resource.rc use `FEATURE_PIXLWIZ_*` from target_compile_definitions
    # only — do not add per-file /D for Resource.rc (rc.exe warns RC4005 on redefinition).

    # ── FEATURE_SESSION_VIDEO_RECORDER — WGC + H.264 MP4 (MSVC; C++/WinRT + SDK) ─
    if(MSVC)
        option(FEATURE_SESSION_VIDEO_RECORDER
            "Record the main window to Videos/session-HHMMSS.mp4 (known folder; Windows.Graphics.Capture + H.264)"
            ON)
    else()
        set(FEATURE_SESSION_VIDEO_RECORDER OFF CACHE BOOL
            "Record the main window to Videos/session-HHMMSS.mp4 (requires MSVC; disabled)" FORCE)
    endif()
    if(FEATURE_SESSION_VIDEO_RECORDER)
        message(STATUS "FEATURE_SESSION_VIDEO_RECORDER: ON")
        target_compile_definitions(${MEDIA_TARGET_PM_IMAGE} PRIVATE FEATURE_SESSION_VIDEO_RECORDER=1)
        set(_ROBMIKH "${CMAKE_CURRENT_SOURCE_DIR}/src/session/recorder/vendor/robmikh.common.0.0.23-beta/include")
        target_include_directories(${MEDIA_TARGET_PM_IMAGE} PRIVATE "${_ROBMIKH}")
        if(DEFINED ENV{WindowsSdkDir})
            set(_SDKIN "$ENV{WindowsSdkDir}Include")
            if(CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION)
                set(_CPPWINRT "${_SDKIN}/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/cppwinrt")
            else()
                file(GLOB _W10KITS RELATIVE "${_SDKIN}" "${_SDKIN}/10.0.*")
                if(_W10KITS)
                    list(SORT _W10KITS)
                    list(GET _W10KITS -1 _KVER)
                    set(_CPPWINRT "${_SDKIN}/${_KVER}/cppwinrt")
                else()
                    set(_CPPWINRT "${_SDKIN}/cppwinrt")
                endif()
            endif()
            if(EXISTS "${_CPPWINRT}/winrt/base.h")
                target_include_directories(${MEDIA_TARGET_PM_IMAGE} PRIVATE "${_CPPWINRT}")
            else()
                message(WARNING
                    "FEATURE_SESSION_VIDEO_RECORDER: C++/WinRT not found at ${_CPPWINRT} (install Windows 10+ SDK; cppwinrt).")
            endif()
        endif()
        target_sources(${MEDIA_TARGET_PM_IMAGE} PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/src/session/recorder/pm_session_video_recorder.cpp"
        )
        target_link_libraries(${MEDIA_TARGET_PM_IMAGE} PRIVATE
            windowsapp.lib mfplat.lib mfreadwrite.lib mfuuid.lib strmiids.lib RuntimeObject.lib
        )
    else()
        message(STATUS "FEATURE_SESSION_VIDEO_RECORDER: OFF")
    endif()

    # ── FEATURE_TRIAL_CHECK — 14-day trial (registry + hidden file + HMAC) ───────
    option(FEATURE_TRIAL_CHECK "Enforce 14-day trial (registry + %APPDATA%\\Microsoft\\clr.dat + HMAC)" OFF)
    if(FEATURE_TRIAL_CHECK)
        message(STATUS "FEATURE_TRIAL_CHECK: ON")
        target_compile_definitions(${MEDIA_TARGET_PM_IMAGE} PRIVATE FEATURE_TRIAL_CHECK=1)
    else()
        message(STATUS "FEATURE_TRIAL_CHECK: OFF")
    endif()

    # ── FEATURE_LICENSE_FILE — offline Ed25519 license.json (sample issuer key) ─
    option(FEATURE_LICENSE_FILE "Accept offline license.json (Ed25519); skips trial when valid" OFF)
    if(FEATURE_LICENSE_FILE)
        message(STATUS "FEATURE_LICENSE_FILE: ON")
        target_compile_definitions(${MEDIA_TARGET_PM_IMAGE} PRIVATE FEATURE_LICENSE_FILE=1)
        target_sources(${MEDIA_TARGET_PM_IMAGE} PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/src/win/license_file.cpp"
        )
    else()
        message(STATUS "FEATURE_LICENSE_FILE: OFF")
    endif()

    # ── FEATURE_RAW_PREVIEW — fast RAW preview via vips_thumbnail ─────────────────
    # vips_thumbnail extracts the camera-embedded JPEG (via libraw inside vips);
    # no separate libraw linking needed — Vips::vips already covers it.
    option(FEATURE_RAW_PREVIEW "Fast preview for camera RAW files (ARW, CR2, NEF, DNG …)" ON)
    if(FEATURE_RAW_PREVIEW)
        message(STATUS "FEATURE_RAW_PREVIEW: ON — fast RAW preview enabled")
        target_compile_definitions(${MEDIA_TARGET_PM_IMAGE} PRIVATE FEATURE_RAW_PREVIEW=1)
    else()
        message(STATUS "FEATURE_RAW_PREVIEW: OFF")
    endif()

    # ── FEATURE_RAW_VIEW — full-quality background Bayer decode ──────────────────
    # Replaces the fast preview with a full vips_image_new_from_file decode
    # a few seconds later.  Requires FEATURE_RAW_PREVIEW.
    option(FEATURE_RAW_VIEW "Full-quality background RAW decode (replaces fast preview)" ON)
    if(FEATURE_RAW_VIEW)
        if(FEATURE_RAW_PREVIEW)
            message(STATUS "FEATURE_RAW_VIEW: ON — quality RAW decode enabled")
            target_compile_definitions(${MEDIA_TARGET_PM_IMAGE} PRIVATE FEATURE_RAW_VIEW=1)
        else()
            message(WARNING "FEATURE_RAW_VIEW requires FEATURE_RAW_PREVIEW=ON — skipping")
        endif()
    else()
        message(STATUS "FEATURE_RAW_VIEW: OFF")
    endif()

    # ── FEATURE_D2D_GALLERY — D2D filmstrip dock overlay in CFileViewer ──────────
    # Adds a cover-flow filmstrip at the bottom of the image viewer.
    # Sources live in src/win/ui_next/gallery/filmstrip/ (owned copy, not win32-mini).
    # New link deps: d2d1 dwrite windowscodecs (standard Windows SDK — no downloads).
    # Tier A: software DC render target. GPU effects require a future hardware upgrade.
    option(FEATURE_D2D_GALLERY "D2D filmstrip dock overlay in the centre image viewer" ON)
    if(FEATURE_D2D_GALLERY)
        message(STATUS "FEATURE_D2D_GALLERY: ON — D2D filmstrip dock in CFileViewer")
        target_compile_definitions(${MEDIA_TARGET_PM_IMAGE} PRIVATE FEATURE_D2D_GALLERY=1)
        set(_GALLERY_DIR "${_UI_NEXT_DIR}/gallery/filmstrip")
        target_sources(${MEDIA_TARGET_PM_IMAGE} PRIVATE
            "${_GALLERY_DIR}/FilmStrip.cpp"
            "${_GALLERY_DIR}/FilmStripThumb.cpp"
        )
        target_include_directories(${MEDIA_TARGET_PM_IMAGE} PRIVATE
            "${_UI_NEXT_DIR}/gallery"
        )
        target_link_libraries(${MEDIA_TARGET_PM_IMAGE} PRIVATE
            d2d1.lib dwrite.lib windowscodecs.lib
        )
    else()
        message(STATUS "FEATURE_D2D_GALLERY: OFF")
    endif()

    # ── FEATURE_STT — mic capture via miniaudio (WASAPI on Win32, CoreAudio later on macOS) ─────────
    # miniaudio is a single-header C/C++ audio I/O library; no extra link deps on Win32
    # (WASAPI is part of ole32/mmdevapi, both already present).
    # Sources live in src/win/stt/ (AudioInput, DictationController, etc.).
    option(FEATURE_STT "Mic capture + realtime STT via miniaudio (WASAPI)" ON)
    if(FEATURE_STT)
        message(STATUS "FEATURE_STT: ON — miniaudio mic capture enabled")
        FetchContent_Declare(
            miniaudio
            GIT_REPOSITORY https://github.com/mackron/miniaudio.git
            GIT_TAG        0.11.21
            GIT_SHALLOW    TRUE
        )
        FetchContent_GetProperties(miniaudio)
        if(NOT miniaudio_POPULATED)
            FetchContent_Populate(miniaudio)
        endif()
        add_library(miniaudio_hdr INTERFACE)
        target_include_directories(miniaudio_hdr INTERFACE "${miniaudio_SOURCE_DIR}")
        target_link_libraries(${MEDIA_TARGET_PM_IMAGE} PRIVATE miniaudio_hdr)
        target_compile_definitions(${MEDIA_TARGET_PM_IMAGE} PRIVATE FEATURE_STT=1)
        target_sources(${MEDIA_TARGET_PM_IMAGE} PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/src/core/audio.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/core/audio_record_session.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/audio/stt/elevenlabs.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/audio/stt/proxy_stt.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/audio/tts/elevenlabs_tts.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/audio/tts/proxy_tts.cpp"
        )
        if(FEATURE_COMMAND_AUDIO)
            target_sources(${MEDIA_TARGET_PM_IMAGE} PRIVATE
                "${CMAKE_CURRENT_SOURCE_DIR}/src/cli/pm_image_cmd_audio.cpp"
            )
        endif()
        target_include_directories(${MEDIA_TARGET_PM_IMAGE} PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/src/audio"
        )
        # pm-media hosts path_tool_executor.cpp which calls speak → proxy_tts / elevenlabs_tts / AudioOutput.
        # The implementations are compiled into pm-image; pm-media only needs the define + headers
        # so its TUs see the FEATURE_STT guard and can include the headers at compile time.
        target_compile_definitions(pm-media PRIVATE FEATURE_STT=1)
        target_include_directories(pm-media PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/src/audio"
        )
    else()
        message(STATUS "FEATURE_STT: OFF — no mic capture / dictation")
    endif()

    # ── FEATURE_VIDEO — webcam / video capture via Media Foundation (Win32) ──────
    # Uses IMFSourceReader for camera enumeration and frame streaming.
    # Encoding uses WIC (Windows Imaging Component) — no extra dependencies needed.
    # Link libs: mf.lib mfplat.lib mfreadwrite.lib mfuuid.lib wincodec.lib
    option(FEATURE_VIDEO "Webcam capture via Media Foundation + WIC (Win32)" ON)
    if(FEATURE_VIDEO)
        message(STATUS "FEATURE_VIDEO: ON — Media Foundation webcam capture enabled")
        # Propagate to pm-media so path_tool_executor.cpp compiles the camera tool block.
        # The pm::video::* symbols are defined in video.cpp compiled into pm-image.exe;
        # the linker resolves them when pm-image.exe links pm-media.lib.
        target_compile_definitions(pm-media PRIVATE FEATURE_VIDEO=1)
        target_compile_definitions(${MEDIA_TARGET_PM_IMAGE} PRIVATE FEATURE_VIDEO=1)
        target_sources(${MEDIA_TARGET_PM_IMAGE} PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/src/core/video.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/core/video_record_session.cpp"
        )
        if(FEATURE_COMMAND_VIDEO)
            target_sources(${MEDIA_TARGET_PM_IMAGE} PRIVATE
                "${CMAKE_CURRENT_SOURCE_DIR}/src/cli/pm_image_cmd_video.cpp"
            )
        endif()
        target_link_libraries(${MEDIA_TARGET_PM_IMAGE} PRIVATE
            mf mfplat mfreadwrite mfuuid Windowscodecs
        )
    else()
        message(STATUS "FEATURE_VIDEO: OFF — no webcam capture")
    endif()

    # ── FEATURE_ENCRYPT_SESSIONS — propagate to pm-image if set ──────────────────
    if(FEATURE_ENCRYPT_SESSIONS)
        target_compile_definitions(${MEDIA_TARGET_PM_IMAGE} PRIVATE FEATURE_ENCRYPT_SESSIONS=1)
    endif()

    # ── FEATURE_USE_OWN_RIBBON — Win32++ CTab + toolbar strip (default; only supported UI host) ─
    # The stock UIRibbon path (Ribbon.bml from uicc) is no longer built; `RibbonUI.h`/`RibbonUI.rc`
    # stay as sources for command ids and ribbon BMP/string resources.
    option(FEATURE_USE_OWN_RIBBON "Use Win32++ CTab own ribbon strip (no UIRibbon framework)" ON)
    if(NOT FEATURE_USE_OWN_RIBBON)
        message(FATAL_ERROR
            "pm-image: FEATURE_USE_OWN_RIBBON=OFF is not supported: Ribbon.xml/uicc and Ribbon.bml "
            "were removed. Use the default -DFEATURE_USE_OWN_RIBBON=ON (Win32++ toolbar strip).")
    endif()
    message(STATUS "FEATURE_USE_OWN_RIBBON: ON — CTab ribbon strip (no UIRibbon.dll UI host)")
    target_compile_definitions(${MEDIA_TARGET_PM_IMAGE} PRIVATE FEATURE_USE_OWN_RIBBON=1)
    target_sources(${MEDIA_TARGET_PM_IMAGE} PRIVATE
        "${_UI_NEXT_DIR}/OwnRibbonLayout.cpp"
        "${_UI_NEXT_DIR}/OwnRibbonTab.cpp"
        "${_UI_NEXT_DIR}/OwnRibbonTab_Commands.cpp"
    )

    # ThorVG + Tabler `icons/filled` SVGs for the own-ribbon strip (ON by default when own ribbon is ON).
    include(CMakeDependentOption)
    cmake_dependent_option(FEATURE_SVG_BUTTONS
        "ThorVG-rasterized Tabler filled SVG toolbar icons (own ribbon)"
        ON
        "FEATURE_USE_OWN_RIBBON"
        OFF)
    if(FEATURE_SVG_BUTTONS)
        set(THORVG_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/packages/thorvg")
        include(cmake/thorvg_static.cmake)
        include(cmake/physfs_static.cmake)
        set(_TABLER_FILLED "${PM_DIST_VENDOR_DIR}/tabler-icons/icons/filled")
        if(NOT IS_DIRECTORY "${_TABLER_FILLED}")
            set(_TABLER_FILLED "${CMAKE_CURRENT_SOURCE_DIR}/packages/tabler-icons/icons/filled")
        endif()
        if(NOT IS_DIRECTORY "${_TABLER_FILLED}")
            message(FATAL_ERROR "FEATURE_SVG_BUTTONS: missing Tabler icons: ${_TABLER_FILLED}")
        endif()
        file(TO_CMAKE_PATH "${_TABLER_FILLED}" _TABL_NORM)
        string(REPLACE "\\" "/" PM_TABLER_FILLED_DIR_W "${_TABL_NORM}")
        configure_file(
            "${_UI_NEXT_DIR}/helpers/svg_paths.h.in"
            "${CMAKE_CURRENT_BINARY_DIR}/svg_paths.generated.h"
            @ONLY
        )
        target_include_directories(${MEDIA_TARGET_PM_IMAGE} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
        target_sources(${MEDIA_TARGET_PM_IMAGE} PRIVATE "${_UI_NEXT_DIR}/helpers/svg_raster.cpp")
        target_link_libraries(${MEDIA_TARGET_PM_IMAGE} PRIVATE thorvg_pm physfs-static)
        target_compile_definitions(${MEDIA_TARGET_PM_IMAGE} PRIVATE FEATURE_SVG_BUTTONS=1 TVG_STATIC=1)
        if(MSVC)
            target_compile_options(${MEDIA_TARGET_PM_IMAGE} PRIVATE /bigobj)
        endif()
        # Runtime: dist/shared/assets.pfs; loose SVGs live under dist/vendor/tabler-icons.
        set(_PFS "${PM_DIST_SHARED_DIR}/assets.pfs")
        add_custom_command(TARGET ${MEDIA_TARGET_PM_IMAGE} POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${PM_DIST_SHARED_DIR}"
            COMMAND "${CMAKE_COMMAND}"
                -D "PACK_OUT=${_PFS}"
                -D "PACK_SRC_DIR=${_TABLER_FILLED}"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/pack_tabler_assets_pfs.cmake"
            COMMENT "Pack Tabler filled icons into dist/shared/assets.pfs"
        )
        message(STATUS "FEATURE_SVG_BUTTONS: ON (Tabler filled @ ${PM_TABLER_FILLED_DIR_W})")
    else()
        message(STATUS "FEATURE_SVG_BUTTONS: OFF")
    endif()

    # ── FileTreePanel — IExplorerBrowser panel (shell32/ole32; no extra deps) ──
    target_sources(${MEDIA_TARGET_PM_IMAGE} PRIVATE "${_UI_NEXT_DIR}/FileTreePanel.cpp")

    # ── FEATURE_SPLASH — GDI+ startup window: src/res/splash-1.png + logo.png (see docs/splash.md) ──
    # splash_window.cpp always: CLI `--splash` / `--no-splash` session flag (default no splash) + chat workbench coordination;
    # the layered GDI+ window is compiled only when FEATURE_SPLASH defines the block in that TU.
    target_sources(${MEDIA_TARGET_PM_IMAGE} PRIVATE "${_UI_NEXT_DIR}/helpers/splash_window.cpp")
    option(FEATURE_SPLASH
        "Show startup splash (embedded PNGs from src/res/ until main UI is ready; GDI+)"
        ON)
    if(FEATURE_SPLASH)
        message(STATUS "FEATURE_SPLASH: ON — startup splash (splash-1.png + logo.png)")
        target_compile_definitions(${MEDIA_TARGET_PM_IMAGE} PRIVATE FEATURE_SPLASH=1)
    else()
        message(STATUS "FEATURE_SPLASH: OFF")
    endif()

    # ── FEATURE_CHAT_WEB — apps/chat-next → dist/shared/chat.html (webpack via npm only) ─
    # When ON:
    #   1. Web bundles are built only from package.json (`npm run buildf:web`, …). CMake does not run npm
    #      or check dist/shared/*.html; run web embeds explicitly before shipping or when UI assets change.
    #   2. No RT_RCDATA embed; pmui::load_chat_web_html() reads dist/shared/chat.html at runtime.
    #   3. FEATURE_CHAT_WEB=1 selects WebView2-hosted chat vs native CChatView.
    # Turn off: `-DFEATURE_CHAT_WEB=OFF`.
    option(FEATURE_CHAT_WEB "Build apps/chat-next (webpack embed -> dist/shared/chat.html) for the WebView2 chat host" ON)
    option(FEATURE_VIEWER_WEB "Build apps/viewer-next (webpack -> dist/shared/viewer.html) for the viewer WebView2 dock (requires chat Web for SDK)" ON)
    # ChatWebResource.cpp is ALWAYS compiled — the #ifdef inside picks between
    # the disk loader (FEATURE_CHAT_WEB) and a stub that returns "" / false.
    # Callers can do `if (pmui::chat_web_available()) { … }` regardless.
    target_sources(${MEDIA_TARGET_PM_IMAGE} PRIVATE
        "${_UI_NEXT_DIR}/ChatWebResource.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/win/viewers/ViewerWebResource.cpp"
    )
    if(FEATURE_CHAT_WEB)
        # ── Microsoft.Web.WebView2 SDK ──────────────────────────────────────
        # The NuGet package is a .zip. We grab the headers + the static loader
        # so pm-image can host a CoreWebView2Controller without a runtime DLL
        # next to the binary. The WebView2 *runtime* itself ships with modern
        # Windows (10 1803+ / 11) — no redistributable to ship.
        set(_WV2_VERSION "1.0.2592.51")
        set(_WV2_URL "https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2/${_WV2_VERSION}")
        FetchContent_Declare(
            webview2_sdk
            URL "${_WV2_URL}"
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        )
        FetchContent_GetProperties(webview2_sdk)
        if(NOT webview2_sdk_POPULATED)
            message(STATUS "FEATURE_CHAT_WEB: fetching WebView2 SDK ${_WV2_VERSION}")
            FetchContent_Populate(webview2_sdk)
        endif()
        set(_WV2_INCLUDE "${webview2_sdk_SOURCE_DIR}/build/native/include")
        # NuGet package layout: build/native/{x64,x86,arm64}/WebView2LoaderStatic.lib
        if(CMAKE_SIZEOF_VOID_P EQUAL 8)
            set(_WV2_ARCH x64)
        else()
            set(_WV2_ARCH x86)
        endif()
        set(_WV2_LIB "${webview2_sdk_SOURCE_DIR}/build/native/${_WV2_ARCH}/WebView2LoaderStatic.lib")
        if(NOT EXISTS "${_WV2_LIB}")
            message(FATAL_ERROR
                "FEATURE_CHAT_WEB: WebView2 static loader not found at ${_WV2_LIB}.\n"
                "Possible causes: NuGet layout changed, or the fetch failed.")
        endif()
        target_include_directories(${MEDIA_TARGET_PM_IMAGE} PRIVATE "${_WV2_INCLUDE}")
        target_link_libraries(${MEDIA_TARGET_PM_IMAGE} PRIVATE "${_WV2_LIB}" version)
        message(STATUS "FEATURE_CHAT_WEB: WebView2 SDK at ${webview2_sdk_SOURCE_DIR}")

        set(_CHAT_WEB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/apps/chat-next")

        target_sources(${MEDIA_TARGET_PM_IMAGE} PRIVATE
            "${_UI_NEXT_DIR}/ChatWebPanel.cpp"
            "${_UI_NEXT_DIR}/ChatImageFullscreenHost.cpp"
            "${_UI_NEXT_DIR}/helpers/win_ui_debug.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/win/web/webview_bootstrap.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/win/custom_commands_host.cpp"
        )
        target_compile_definitions(${MEDIA_TARGET_PM_IMAGE} PRIVATE FEATURE_CHAT_WEB=1)
        message(STATUS "FEATURE_CHAT_WEB: ON - apps/chat-next at ${_CHAT_WEB_DIR}; web bundles from npm (package.json)")

        if(FEATURE_VIEWER_WEB)
            set(_VIEWER_WEB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/apps/viewer-next")
            target_sources(${MEDIA_TARGET_PM_IMAGE} PRIVATE
                "${CMAKE_CURRENT_SOURCE_DIR}/src/win/viewers/text/ViewerWebPanel.cpp"
            )
            target_compile_definitions(${MEDIA_TARGET_PM_IMAGE} PRIVATE FEATURE_VIEWER_WEB=1)
            message(STATUS "FEATURE_VIEWER_WEB: ON - apps/viewer-next at ${_VIEWER_WEB_DIR}")
        else()
            message(STATUS "FEATURE_VIEWER_WEB: OFF")
        endif()
    else()
        message(STATUS "FEATURE_CHAT_WEB: OFF — native CChatView is the only chat panel")
        if(FEATURE_VIEWER_WEB)
            message(STATUS "FEATURE_VIEWER_WEB: ignored (requires FEATURE_CHAT_WEB for WebView2 SDK + npm)")
        endif()
    endif()

    # ── FEATURE_BROWSER — CWebView general-purpose WebView2 wrapper ────────
    # Provides CDockWebBrowser (View → Web Browser menu) and the CWebView / CWebViewOptions
    # API for future web-driven dialogs, modals, and floating panels.
    # Requires FEATURE_CHAT_WEB (WebView2 SDK already fetched above).
    option(FEATURE_BROWSER "Build src/win/web/CWebView.cpp (general WebView2 host + View→Web Browser)" OFF)
    if(FEATURE_BROWSER)
        if(FEATURE_CHAT_WEB)
            target_sources(${MEDIA_TARGET_PM_IMAGE} PRIVATE
                "${CMAKE_CURRENT_SOURCE_DIR}/src/win/web/CWebView.cpp"
                "${CMAKE_CURRENT_SOURCE_DIR}/src/win/web/CWebViewManager.cpp"
            )
            target_compile_definitions(${MEDIA_TARGET_PM_IMAGE} PRIVATE FEATURE_BROWSER=1)
            message(STATUS "FEATURE_BROWSER: ON — CWebView (src/win/web/CWebView.cpp)")
        else()
            message(STATUS "FEATURE_BROWSER: ignored (requires FEATURE_CHAT_WEB for WebView2 SDK)")
        endif()
    else()
        message(STATUS "FEATURE_BROWSER: OFF")
    endif()

    # ── FEATURE_HOME_PAGE — centre FileViewer home app (dist/shared/home.html) ─
    option(FEATURE_HOME_PAGE "Add View→Home and host dist/shared/home.html in the centre CFileViewer" OFF)
    if(FEATURE_HOME_PAGE)
        if(FEATURE_BROWSER)
            target_compile_definitions(${MEDIA_TARGET_PM_IMAGE} PRIVATE FEATURE_HOME_PAGE=1)
            message(STATUS "FEATURE_HOME_PAGE: ON - View->Home loads dist/shared/home.html")
        else()
            message(STATUS "FEATURE_HOME_PAGE: ignored (requires FEATURE_BROWSER for CWebView API)")
        endif()
    else()
        message(STATUS "FEATURE_HOME_PAGE: OFF")
    endif()

    # ── FEATURE_CONSOLE - dockable xterm.js WebView2 console panel ────────────
    # Hosts the shell web bundle (dist/shared/shell.html) in a CDockWebConsole
    # docked panel.  Requires FEATURE_BROWSER (CWebView API).
    # For dev: set PM_CONSOLE_URL=http://localhost:5173/ to use the Vite server.
    option(FEATURE_CONSOLE "Build src/win/web/CWebConsole.cpp (dockable xterm.js console panel)" ON)
    if(FEATURE_CONSOLE)
        if(FEATURE_BROWSER)
            target_sources(${MEDIA_TARGET_PM_IMAGE} PRIVATE
                "${CMAKE_CURRENT_SOURCE_DIR}/src/win/web/CWebConsole.cpp"
                "${CMAKE_CURRENT_SOURCE_DIR}/src/pty/PtySession.cpp"
            )
            target_compile_definitions(${MEDIA_TARGET_PM_IMAGE} PRIVATE FEATURE_CONSOLE=1)
            message(STATUS "FEATURE_CONSOLE: ON - CDockWebConsole (src/win/web/CWebConsole.cpp)")
        else()
            message(STATUS "FEATURE_CONSOLE: ignored (requires FEATURE_BROWSER for CWebView API)")
        endif()
    else()
        message(STATUS "FEATURE_CONSOLE: OFF")
    endif()

    # ── NodeHub graph editor (opt-in via -DFEATURE_NODES=ON) ────────────
    option(FEATURE_NODES "Embed the NodeHub graph editor (DX11 + ImGui)" OFF)
    if(FEATURE_NODES)
    message(STATUS "FEATURE_NODES: ON — NodeHub graph editor will be compiled")
    target_compile_definitions(${MEDIA_TARGET_PM_IMAGE} PRIVATE FEATURE_NODES=1)

    set(_NODEHUB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/packages/nodehub")
    set(_NODEHUB_IMGUI_DIR "${_NODEHUB_DIR}/external/imgui")
    set(_NODEHUB_BASE_DIR "${_NODEHUB_DIR}/applications/base/source")
    set(_NODEHUB_APP_DIR "${_NODEHUB_DIR}/applications/nodehub")

    # -- ImGui core ---
    target_sources(${MEDIA_TARGET_PM_IMAGE} PRIVATE
        "${_NODEHUB_IMGUI_DIR}/imgui.cpp"
        "${_NODEHUB_IMGUI_DIR}/imgui_draw.cpp"
        "${_NODEHUB_IMGUI_DIR}/imgui_tables.cpp"
        "${_NODEHUB_IMGUI_DIR}/imgui_widgets.cpp"
        "${_NODEHUB_IMGUI_DIR}/imgui_demo.cpp"
    )

    # -- ImGui backends + Application base (DX11, Win32, base.cpp) ---
    target_sources(${MEDIA_TARGET_PM_IMAGE} PRIVATE
        "${_NODEHUB_BASE_DIR}/imgui_impl_dx11.cpp"
        "${_NODEHUB_BASE_DIR}/imgui_impl_win32.cpp"
        "${_NODEHUB_BASE_DIR}/base.cpp"
        "${_NODEHUB_BASE_DIR}/platform_win32.cpp"
        "${_NODEHUB_BASE_DIR}/renderer_dx11.cpp"
    )

    # -- imgui-node-editor library ---
    target_sources(${MEDIA_TARGET_PM_IMAGE} PRIVATE
        "${_NODEHUB_DIR}/external/crude_json.cpp"
        "${_NODEHUB_DIR}/external/imgui_node/imgui_canvas.cpp"
        "${_NODEHUB_DIR}/external/imgui_node/imgui_node_editor.cpp"
        "${_NODEHUB_DIR}/external/imgui_node/imgui_node_editor_api.cpp"
        "${_NODEHUB_DIR}/external/imgui_node/imgui_node_editor_animation.cpp"
        "${_NODEHUB_DIR}/external/imgui_node/imgui_node_editor_store.cpp"
        "${_NODEHUB_DIR}/external/imgui_node/EditorContext.cpp"
        "${_NODEHUB_DIR}/external/imgui_node/links-guided.cpp"
    )

    # -- NodeHub App sources (everything except main.cpp / entry_point.cpp) ---
    target_sources(${MEDIA_TARGET_PM_IMAGE} PRIVATE
        "${_NODEHUB_APP_DIR}/app.cpp"
        "${_NODEHUB_APP_DIR}/app-logic.cpp"
        "${_NODEHUB_APP_DIR}/app-render.cpp"
        "${_NODEHUB_APP_DIR}/app-runtime.cpp"
        "${_NODEHUB_APP_DIR}/app-screenshot.cpp"
        "${_NODEHUB_APP_DIR}/Logging.cpp"
        "${_NODEHUB_APP_DIR}/stats.cpp"
        "${_NODEHUB_APP_DIR}/blocks/block.cpp"
        "${_NODEHUB_APP_DIR}/blocks/block_edit_dialog.cpp"
        "${_NODEHUB_APP_DIR}/blocks/group_block.cpp"
        "${_NODEHUB_APP_DIR}/blocks/logic_blocks.cpp"
        "${_NODEHUB_APP_DIR}/blocks/log_block.cpp"
        "${_NODEHUB_APP_DIR}/blocks/math_blocks.cpp"
        "${_NODEHUB_APP_DIR}/blocks/NodeEx.cpp"
        "${_NODEHUB_APP_DIR}/blocks/parameter_edit_dialog.cpp"
        "${_NODEHUB_APP_DIR}/blocks/parameter_node.cpp"
        "${_NODEHUB_APP_DIR}/blocks/parameter_operation.cpp"
        "${_NODEHUB_APP_DIR}/blocks/start_block.cpp"
        "${_NODEHUB_APP_DIR}/containers/container.cpp"
        "${_NODEHUB_APP_DIR}/containers/root_container.cpp"
        "${_NODEHUB_APP_DIR}/core/BaseManager.cpp"
        "${_NODEHUB_APP_DIR}/core/Context.cpp"
        "${_NODEHUB_APP_DIR}/core/graph_state.cpp"
        "${_NODEHUB_APP_DIR}/core/Object.cpp"
        "${_NODEHUB_APP_DIR}/core/Parameter.cpp"
        "${_NODEHUB_APP_DIR}/core/ParameterIn.cpp"
        "${_NODEHUB_APP_DIR}/core/ParameterManager.cpp"
        "${_NODEHUB_APP_DIR}/core/ParameterOut.cpp"
        "${_NODEHUB_APP_DIR}/utilities/edge_editing.cpp"
        "${_NODEHUB_APP_DIR}/utilities/node_renderer_base.cpp"
        "${_NODEHUB_APP_DIR}/utilities/pathfinding.cpp"
        "${_NODEHUB_APP_DIR}/utilities/pin_renderer.cpp"
        "${_NODEHUB_APP_DIR}/utilities/style_manager.cpp"
        "${_NODEHUB_APP_DIR}/utilities/uuid_generator.cpp"
        "${_NODEHUB_APP_DIR}/utilities/uuid_id_manager.cpp"
    )

    # -- Include directories ---
    target_include_directories(${MEDIA_TARGET_PM_IMAGE} PRIVATE
        "${_NODEHUB_IMGUI_DIR}"
        "${_NODEHUB_BASE_DIR}"
        "${_NODEHUB_DIR}/applications/base/include"
        "${_NODEHUB_DIR}/external/stb_image"
        "${_NODEHUB_DIR}/external/stb_image_latest"
        "${_NODEHUB_DIR}/external"
        "${_NODEHUB_DIR}/external/imgui_node"
        "${_NODEHUB_DIR}/external/spdlog/include"
        "${_NODEHUB_APP_DIR}"
        "${_NODEHUB_APP_DIR}/core"
        "${_NODEHUB_APP_DIR}/blocks"
    )

    # -- Compile definitions for spdlog + fmt header-only ---
    target_compile_definitions(${MEDIA_TARGET_PM_IMAGE} PRIVATE
        FMT_HEADER_ONLY=1
        SPDLOG_HEADER_ONLY
    )

    target_link_libraries(${MEDIA_TARGET_PM_IMAGE} PRIVATE d3d11.lib d3dcompiler.lib)

    # -- Copy NodeHub data (fonts, textures) to dist/data/ ---
    add_custom_command(TARGET ${MEDIA_TARGET_PM_IMAGE} POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_directory
                "${_NODEHUB_APP_DIR}/data"
                "${CMAKE_SOURCE_DIR}/dist/data"
        COMMENT "Copy NodeHub data (fonts, textures) to dist/data/"
    )
    else()
        message(STATUS "FEATURE_NODES: OFF — NodeHub graph editor disabled")
    endif() # FEATURE_NODES

  # IExecuteCommand + IObjectWithSelection (resize 1980/1200; shell passes full selection).
  add_library(${MEDIA_TARGET_IEXECUTE} SHARED
      "${CMAKE_CURRENT_SOURCE_DIR}/src/win/shell/pm_image_iexecute.cpp"
      "${CMAKE_CURRENT_SOURCE_DIR}/src/win/shell/pm_iexecute_map.cpp"
      "${CMAKE_CURRENT_SOURCE_DIR}/src/win/shell/pm_image_iexecute.def"
  )
  set_target_properties(${MEDIA_TARGET_IEXECUTE} PROPERTIES
      RUNTIME_OUTPUT_DIRECTORY "${PM_DIST_DIR}"
      RUNTIME_OUTPUT_DIRECTORY_DEBUG "${PM_DIST_DIR}"
      RUNTIME_OUTPUT_DIRECTORY_RELEASE "${PM_DIST_DIR}"
      RUNTIME_OUTPUT_NAME "${PM_IEXECUTE_BASENAME}"
  )
  target_include_directories(${MEDIA_TARGET_IEXECUTE} PRIVATE
      "${CMAKE_CURRENT_SOURCE_DIR}/src"
      "${CMAKE_CURRENT_SOURCE_DIR}/src/win/ui_next"
      "${CMAKE_CURRENT_BINARY_DIR}/include"
  )
  target_compile_definitions(${MEDIA_TARGET_IEXECUTE} PRIVATE
      _WIN32_WINNT=0x0A00
      NOMINMAX
      UNICODE
      _UNICODE
  )
  target_link_libraries(${MEDIA_TARGET_IEXECUTE} PRIVATE ole32 shell32 shlwapi propsys)
  # In-proc shell extension: /MT so STL uses our static CRT. Hosts (e.g. Altap Salamander) may ship an older
  # MSVCP140; using /MD here can bind to that and crash in MSVCP140 (null deref in std::string, etc.).
  if(MSVC)
    set_target_properties(${MEDIA_TARGET_IEXECUTE} PROPERTIES
      MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    )
  endif()
  add_dependencies(${MEDIA_TARGET_PM_IMAGE} ${MEDIA_TARGET_IEXECUTE})
  # Win11: IExplorerCommand for packaged / sparse context menu (docs/win11.md; does not replace register-explorer IExecute)
  option(PM_BUILD_EXPLORER11_DLL "Build IExplorerCommand DLL and sparse AppxManifest under the Windows dist folder" ON)
  if(PM_BUILD_EXPLORER11_DLL)
    set(PM_EXPLORER11_CLSID "{A1B2C3D4-E5F6-4A5B-8C9D-0E1F2A3B4C5D}")
    set(PM_EXPLORER11_VIEWER_CLSID "{B2B3C4D5-E6F7-4A6B-9D8E-1F2A3B4C5D6E}")
    string(REGEX REPLACE "^\\{|\\}$" "" PM_EXPLORER11_CLSID_RAW "${PM_EXPLORER11_CLSID}")
    string(REGEX REPLACE "^\\{|\\}$" "" PM_EXPLORER11_VIEWER_CLSID_RAW "${PM_EXPLORER11_VIEWER_CLSID}")
    set(PM_SPARSE_IDENTITY_NAME "PolyMech.PMImage.Explorer11")
    set(PM_SPARSE_PUBLISHER "CN=PolyMech PM-Image Sparse")
    set(PM_EXPLORER11_DLL_FILE "${PM_EXPLORER11_BASENAME}.dll")
    set(PM_EXPLORER11_STUB_DLL_FILE "${PM_EXPLORER11_STUB_BASENAME}.dll")
    set(PM_EXPLORER11_VERB_ID "PixlWiz")
    set(PM_EXPLORER11_VERB_TITLE "PixlWiz")
    set(PM_EXPLORER11_VIEWER_VERB_TITLE "Open in PM Viewer")
    set(_PM_APPX_MANIFEST_TEMPLATE "${PM_DIST_ROOT_DIR}/branding/AppxManifest.xml.in")
    if(NOT EXISTS "${_PM_APPX_MANIFEST_TEMPLATE}" AND EXISTS "${CMAKE_SOURCE_DIR}/branding/AppxManifest.xml.in")
      set(_PM_APPX_MANIFEST_TEMPLATE "${CMAKE_SOURCE_DIR}/branding/AppxManifest.xml.in")
    endif()
    file(MAKE_DIRECTORY "${PM_DIST_DIR}/sparse")
    configure_file(
      "${_PM_APPX_MANIFEST_TEMPLATE}"
      "${PM_DIST_DIR}/sparse/AppxManifest.xml"
      @ONLY
    )
    add_library(${MEDIA_TARGET_EXPLORER11} SHARED
        "${CMAKE_CURRENT_SOURCE_DIR}/src/win/explorer11/pm_image_explorer11.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/win/explorer11/pm_image_explorer11.def"
    )
    set_target_properties(${MEDIA_TARGET_EXPLORER11} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${PM_DIST_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${PM_DIST_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${PM_DIST_DIR}"
        RUNTIME_OUTPUT_NAME "${PM_EXPLORER11_BASENAME}"
    )
    target_include_directories(${MEDIA_TARGET_EXPLORER11} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_BINARY_DIR}/include"
    )
    target_compile_definitions(${MEDIA_TARGET_EXPLORER11} PRIVATE
        _WIN32_WINNT=0x0A00
        NOMINMAX
        UNICODE
        _UNICODE
    )
    target_link_libraries(${MEDIA_TARGET_EXPLORER11} PRIVATE ole32 shell32 shlwapi)
    if(MSVC)
      set_target_properties(${MEDIA_TARGET_EXPLORER11} PROPERTIES
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
      )
    endif()
    add_library(${MEDIA_TARGET_EXPLORER11_STUB} SHARED
        "${CMAKE_CURRENT_SOURCE_DIR}/src/win/explorer11/pm_image_explorer11_stub.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/win/explorer11/pm_image_explorer11_stub.def"
    )
    set_target_properties(${MEDIA_TARGET_EXPLORER11_STUB} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${PM_DIST_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${PM_DIST_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${PM_DIST_DIR}"
        RUNTIME_OUTPUT_NAME "${PM_EXPLORER11_STUB_BASENAME}"
    )
    target_include_directories(${MEDIA_TARGET_EXPLORER11_STUB} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_BINARY_DIR}/include"
    )
    target_compile_definitions(${MEDIA_TARGET_EXPLORER11_STUB} PRIVATE
        _WIN32_WINNT=0x0A00
        NOMINMAX
        UNICODE
        _UNICODE
    )
    target_link_libraries(${MEDIA_TARGET_EXPLORER11_STUB} PRIVATE shell32 shlwapi)
    if(MSVC)
      set_target_properties(${MEDIA_TARGET_EXPLORER11_STUB} PROPERTIES
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
      )
    endif()
    set(_PM_SPARSE_ASSET_SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/generate_sparse_assets.ps1")
    file(WRITE "${_PM_SPARSE_ASSET_SCRIPT}" [=[
param([Parameter(Mandatory=$true)][string]$OutDir)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
function New-SparseLogo([string]$Path, [int]$Size) {
  $bmp = New-Object System.Drawing.Bitmap $Size, $Size
  $gfx = [System.Drawing.Graphics]::FromImage($bmp)
  try {
    $gfx.Clear([System.Drawing.Color]::FromArgb(0, 64, 92, 124))
    $brush = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255, 41, 128, 185))
    try { $gfx.FillEllipse($brush, 2, 2, $Size - 4, $Size - 4) } finally { $brush.Dispose() }
    $fontSize = [Math]::Max(8, [Math]::Floor($Size * 0.42))
    $font = New-Object System.Drawing.Font 'Segoe UI', $fontSize, ([System.Drawing.FontStyle]::Bold), ([System.Drawing.GraphicsUnit]::Pixel)
    $textBrush = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::White)
    $format = New-Object System.Drawing.StringFormat
    try {
      $format.Alignment = [System.Drawing.StringAlignment]::Center
      $format.LineAlignment = [System.Drawing.StringAlignment]::Center
      $gfx.DrawString('P', $font, $textBrush, (New-Object System.Drawing.RectangleF 0, 0, $Size, $Size), $format)
    } finally {
      $format.Dispose()
      $textBrush.Dispose()
      $font.Dispose()
    }
    $bmp.Save($Path, [System.Drawing.Imaging.ImageFormat]::Png)
  } finally {
    $gfx.Dispose()
    $bmp.Dispose()
  }
}
New-SparseLogo (Join-Path $OutDir 'StoreLogo.png') 50
New-SparseLogo (Join-Path $OutDir 'Square44x44Logo.png') 44
New-SparseLogo (Join-Path $OutDir 'Square150x150Logo.png') 150
]=])
    add_dependencies(${MEDIA_TARGET_PM_IMAGE} ${MEDIA_TARGET_EXPLORER11} ${MEDIA_TARGET_EXPLORER11_STUB})
    add_custom_command(
      TARGET ${MEDIA_TARGET_PM_IMAGE} POST_BUILD
      COMMAND "${CMAKE_COMMAND}" -E make_directory "${PM_DIST_DIR}/sparse"
      COMMAND powershell -NoProfile -ExecutionPolicy Bypass -File "${_PM_SPARSE_ASSET_SCRIPT}" "${PM_DIST_DIR}/Assets"
      COMMAND "${CMAKE_COMMAND}" -E rm -f
              "${PM_DIST_DIR}/sparse/pm-image.exe"
              "${PM_DIST_DIR}/sparse/${PM_VIEWER_EXE_STEM}.exe"
              "${PM_DIST_DIR}/sparse/${PM_EXPLORER11_BASENAME}.dll"
      COMMAND "${CMAKE_COMMAND}" -E remove_directory "${PM_DIST_DIR}/sparse/Assets"
      COMMENT "Update Windows dist sparse package (AppxManifest from configure; run register-sparse-explorer11.ps1 to register)"
    )
  endif()
  # Distinct `Applications\\*.exe` row for “Open with” (shell dedupes same basename as PM-Image).
  add_custom_command(
    TARGET ${MEDIA_TARGET_PM_IMAGE} POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:${MEDIA_TARGET_PM_IMAGE}>"
            "$<TARGET_FILE_DIR:${MEDIA_TARGET_PM_IMAGE}>/${PM_VIEWER_EXE_STEM}.exe"
    COMMENT "Copy ${PM_APP_ID_SLUG}.exe → ${PM_VIEWER_EXE_STEM}.exe (Open with lists PM Viewer)"
  )
  # GUI subsystem (wWinMain in pm_image_entry.cpp) — no default conhost; use `--console` for AllocConsole (see pm_image_run.cpp)
  set_target_properties(${MEDIA_TARGET_PM_IMAGE} PROPERTIES WIN32_EXECUTABLE TRUE)
endif() # WIN32

# ── Non-Win32: FEATURE_STT + FEATURE_VIDEO ────────────────────────────────────
# Mirror of the WIN32 blocks above for Linux and macOS.
if(NOT WIN32)

    # ── FEATURE_STT — mic capture via miniaudio (cross-platform) ─────────────
    # miniaudio uses ALSA/PulseAudio/PipeWire on Linux (via dlopen; no extra link),
    # and CoreAudio on macOS.  The same audio.cpp compiles on all platforms.
    #
    # On macOS the audio implementation is extracted into a separate pm-audio static
    # library so it can be staged for Xcode (pm-codeedit / PixelWiz) which links
    # pm-media.a — whose path_tool_executor.cpp references pm::audio::AudioOutput.
    option(FEATURE_STT "Mic capture + realtime STT via miniaudio" ON)
    if(FEATURE_STT)
        message(STATUS "FEATURE_STT: ON — miniaudio mic capture enabled")
        FetchContent_Declare(
            miniaudio
            GIT_REPOSITORY https://github.com/mackron/miniaudio.git
            GIT_TAG        0.11.21
            GIT_SHALLOW    TRUE
        )
        FetchContent_GetProperties(miniaudio)
        if(NOT miniaudio_POPULATED)
            FetchContent_Populate(miniaudio)
        endif()
        if(NOT TARGET miniaudio_hdr)
            add_library(miniaudio_hdr INTERFACE)
            target_include_directories(miniaudio_hdr INTERFACE "${miniaudio_SOURCE_DIR}")
        endif()

        if(APPLE)
            # macOS: build a separate pm-audio static lib so the symbols are available
            # both to pm-image (CLI) and to PixelWiz / pm-codeedit (Xcode build).
            add_library(pm-audio STATIC
                "${CMAKE_CURRENT_SOURCE_DIR}/src/core/audio.cpp"
                "${CMAKE_CURRENT_SOURCE_DIR}/src/audio/stt/elevenlabs.cpp"
                "${CMAKE_CURRENT_SOURCE_DIR}/src/audio/stt/proxy_stt.cpp"
                "${CMAKE_CURRENT_SOURCE_DIR}/src/audio/tts/elevenlabs_tts.cpp"
                "${CMAKE_CURRENT_SOURCE_DIR}/src/audio/tts/proxy_tts.cpp"
            )
            target_compile_definitions(pm-audio PRIVATE FEATURE_STT=1)
            target_include_directories(pm-audio PRIVATE
                "${CMAKE_CURRENT_SOURCE_DIR}/src/audio"
            )
            target_link_libraries(pm-audio PRIVATE
                miniaudio_hdr
                nlohmann_json::nlohmann_json
                "-framework CoreAudio"
                "-framework AudioUnit"
                "-framework AudioToolbox"
            )
            # pm-image links pm-audio (audio symbols) + the CLI audio command.
            target_link_libraries(${MEDIA_TARGET_PM_IMAGE} PRIVATE pm-audio)
            target_compile_definitions(${MEDIA_TARGET_PM_IMAGE} PRIVATE FEATURE_STT=1)
            target_sources(${MEDIA_TARGET_PM_IMAGE} PRIVATE
                "${CMAKE_CURRENT_SOURCE_DIR}/src/audio/stt/proxy_stt.cpp"
                "${CMAKE_CURRENT_SOURCE_DIR}/src/audio/tts/proxy_tts.cpp"
            )
            if(FEATURE_COMMAND_AUDIO)
                target_sources(${MEDIA_TARGET_PM_IMAGE} PRIVATE
                    "${CMAKE_CURRENT_SOURCE_DIR}/src/cli/pm_image_cmd_audio.cpp"
                )
            endif()
            target_include_directories(${MEDIA_TARGET_PM_IMAGE} PRIVATE
                "${CMAKE_CURRENT_SOURCE_DIR}/src/audio"
            )
        else()
            # Linux: compile audio sources directly into pm-image (no separate Xcode target needed).
            target_link_libraries(${MEDIA_TARGET_PM_IMAGE} PRIVATE miniaudio_hdr)
            target_compile_definitions(${MEDIA_TARGET_PM_IMAGE} PRIVATE FEATURE_STT=1)
            target_sources(${MEDIA_TARGET_PM_IMAGE} PRIVATE
                "${CMAKE_CURRENT_SOURCE_DIR}/src/core/audio.cpp"
                "${CMAKE_CURRENT_SOURCE_DIR}/src/audio/stt/elevenlabs.cpp"
                "${CMAKE_CURRENT_SOURCE_DIR}/src/audio/stt/proxy_stt.cpp"
                "${CMAKE_CURRENT_SOURCE_DIR}/src/audio/tts/elevenlabs_tts.cpp"
                "${CMAKE_CURRENT_SOURCE_DIR}/src/audio/tts/proxy_tts.cpp"
            )
            if(FEATURE_COMMAND_AUDIO)
                target_sources(${MEDIA_TARGET_PM_IMAGE} PRIVATE
                    "${CMAKE_CURRENT_SOURCE_DIR}/src/cli/pm_image_cmd_audio.cpp"
                )
            endif()
            target_include_directories(${MEDIA_TARGET_PM_IMAGE} PRIVATE
                "${CMAKE_CURRENT_SOURCE_DIR}/src/audio"
            )
            # Linux: miniaudio uses dlopen() for ALSA/PulseAudio/PipeWire.
            target_link_libraries(${MEDIA_TARGET_PM_IMAGE} PRIVATE dl m)
        endif()

        # pm-media: expose FEATURE_STT guard + audio headers so path_tool_executor
        # and other shared TUs compile the speak/audio blocks correctly.
        target_compile_definitions(pm-media PRIVATE FEATURE_STT=1)
        target_include_directories(pm-media PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/src/audio"
        )
    else()
        message(STATUS "FEATURE_STT: OFF — no mic capture / dictation")
    endif()

    # ── FEATURE_VIDEO — webcam capture ───────────────────────────────────────

    if(UNIX AND NOT APPLE)
        # Linux: V4L2 mmap streaming + libjpeg (src/core/video_linux.cpp).
        option(FEATURE_VIDEO "Webcam capture via V4L2 (Linux)" ON)
        if(FEATURE_VIDEO)
            message(STATUS "FEATURE_VIDEO: ON — V4L2 webcam capture enabled (Linux)")
            find_package(JPEG REQUIRED)
            target_compile_definitions(pm-media PRIVATE FEATURE_VIDEO=1)
            target_compile_definitions(${MEDIA_TARGET_PM_IMAGE} PRIVATE FEATURE_VIDEO=1)
            target_sources(${MEDIA_TARGET_PM_IMAGE} PRIVATE
                "${CMAKE_CURRENT_SOURCE_DIR}/src/core/video_linux.cpp"
            )
            if(FEATURE_COMMAND_VIDEO)
                target_sources(${MEDIA_TARGET_PM_IMAGE} PRIVATE
                    "${CMAKE_CURRENT_SOURCE_DIR}/src/cli/pm_image_cmd_video.cpp"
                )
            endif()
            target_link_libraries(${MEDIA_TARGET_PM_IMAGE} PRIVATE JPEG::JPEG)
        else()
            message(STATUS "FEATURE_VIDEO: OFF — no webcam capture")
        endif()
    endif()

    if(APPLE)
        # macOS: AVFoundation via a separate pm-video static lib so the symbols are
        # available both to pm-image (CLI) and to PixelWiz / pm-codeedit (Xcode build),
        # which links pm-media.a whose path_tool_executor.cpp calls pm::video::*.
        option(FEATURE_VIDEO "Webcam capture via AVFoundation (macOS)" OFF)
        if(FEATURE_VIDEO)
            message(STATUS "FEATURE_VIDEO: ON — AVFoundation webcam capture (macOS)")
            add_library(pm-video STATIC
                "${CMAKE_CURRENT_SOURCE_DIR}/src/core/video_osx.mm"
            )
            target_compile_definitions(pm-video PRIVATE FEATURE_VIDEO=1)
            target_include_directories(pm-video PRIVATE
                "${CMAKE_CURRENT_SOURCE_DIR}/src/core"
                "${CMAKE_CURRENT_SOURCE_DIR}/src"
            )
            target_link_libraries(pm-video PRIVATE
                "-framework AVFoundation"
                "-framework CoreMedia"
                "-framework CoreVideo"
                "-framework CoreGraphics"
                "-framework ImageIO"
                "-framework Foundation"
            )
            # pm-image: link pm-video + register the CLI video command.
            target_link_libraries(${MEDIA_TARGET_PM_IMAGE} PRIVATE pm-video)
            target_compile_definitions(${MEDIA_TARGET_PM_IMAGE} PRIVATE FEATURE_VIDEO=1)
            if(FEATURE_COMMAND_VIDEO)
                target_sources(${MEDIA_TARGET_PM_IMAGE} PRIVATE
                    "${CMAKE_CURRENT_SOURCE_DIR}/src/cli/pm_image_cmd_video.cpp"
                )
            endif()
            # pm-media: enable the FEATURE_VIDEO guard so path_tool_executor.cpp
            # compiles the camera tool block (symbols resolved via pm-video at link time).
            target_compile_definitions(pm-media PRIVATE FEATURE_VIDEO=1)
        else()
            message(STATUS "FEATURE_VIDEO: OFF — no webcam capture (macOS)")
        endif()
    endif()

endif() # NOT WIN32

# Win32++ — header-only (vendor tree under packages/Win32xx). See samples e.g.
# packages/Win32xx/samples/MovieShow/ProjectFiles/*_2026.vcxproj: AdditionalIncludeDirectories=..\..\..\include
if(WIN32)
  set(MEDIA_WIN32XX_DIR "${CMAKE_CURRENT_SOURCE_DIR}/packages/Win32xx" CACHE PATH "Win32++ root (expects include/wxx_*.h)")
  if(EXISTS "${MEDIA_WIN32XX_DIR}/include/wxx_wincore.h")
    add_library(win32xx INTERFACE)
    target_include_directories(win32xx INTERFACE "${MEDIA_WIN32XX_DIR}/include")
    target_compile_definitions(win32xx INTERFACE UNICODE _UNICODE)
    # Same optional deps as MovieShow (beyond what pragma in our .cpp already pulls).
    target_link_libraries(win32xx INTERFACE shlwapi Propsys gdiplus Crypt32 uxtheme)
    add_library(media::win32xx ALIAS win32xx)
    target_link_libraries(${MEDIA_TARGET_PM_IMAGE} PRIVATE win32xx)
  endif()
endif()

target_include_directories(${MEDIA_TARGET_PM_IMAGE} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/cli
    ${CMAKE_CURRENT_BINARY_DIR}/include
    ${asio_SOURCE_DIR}/asio/include
    ${picosha2_SOURCE_DIR}
)

target_compile_definitions(${MEDIA_TARGET_PM_IMAGE} PRIVATE
    ASIO_STANDALONE
    ASIO_NO_DEPRECATED
    CPPHTTPLIB_NO_EXCEPTIONS=0
)

if(WIN32)
    target_compile_definitions(${MEDIA_TARGET_PM_IMAGE} PRIVATE
        _WIN32_WINNT=0x0A00
        NOMINMAX
    )
endif()

# ── html package: parsing + Markdown rendering (md4c) ──────────────────────
# Sub-project at packages/html/. We don't need its FetchContent test target
# here, so HTML_BUILD_TESTS stays OFF (run them standalone with that flag).
if(NOT TARGET html)
    add_subdirectory(packages/html)
endif()

target_link_libraries(${MEDIA_TARGET_PM_IMAGE} PRIVATE
    pm-media
    logger
    CLI11::CLI11
    nlohmann_json::nlohmann_json
    httplib::httplib
    laserpants::dotenv
    pranav_glob
    CURL::libcurl
    Vips::vips
    html
    kbot
    tomlplusplus_hdr
)
if(WIN32)
  target_link_libraries(${MEDIA_TARGET_PM_IMAGE} PRIVATE sodium ole32 Advapi32 User32 Shell32)
endif()

if(UNIX AND NOT APPLE)
    target_link_libraries(${MEDIA_TARGET_PM_IMAGE} PRIVATE pthread)
endif()

target_compile_definitions(${MEDIA_TARGET_PM_IMAGE} PRIVATE
    "MEDIA_IMG_VERSION=\"${PROJECT_VERSION}\""
    "PM_HAS_BRANDING_ICON=${PM_HAS_BRANDING_ICON}"
    "PM_HAS_BRANDING_SPLASH=${PM_HAS_BRANDING_SPLASH}"
)

if(UNIX AND NOT APPLE)
  install(TARGETS ${MEDIA_TARGET_PM_IMAGE}
      RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
  )
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/README.md")
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/README.md"
        DESTINATION "${CMAKE_INSTALL_DOCDIR}"
    )
  endif()
endif()

if(WIN32)
  set(MEDIA_TARGET_PM_IMAGE_CLI "${PM_APP_ID_SLUG}-cli")
  add_executable(${MEDIA_TARGET_PM_IMAGE_CLI}
      src/cli/pm_image_cli_helpers.cpp
      src/cli/cli_tty.cpp
      src/cli/pm_image_register_cli.cpp
      src/cli/pm_image_settings.cpp
      src/cli/pm_image_dispatch.cpp
      src/cli/pm_image_dispatch_remaining.cpp
      src/cli/pm_image_cmd_resize.cpp
      src/cli/pm_image_cmd_find.cpp
      src/cli/pm_image_cmd_search.cpp
      src/cli/pm_image_cmd_dup.cpp
      src/cli/pm_image_cmd_meta.cpp
      src/cli/pm_image_cmd_transform.cpp
      src/cli/pm_image_cmd_create.cpp
      src/cli/pm_image_cmd_compress.cpp
      src/cli/pm_image_cmd_provider_models.cpp
      src/cli/pm_image_cmd_daemon.cpp
      src/cli/pm_image_cmd_llm_list.cpp
      src/cli/pm_image_cmd_llm_call.cpp
      src/cli/pm_image_cmd_llm_agent.cpp
      src/cli/pm_image_cmd_llm_info.cpp
      src/cli/pm_image_cmd_info.cpp
      src/cli/pm_image_cmd_register_explorer.cpp
      src/cli/pm_image_cmd_register_startmenu.cpp
      src/cli/pm_image_cmd_installer.cpp
      src/cli/pm_image_cmd_app.cpp
      src/cli/pm_image_cmd_batch.cpp
      src/cli/pm_image_cmd_service.cpp
      src/lib/pm_zitadel_oauth.cpp
      src/lib/pm_service_upload.cpp
      src/lib/pm_service_posts.cpp
      src/cli/pm_image_cmd_print_help.cpp
      src/cli/pm_image_run.cpp
      src/cli/pm_image_entry.cpp
      src/cmd_kbot.cpp
      src/win/app_commands.cpp
      src/win/session_replay/session_replay.cpp
      src/win/register_explorer.cpp
      src/win/register_startmenu.cpp
      src/win/shell/pm_iexecute_reg.cpp
      src/win/ui_singleton.cpp
      src/win/machine_fingerprint.cpp
      src/win/settings_store.cpp
      src/win/LayoutStore.cpp
  )
  if(PM_HAS_BRANDING_ICON)
      target_sources(${MEDIA_TARGET_PM_IMAGE_CLI} PRIVATE "${PM_APP_ICON_RC}")
  endif()
  target_include_directories(${MEDIA_TARGET_PM_IMAGE_CLI} PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/src
      ${CMAKE_CURRENT_SOURCE_DIR}/src/cli
      ${CMAKE_CURRENT_BINARY_DIR}/include
      ${asio_SOURCE_DIR}/asio/include
      ${picosha2_SOURCE_DIR}
  )
  target_compile_definitions(${MEDIA_TARGET_PM_IMAGE_CLI} PRIVATE
      PM_IMAGE_CLI_ONLY=1
      PM_IMAGE_CONSOLE_MAIN=1
      "FEATURE_REGISTER_EXPLORER=$<BOOL:${FEATURE_REGISTER_EXPLORER}>"
      "FEATURE_EXPLORER_INTEGRATION=$<BOOL:${FEATURE_EXPLORER_INTEGRATION}>"
      "FEATURE_PIXLWIZ_AUTH=$<BOOL:${FEATURE_PIXLWIZ_AUTH}>"
      "FEATURE_PIXLWIZ_SHARE=$<BOOL:${FEATURE_PIXLWIZ_SHARE}>"
      "FEATURE_TRIAL_CHECK=$<BOOL:${FEATURE_TRIAL_CHECK}>"
      "FEATURE_LICENSE_FILE=$<BOOL:${FEATURE_LICENSE_FILE}>"
      "FEATURE_SESSION_VIDEO_RECORDER=$<BOOL:${FEATURE_SESSION_VIDEO_RECORDER}>"
      "FEATURE_XBLOX=$<BOOL:${FEATURE_XBLOX}>"
      "FEATURE_MODBUS=$<BOOL:${FEATURE_MODBUS}>"
      "FEATURE_MODBUS_XBLOX=$<BOOL:${FEATURE_MODBUS_XBLOX}>"
      "FEATURE_DAEMON=$<BOOL:${FEATURE_DAEMON}>"
      "FEATURE_ASSISTANT=$<BOOL:${FEATURE_ASSISTANT}>"
      "FEATURE_MCP_SERVER=$<BOOL:${FEATURE_MCP_SERVER}>"
      "FEATURE_CUSTOM_COMMANDS=$<BOOL:${FEATURE_CUSTOM_COMMANDS}>"
      ${_PM_COMMAND_DEFS}
      ASIO_STANDALONE
      ASIO_NO_DEPRECATED
      CPPHTTPLIB_NO_EXCEPTIONS=0
      _WIN32_WINNT=0x0A00
      NOMINMAX
      "MEDIA_IMG_VERSION=\"${PROJECT_VERSION}\""
  )
  if(FEATURE_PIXLWIZ_AUTH)
      target_sources(${MEDIA_TARGET_PM_IMAGE_CLI} PRIVATE
          "${CMAKE_CURRENT_SOURCE_DIR}/src/cli/pm_image_cmd_login.cpp"
          "${CMAKE_CURRENT_SOURCE_DIR}/src/cli/pm_image_cmd_logout.cpp")
  endif()
  if(FEATURE_COMMAND_STATUS)
      target_sources(${MEDIA_TARGET_PM_IMAGE_CLI} PRIVATE
          "${CMAKE_CURRENT_SOURCE_DIR}/src/cli/pm_image_cmd_status.cpp")
  endif()
  if(FEATURE_TRIAL_CHECK)
      target_sources(${MEDIA_TARGET_PM_IMAGE_CLI} PRIVATE
          "${CMAKE_CURRENT_SOURCE_DIR}/src/win/trial_protection.cpp")
  endif()
  if(FEATURE_LICENSE_FILE)
      target_sources(${MEDIA_TARGET_PM_IMAGE_CLI} PRIVATE
          "${CMAKE_CURRENT_SOURCE_DIR}/src/win/license_file.cpp")
  endif()
  if(FEATURE_COMMAND_XBLOX)
      target_sources(${MEDIA_TARGET_PM_IMAGE_CLI} PRIVATE
          "${CMAKE_CURRENT_SOURCE_DIR}/src/cli/pm_image_cmd_xblox.cpp"
      )
  endif()
  if(FEATURE_MCP_SERVER)
      target_sources(${MEDIA_TARGET_PM_IMAGE_CLI} PRIVATE
          "${CMAKE_CURRENT_SOURCE_DIR}/src/cli/pm_image_mcp_embed.cpp"
          "${CMAKE_CURRENT_SOURCE_DIR}/src/cli/pm_image_mcp_streamable.cpp"
      )
  endif()
  if(FEATURE_SERVE)
      target_compile_definitions(${MEDIA_TARGET_PM_IMAGE_CLI} PRIVATE FEATURE_SERVE=1)
      target_sources(${MEDIA_TARGET_PM_IMAGE_CLI} PRIVATE
          "${CMAKE_CURRENT_SOURCE_DIR}/src/cli/pm_image_cmd_serve.cpp"
          "${CMAKE_CURRENT_SOURCE_DIR}/src/http/serve.cpp"
          "${CMAKE_CURRENT_BINARY_DIR}/openapi_embed.cpp"
      )
  endif()
  if(FEATURE_IPC)
      target_compile_definitions(${MEDIA_TARGET_PM_IMAGE_CLI} PRIVATE FEATURE_IPC=1)
      target_sources(${MEDIA_TARGET_PM_IMAGE_CLI} PRIVATE
          "${CMAKE_CURRENT_SOURCE_DIR}/src/cli/pm_image_cmd_ipc.cpp"
          "${CMAKE_CURRENT_SOURCE_DIR}/src/ipc/ipc_serve.cpp"
      )
  endif()
  target_link_libraries(${MEDIA_TARGET_PM_IMAGE_CLI} PRIVATE
      pm-media
      logger
      CLI11::CLI11
      nlohmann_json::nlohmann_json
      httplib::httplib
      laserpants::dotenv
      pranav_glob
      CURL::libcurl
      Vips::vips
      html
      kbot
      tomlplusplus_hdr
      sodium
      ole32
      Advapi32
      User32
      Shell32
  )
  if(FEATURE_STT)
      target_compile_definitions(${MEDIA_TARGET_PM_IMAGE_CLI} PRIVATE FEATURE_STT=1)
      target_sources(${MEDIA_TARGET_PM_IMAGE_CLI} PRIVATE
          "${CMAKE_CURRENT_SOURCE_DIR}/src/core/audio.cpp"
          "${CMAKE_CURRENT_SOURCE_DIR}/src/core/audio_record_session.cpp"
          "${CMAKE_CURRENT_SOURCE_DIR}/src/audio/stt/elevenlabs.cpp"
          "${CMAKE_CURRENT_SOURCE_DIR}/src/audio/stt/proxy_stt.cpp"
          "${CMAKE_CURRENT_SOURCE_DIR}/src/audio/tts/elevenlabs_tts.cpp"
          "${CMAKE_CURRENT_SOURCE_DIR}/src/audio/tts/proxy_tts.cpp"
      )
      if(FEATURE_COMMAND_AUDIO)
          target_sources(${MEDIA_TARGET_PM_IMAGE_CLI} PRIVATE
              "${CMAKE_CURRENT_SOURCE_DIR}/src/cli/pm_image_cmd_audio.cpp"
          )
      endif()
      target_include_directories(${MEDIA_TARGET_PM_IMAGE_CLI} PRIVATE
          "${CMAKE_CURRENT_SOURCE_DIR}/src/audio"
      )
      if(TARGET miniaudio_hdr)
          target_link_libraries(${MEDIA_TARGET_PM_IMAGE_CLI} PRIVATE miniaudio_hdr)
      endif()
  endif()
  if(FEATURE_VIDEO)
      target_compile_definitions(${MEDIA_TARGET_PM_IMAGE_CLI} PRIVATE FEATURE_VIDEO=1)
      target_sources(${MEDIA_TARGET_PM_IMAGE_CLI} PRIVATE
          "${CMAKE_CURRENT_SOURCE_DIR}/src/core/video.cpp"
          "${CMAKE_CURRENT_SOURCE_DIR}/src/core/video_record_session.cpp"
      )
      if(FEATURE_COMMAND_VIDEO)
          target_sources(${MEDIA_TARGET_PM_IMAGE_CLI} PRIVATE
              "${CMAKE_CURRENT_SOURCE_DIR}/src/cli/pm_image_cmd_video.cpp"
          )
      endif()
      target_link_libraries(${MEDIA_TARGET_PM_IMAGE_CLI} PRIVATE
          mf mfplat mfreadwrite mfuuid Windowscodecs
      )
  endif()
  if(FEATURE_ASSISTANT)
      target_sources(${MEDIA_TARGET_PM_IMAGE_CLI} PRIVATE
          "${CMAKE_CURRENT_SOURCE_DIR}/src/cli/pm_image_cmd_assistant.cpp"
          "${CMAKE_CURRENT_SOURCE_DIR}/src/win/assistant/app_targets.cpp"
          "${CMAKE_CURRENT_SOURCE_DIR}/src/win/assistant/uia_spy.cpp"
          "${CMAKE_CURRENT_SOURCE_DIR}/src/win/assistant/assistant_bar.cpp"
          "${CMAKE_CURRENT_SOURCE_DIR}/src/win/assistant/app_inspect.cpp"
          "${CMAKE_CURRENT_SOURCE_DIR}/src/win/assistant/app_use.cpp"
          "${CMAKE_CURRENT_SOURCE_DIR}/src/win/assistant/app_batch.cpp"
      )
      target_link_libraries(${MEDIA_TARGET_PM_IMAGE_CLI} PRIVATE
          uiautomationcore.lib windowscodecs.lib gdi32 shlwapi)
  endif()
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/data/system-prompt.md")
  add_custom_command(
    TARGET ${MEDIA_TARGET_PM_IMAGE} POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/data/system-prompt.md"
            "${CMAKE_SOURCE_DIR}/dist/system-prompt.md"
    COMMENT "Copy optional data/system-prompt.md next to pm-image.exe (empty = use built-in prompt)"
  )
endif()

function(pm_write_features_json out_path)
  get_cmake_property(_pm_all_vars VARIABLES)
  set(_pm_feature_vars "")
  foreach(_pm_var IN LISTS _pm_all_vars)
    if(_pm_var MATCHES "^FEATURE_")
      list(APPEND _pm_feature_vars "${_pm_var}")
    endif()
  endforeach()
  list(REMOVE_DUPLICATES _pm_feature_vars)
  list(SORT _pm_feature_vars)

  file(MAKE_DIRECTORY "${PM_DIST_SHARED_DIR}")
  set(_pm_features_json "{\n")
  set(_pm_sep "")
  foreach(_pm_var IN LISTS _pm_feature_vars)
    if(${_pm_var})
      set(_pm_json_value "true")
    else()
      set(_pm_json_value "false")
    endif()
    string(APPEND _pm_features_json "${_pm_sep}  \"${_pm_var}\": ${_pm_json_value}")
    set(_pm_sep ",\n")
  endforeach()
  string(APPEND _pm_features_json "\n}\n")

  set(_pm_should_write TRUE)
  if(EXISTS "${out_path}")
    file(READ "${out_path}" _pm_existing_features_json)
    if(_pm_existing_features_json STREQUAL _pm_features_json)
      set(_pm_should_write FALSE)
    endif()
  endif()

  if(_pm_should_write)
    file(WRITE "${out_path}" "${_pm_features_json}")
    message(STATUS "Wrote feature manifest: ${out_path}")
  else()
    message(STATUS "Feature manifest unchanged: ${out_path}")
  endif()
endfunction()

pm_write_features_json("${PM_DIST_SHARED_DIR}/features.json")

# Runtime: libvips and deps are DLLs next to pm-image.exe (Windows dev bundle bin/).
if(WIN32)
  file(GLOB _MEDIA_VIPS_SDK_LIST
    "${PM_DIST_ROOT_DIR}/vendor/vips-dev-*"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/vips-dev-*")
  if(_MEDIA_VIPS_SDK_LIST)
    list(GET _MEDIA_VIPS_SDK_LIST 0 _MEDIA_VIPS_SDK)
    if(EXISTS "${_MEDIA_VIPS_SDK}/bin")
      # robocopy exit codes 0-7 are non-error (files copied, no-op, extra files, etc.).
      # Normalise to 0 so MSBuild does not treat a successful copy as a build failure.
      # This also avoids false failures when DLLs are in use by a running pm-image.exe.
      add_custom_command(
        TARGET ${MEDIA_TARGET_PM_IMAGE}
        POST_BUILD
        COMMAND cmd /C
          "robocopy \"${_MEDIA_VIPS_SDK}/bin\" \"${PM_DIST_DIR}\" /E /NFL /NDL /NJH /NJS /nc /ns /np & if %errorlevel% leq 7 exit 0"
        COMMENT "Copy libvips DLLs to Windows dist folder"
      )
    endif()
  endif()

  # MSVC release runtime next to the exe (msvcp140, vcruntime140, …) for clean Win11 PCs without Visual Studio.
  # Debug *D.dll are never copied — build Release to ship. See vcruntime140D / MSVCP140D / VCRUNTIME140_1D.
  if(MSVC)
    add_custom_command(
      TARGET ${MEDIA_TARGET_PM_IMAGE}
      POST_BUILD
      COMMAND
        "${CMAKE_COMMAND}"
        "-DPM_DIST=${PM_DIST_DIR}"
        "-DPM_CL=${CMAKE_CXX_COMPILER}"
        "-DPM_CMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}"
        "-DPM_CONFIG=$<CONFIG>"
        -P
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/copy_msvc_crt_to_dist.cmake"
      COMMENT "Copy MSVC release runtime DLLs to Windows dist folder (app-local, non-Debug only)"
    )
  endif()

  # Re-register Explorer context menu verbs so HKCU points at the Windows dist folder (pm-image.exe, pm-image-execute.dll, …).
  # Default OFF: each post-build run spawns pm-image (brief “headless” process) and can keep pm-image.exe
  # busy—bad while iterating. Enable with -DMEDIA_AUTO_REGISTER_EXPLORER=ON or run register-explorer manually.
  option(
    MEDIA_AUTO_REGISTER_EXPLORER
    "After pm-image: run dist\\win-x64\\pm-image register-explorer (updates HKCU shell verbs)"
    OFF
  )
  if(MEDIA_AUTO_REGISTER_EXPLORER)
    set(_MEDIA_REG_EX_ARGS register-explorer)
    add_custom_command(
      TARGET ${MEDIA_TARGET_PM_IMAGE}
      POST_BUILD
      COMMAND "$<TARGET_FILE:${MEDIA_TARGET_PM_IMAGE}>" ${_MEDIA_REG_EX_ARGS}
      WORKING_DIRECTORY "${PM_DIST_DIR}"
      COMMENT "register-explorer (paths -> Windows dist folder)"
    )
  endif()
  message(STATUS "MEDIA_AUTO_REGISTER_EXPLORER=${MEDIA_AUTO_REGISTER_EXPLORER} (ON = post-build register-explorer; OFF = default, avoids extra pm-image during link)")
endif()

# macOS: AppKit PM-Image.app and/or CodeEdit.app (vendored Xcode project; optional)
if(APPLE AND (PM_BUILD_MACOS_APP OR PM_BUILD_CODEEDIT))
  add_subdirectory(osx)
endif()

# Linux: GTK4/libadwaita GUI (optional)
if(UNIX AND NOT APPLE AND PM_BUILD_LINUX_GUI)
  add_subdirectory(linux)
endif()

if(UNIX AND NOT APPLE)
  include(cmake/Packaging.cmake)
endif()

if(POLYMECH_CLANG_TIDY_BUILD)
  find_program(
    _PM_CLANG_TIDY_EXE
    NAMES clang-tidy clang-tidy-20 clang-tidy-19 clang-tidy-18 clang-tidy-17
    HINTS
      "$ENV{LLVM_DIR}/../bin"
      "$ENV{LLVM_DIR}/bin"
      "$ENV{LLVM_INSTALL_DIR}/bin"
      "C:/Program Files/LLVM/bin"
      "C:/Program Files (x86)/LLVM/bin"
  )
  if(NOT _PM_CLANG_TIDY_EXE)
    message(
      FATAL_ERROR
        "POLYMECH_CLANG_TIDY_BUILD is ON but clang-tidy was not found. "
        "Install the LLVM/Clang package or add the folder containing clang-tidy.exe to PATH."
    )
  endif()
  set(_PM_TIDY_TARGETS pm-media kbot ${MEDIA_TARGET_PM_IMAGE})
  if(WIN32 AND TARGET ${MEDIA_TARGET_IEXECUTE})
    list(APPEND _PM_TIDY_TARGETS ${MEDIA_TARGET_IEXECUTE})
  endif()
  if(WIN32 AND PM_BUILD_EXPLORER11_DLL AND TARGET ${MEDIA_TARGET_EXPLORER11})
    list(APPEND _PM_TIDY_TARGETS ${MEDIA_TARGET_EXPLORER11})
  endif()
  foreach(_t IN LISTS _PM_TIDY_TARGETS)
    if(TARGET "${_t}")
      set_target_properties(
        "${_t}"
        PROPERTIES
          CXX_CLANG_TIDY
          "${_PM_CLANG_TIDY_EXE}"
      )
    endif()
  endforeach()
endif()
