#!/bin/bash
set -e

# This script generates C++ source and header files from .proto definitions
# using the protoc compiler.

# Get the project root directory
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

# Define the output directory for the generated files
OUTPUT_DIR="$PROJECT_ROOT/applications/tests/protos"

# Create the output directory if it doesn't exist
mkdir -p "$OUTPUT_DIR"

echo "Generating protobuf files..."

# Generate files for parameter.proto
protoc --proto_path="$PROJECT_ROOT/applications/protos" --cpp_out="$OUTPUT_DIR" "$PROJECT_ROOT/applications/protos/parameter.proto"

# Generate files for test_primitives.proto
protoc --proto_path="$PROJECT_ROOT/applications/tests" --cpp_out="$OUTPUT_DIR" "$PROJECT_ROOT/applications/tests/test_primitives.proto"

echo "Protobuf files generated successfully in $OUTPUT_DIR"
