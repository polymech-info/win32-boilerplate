# Static C/JSON bridge for CodeEdit (Swift) — same `media::` entry points as `pm-image` CLI.
if(NOT (PM_BUILD_CODEEDIT OR PM_BUILD_MACOS_APP))
  return()
endif()

add_library(polymech-bridge STATIC
  pm_polymech_bridge.cpp
  "${CMAKE_SOURCE_DIR}/src/lib/pm_zitadel_oauth.cpp"
  "${CMAKE_SOURCE_DIR}/src/lib/pm_service_upload.cpp"
  "${CMAKE_SOURCE_DIR}/src/lib/pm_service_posts.cpp"
)
target_link_libraries(polymech-bridge PUBLIC pm-media PRIVATE logger)
target_include_directories(polymech-bridge PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}/include
)

message(STATUS "polymech-bridge: C JSON API for resize / meta / compress / find / transform / service create post")
