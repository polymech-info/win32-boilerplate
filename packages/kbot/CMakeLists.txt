cmake_minimum_required(VERSION 3.20)

project(kbot CXX)

option(POLYMECH_KBOT_SHARED "Build kbot as a shared library (DLL/so)" OFF)

set(_kbot_sources kbot.cpp llm_client.cpp source_files.cpp)

if(POLYMECH_KBOT_SHARED)
  add_library(kbot SHARED ${_kbot_sources})
  target_compile_definitions(kbot PRIVATE POLYMECH_BUILDING_LIBRARY)
else()
  add_library(kbot STATIC ${_kbot_sources})
  target_compile_definitions(kbot PRIVATE POLYMECH_STATIC_BUILD=1)
  target_compile_definitions(kbot INTERFACE POLYMECH_STATIC_BUILD=1)
endif()

target_include_directories(kbot PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${taskflow_SOURCE_DIR}
)

target_link_libraries(kbot PUBLIC
    logger
    json
    oai
    pranav_glob
)

if(POLYMECH_KBOT_SHARED)
  set_target_properties(kbot PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/dist"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_SOURCE_DIR}/dist"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/dist"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/dist"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/dist"
  )
endif()

install(TARGETS kbot
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin
)
install(FILES
  ${CMAKE_CURRENT_SOURCE_DIR}/kbot.h
  ${CMAKE_CURRENT_SOURCE_DIR}/llm_client.h
  ${CMAKE_CURRENT_SOURCE_DIR}/polymech_export.h
  DESTINATION include/polymech
)
