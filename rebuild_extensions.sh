#!/bin/bash

################################################################################
# Quartz Language - Extensions Rebuild Script
#
# This script rebuilds one or more extensions without rebuilding the core.
# Useful for development when only extension code has changed.
#
# Usage: bash rebuild_extensions.sh [options] [extension_name]
#   (no arguments)           - Rebuild all extensions
#   extension_name           - Rebuild specific extension (e.g., system_io)
#   --list                   - List all available extensions
#   --clean                  - Remove extension build artifacts first
#   --verbose                - Show detailed build output
#
# Project Structure:
#   extensions/              - Extension directories
#   extensions/system_io/    - System I/O extension
#   extensions/sample_ext/   - Sample extension
#   build/extensions/        - Built extension libraries
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
VERBOSE=false
CLEAN_BUILD=false
TARGET_EXTENSION=""
LIST_ONLY=false

while [[ $# -gt 0 ]]; do
    case $1 in
        --verbose)
            VERBOSE=true
            shift
            ;;
        --clean)
            CLEAN_BUILD=true
            shift
            ;;
        --list)
            LIST_ONLY=true
            shift
            ;;
        --help|-h)
            echo "Quartz Language - Extensions Rebuild Script"
            echo ""
            echo "Usage: bash rebuild_extensions.sh [options] [extension_name]"
            echo ""
            echo "Options:"
            echo "  (no arguments)           - Rebuild all extensions"
            echo "  extension_name           - Rebuild specific extension (e.g., system_io)"
            echo "  --list                   - List all available extensions"
            echo "  --clean                  - Remove extension build artifacts first"
            echo "  --verbose                - Show detailed build output"
            echo "  --help, -h               - Show this help message"
            echo ""
            echo "Examples:"
            echo "  bash rebuild_extensions.sh              # Rebuild all"
            echo "  bash rebuild_extensions.sh --list       # List available"
            echo "  bash rebuild_extensions.sh system_io    # Rebuild specific"
            echo "  bash rebuild_extensions.sh --clean      # Clean rebuild all"
            exit 0
            ;;
        --*)
            echo "Unknown option: $1"
            exit 1
            ;;
        *)
            TARGET_EXTENSION="$1"
            shift
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

# Function to discover available extensions
discover_extensions() {
    local extensions=()
    for ext_dir in extensions/*/; do
        if [ -d "$ext_dir" ]; then
            local ext_name=$(basename "$ext_dir")
            # Skip CMakeFiles directory
            if [[ ! "$ext_name" =~ ^CMakeFiles$ ]]; then
                extensions+=("$ext_name")
            fi
        fi
    done
    printf '%s\n' "${extensions[@]}"
}

# Function to validate extension exists
validate_extension() {
    local ext=$1
    if [ ! -d "extensions/$ext" ]; then
        print_error "Extension not found: extensions/$ext"
        echo "Available extensions:"
        discover_extensions | sed 's/^/  - /'
        exit 1
    fi
}

# Function to build an extension
build_extension() {
    local ext=$1
    print_status "Building extension: $ext"
    
    if [ "$VERBOSE" = true ]; then
        cmake --build build --target "$ext" --verbose
    else
        cmake --build build --target "$ext"
    fi
    
    print_success "Extension '$ext' built successfully"
}

# List available extensions
if [ "$LIST_ONLY" = true ]; then
    print_section "Available Extensions"
    extensions=$(discover_extensions)
    if [ -z "$extensions" ]; then
        print_error "No extensions found"
        exit 1
    fi
    echo "$extensions" | nl
    exit 0
fi

# Check if build directory and CMake are configured
if [ ! -f build/CMakeCache.txt ]; then
    print_error "CMake not configured yet. Run 'bash rebuild_core.sh' first."
    exit 1
fi

# Discover available extensions
extensions=$(discover_extensions)
if [ -z "$extensions" ]; then
    print_error "No extensions found in extensions/ directory"
    exit 1
fi

# Determine which extensions to build
if [ -z "$TARGET_EXTENSION" ]; then
    # Build all extensions
    print_section "Rebuilding All Extensions"
    
    # Clean if requested
    if [ "$CLEAN_BUILD" = true ]; then
        print_status "Cleaning extension build artifacts..."
        rm -rf build/extensions/*/lib*.dylib
        rm -rf build/extensions/*/CMakeFiles
    fi
    
    extension_count=0
    failed_count=0
    
    while IFS= read -r ext; do
        if build_extension "$ext" 2>/dev/null; then
            extension_count=$((extension_count + 1))
        else
            print_error "Failed to build extension: $ext"
            failed_count=$((failed_count + 1))
        fi
    done <<< "$extensions"
    
    echo ""
    print_section "Build Summary"
    echo "Built successfully: $extension_count"
    echo "Failed: $failed_count"
    
    if [ $failed_count -eq 0 ]; then
        print_success "All extensions rebuilt"
    else
        print_error "Some extensions failed to build"
        exit 1
    fi
else
    # Build specific extension
    validate_extension "$TARGET_EXTENSION"
    
    print_section "Rebuilding Extension: $TARGET_EXTENSION"
    
    if [ "$CLEAN_BUILD" = true ]; then
        print_status "Cleaning build artifacts for $TARGET_EXTENSION..."
        rm -rf build/extensions/$TARGET_EXTENSION/lib*.dylib
        rm -rf build/extensions/$TARGET_EXTENSION/CMakeFiles
    fi
    
    build_extension "$TARGET_EXTENSION"
    
    echo ""
    print_section "Build Complete"
    echo "Extension:  $TARGET_EXTENSION"
    echo "Location:   ./build/extensions/$TARGET_EXTENSION/lib$TARGET_EXTENSION.dylib"
    print_success "Extension rebuilt successfully"
fi

echo ""
echo "To test an extension, run:"
echo "  ./build/quartz samples/sample_import.qz"
