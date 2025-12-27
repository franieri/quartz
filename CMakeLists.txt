cmake_minimum_required(VERSION 3.10)

project(Quartz VERSION 0.1.0 LANGUAGES CXX)

# ============================================================================
# Compiler Configuration
# ============================================================================
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Platform detection
if(APPLE)
    set(PLATFORM_NAME "macOS")
elseif(UNIX AND NOT APPLE)
    set(PLATFORM_NAME "Linux")
elseif(WIN32)
    set(PLATFORM_NAME "Windows")
else()
    set(PLATFORM_NAME "Unknown")
endif()

message(STATUS "Building for platform: ${PLATFORM_NAME}")

# Symbol visibility
if(NOT WIN32)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fvisibility=default")
endif()

# Performance optimizations
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    message(STATUS "Release build - enabling optimizations")
    if(MSVC)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /O2 /DNDEBUG")
    else()
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -DNDEBUG")
        # LTO/IPO support
        include(CheckIPOSupported)
        check_ipo_supported(RESULT ipo_supported OUTPUT ipo_error)
        if(ipo_supported)
            message(STATUS "IPO/LTO enabled")
            set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
        else()
            message(STATUS "IPO/LTO not supported: ${ipo_error}")
        endif()
    endif()
else()
    message(STATUS "Debug build - moderate optimizations")
    if(MSVC)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /O1")
    else()
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2 -g")
    endif()
endif()

# Platform-specific compiler flags
if(UNIX AND NOT APPLE)
    # Linux-specific flags
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread")
endif()

# ============================================================================
# Build Configuration
# ============================================================================

# Keep outputs in ./build/ (scripts and tooling assume this)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

# RPATH settings for dynamic library loading
if(APPLE)
    set(CMAKE_INSTALL_RPATH "@executable_path")
    set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
elseif(UNIX)
    set(CMAKE_INSTALL_RPATH "$ORIGIN")
    set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
endif()

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

# Set library properties
set_target_properties(qz-core PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    OUTPUT_NAME "qz-core"
)

# ============================================================================
# Main Executable
# ============================================================================
add_executable(quartz src/core/main.cpp)

# Platform-specific linking
if(APPLE)
    # macOS: Use -force_load to ensure all symbols from core library are available
    target_link_libraries(quartz PRIVATE -Wl,-force_load qz-core)
elseif(UNIX)
    # Linux: Use --whole-archive to ensure all symbols are available
    target_link_libraries(quartz PRIVATE 
        -Wl,--whole-archive 
        qz-core 
        -Wl,--no-whole-archive
    )
    # Link with dl for dynamic loading and pthread
    target_link_libraries(quartz PRIVATE dl pthread)
else()
    # Fallback for other platforms
    target_link_libraries(quartz PRIVATE qz-core)
endif()

# Set executable properties
set_target_properties(quartz PROPERTIES
    OUTPUT_NAME "quartz"
)

# ============================================================================
# Installation (optional)
# ============================================================================
install(TARGETS quartz qz-core
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)

install(DIRECTORY include/
    DESTINATION include
    FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp"
)

# ============================================================================
# Summary
# ============================================================================
message(STATUS "==============================================")
message(STATUS "Quartz Language Build Configuration")
message(STATUS "==============================================")
message(STATUS "Platform:       ${PLATFORM_NAME}")
message(STATUS "Build type:     ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ Standard:   ${CMAKE_CXX_STANDARD}")
message(STATUS "Compiler:       ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "Install prefix: ${CMAKE_INSTALL_PREFIX}")
message(STATUS "==============================================")