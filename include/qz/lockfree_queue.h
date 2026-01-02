// ============================================================================
// Lock-Free Queue for Quartz Extensions
// Single-Producer Single-Consumer (SPSC) Queue
// Designed for efficient FFI/extension boundary communication
// Cross-platform: Windows, Linux, macOS
// ============================================================================

#ifndef QZ_LOCKFREE_QUEUE_H
#define QZ_LOCKFREE_QUEUE_H

#include <atomic>
#include <memory>
#include <optional>
#include <type_traits>
#include <new>
#include <cstdint>

namespace qz {

// ============================================================================
// Platform-specific cache line detection
// ============================================================================

#ifdef __cpp_lib_hardware_interference_size
    // C++17 feature (may not be available on all compilers)
    constexpr size_t CACHE_LINE_SIZE = std::hardware_destructive_interference_size;
#else
    // Conservative default that works on most platforms
    // x86/x64: 64 bytes
    // ARM64 (Apple Silicon): 128 bytes (we use 64 for compatibility)
    // Most modern CPUs: 64 bytes
    constexpr size_t CACHE_LINE_SIZE = 64;
#endif

// ============================================================================
// Cross-platform aligned storage
// std::aligned_storage_t is deprecated in C++23, so we use a custom solution
// ============================================================================

template<typename T>
struct AlignedStorage {
    alignas(T) unsigned char data[sizeof(T)];
    
    T* ptr() noexcept { return reinterpret_cast<T*>(data); }
    const T* ptr() const noexcept { return reinterpret_cast<const T*>(data); }
};

// ============================================================================
// SPSC (Single Producer, Single Consumer) Lock-Free Queue
// - Optimized for one producer thread and one consumer thread
// - No locks, uses atomic operations with memory ordering
// - Fixed capacity, fails to enqueue if full (non-blocking)
// - Cache-line aligned to avoid false sharing
// - Cross-platform: Windows, Linux, macOS
// ============================================================================

template<typename T, size_t Capacity = 1024>
class SPSCQueue {
    static_assert(Capacity > 0 && (Capacity & (Capacity - 1)) == 0, 
                  "Capacity must be a power of 2");
    
    // Slot holds either a value (when occupied) or nothing
    struct alignas(CACHE_LINE_SIZE) Slot {
        AlignedStorage<T> storage;
        std::atomic<bool> occupied{false};
        
        T* ptr() noexcept { return storage.ptr(); }
        const T* ptr() const noexcept { return storage.ptr(); }
    };
    
public:
    SPSCQueue() : head_(0), tail_(0) {
        for (size_t i = 0; i < Capacity; ++i) {
            slots_[i].occupied.store(false, std::memory_order_relaxed);
        }
    }
    
    ~SPSCQueue() {
        // Drain any remaining items
        while (auto item = try_dequeue()) {
            // Destructor will be called automatically
        }
    }
    
    // Non-copyable, non-movable
    SPSCQueue(const SPSCQueue&) = delete;
    SPSCQueue& operator=(const SPSCQueue&) = delete;
    SPSCQueue(SPSCQueue&&) = delete;
    SPSCQueue& operator=(SPSCQueue&&) = delete;
    
    // ========================================================================
    // Producer API (called from one thread only)
    // ========================================================================
    
    // Try to enqueue an item. Returns false if queue is full.
    // Does not block - suitable for IO threads.
    template<typename U>
    bool try_enqueue(U&& item) {
        const size_t head = head_.load(std::memory_order_relaxed);
        Slot& slot = slots_[head & (Capacity - 1)];
        
        // Check if slot is available (not occupied)
        if (slot.occupied.load(std::memory_order_acquire)) {
            return false;  // Queue is full
        }
        
        // Construct item in-place
        new (slot.ptr()) T(std::forward<U>(item));
        
        // Mark as occupied - this synchronizes with consumer
        slot.occupied.store(true, std::memory_order_release);
        
        // Advance head
        head_.store(head + 1, std::memory_order_release);
        
        return true;
    }
    
    // Emplace-construct an item in the queue
    template<typename... Args>
    bool try_emplace(Args&&... args) {
        const size_t head = head_.load(std::memory_order_relaxed);
        Slot& slot = slots_[head & (Capacity - 1)];
        
        if (slot.occupied.load(std::memory_order_acquire)) {
            return false;
        }
        
        new (slot.ptr()) T(std::forward<Args>(args)...);
        slot.occupied.store(true, std::memory_order_release);
        head_.store(head + 1, std::memory_order_release);
        
        return true;
    }
    
    // ========================================================================
    // Consumer API (called from one thread only)
    // ========================================================================
    
    // Try to dequeue an item. Returns nullopt if queue is empty.
    // Does not block - suitable for worker threads polling.
    std::optional<T> try_dequeue() {
        const size_t tail = tail_.load(std::memory_order_relaxed);
        Slot& slot = slots_[tail & (Capacity - 1)];
        
        // Check if slot has data
        if (!slot.occupied.load(std::memory_order_acquire)) {
            return std::nullopt;  // Queue is empty
        }
        
        // Move out the value
        T value = std::move(*slot.ptr());
        
        // Destroy the object
        slot.ptr()->~T();
        
        // Mark slot as free - this synchronizes with producer
        slot.occupied.store(false, std::memory_order_release);
        
        // Advance tail
        tail_.store(tail + 1, std::memory_order_release);
        
        return value;
    }
    
    // ========================================================================
    // Status queries (can be called from either thread, but approximate)
    // ========================================================================
    
    // Approximate size (may be stale, but safe)
    size_t approximate_size() const {
        const size_t head = head_.load(std::memory_order_acquire);
        const size_t tail = tail_.load(std::memory_order_acquire);
        return head - tail;
    }
    
    // Check if queue appears empty (approximate)
    bool empty() const {
        return approximate_size() == 0;
    }
    
    // Check if queue appears full (approximate)
    bool full() const {
        return approximate_size() >= Capacity;
    }
    
    // Maximum capacity
    static constexpr size_t capacity() { return Capacity; }
    
private:
    // Padding to avoid false sharing between producer and consumer
    alignas(CACHE_LINE_SIZE) std::atomic<size_t> head_;  // Written by producer
    char padding1_[CACHE_LINE_SIZE - sizeof(std::atomic<size_t>)];
    
    alignas(CACHE_LINE_SIZE) std::atomic<size_t> tail_;  // Written by consumer
    char padding2_[CACHE_LINE_SIZE - sizeof(std::atomic<size_t>)];
    
    // Ring buffer of slots
    alignas(CACHE_LINE_SIZE) Slot slots_[Capacity];
};

// ============================================================================
// MPSC (Multi-Producer Single-Consumer) Lock-Free Queue
// - Multiple producers can enqueue concurrently
// - Single consumer dequeues
// - Uses atomic operations with CAS for producer coordination
// - More expensive than SPSC but supports multiple IO threads
// - Cross-platform: Windows, Linux, macOS
// ============================================================================

template<typename T, size_t Capacity = 1024>
class MPSCQueue {
    static_assert(Capacity > 0 && (Capacity & (Capacity - 1)) == 0, 
                  "Capacity must be a power of 2");
    
    struct alignas(CACHE_LINE_SIZE) Slot {
        AlignedStorage<T> storage;
        std::atomic<uint64_t> sequence{0};
        
        T* ptr() noexcept { return storage.ptr(); }
        const T* ptr() const noexcept { return storage.ptr(); }
    };
    
public:
    MPSCQueue() : head_(0), tail_(0) {
        for (size_t i = 0; i < Capacity; ++i) {
            slots_[i].sequence.store(i, std::memory_order_relaxed);
        }
    }
    
    ~MPSCQueue() {
        while (auto item = try_dequeue()) {
            // Destructor will be called
        }
    }
    
    MPSCQueue(const MPSCQueue&) = delete;
    MPSCQueue& operator=(const MPSCQueue&) = delete;
    
    // ========================================================================
    // Producer API (can be called from multiple threads)
    // ========================================================================
    
    template<typename U>
    bool try_enqueue(U&& item) {
        size_t head = head_.load(std::memory_order_relaxed);
        
        for (;;) {
            Slot& slot = slots_[head & (Capacity - 1)];
            size_t seq = slot.sequence.load(std::memory_order_acquire);
            intptr_t diff = static_cast<intptr_t>(seq) - static_cast<intptr_t>(head);
            
            if (diff == 0) {
                // Slot is available, try to claim it
                if (head_.compare_exchange_weak(head, head + 1, 
                                                std::memory_order_relaxed)) {
                    // Successfully claimed, now write data
                    new (slot.ptr()) T(std::forward<U>(item));
                    slot.sequence.store(head + 1, std::memory_order_release);
                    return true;
                }
                // CAS failed, retry with updated head
            } else if (diff < 0) {
                // Queue is full
                return false;
            } else {
                // Another producer is working on this slot, advance
                head = head_.load(std::memory_order_relaxed);
            }
        }
    }
    
    // ========================================================================
    // Consumer API (called from one thread only)
    // ========================================================================
    
    std::optional<T> try_dequeue() {
        size_t tail = tail_.load(std::memory_order_relaxed);
        Slot& slot = slots_[tail & (Capacity - 1)];
        size_t seq = slot.sequence.load(std::memory_order_acquire);
        intptr_t diff = static_cast<intptr_t>(seq) - static_cast<intptr_t>(tail + 1);
        
        if (diff == 0) {
            // Item is ready
            T value = std::move(*slot.ptr());
            slot.ptr()->~T();
            slot.sequence.store(tail + Capacity, std::memory_order_release);
            tail_.store(tail + 1, std::memory_order_release);
            return value;
        } else if (diff < 0) {
            // Queue is empty
            return std::nullopt;
        } else {
            // Producer hasn't finished writing yet (very rare)
            return std::nullopt;
        }
    }
    
    size_t approximate_size() const {
        const size_t head = head_.load(std::memory_order_acquire);
        const size_t tail = tail_.load(std::memory_order_acquire);
        return head - tail;
    }
    
    bool empty() const { return approximate_size() == 0; }
    bool full() const { return approximate_size() >= Capacity; }
    static constexpr size_t capacity() { return Capacity; }
    
private:
    alignas(CACHE_LINE_SIZE) std::atomic<size_t> head_;
    char padding1_[CACHE_LINE_SIZE - sizeof(std::atomic<size_t>)];
    
    alignas(CACHE_LINE_SIZE) std::atomic<size_t> tail_;
    char padding2_[CACHE_LINE_SIZE - sizeof(std::atomic<size_t>)];
    
    alignas(CACHE_LINE_SIZE) Slot slots_[Capacity];
};

} // namespace qz

#endif // QZ_LOCKFREE_QUEUE_H
