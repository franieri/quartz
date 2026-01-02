// ============================================================================
// Extension Function Profiler
// Lightweight timing infrastructure for measuring FFI/extension performance
// Cross-platform: Windows, Linux, macOS
// Uses std::chrono::high_resolution_clock for portable high-precision timing
// ============================================================================

#ifndef QZ_EXTENSION_PROFILER_H
#define QZ_EXTENSION_PROFILER_H

#include <chrono>
#include <string>
#include <unordered_map>
#include <atomic>
#include <mutex>
#include <cstdint>
#include <algorithm>

namespace qz {

// ============================================================================
// Function Timing Statistics
// ============================================================================

struct FunctionStats {
    std::atomic<uint64_t> callCount{0};
    std::atomic<uint64_t> totalTimeNs{0};
    std::atomic<uint64_t> minTimeNs{UINT64_MAX};
    std::atomic<uint64_t> maxTimeNs{0};
    
    void record(uint64_t durationNs) {
        callCount.fetch_add(1, std::memory_order_relaxed);
        totalTimeNs.fetch_add(durationNs, std::memory_order_relaxed);
        
        // Update min (relaxed CAS loop)
        uint64_t currentMin = minTimeNs.load(std::memory_order_relaxed);
        while (durationNs < currentMin && 
               !minTimeNs.compare_exchange_weak(currentMin, durationNs,
                                               std::memory_order_relaxed)) {}
        
        // Update max
        uint64_t currentMax = maxTimeNs.load(std::memory_order_relaxed);
        while (durationNs > currentMax && 
               !maxTimeNs.compare_exchange_weak(currentMax, durationNs,
                                               std::memory_order_relaxed)) {}
    }
    
    uint64_t avgTimeNs() const {
        uint64_t count = callCount.load(std::memory_order_relaxed);
        if (count == 0) return 0;
        return totalTimeNs.load(std::memory_order_relaxed) / count;
    }
    
    void reset() {
        callCount.store(0, std::memory_order_relaxed);
        totalTimeNs.store(0, std::memory_order_relaxed);
        minTimeNs.store(UINT64_MAX, std::memory_order_relaxed);
        maxTimeNs.store(0, std::memory_order_relaxed);
    }
};

// ============================================================================
// Extension Profiler
// - Tracks timing for extension functions
// - Minimal overhead using atomics
// - Thread-safe
// ============================================================================

class ExtensionProfiler {
public:
    static ExtensionProfiler& instance() {
        static ExtensionProfiler profiler;
        return profiler;
    }
    
    // Enable/disable profiling
    void setEnabled(bool enabled) {
        enabled_.store(enabled, std::memory_order_release);
    }
    
    bool isEnabled() const {
        return enabled_.load(std::memory_order_acquire);
    }
    
    // Record a function call timing
    void record(const std::string& functionName, uint64_t durationNs) {
        if (!enabled_.load(std::memory_order_acquire)) return;
        
        // Lazy initialization of stats entry
        std::lock_guard<std::mutex> lock(mutex_);
        stats_[functionName].record(durationNs);
    }
    
    // Get stats for a specific function
    FunctionStats getStats(const std::string& functionName) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = stats_.find(functionName);
        if (it != stats_.end()) {
            // Return a copy (atomic loads)
            FunctionStats copy;
            const FunctionStats& src = it->second;
            copy.callCount.store(src.callCount.load(std::memory_order_relaxed));
            copy.totalTimeNs.store(src.totalTimeNs.load(std::memory_order_relaxed));
            copy.minTimeNs.store(src.minTimeNs.load(std::memory_order_relaxed));
            copy.maxTimeNs.store(src.maxTimeNs.load(std::memory_order_relaxed));
            return copy;
        }
        return FunctionStats{};
    }
    
    // Get all stats
    std::unordered_map<std::string, FunctionStats> getAllStats() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::unordered_map<std::string, FunctionStats> result;
        for (const auto& [name, stats] : stats_) {
            FunctionStats copy;
            copy.callCount.store(stats.callCount.load(std::memory_order_relaxed));
            copy.totalTimeNs.store(stats.totalTimeNs.load(std::memory_order_relaxed));
            copy.minTimeNs.store(stats.minTimeNs.load(std::memory_order_relaxed));
            copy.maxTimeNs.store(stats.maxTimeNs.load(std::memory_order_relaxed));
            result[name] = copy;
        }
        return result;
    }
    
    // Reset all stats
    void reset() {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& [name, stats] : stats_) {
            stats.reset();
        }
    }
    
    // Clear all stats
    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        stats_.clear();
    }
    
    // Get top N slowest functions (by average time)
    std::vector<std::pair<std::string, FunctionStats>> getTopSlowest(size_t n) const {
        auto all = getAllStats();
        std::vector<std::pair<std::string, FunctionStats>> vec(all.begin(), all.end());
        
        // Sort by average time descending
        std::sort(vec.begin(), vec.end(), 
                  [](const auto& a, const auto& b) {
                      return a.second.avgTimeNs() > b.second.avgTimeNs();
                  });
        
        if (vec.size() > n) {
            vec.resize(n);
        }
        return vec;
    }
    
    // Get top N most called functions
    std::vector<std::pair<std::string, FunctionStats>> getTopCalled(size_t n) const {
        auto all = getAllStats();
        std::vector<std::pair<std::string, FunctionStats>> vec(all.begin(), all.end());
        
        std::sort(vec.begin(), vec.end(), 
                  [](const auto& a, const auto& b) {
                      return a.second.callCount.load() > b.second.callCount.load();
                  });
        
        if (vec.size() > n) {
            vec.resize(n);
        }
        return vec;
    }
    
private:
    ExtensionProfiler() : enabled_(false) {}
    
    std::atomic<bool> enabled_;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, FunctionStats> stats_;
};

// ============================================================================
// RAII Timer for Automatic Profiling
// ============================================================================

class ScopedTimer {
public:
    explicit ScopedTimer(const std::string& functionName) 
        : functionName_(functionName)
        , start_(std::chrono::high_resolution_clock::now()) {}
    
    ~ScopedTimer() {
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(
            end - start_).count();
        ExtensionProfiler::instance().record(functionName_, duration);
    }
    
private:
    std::string functionName_;
    std::chrono::time_point<std::chrono::high_resolution_clock> start_;
};

// ============================================================================
// Convenience Macros
// ============================================================================

// Profile an entire function
#define QZ_PROFILE_FUNCTION() \
    qz::ScopedTimer _qz_scoped_timer_(__FUNCTION__)

// Profile a named scope
#define QZ_PROFILE_SCOPE(name) \
    qz::ScopedTimer _qz_scoped_timer_##name(#name)

// Profile with custom name
#define QZ_PROFILE_NAMED(name) \
    qz::ScopedTimer _qz_scoped_timer_(name)

// Conditional profiling (only if enabled)
#define QZ_PROFILE_IF_ENABLED(name) \
    std::unique_ptr<qz::ScopedTimer> _qz_scoped_timer_; \
    if (qz::ExtensionProfiler::instance().isEnabled()) { \
        _qz_scoped_timer_ = std::make_unique<qz::ScopedTimer>(name); \
    }

} // namespace qz

#endif // QZ_EXTENSION_PROFILER_H
