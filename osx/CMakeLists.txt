# macOS: optional AppKit PM-Image.app and/or CodeEdit (Swift) built via xcodebuild.
# Parent sets PM_BUILD_MACOS_APP and/or PM_BUILD_CODEEDIT; add_subdirectory(osx) only on Apple.
# CLI `pm-image` is a separate target.

if(NOT APPLE)
  return()
endif()

if(NOT PM_BUILD_MACOS_APP AND NOT PM_BUILD_CODEEDIT)
  return()
endif()

# C/JSON bridge (resize, meta, compress, find, transform) for CodeEdit / future AppKit — before pm-image + CodeEdit.
if(PM_BUILD_CODEEDIT OR PM_BUILD_MACOS_APP)
  add_subdirectory(polymech_bridge)
endif()

# ── PM-Image (AppKit + ThorVG + Tabler) ───────────────────────────────────
if(PM_BUILD_MACOS_APP)
  enable_language(OBJCXX)

  set(_TABLER_SRC "${CMAKE_SOURCE_DIR}/packages/tabler-icons/icons/filled")
  if(NOT IS_DIRECTORY "${_TABLER_SRC}")
    message(FATAL_ERROR "pm-image-gui: missing Tabler icons: ${_TABLER_SRC}")
  endif()

  set(THORVG_ROOT "${CMAKE_SOURCE_DIR}/packages/thorvg")
  include("${CMAKE_SOURCE_DIR}/cmake/thorvg_static.cmake")

  add_executable(pm-image-gui MACOSX_BUNDLE
      src/PmImageGuiMain.mm
      pm_image_gui_log.cpp
      pm_dev_settings.cpp
      pm_svg_raster_mac.mm
  )
  set(_OSX_ARC_MM
      "${CMAKE_CURRENT_SOURCE_DIR}/src/PmImageGuiMain.mm"
      "${CMAKE_CURRENT_SOURCE_DIR}/pm_svg_raster_mac.mm"
  )
  set_source_files_properties(
      ${_OSX_ARC_MM}
      PROPERTIES
          COMPILE_FLAGS "-fobjc-arc"
  )

  set_target_properties(pm-image-gui PROPERTIES
      MACOSX_BUNDLE TRUE
      MACOSX_BUNDLE_BUNDLE_NAME "PM-Image"
      MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
      MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
      MACOSX_BUNDLE_LONG_VERSION_STRING "${PROJECT_VERSION}"
      MACOSX_BUNDLE_GUI_IDENTIFIER "com.polymech.pm-image.gui"
      XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED "NO"
  )

  target_link_libraries(pm-image-gui PRIVATE
      pm-media
      spdlog::spdlog
      thorvg_pm
      "-framework Cocoa"
      "-framework UniformTypeIdentifiers"
  )
  target_compile_definitions(pm-image-gui PRIVATE
      TVG_STATIC=1
      PM_TOOLBAR_SVG=1
  )
  target_include_directories(pm-image-gui PRIVATE
      ${CMAKE_SOURCE_DIR}/src
      ${CMAKE_BINARY_DIR}/include
      ${CMAKE_CURRENT_SOURCE_DIR}
      ${THORVG_ROOT}/inc
      ${CMAKE_SOURCE_DIR}/packages/thorvg/inc
  )
  target_include_directories(pm-image-gui SYSTEM PRIVATE
      ${CMAKE_SOURCE_DIR}/packages/thorvg/src
  )

  add_custom_command(
      TARGET pm-image-gui
      POST_BUILD
      COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_SOURCE_DIR}/dist-osx/tabler-icons/icons/filled"
      COMMAND "${CMAKE_COMMAND}" -E copy_directory
              "${_TABLER_SRC}"
              "${CMAKE_SOURCE_DIR}/dist-osx/tabler-icons/icons/filled"
      COMMAND "${CMAKE_COMMAND}" -E make_directory
              "$<TARGET_FILE_DIR:pm-image-gui>/../Resources/tabler-icons/icons/filled"
      COMMAND "${CMAKE_COMMAND}" -E copy_directory
              "${_TABLER_SRC}"
              "$<TARGET_FILE_DIR:pm-image-gui>/../Resources/tabler-icons/icons/filled"
      COMMENT "pm-image-gui: copy Tabler filled icons → dist-osx + .app Resources"
      VERBATIM
  )

  message(STATUS "PM_BUILD_MACOS_APP: ON — target pm-image-gui (AppKit .app, ThorVG toolbar)")
endif()

# ── CodeEdit (vendored Swift; built with Xcode) ───────────────────────────
if(PM_BUILD_CODEEDIT)
  add_subdirectory(CodeEdit)
endif()
