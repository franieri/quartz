cmake_minimum_required(VERSION 3.10)

project(Quartz VERSION 0.1.0 LANGUAGES CXX)

# ============================================================================
# Compiler Configuration
# ============================================================================
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fvisibility=default")

# Performance optimizations
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -DNDEBUG -flto")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -flto")
else()
    # Default optimization even in debug for better baseline performance
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2")
endif()

# ============================================================================
# Build Configuration
# ============================================================================
set(CMAKE_EXE_LINKER_FLAGS "-Wl,-all_load")

# Keep outputs in ./build/ (scripts and tooling assume this)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

# ============================================================================
# Include Directories
# ============================================================================
include_directories(include)
include_directories(include/core)

# ============================================================================
# Extensions
# ============================================================================
add_subdirectory(extensions)

# ============================================================================
# Core Library
# ============================================================================
file(GLOB_RECURSE CORE_SOURCES
    src/core/*.cpp
)

# Remove main.cpp from core sources (it's for the executable)
list(REMOVE_ITEM CORE_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/core/main.cpp)

add_library(qz-core SHARED ${CORE_SOURCES})

# ============================================================================
# Main Executable
# ============================================================================
add_executable(quartz src/core/main.cpp)
target_link_libraries(quartz qz-core)