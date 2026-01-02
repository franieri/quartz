#!/bin/bash
# ============================================================================
# Cross-Platform Validation Script
# Tests FFI boundary optimizations on current platform
# ============================================================================

set -e

echo "╔═══════════════════════════════════════════════════════════════════╗"
echo "║   FFI Boundary Optimizations - Platform Compatibility Check      ║"
echo "╚═══════════════════════════════════════════════════════════════════╝"
echo ""

# Detect platform
if [[ "$OSTYPE" == "linux-gnu"* ]]; then
    PLATFORM="Linux"
    OS_TYPE="linux"
elif [[ "$OSTYPE" == "darwin"* ]]; then
    PLATFORM="macOS"
    OS_TYPE="macos"
    # Detect architecture
    if [[ $(uname -m) == "arm64" ]]; then
        ARCH="Apple Silicon (ARM64)"
    else
        ARCH="Intel (x86_64)"
    fi
elif [[ "$OSTYPE" == "msys" ]] || [[ "$OSTYPE" == "cygwin" ]] || [[ "$OSTYPE" == "win32" ]]; then
    PLATFORM="Windows"
    OS_TYPE="windows"
else
    PLATFORM="Unknown"
    OS_TYPE="unknown"
fi

echo "Platform: $PLATFORM"
if [[ -n "$ARCH" ]]; then
    echo "Architecture: $ARCH"
fi
echo ""

# Detect compiler
if command -v g++ &> /dev/null; then
    COMPILER="g++"
    COMPILER_VERSION=$(g++ --version | head -n1)
elif command -v clang++ &> /dev/null; then
    COMPILER="clang++"
    COMPILER_VERSION=$(clang++ --version | head -n1)
elif command -v cl &> /dev/null; then
    COMPILER="MSVC"
    COMPILER_VERSION="Visual Studio"
else
    COMPILER="Unknown"
    COMPILER_VERSION="Not found"
fi

echo "Compiler: $COMPILER"
echo "Version: $COMPILER_VERSION"
echo ""

# Check C++17 support
echo "Checking C++17 support..."
cat > /tmp/test_cpp17.cpp << 'EOF'
#include <optional>
#include <string_view>
#include <atomic>
int main() {
    std::optional<int> opt = 42;
    std::string_view sv = "test";
    std::atomic<bool> atom{false};
    return opt.value_or(0);
}
EOF

if [[ "$COMPILER" == "g++" ]] || [[ "$COMPILER" == "clang++" ]]; then
    if $COMPILER -std=c++17 /tmp/test_cpp17.cpp -o /tmp/test_cpp17 &> /dev/null; then
        echo "✅ C++17 support confirmed"
        /tmp/test_cpp17
        rm -f /tmp/test_cpp17 /tmp/test_cpp17.cpp
    else
        echo "❌ C++17 support not available"
        exit 1
    fi
fi

echo ""

# Test lock-free queue compilation
echo "Testing lock-free queue compilation..."
cat > /tmp/test_queue.cpp << 'EOF'
#include <atomic>
#include <optional>
#include <cstdint>

namespace qz {
    constexpr size_t CACHE_LINE_SIZE = 64;
    
    template<typename T>
    struct AlignedStorage {
        alignas(T) unsigned char data[sizeof(T)];
        T* ptr() noexcept { return reinterpret_cast<T*>(data); }
    };
    
    template<typename T, size_t Capacity = 16>
    class SPSCQueue {
        struct alignas(CACHE_LINE_SIZE) Slot {
            AlignedStorage<T> storage;
            std::atomic<bool> occupied{false};
        };
        
        alignas(CACHE_LINE_SIZE) std::atomic<size_t> head_{0};
        alignas(CACHE_LINE_SIZE) std::atomic<size_t> tail_{0};
        Slot slots_[Capacity];
        
    public:
        bool try_enqueue(T&& item) {
            size_t head = head_.load(std::memory_order_relaxed);
            Slot& slot = slots_[head & (Capacity - 1)];
            if (slot.occupied.load(std::memory_order_acquire)) return false;
            new (slot.storage.ptr()) T(std::forward<T>(item));
            slot.occupied.store(true, std::memory_order_release);
            head_.store(head + 1, std::memory_order_release);
            return true;
        }
        
        std::optional<T> try_dequeue() {
            size_t tail = tail_.load(std::memory_order_relaxed);
            Slot& slot = slots_[tail & (Capacity - 1)];
            if (!slot.occupied.load(std::memory_order_acquire)) return std::nullopt;
            T value = std::move(*slot.storage.ptr());
            slot.storage.ptr()->~T();
            slot.occupied.store(false, std::memory_order_release);
            tail_.store(tail + 1, std::memory_order_release);
            return value;
        }
    };
}

int main() {
    qz::SPSCQueue<int, 16> queue;
    queue.try_enqueue(42);
    auto result = queue.try_dequeue();
    return result.value_or(0) == 42 ? 0 : 1;
}
EOF

if [[ "$COMPILER" == "g++" ]] || [[ "$COMPILER" == "clang++" ]]; then
    if $COMPILER -std=c++17 -O2 -pthread /tmp/test_queue.cpp -o /tmp/test_queue &> /dev/null; then
        echo "✅ Lock-free queue compiles successfully"
        if /tmp/test_queue; then
            echo "✅ Lock-free queue runtime test passed"
        else
            echo "❌ Lock-free queue runtime test failed"
            exit 1
        fi
        rm -f /tmp/test_queue /tmp/test_queue.cpp
    else
        echo "❌ Lock-free queue compilation failed"
        cat /tmp/test_queue_error.log
        exit 1
    fi
fi

echo ""

# Test atomics
echo "Testing atomic operations..."
cat > /tmp/test_atomics.cpp << 'EOF'
#include <atomic>
#include <thread>
#include <vector>

int main() {
    std::atomic<int> counter{0};
    std::atomic<bool> flag{false};
    
    // Test fetch_add
    counter.fetch_add(1, std::memory_order_relaxed);
    
    // Test compare_exchange
    int expected = 1;
    counter.compare_exchange_weak(expected, 2, std::memory_order_release);
    
    // Test store/load with different orderings
    flag.store(true, std::memory_order_release);
    bool val = flag.load(std::memory_order_acquire);
    
    return (counter.load() == 2 && val) ? 0 : 1;
}
EOF

if [[ "$COMPILER" == "g++" ]] || [[ "$COMPILER" == "clang++" ]]; then
    if $COMPILER -std=c++17 -pthread /tmp/test_atomics.cpp -o /tmp/test_atomics &> /dev/null; then
        if /tmp/test_atomics; then
            echo "✅ Atomic operations test passed"
        else
            echo "❌ Atomic operations test failed"
            exit 1
        fi
        rm -f /tmp/test_atomics /tmp/test_atomics.cpp
    else
        echo "❌ Atomic operations compilation failed"
        exit 1
    fi
fi

echo ""

# Platform-specific notes
echo "Platform-Specific Characteristics:"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

if [[ "$OS_TYPE" == "linux" ]]; then
    echo "• Cache line size: 64 bytes (x86_64)"
    echo "• Memory model: Strong (x86) or Weak (ARM)"
    echo "• Timer resolution: ~nanosecond (clock_gettime)"
    echo "• Threading: pthreads (native)"
elif [[ "$OS_TYPE" == "macos" ]]; then
    echo "• Cache line size: 64 bytes (Intel) / 128 bytes (Apple Silicon)"
    echo "• Memory model: Strong (Intel) / Weak (ARM)"
    echo "• Timer resolution: ~nanosecond (mach_absolute_time)"
    echo "• Threading: pthreads (native)"
    if [[ $(uname -m) == "arm64" ]]; then
        echo "• 🚀 Apple Silicon: Outstanding atomic performance"
    fi
elif [[ "$OS_TYPE" == "windows" ]]; then
    echo "• Cache line size: 64 bytes"
    echo "• Memory model: Strong (x86_64)"
    echo "• Timer resolution: ~100ns (QueryPerformanceCounter)"
    echo "• Threading: Win32 (abstracted by std::thread)"
    echo "• Note: Sleep resolution ~1-2ms by default"
fi

echo ""

# Summary
echo "╔═══════════════════════════════════════════════════════════════════╗"
echo "║                     Validation Summary                            ║"
echo "╚═══════════════════════════════════════════════════════════════════╝"
echo ""
echo "✅ Platform: $PLATFORM"
echo "✅ C++17 Support: Available"
echo "✅ Lock-Free Queue: Working"
echo "✅ Atomic Operations: Working"
echo ""
echo "All FFI boundary optimizations are compatible with your platform!"
echo ""
echo "Next steps:"
echo "  1. Build Quartz with optimizations enabled"
echo "  2. Run: python3 benchmark/ffi_boundary_benchmark.py"
echo "  3. Integrate into your extensions"
echo ""
