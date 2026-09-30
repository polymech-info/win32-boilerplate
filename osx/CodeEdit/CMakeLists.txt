# Vendored PixelWiz (PixlWiz sources) — `xcodebuild` with the same static image stack as `pm-image` (pm-media + kbot + … + libcurl) and
# pkg-config `vips` (libvips on macOS, same as CLI). CMake target remains `pm-codeedit`; UI lives under `PixlWiz/`.
#
# Xcode: `PixelWiz.xcodeproj`, app product `PixelWiz.app`, main scheme name is still `CodeEdit` (shared scheme file CodeEdit.xcscheme).

if(NOT PM_BUILD_CODEEDIT)
  return()
endif()

set(_CE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
set(_PW_XCODEPROJ "PixelWiz.xcodeproj")
set(_PW_XCODE_SCHEME "CodeEdit")
set(_PW_PRODUCT_APP "PixelWiz.app")
set(_PW_SOURCE_DIR "PixlWiz")
set(_CE_XCODEPROJ "${_CE_ROOT}/${_PW_XCODEPROJ}")
if(NOT EXISTS "${_CE_XCODEPROJ}/project.pbxproj")
  message(FATAL_ERROR "PixelWiz: missing ${_CE_XCODEPROJ} (vendored app tree).")
endif()

find_program(PM_XCODEBUILD_EXE xcodebuild
  HINTS
    "/Applications/Xcode.app/Contents/Developer/usr/bin"
  DOC "Xcode xcodebuild (required for PixelWiz / pm-codeedit)"
)
if(NOT PM_XCODEBUILD_EXE)
  message(FATAL_ERROR "PM_BUILD_CODEEDIT: xcodebuild not found. Install Xcode command-line tools.")
endif()

set(PM_CODEEDIT_CONFIGURATION "Production" CACHE STRING "xcodebuild -configuration for PixelWiz (Pre or Production)")
# Older CMake caches still had Debug/Release/Alpha/Beta; the Xcode project now only has Pre + Production.
set(_PM_XCODE_LEGACY_CFG "${PM_CODEEDIT_CONFIGURATION}")
if(_PM_XCODE_LEGACY_CFG MATCHES "^(Debug|Release|Alpha|Beta)$")
	set(PM_CODEEDIT_CONFIGURATION "Production" CACHE STRING "xcodebuild -configuration for PixelWiz (Pre or Production)" FORCE)
	message(STATUS "PixelWiz: PM_CODEEDIT_CONFIGURATION was legacy '${_PM_XCODE_LEGACY_CFG}'; reset to Production. Pass -DPM_CODEEDIT_CONFIGURATION=Pre for debug-style builds.")
endif()
if(NOT PM_CODEEDIT_CONFIGURATION STREQUAL "Pre" AND NOT PM_CODEEDIT_CONFIGURATION STREQUAL "Production")
	message(FATAL_ERROR "PM_CODEEDIT_CONFIGURATION must be Pre or Production (got: '${PM_CODEEDIT_CONFIGURATION}')")
endif()
set(_CE_DERIVED "${CMAKE_CURRENT_BINARY_DIR}/pixelwiz-derived")
set(_CE_BUILT_APP "${_CE_DERIVED}/Build/Products/${PM_CODEEDIT_CONFIGURATION}/${_PW_PRODUCT_APP}")
set(_CE_OUT_APP "${CMAKE_SOURCE_DIR}/dist-osx/${_PW_PRODUCT_APP}")
set(_CE_STAGED_LIB_DIR "${CMAKE_SOURCE_DIR}/dist-osx/polymech-cmake-libs")
set(_CE_VIPS_PC_FILE "${_CE_STAGED_LIB_DIR}/polymech_pkg_vips_flags.txt")
set(_CE_B_SCRIPT "${_CE_ROOT}/polymech_xcode_with_libs.sh")
set(_BRIDGE_A "$<TARGET_FILE:polymech-bridge>")
set(_BRIDGE_H "${CMAKE_SOURCE_DIR}/osx/polymech_bridge/pm_polymech_bridge.h")

# Match CLI: vips (glib, etc.) from the same `pkg-config` the main project uses.
find_package(PkgConfig QUIET)
if(NOT PKG_CONFIG_EXECUTABLE)
  find_program(PKG_CONFIG_EXECUTABLE pkg-config
    "/opt/homebrew/bin" "/usr/local/bin"
    "/opt/homebrew/opt/pkgconf/bin" "/usr/local/opt/pkgconf/bin"
  )
endif()
set(PM_XCODE_VIPS_PC "")
if(PkgConfig_FOUND AND PKG_CONFIG_EXECUTABLE)
  execute_process(
    COMMAND ${PKG_CONFIG_EXECUTABLE} --libs vips
    OUTPUT_VARIABLE _PM_VIPS_OUT
    ERROR_QUIET
  )
  string(STRIP "${_PM_VIPS_OUT}" PM_XCODE_VIPS_PC)
endif()
if(PM_XCODE_VIPS_PC STREQUAL "" AND PKG_CONFIG_EXECUTABLE)
  # Try common macOS / Linux names
  execute_process(
    COMMAND ${PKG_CONFIG_EXECUTABLE} --libs libvips
    OUTPUT_VARIABLE _PM_VIPS2
    ERROR_QUIET
  )
  string(STRIP "${_PM_VIPS2}" PM_XCODE_VIPS_PC)
endif()
file(MAKE_DIRECTORY "${_CE_STAGED_LIB_DIR}")
file(WRITE "${_CE_VIPS_PC_FILE}" "${PM_XCODE_VIPS_PC}")
if(PM_XCODE_VIPS_PC STREQUAL "")
  message(
    WARNING
    "PixelWiz: pkg-config vips (or libvips) is empty. Install vips (e.g. brew install vips) for correct linking; "
    "xcodebuild may still run but fail at link if you add Polymech symbols."
  )
endif()

# libcurl (Fetch) on macOS still needs OpenSSL, IDN, zlib, LDAP, and a few frameworks at link time.
set(PM_XCODE_CURL_TRANSCRIPT "")
if(APPLE)
  if(PkgConfig_FOUND AND PKG_CONFIG_EXECUTABLE)
    execute_process(
      COMMAND ${PKG_CONFIG_EXECUTABLE} --libs openssl
      OUTPUT_VARIABLE _PM_SSL_PC
      ERROR_QUIET
    )
    string(STRIP "${_PM_SSL_PC}" _PM_SSL_PC)
    execute_process(
      COMMAND ${PKG_CONFIG_EXECUTABLE} --libs libidn2
      OUTPUT_VARIABLE _PM_IDN_PC
      ERROR_QUIET
    )
    string(STRIP "${_PM_IDN_PC}" _PM_IDN_PC)
    set(PM_XCODE_CURL_TRANSCRIPT " ${_PM_SSL_PC} ${_PM_IDN_PC} -lz -framework Security -framework SystemConfiguration -lldap -llber")
  endif()
endif()

# App-target-only link flags (do not pass on xcodebuild — would link SPM packages like CodeEditKit against pm-media on non-app targets).
set(_PM_CODEEDIT_LINK_XC "${_CE_STAGED_LIB_DIR}/polymech_codeedit_link.xcconfig")
file(
  GENERATE
  OUTPUT "${_PM_CODEEDIT_LINK_XC}"
  CONTENT
    "OTHER_LDFLAGS = $(inherited) -Wl,-search_paths_first -Wl,-force_load,$<TARGET_FILE:polymech-bridge> $<TARGET_FILE:pm-media> $<IF:$<TARGET_EXISTS:pm-audio>,$<TARGET_FILE:pm-audio>,> $<IF:$<TARGET_EXISTS:pm-video>,$<TARGET_FILE:pm-video>,> $<TARGET_FILE:kbot> $<TARGET_FILE:oai> $<TARGET_FILE:json> $<TARGET_FILE:logger> $<IF:$<TARGET_EXISTS:spdlog::spdlog>,$<TARGET_FILE:spdlog::spdlog>,$<TARGET_FILE:spdlog>> $<TARGET_FILE:pranav_glob> $<IF:$<TARGET_EXISTS:libcurl>,$<TARGET_FILE:libcurl>,$<TARGET_FILE:CURL::libcurl>> -lc++ -framework CoreAudio -framework AudioUnit -framework AudioToolbox -framework AVFoundation -framework CoreMedia -framework CoreVideo -framework CoreGraphics -framework ImageIO ${PM_XCODE_VIPS_PC} ${PM_XCODE_CURL_TRANSCRIPT}\n"
)

# libcurl from Fetch (same as pm-media) — not system curl.
# Order for static: pm → kbot → oai, json, logger, pranav, libcurl, then vips (from pkg file).
if(NOT (TARGET libcurl OR TARGET "CURL::libcurl"))
  message(FATAL_ERROR "PixelWiz: expected libcurl or CURL::libcurl (Fetch).")
endif()
# `$<TARGET_FILE:name>` only accepts a literal `name` — branch at configure.
if(TARGET libcurl)
  set(_CURL_TGT "$<TARGET_FILE:libcurl>")
else()
  set(_CURL_TGT "$<TARGET_FILE:CURL::libcurl>")
endif()

# Not ALL: `cmake --build … --target pm-codeedit` or `npm run build:codeedit`.
add_custom_target(
  pm-codeedit
  COMMAND "${CMAKE_COMMAND}" -E echo
          "-- xcodebuild PixelWiz scheme=${_PW_XCODE_SCHEME} (${PM_CODEEDIT_CONFIGURATION}) + Polymech image stack (staged in polymech-cmake-libs/)"
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${_CE_STAGED_LIB_DIR}"
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${_CE_STAGED_LIB_DIR}/include"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${_BRIDGE_H}" "${_CE_STAGED_LIB_DIR}/include/"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${_BRIDGE_A}" "${_CE_STAGED_LIB_DIR}/"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:pm-media>" "${_CE_STAGED_LIB_DIR}/"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:kbot>" "${_CE_STAGED_LIB_DIR}/"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:oai>" "${_CE_STAGED_LIB_DIR}/"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:json>" "${_CE_STAGED_LIB_DIR}/"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:logger>" "${_CE_STAGED_LIB_DIR}/"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:pranav_glob>" "${_CE_STAGED_LIB_DIR}/"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${_CURL_TGT}" "${_CE_STAGED_LIB_DIR}/"
  COMMAND bash
          "${_CE_B_SCRIPT}"
          "${PM_XCODEBUILD_EXE}"
          "${_CE_DERIVED}"
          "${_CE_ROOT}"
          "${_PW_XCODEPROJ}"
          "${_PW_XCODE_SCHEME}"
          "${PM_CODEEDIT_CONFIGURATION}"
  COMMAND "${CMAKE_COMMAND}" -E rm -rf "${_CE_OUT_APP}"
  COMMAND "${CMAKE_COMMAND}" -E copy_directory
          "${_CE_BUILT_APP}"
          "${_CE_OUT_APP}"
  COMMAND "${CMAKE_COMMAND}" -E make_directory
          "${_CE_OUT_APP}/Contents/Resources/PolymechChat"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
          "${CMAKE_SOURCE_DIR}/dist/chat.html"
          "${_CE_OUT_APP}/Contents/Resources/PolymechChat/chat.html"
  COMMAND "${CMAKE_COMMAND}" -E make_directory
          "${_CE_OUT_APP}/Contents/Resources/PolymechViewer"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
          "${CMAKE_SOURCE_DIR}/dist/viewer.html"
          "${_CE_OUT_APP}/Contents/Resources/PolymechViewer/viewer.html"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
          "${_CE_ROOT}/${_PW_SOURCE_DIR}/Resources/codeedit"
          "${_CE_OUT_APP}/Contents/Resources/codeedit"
  COMMAND chmod +x "${_CE_OUT_APP}/Contents/Resources/codeedit"
  BYPRODUCTS
    "${_CE_DERIVED}/Build/Products/${PM_CODEEDIT_CONFIGURATION}/${_PW_PRODUCT_APP}"
  WORKING_DIRECTORY "${_CE_ROOT}"
  USES_TERMINAL
  VERBATIM
  COMMENT "PixelWiz: xcodebuild (Polymech libs + vips pkg-config), then copy to dist-osx/"
)

# Build polymech-bridge + the static stack before `xcodebuild` (CURL: dep of oai / pm-media / kbot).
add_dependencies(
  pm-codeedit
  polymech-bridge
  pm-media
  kbot
  oai
  json
  logger
  spdlog
  pranav_glob
)

# pm-audio: audio implementation lib (macOS, FEATURE_STT=ON) — AudioOutput symbols
# referenced from pm-media (path_tool_executor.cpp). Stage it alongside pm-media.
if(TARGET pm-audio)
  add_dependencies(pm-codeedit pm-audio)
  add_custom_command(TARGET pm-codeedit PRE_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:pm-audio>" "${_CE_STAGED_LIB_DIR}/"
    COMMENT "Staging pm-audio for Xcode link"
  )
endif()

# pm-video: video capture lib (macOS, FEATURE_VIDEO=ON) — pm::video::* symbols
# referenced from pm-media (path_tool_executor.cpp). Stage it alongside pm-media.
if(TARGET pm-video)
  add_dependencies(pm-codeedit pm-video)
  add_custom_command(TARGET pm-codeedit PRE_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:pm-video>" "${_CE_STAGED_LIB_DIR}/"
    COMMENT "Staging pm-video for Xcode link"
  )
endif()

# Ensure FetchContent curl exists when the libcurl target is that build.
if(TARGET libcurl)
  add_dependencies(pm-codeedit libcurl)
endif()

message(
  STATUS
    "PM_BUILD_CODEEDIT: ON — `cmake --build … --target pm-codeedit` → ${_CE_OUT_APP} (PixelWiz.app). "
    "Staged: ${_CE_STAGED_LIB_DIR} (libpolymech-bridge + pm-media + … + vips) + include/pm_polymech_bridge.h for Swift/ObjC"
)
