#!/bin/bash

################################################################################
# Quartz Language - Core Rebuild Script
#
# This script rebuilds the core language and all extensions.
# It performs a clean reconfiguration of CMake and builds all targets.
#
# Usage: bash rebuild_core.sh [options]
#   (no options)     - Full rebuild of core and all extensions
#   --clean          - Remove build directory before rebuilding
#   --verbose        - Show detailed build output
#
# Project Structure:
#   src/core/        - Core language implementation
#   include/core/    - Core language headers
#   extensions/      - Dynamic extensions
#   build/           - Build output directory
################################################################################

set -e  # Exit on error

# Get script directory (root of project)
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Parse command line arguments
CLEAN_BUILD=false
VERBOSE=false

while [[ $# -gt 0 ]]; do
    case $1 in
        --clean)
            CLEAN_BUILD=true
            shift
            ;;
        --verbose)
            VERBOSE=true
            shift
            ;;
        --help|-h)
            echo "Quartz Language - Core Rebuild Script"
            echo ""
            echo "Usage: bash rebuild_core.sh [options]"
            echo ""
            echo "Options:"
            echo "  (no options)     - Full rebuild of core and all extensions"
            echo "  --clean          - Remove build directory before rebuilding"
            echo "  --verbose        - Show detailed build output"
            echo "  --help, -h       - Show this help message"
            echo ""
            echo "Examples:"
            echo "  bash rebuild_core.sh          # Standard rebuild"
            echo "  bash rebuild_core.sh --clean  # Clean rebuild"
            echo "  bash rebuild_core.sh --verbose # With detailed output"
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            exit 1
            ;;
    esac
done

# Function to print colored messages
print_status() {
    echo -e "${BLUE}➜${NC} $1"
}

print_success() {
    echo -e "${GREEN}✓${NC} $1"
}

print_error() {
    echo -e "${RED}✗${NC} $1"
}

print_section() {
    echo -e "\n${YELLOW}═══════════════════════════════════════${NC}"
    echo -e "${YELLOW}$1${NC}"
    echo -e "${YELLOW}═══════════════════════════════════════${NC}\n"
}

# Check if build directory exists
if [ "$CLEAN_BUILD" = true ]; then
    print_status "Removing old build directory..."
    rm -rf build
fi

# Create build directory if it doesn't exist
if [ ! -d build ]; then
    print_status "Creating build directory..."
    mkdir -p build
fi

# Clean CMake cache for fresh configuration
print_status "Cleaning CMake cache..."
rm -f build/CMakeCache.txt
rm -f build/cmake_install.cmake

# Configure with CMake
print_section "Configuring CMake"
if [ "$VERBOSE" = true ]; then
    cmake -S . -B build --verbose
else
    cmake -S . -B build
fi
print_success "CMake configuration complete"

# Build core library
print_section "Building Core Library"
print_status "Building qz-core (shared library)..."
if [ "$VERBOSE" = true ]; then
    cmake --build build --target qz-core --verbose
else
    cmake --build build --target qz-core
fi
print_success "Core library built"

# Build main executable
print_section "Building Main Executable"
print_status "Building quartz executable..."
if [ "$VERBOSE" = true ]; then
    cmake --build build --target quartz --verbose
else
    cmake --build build --target quartz
fi
print_success "Main executable built"

# Rebuild all extensions
print_section "Building Extensions"
echo "Discovering and building extensions..."

extension_count=0
for ext_dir in extensions/*/; do
    if [ -d "$ext_dir" ]; then
        ext_name=$(basename "$ext_dir")

        # Skip CMakeFiles and other non-extension directories
        if [[ ! "$ext_name" =~ ^CMakeFiles$ ]]; then
            print_status "Building extension: $ext_name"
            if [ "$VERBOSE" = true ]; then
                cmake --build build --target "$ext_name" --verbose
            else
                cmake --build build --target "$ext_name"
            fi
            print_success "Extension '$ext_name' built"
            extension_count=$((extension_count + 1))
        fi
    fi
done

if [ $extension_count -eq 0 ]; then
    print_error "No extensions found"
else
    print_success "$extension_count extension(s) built"
fi

# Summary
print_section "Build Summary"
echo "Build artifacts location: ./build/"
echo "Main executable:          ./build/quartz"
echo "Core library:             ./build/libqz-core.so"
echo "Extensions:               ./build/extensions/*/lib*.so"
echo ""
print_success "Full build completed successfully!"
echo ""
echo "To run a sample program:"
echo "  ./build/quartz samples/sample_import.qz"
echo ""
echo "To run with input:"
echo "  echo 'input' | ./build/quartz samples/sample_import.qz"
