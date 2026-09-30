#pragma once

/**
 * DLL / shared-object exports for the length-prefixed JSON IPC framing library.
 *
 * CMake:
 *   - Building libipc: IPC_BUILDING_LIBRARY (PRIVATE)
 *   - Linking static ipc: IPC_STATIC_BUILD=1 (INTERFACE)
 */

#if defined(IPC_STATIC_BUILD)
#  define IPC_API
#elif defined(_WIN32)
#  if defined(IPC_BUILDING_LIBRARY)
#    define IPC_API __declspec(dllexport)
#  else
#    define IPC_API __declspec(dllimport)
#  endif
#else
#  if defined(IPC_BUILDING_LIBRARY)
#    define IPC_API __attribute__((visibility("default")))
#  else
#    define IPC_API
#  endif
#endif
