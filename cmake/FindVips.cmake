#[=======================================================================[
FindVips — libvips (Sharp uses the same library).

Linux / macOS: `pkg-config vips` after installing:
  Debian/Ubuntu: sudo apt install libvips-dev
  macOS: brew install vips

Windows: vcpkg / third_party vips-dev-*.zip, then set CMAKE_PREFIX_PATH or VIPS_ROOT.
Static link: build from source with Meson (-Ddefault_library=static), set VIPS_ROOT and
PKG_CONFIG_PATH to the install prefix, and enable POLYMECH_VIPS_PREFER_STATIC or -DVips_PREFER_STATIC=ON
so this module uses `pkg-config --static` (PkgConfig::… imported target with transitive usage).
#]=======================================================================]

include(FindPackageHandleStandardArgs)

if(DEFINED ENV{VIPS_ROOT} AND NOT VIPS_ROOT)
  set(VIPS_ROOT "$ENV{VIPS_ROOT}")
endif()
if(VIPS_ROOT)
  list(APPEND CMAKE_PREFIX_PATH "${VIPS_ROOT}")
endif()
option(Vips_PREFER_STATIC "Use pkg-config static entry for vips (requires vips.pc with static libs)" OFF)

find_package(PkgConfig QUIET)
set(_Vips_FOUND FALSE)
if(PkgConfig_FOUND)
  if(Vips_PREFER_STATIC)
    pkg_check_modules(VIPS_S IMPORTED_TARGET QUIET STATIC vips)
  endif()
  if(TARGET PkgConfig::VIPS_S)
    if(NOT TARGET Vips::vips)
      add_library(Vips::vips ALIAS PkgConfig::VIPS_S)
    endif()
    set(_Vips_FOUND TRUE)
  else()
    pkg_check_modules(VIPS IMPORTED_TARGET QUIET vips)
  endif()
endif()

if(TARGET PkgConfig::VIPS)
  if(NOT TARGET Vips::vips)
    add_library(Vips::vips ALIAS PkgConfig::VIPS)
  endif()
  set(_Vips_FOUND TRUE)
endif()

if(_Vips_FOUND)
  set(Vips_FOUND TRUE)
else()
  if(DEFINED Vips_LIBRARY AND NOT EXISTS "${Vips_LIBRARY}")
    unset(Vips_LIBRARY CACHE)
  endif()
  if(DEFINED Vips_INCLUDE_DIR AND NOT EXISTS "${Vips_INCLUDE_DIR}/vips/vips.h")
    unset(Vips_INCLUDE_DIR CACHE)
  endif()
  find_path(
    Vips_INCLUDE_DIR
    NAMES vips/vips.h
    PATHS "${CMAKE_PREFIX_PATH}"
    PATH_SUFFIXES include
  )
  find_library(
    Vips_LIBRARY
    NAMES vips libvips vips-8.0
    PATHS "${CMAKE_PREFIX_PATH}"
    PATH_SUFFIXES lib lib64
  )
  find_package_handle_standard_args(Vips DEFAULT_MSG Vips_LIBRARY Vips_INCLUDE_DIR)
  if(Vips_FOUND)
    add_library(Vips::vips UNKNOWN IMPORTED)
    get_filename_component(Vips_LIBRARY_DIR "${Vips_LIBRARY}" DIRECTORY)
    # vips.h includes glib.h — Windows SDK bundles glib headers next to vips.
    set(_VIPS_INCLUDES
        "${Vips_INCLUDE_DIR}"
        "${Vips_INCLUDE_DIR}/glib-2.0"
        "${Vips_LIBRARY_DIR}/glib-2.0/include")
    set_target_properties(
      Vips::vips
      PROPERTIES
        IMPORTED_LOCATION "${Vips_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${_VIPS_INCLUDES}"
    )
  endif()
endif()
