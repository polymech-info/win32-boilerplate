# Build PhysicsFS 3.2.0 as a static lib for VFS (directory / zip mounts) + SVG byte reads.
include(FetchContent)
set(PHYSFS_BUILD_STATIC ON CACHE BOOL "" FORCE)
set(PHYSFS_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(PHYSFS_BUILD_TEST OFF CACHE BOOL "" FORCE)
set(PHYSFS_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(PHYSFS_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
FetchContent_Declare(physfs
  GIT_REPOSITORY https://github.com/icculus/physfs.git
  GIT_TAG        release-3.2.0
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(physfs)

if(NOT TARGET physfs-static)
  message(FATAL_ERROR "physfs_static: expected target physfs-static (PhysFS 3.2+)")
endif()
