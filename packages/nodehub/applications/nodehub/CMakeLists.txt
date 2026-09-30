cmake_minimum_required(VERSION 3.5)

add_example_executable(nodehub
    main.cpp
    types.h
    app.h
    app.cpp
    app-logic.cpp
    app-render.cpp
    app-screenshot.cpp
    app-runtime.cpp
    containers/container.h
    containers/container.cpp
    containers/root_container.h
    containers/root_container.cpp
    core/graph_state.h
    core/graph_state.cpp
    core/Object.h
    core/Object.cpp
    core/Parameter.h
    core/Parameter.cpp
    core/ParameterIn.h
    core/ParameterIn.cpp
    core/ParameterOut.h
    core/ParameterOut.cpp
    core/ParameterManager.h
    core/ParameterManager.cpp
    core/BaseManager.h
    core/BaseManager.cpp
    core/Context.h
    core/Context.cpp
    blocks/NodeEx.h
    blocks/NodeEx.cpp
    blocks/block.h
    blocks/block.cpp
    blocks/math_blocks.h
    blocks/math_blocks.cpp
    blocks/logic_blocks.h
    blocks/logic_blocks.cpp
    blocks/start_block.h
    blocks/start_block.cpp
    blocks/log_block.h
    blocks/log_block.cpp
    blocks/parameter_operation.h
    blocks/parameter_operation.cpp
    blocks/group_block.h
    blocks/group_block.cpp
    blocks/parameter_node.h
    blocks/parameter_node.cpp
    blocks/block_edit_dialog.h
    blocks/block_edit_dialog.cpp
    blocks/parameter_edit_dialog.h
    blocks/parameter_edit_dialog.cpp
    utilities/node_renderer_base.h
    utilities/pathfinding.h
    utilities/edge_editing.h
    utilities/pin_renderer.h
    utilities/style_manager.h
    utilities/uuid_generator.h
    utilities/uuid_id_manager.h
    utilities/node_renderer_base.cpp
    utilities/pathfinding.cpp
    utilities/edge_editing.cpp
    utilities/pin_renderer.cpp
    utilities/style_manager.cpp
    utilities/uuid_generator.cpp
    utilities/uuid_id_manager.cpp
    Logging.h
    Logging.cpp
    stats.cpp
)

target_include_directories(nodehub PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../application/include")
target_include_directories(nodehub PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../nodehub/core")
target_include_directories(nodehub PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../nodehub/blocks")

# Add local spdlog include directory (we copied it into our project)
target_include_directories(nodehub PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/external")
target_compile_definitions(nodehub PRIVATE FMT_HEADER_ONLY=1)

# Add /utf-8 compiler flag for spdlog on MSVC
if (MSVC)
    target_compile_options(nodehub PRIVATE /utf-8)
endif()

# Link protobuf if available
if (PROTOBUF_AVAILABLE AND TARGET protobuf_interface)
    target_link_libraries(nodehub PRIVATE protobuf_interface)
endif()

# Also add to console variant if it exists
if (WIN32 AND BUILD_CONSOLE_VARIANTS)
    target_include_directories(nodehub-console PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../application/include")
    target_include_directories(nodehub-console PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/external")
    target_compile_definitions(nodehub-console PRIVATE FMT_HEADER_ONLY=1)
    if (MSVC)
        target_compile_options(nodehub-console PRIVATE /utf-8)
    endif()
    # Link protobuf to console variant if available
    if (PROTOBUF_AVAILABLE AND TARGET protobuf_interface)
        target_link_libraries(nodehub-console PRIVATE protobuf_interface)
    endif()
endif()

file(GLOB NodeHubResources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/data/*")
add_custom_command(
    TARGET nodehub
    PRE_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_CURRENT_SOURCE_DIR}/data"
    "$<TARGET_FILE_DIR:nodehub>/data"
)
