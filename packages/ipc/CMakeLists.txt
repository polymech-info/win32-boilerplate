cmake_minimum_required(VERSION 3.20)

project(ipc CXX)

option(IPC_BUILD_SHARED "Build ipc as a shared library (DLL/so)" OFF)

set(_ipc_sources src/ipc.cpp)

if(IPC_BUILD_SHARED)
  add_library(ipc SHARED ${_ipc_sources})
  target_compile_definitions(ipc PRIVATE IPC_BUILDING_LIBRARY)
else()
  add_library(ipc STATIC ${_ipc_sources})
  target_compile_definitions(ipc PRIVATE IPC_STATIC_BUILD=1)
  target_compile_definitions(ipc INTERFACE IPC_STATIC_BUILD=1)
endif()

target_include_directories(ipc
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(ipc
  PUBLIC json logger
)

if(IPC_BUILD_SHARED)
  set_target_properties(ipc PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/dist"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_SOURCE_DIR}/dist"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/dist"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/dist"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/dist"
  )
endif()

install(TARGETS ipc
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin
)
install(FILES
  ${CMAKE_CURRENT_SOURCE_DIR}/include/ipc/ipc.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/ipc/ipc_export.h
  DESTINATION include/ipc
)
