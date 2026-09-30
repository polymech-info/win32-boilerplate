# Define the test project
project(NodeEditorTests)

# Custom target to generate protobuf files
add_custom_target(generate_protos
    COMMAND sh ${IMGUI_NODE_EDITOR_ROOT_DIR}/scripts/generate_protos.sh
    WORKING_DIRECTORY ${IMGUI_NODE_EDITOR_ROOT_DIR}
    COMMENT "Generating protobuf files"
)

# Create the executable for the tests

set(CORE_TESTS_SOURCES
    "../nodehub/app.cpp"
    "../nodehub/app-logic.cpp"
    "../nodehub/app-render.cpp"
    "../nodehub/app-screenshot.cpp"
    "../nodehub/app-runtime.cpp"
    "../nodehub/containers/container.cpp"
    "../nodehub/containers/root_container.cpp"
    "../nodehub/core/graph_state.cpp"
    "../nodehub/core/Object.cpp"
    "../nodehub/core/Parameter.cpp"
    "../nodehub/core/ParameterIn.cpp"
    "../nodehub/core/ParameterOut.cpp"
    "../nodehub/core/ParameterManager.cpp"
    "../nodehub/core/BaseManager.cpp"
    "../nodehub/core/Context.cpp"
    "../nodehub/blocks/NodeEx.cpp"
    "../nodehub/blocks/block.cpp"
    "../nodehub/blocks/math_blocks.cpp"
    "../nodehub/blocks/logic_blocks.cpp"
    "../nodehub/blocks/start_block.cpp"
    "../nodehub/blocks/log_block.cpp"
    "../nodehub/blocks/parameter_operation.cpp"
    "../nodehub/blocks/group_block.cpp"
    "../nodehub/blocks/parameter_node.cpp"
    "../nodehub/blocks/block_edit_dialog.cpp"
    "../nodehub/blocks/parameter_edit_dialog.cpp"
    "../nodehub/utilities/node_renderer_base.cpp"
    "../nodehub/utilities/pathfinding.cpp"
    "../nodehub/utilities/edge_editing.cpp"
    "../nodehub/utilities/pin_renderer.cpp"
    "../nodehub/utilities/style_manager.cpp"
    "../nodehub/utilities/uuid_generator.cpp"
    "../nodehub/utilities/uuid_id_manager.cpp"
    "../nodehub/Logging.cpp"
    "../nodehub/stats.cpp"
    "main-test.cpp"
    "commons-test.cpp"
    "block_test.cpp"
    "parameter_test.cpp"
)

if (PROTOBUF_AVAILABLE)
    # list(APPEND CORE_TESTS_SOURCES "protobuf_test.cpp")
endif()

add_executable(core_tests ${CORE_TESTS_SOURCES})

# Add dependency to ensure protos are generated before building the tests
add_dependencies(core_tests generate_protos)

# Add /utf-8 compiler flag for spdlog on MSVC
if (MSVC)
    target_compile_options(core_tests PRIVATE /utf-8)
endif()

# Add definitions to match the main application
target_compile_definitions(core_tests PRIVATE FMT_HEADER_ONLY=1)

# Add include directories to find project headers
target_include_directories(core_tests PRIVATE
    "${CMAKE_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/base/include"
    "${CMAKE_SOURCE_DIR}/nodehub"
    "${CMAKE_CURRENT_SOURCE_DIR}/protos"
)

# Link libraries
target_link_libraries(core_tests
    PRIVATE
    gtest_main
    imgui_node_editor
    base
    imgui
)

# Link protobuf if available
if (PROTOBUF_AVAILABLE AND TARGET protobuf_interface)
    target_link_libraries(core_tests PRIVATE protobuf_interface)
    
    # Generate protobuf sources for testing
    set(TEST_PROTO_FILES "${CMAKE_CURRENT_SOURCE_DIR}/test_primitives.proto")
    protobuf_generate_cpp(TEST_PROTO_SRCS TEST_PROTO_HDRS ${TEST_PROTO_FILES})
    
    # Add generated files to executable
    target_sources(core_tests PRIVATE ${TEST_PROTO_SRCS})
    
    # Include generated directory
    target_include_directories(core_tests PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
endif()

# Enable test discovery for CTest
include(GoogleTest)
gtest_discover_tests(core_tests)
