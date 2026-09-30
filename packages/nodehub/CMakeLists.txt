cmake_minimum_required(VERSION 3.5)
project(NodeHub)

# ==============================================================================
# Dependencies
# ==============================================================================
include(FetchContent)

# Google Test (for C++ unit tests)
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/03597a01ee50ed33e9dfd640b249b4be3799d395.zip
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# Protocol Buffers
FetchContent_Declare(
    protobuf
    GIT_REPOSITORY https://github.com/protocolbuffers/protobuf.git
    GIT_TAG        v21.9
)
FetchContent_MakeAvailable(protobuf)

# ==============================================================================
# Add subdirectories
# ==============================================================================
add_subdirectory(applications/base)
add_subdirectory(applications/nodehub)
add_subdirectory(applications/protos)
if (BUILD_TESTING)
    add_subdirectory(applications/tests)
endif()
