# Build-time: pack Tabler icons/filled/*.svg into a single physfs-supported ZIP archive
# (written as .pfs; PhysFS opens it as a mounted archive). Invoked with:
#   cmake -DPACK_OUT=.../dist/assets.pfs -DPACK_SRC_DIR=.../icons/filled -P this_file.cmake
if(NOT DEFINED PACK_OUT)
  message(FATAL_ERROR "pack_tabler_assets_pfs: PACK_OUT not set")
endif()
if(NOT DEFINED PACK_SRC_DIR)
  message(FATAL_ERROR "pack_tabler_assets_pfs: PACK_SRC_DIR not set")
endif()
if(NOT IS_DIRECTORY "${PACK_SRC_DIR}")
  message(FATAL_ERROR "pack_tabler_assets_pfs: PACK_SRC_DIR is not a directory: ${PACK_SRC_DIR}")
endif()
get_filename_component(_pfs_parent "${PACK_OUT}" DIRECTORY)
file(MAKE_DIRECTORY "${_pfs_parent}")
file(GLOB _svgs "${PACK_SRC_DIR}/*.svg")
list(LENGTH _svgs _n)
if(_n EQUAL 0)
  message(FATAL_ERROR "pack_tabler_assets_pfs: no .svg under ${PACK_SRC_DIR}")
endif()
# Copy into a flat temp dir so the ZIP has only "name.svg" entries (no long paths or
# backslashes) — some PhysFS/zip builds fail to open by basename otherwise.
get_filename_component(_pfsdir "${PACK_OUT}" DIRECTORY)
set(_flat "${_pfsdir}/.tabler_pfs_flat")
file(REMOVE_RECURSE "${_flat}")
file(MAKE_DIRECTORY "${_flat}")
foreach(_s IN LISTS _svgs)
  file(COPY "${_s}" DESTINATION "${_flat}")
endforeach()
file(GLOB _flat_svgs "${_flat}/*.svg")
# Path-only entries in the ZIP (PATHS must be basenames, not absolute paths) so
# PhysFS_openRead("file.svg") works; see svg_raster.cpp.
set(_flat_entry_names "")
foreach(_p IN LISTS _flat_svgs)
  get_filename_component(_bn "${_p}" NAME)
  list(APPEND _flat_entry_names "${_bn}")
endforeach()
# Standard ZIP; PhysFS opens as assets.pfs next to the exe.
# Some CMake builds accept only FORMAT "zip" with no COMPRESSION / COMPRESSION_LEVEL
# (older: None + level error; newer: "zip does not support COMPRESSION arguments").
file(ARCHIVE_CREATE
  OUTPUT "${PACK_OUT}"
  PATHS ${_flat_entry_names}
  WORKING_DIRECTORY "${_flat}"
  FORMAT "zip"
)
file(REMOVE_RECURSE "${_flat}")
