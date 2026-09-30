# Linux GUI for pm-image
# Parent sets PM_BUILD_LINUX_GUI; add_subdirectory(linux) only on Linux.
# CLI `pm-image` is a separate target built from parent.

if(NOT UNIX OR APPLE)
  return()
endif()

if(NOT PM_BUILD_LINUX_GUI)
  return()
endif()

# ── Find dependencies ─────────────────────────────────────────────────────────

find_package(PkgConfig REQUIRED)

# Ubuntu focal fallback: prefer GTK3, optionally detect GTK4/libadwaita.
pkg_check_modules(GTK4 gtk4)
pkg_check_modules(ADWAITA libadwaita-1)
pkg_check_modules(GTK3 gtk+-3.0)
if(LINUX_USE_LIBPANEL)
  pkg_check_modules(LIBPANEL libpanel-1)
endif()

# Web panel backend: behind LINUX_USE_WEBKIT_GTK.
if(LINUX_USE_WEBKIT_GTK)
  # Match WebKit package line to selected GTK major to avoid API mismatches.
  if(GTK4_FOUND AND ADWAITA_FOUND)
    # Clear any stale GTK3-era WebKit cache from previous configure runs.
    unset(WEBKITGTK_41_FOUND CACHE)
    unset(WEBKITGTK_41_INCLUDE_DIRS CACHE)
    unset(WEBKITGTK_41_LIBRARIES CACHE)
    unset(WEBKITGTK_41_CFLAGS_OTHER CACHE)
    unset(WEBKITGTK_40_FOUND CACHE)
    unset(WEBKITGTK_40_INCLUDE_DIRS CACHE)
    unset(WEBKITGTK_40_LIBRARIES CACHE)
    unset(WEBKITGTK_40_CFLAGS_OTHER CACHE)
    # GTK4-compatible WebKit package.
    pkg_check_modules(WEBKITGTK_60 webkitgtk-6.0)
  elseif(GTK3_FOUND)
    # GTK3-compatible WebKit packages.
    pkg_check_modules(WEBKITGTK_41 webkit2gtk-4.1)
    pkg_check_modules(WEBKITGTK_40 webkit2gtk-4.0)
  endif()
endif()

set(PM_GUI_INCLUDE_DIRS "")
set(PM_GUI_LIBRARIES "")
set(PM_GUI_CFLAGS "")
set(PM_WEBKIT_FOUND OFF)

if(GTK4_FOUND AND ADWAITA_FOUND)
  set(PM_GUI_INCLUDE_DIRS ${GTK4_INCLUDE_DIRS} ${ADWAITA_INCLUDE_DIRS})
  set(PM_GUI_LIBRARIES ${GTK4_LIBRARIES} ${ADWAITA_LIBRARIES})
  list(APPEND PM_GUI_CFLAGS ${GTK4_CFLAGS_OTHER} ${ADWAITA_CFLAGS_OTHER})
  set(PM_GTK_DEFINES PM_GTK_USE_GTK4=1 PM_GTK_USE_ADWAITA=1)
  if(LINUX_USE_LIBPANEL)
    if(LIBPANEL_FOUND)
      list(APPEND PM_GUI_INCLUDE_DIRS ${LIBPANEL_INCLUDE_DIRS})
      list(APPEND PM_GUI_LIBRARIES ${LIBPANEL_LIBRARIES})
      list(APPEND PM_GUI_CFLAGS ${LIBPANEL_CFLAGS_OTHER})
      list(APPEND PM_GTK_DEFINES PM_HAVE_LIBPANEL=1)
    else()
      message(WARNING "LINUX_USE_LIBPANEL=ON but pkg-config could not find libpanel-1; continuing without libpanel.")
    endif()
  endif()
elseif(GTK3_FOUND)
  set(PM_GUI_INCLUDE_DIRS ${GTK3_INCLUDE_DIRS})
  set(PM_GUI_LIBRARIES ${GTK3_LIBRARIES})
  list(APPEND PM_GUI_CFLAGS ${GTK3_CFLAGS_OTHER})
  set(PM_GTK_DEFINES PM_GTK_USE_GTK3=1)
else()
  message(FATAL_ERROR "PM_BUILD_LINUX_GUI requires GTK dev packages. Install libgtk-3-dev (or gtk4 + libadwaita-1 dev packages).")
endif()

if(LINUX_USE_WEBKIT_GTK AND WEBKITGTK_60_FOUND)
  set(PM_WEBKIT_FOUND ON)
  list(APPEND PM_GUI_INCLUDE_DIRS ${WEBKITGTK_60_INCLUDE_DIRS})
  list(APPEND PM_GUI_LIBRARIES ${WEBKITGTK_60_LIBRARIES})
  list(APPEND PM_GUI_CFLAGS ${WEBKITGTK_60_CFLAGS_OTHER})
  set(PM_WEBKIT_DEFINE PM_WEBKITGTK_API_60=1)
elseif(LINUX_USE_WEBKIT_GTK AND GTK3_FOUND AND WEBKITGTK_41_FOUND AND NOT (GTK4_FOUND AND ADWAITA_FOUND))
  set(PM_WEBKIT_FOUND ON)
  list(APPEND PM_GUI_INCLUDE_DIRS ${WEBKITGTK_41_INCLUDE_DIRS})
  list(APPEND PM_GUI_LIBRARIES ${WEBKITGTK_41_LIBRARIES})
  list(APPEND PM_GUI_CFLAGS ${WEBKITGTK_41_CFLAGS_OTHER})
  set(PM_WEBKIT_DEFINE PM_WEBKITGTK_API_41=1)
elseif(LINUX_USE_WEBKIT_GTK AND GTK3_FOUND AND WEBKITGTK_40_FOUND AND NOT (GTK4_FOUND AND ADWAITA_FOUND))
  set(PM_WEBKIT_FOUND ON)
  list(APPEND PM_GUI_INCLUDE_DIRS ${WEBKITGTK_40_INCLUDE_DIRS})
  list(APPEND PM_GUI_LIBRARIES ${WEBKITGTK_40_LIBRARIES})
  list(APPEND PM_GUI_CFLAGS ${WEBKITGTK_40_CFLAGS_OTHER})
  set(PM_WEBKIT_DEFINE PM_WEBKITGTK_API_40=1)
endif()

# ── GUI executable ────────────────────────────────────────────────────────────

add_executable(pm-image-gui
    src/main.cpp
)

target_compile_definitions(pm-image-gui PRIVATE
    PM_GTK_GUI=1
    ${PM_GTK_DEFINES}
)

target_include_directories(pm-image-gui PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/include
    ${PM_GUI_INCLUDE_DIRS}
)

target_compile_options(pm-image-gui PRIVATE ${PM_GUI_CFLAGS})

if(PM_WEBKIT_FOUND)
  target_compile_definitions(pm-image-gui PRIVATE PM_HAVE_WEBKITGTK=1 ${PM_WEBKIT_DEFINE})
endif()

target_link_libraries(pm-image-gui PRIVATE
    pm-media
    spdlog::spdlog
    ${PM_GUI_LIBRARIES}
)

# Set runtime output to dist/ (same as CLI)
set_target_properties(pm-image-gui PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${PM_DIST_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${PM_DIST_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${PM_DIST_DIR}"
)

message(STATUS "PM_BUILD_LINUX_GUI: ON — target pm-image-gui")
if(GTK4_FOUND AND ADWAITA_FOUND)
  message(STATUS "  + Toolkit: GTK4 + libadwaita")
elseif(GTK3_FOUND)
  message(STATUS "  + Toolkit: GTK3")
endif()
if(PM_WEBKIT_FOUND)
  message(STATUS "  + WebKitGTK (web panel enabled)")
else()
  if(LINUX_USE_WEBKIT_GTK)
    message(STATUS "  - WebKitGTK not found (web panel placeholder mode)")
  else()
    message(STATUS "  - WebKitGTK disabled (LINUX_USE_WEBKIT_GTK=OFF)")
  endif()
endif()
if(LINUX_USE_LIBPANEL)
  if(LIBPANEL_FOUND)
    message(STATUS "  + libpanel enabled (experimental docking)")
  else()
    message(STATUS "  - libpanel requested but not found (no docking library)")
  endif()
endif()
