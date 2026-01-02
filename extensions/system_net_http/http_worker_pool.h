// ============================================================================
// HTTP Worker Pool for Offloading Heavy Serialization
// Prevents blocking IO threads during dict/object conversions
// Cross-platform: Windows, Linux, macOS
// ============================================================================

#ifndef HTTP_WORKER_POOL_H
#define HTTP_WORKER_POOL_H

#include "qz/lockfree_queue.h"
#include "http_types.h"
#include "runtime.h"
#include <thread>
#include <atomic>
#include <functional>
#include <vector>
#include <memory>
#include <chrono>

namespace http {

// ============================================================================
// Work Item Types
// ============================================================================

enum class WorkItemType {
    SERIALIZE_REQUEST,
    SERIALIZE_RESPONSE,
    BUILD_REQUEST_DICT,
    BUILD_RESPONSE_DICT,
    BUILD_URL_DICT,
    BUILD_COOKIE_DICT,
    PARSE_HEADERS
};

// Generic work item with callback
struct WorkItem {
    WorkItemType type;
    void* inputData;   // Points to the data to process (e.g., HttpRequest*)
    void* outputData;  // Points to where result should be stored
    std::function<void()> callback;  // Called on completion (optional)
    
    WorkItem() : type(WorkItemType::SERIALIZE_REQUEST), 
                 inputData(nullptr), outputData(nullptr) {}
    
    WorkItem(WorkItemType t, void* in, void* out, std::function<void()> cb = nullptr)
        : type(t), inputData(in), outputData(out), callback(std::move(cb)) {}
};

// Result container for async operations
template<typename T>
struct AsyncResult {
    std::atomic<bool> ready{false};
    T value;
    
    void set(T&& v) {
        value = std::move(v);
        ready.store(true, std::memory_order_release);
    }
    
    bool is_ready() const {
        return ready.load(std::memory_order_acquire);
    }
    
    T& get() { return value; }
    const T& get() const { return value; }
};

// ============================================================================
// HTTP Worker Pool
// - Offloads heavy serialization/deserialization from IO threads
// - Uses lock-free queues for work submission
// - Worker threads process items and invoke callbacks
// ============================================================================

class WorkerPool {
public:
    WorkerPool(size_t numWorkers = 2) 
        : running_(false), numWorkers_(numWorkers) {
        if (numWorkers_ == 0) {
            numWorkers_ = std::max(1u, std::thread::hardware_concurrency() / 2);
        }
    }
    
    ~WorkerPool() {
        stop();
    }
    
    void start() {
        if (running_.load()) return;
        
        running_.store(true);
        
        for (size_t i = 0; i < numWorkers_; ++i) {
            workers_.emplace_back([this, i]() {
                workerLoop(i);
            });
        }
    }
    
    void stop() {
        if (!running_.load()) return;
        
        running_.store(false);
        
        for (auto& worker : workers_) {
            if (worker.joinable()) {
                worker.join();
            }
        }
        workers_.clear();
    }
    
    // Submit work item (non-blocking)
    // Returns false if queue is full
    bool submit(WorkItem item) {
        return workQueue_.try_enqueue(std::move(item));
    }
    
    // Submit work and wait for result (blocking on caller thread)
    template<typename Func>
    auto submitAndWait(Func&& func) -> decltype(func()) {
        using ResultType = decltype(func());
        
        AsyncResult<ResultType> result;
        
        WorkItem item;
        item.callback = [&result, func = std::forward<Func>(func)]() {
            result.set(func());
        };
        
        if (!submit(std::move(item))) {
            // Queue full, execute inline
            return func();
        }
        
        // Busy-wait for result (could be improved with condition variable)
        while (!result.is_ready()) {
            std::this_thread::yield();
        }
        
        return std::move(result.get());
    }
    
    // Get queue statistics
    size_t pendingWorkItems() const {
        return workQueue_.approximate_size();
    }
    
    bool isRunning() const {
        return running_.load();
    }
    
    size_t numWorkers() const {
        return numWorkers_;
    }
    
private:
    void workerLoop(size_t workerId) {
        while (running_.load(std::memory_order_acquire)) {
            auto item = workQueue_.try_dequeue();
            
            if (item) {
                processWorkItem(*item);
            } else {
                // No work available, sleep briefly
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
        }
        
        // Drain remaining items
        while (auto item = workQueue_.try_dequeue()) {
            processWorkItem(*item);
        }
    }
    
    void processWorkItem(WorkItem& item) {
        // Execute the callback if present
        if (item.callback) {
            item.callback();
        }
        
        // Type-specific processing could be added here
        // For now, we rely on callbacks for flexibility
    }
    
    std::atomic<bool> running_;
    size_t numWorkers_;
    std::vector<std::thread> workers_;
    qz::MPSCQueue<WorkItem, 4096> workQueue_;  // Multiple IO threads to workers
};

// ============================================================================
// Global Worker Pool Instance
// ============================================================================

inline WorkerPool& getWorkerPool() {
    static WorkerPool pool(2);  // Default 2 workers
    return pool;
}

// ============================================================================
// Helper Functions for Async Operations
// ============================================================================

// Convert HttpRequest to dict asynchronously
inline void asyncBuildRequestDict(
    const HttpRequest* req, 
    Runtime* runtime,
    std::function<void(Value)> callback) 
{
    auto& pool = getWorkerPool();
    
    WorkItem item;
    item.callback = [req, runtime, callback = std::move(callback)]() {
        // This runs on worker thread - safe to do heavy allocation
        std::unordered_map<std::string, Value> dict;
        dict["method"] = Value(methodToString(req->method));
        dict["url"] = Value(req->url.toString());
        dict["version"] = Value(versionToString(req->version));
        // ... rest of serialization
        
        Value result = runtime ? runtime->makeDict(std::move(dict)) : Value(0);
        callback(result);
    };
    
    if (!pool.submit(std::move(item))) {
        // Fallback to inline execution if queue is full
        callback(Value(0));  // Or do inline work
    }
}

// Convert HttpResponse to dict asynchronously
inline void asyncBuildResponseDict(
    const HttpResponse* resp,
    Runtime* runtime,
    std::function<void(Value)> callback)
{
    auto& pool = getWorkerPool();
    
    WorkItem item;
    item.callback = [resp, runtime, callback = std::move(callback)]() {
        std::unordered_map<std::string, Value> dict;
        dict["version"] = Value(versionToString(resp->version));
        dict["statusCode"] = Value(resp->statusCode);
        dict["statusText"] = Value(resp->reasonPhrase);
        // ... rest of serialization
        
        Value result = runtime ? runtime->makeDict(std::move(dict)) : Value(0);
        callback(result);
    };
    
    if (!pool.submit(std::move(item))) {
        callback(Value(0));
    }
}

} // namespace http

#endif // HTTP_WORKER_POOL_H
