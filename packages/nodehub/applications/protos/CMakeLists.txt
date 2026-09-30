cmake_minimum_required(VERSION 3.5)

project(nodehub_protos)

set(CMAKE_CXX_STANDARD 17)

find_package(Protobuf REQUIRED)

# Define proto files and output directory
set(PROTO_FILES "${CMAKE_CURRENT_SOURCE_DIR}/parameter.proto")
set(PROTO_GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY ${PROTO_GEN_DIR})

# Generate C++ sources
protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS ${PROTO_FILES})

# Create a library from the generated sources
add_library(protos ${PROTO_SRCS})
target_include_directories(protos PUBLIC ${PROTO_GEN_DIR} ${Protobuf_INCLUDE_DIRS})
target_link_libraries(protos PUBLIC protobuf::libprotobuf)

# Make generated headers available to other targets
target_include_directories(protos INTERFACE $<BUILD_INTERFACE:${PROTO_GEN_DIR}>)
