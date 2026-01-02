// ============================================================================
// Integration Example: Applying FFI Boundary Optimizations
// Shows how to integrate lock-free queues, worker pools, and profiling
// into existing extension code
// ============================================================================

// STEP 1: Include the optimization headers
#include "qz/lockfree_queue.h"
#include "qz/extension_profiler.h"
#include "http_worker_pool.h"
#include "uwsgi_response_queue.h"

// ============================================================================
// BEFORE: Original http_functions.cpp pattern (simplified)
// ============================================================================

namespace http_before {

// Heavy dict building on calling thread (could be IO thread)
static Value buildHttpRequestDict_OLD(const http::HttpRequest& req, Runtime* runtime) {
    if (!runtime) return Value(0);
    
    std::unordered_map<std::string, Value> dict;
    
    // Many allocations and copies
    dict["method"] = Value(http::methodToString(req.method));  // String alloc
    dict["url"] = Value(req.url.toString());                   // String alloc
    dict["body"] = Value(req.body);                            // String copy
    
    // Nested dict building
    std::unordered_map<std::string, Value> headers;
    for (const auto& [name, value] : req.headers.entries()) {
        headers[name] = Value(value);  // Multiple allocs
    }
    dict["headers"] = runtime->makeDict(std::move(headers));
    
    return runtime->makeDict(std::move(dict));
}

// Extension function without profiling
Value http_request_post_OLD(const std::vector<Value>& args) {
    if (!global_runtime_ptr || args.empty()) {
        throw LanguageException("ArgumentError", "Invalid arguments");
    }
    
    http::HttpRequest req;
    req.method = http::Method::POST;
    req.url = http::URL::parse(std::get<std::string>(args[0]));
    
    if (args.size() > 1) {
        req.body = std::get<std::string>(args[1]);  // Copy
    }
    
    return buildHttpRequestDict_OLD(req, global_runtime_ptr);
}

} // namespace http_before

// ============================================================================
// AFTER: Optimized version with all techniques applied
// ============================================================================

namespace http_after {

// Optimized dict building with move semantics and profiling
static Value buildHttpRequestDict_NEW(http::HttpRequest&& req, Runtime* runtime) {
    QZ_PROFILE_NAMED("http.buildRequestDict");  // Track performance
    
    if (!runtime) return Value(0);
    
    std::unordered_map<std::string, Value> dict;
    dict.reserve(8);  // Preallocate for known size
    
    // Move strings instead of copying
    dict.emplace("method", Value(http::methodToString(req.method)));
    dict.emplace("url", Value(req.url.toString()));
    dict.emplace("body", Value(std::move(req.body)));  // MOVE
    
    // Profile nested operation separately
    {
        QZ_PROFILE_SCOPE(headers);
        std::unordered_map<std::string, Value> headers;
        headers.reserve(req.headers.size());  // Preallocate
        
        for (auto&& [name, value] : req.headers.entries()) {
            headers.emplace(name, Value(std::move(value)));  // MOVE
        }
        dict.emplace("headers", runtime->makeDict(std::move(headers)));
    }
    
    return runtime->makeDict(std::move(dict));
}

// Async version using worker pool for heavy operations
static void buildHttpRequestDictAsync_NEW(
    const http::HttpRequest& req,
    Runtime* runtime,
    std::function<void(Value)> callback)
{
    QZ_PROFILE_NAMED("http.buildRequestDictAsync");
    
    auto& pool = http::getWorkerPool();
    
    // Copy request for worker thread
    auto reqCopy = std::make_shared<http::HttpRequest>(req);
    
    http::WorkItem item;
    item.callback = [reqCopy, runtime, cb = std::move(callback)]() {
        // Heavy work on worker thread, not IO thread
        auto dict = buildHttpRequestDict_NEW(std::move(*reqCopy), runtime);
        cb(dict);
    };
    
    if (!pool.submit(std::move(item))) {
        // Queue full, fallback to inline
        callback(buildHttpRequestDict_NEW(http::HttpRequest(req), runtime));
    }
}

// Optimized extension function
Value http_request_post_NEW(const std::vector<Value>& args) {
    QZ_PROFILE_FUNCTION();  // Automatic function-level profiling
    
    if (!global_runtime_ptr || args.empty()) {
        throw LanguageException("ArgumentError", "Invalid arguments");
    }
    
    http::HttpRequest req;
    req.method = http::Method::POST;
    req.url = http::URL::parse(std::get<std::string>(args[0]));
    
    if (args.size() > 1) {
        req.body = std::move(const_cast<std::string&>(
            std::get<std::string>(args[1])));  // MOVE from args
    }
    
    // Use optimized builder with move
    return buildHttpRequestDict_NEW(std::move(req), global_runtime_ptr);
}

} // namespace http_after

// ============================================================================
// BEFORE: Original uwsgi_core.cpp sendResponse (simplified)
// ============================================================================

namespace uwsgi_before {

class UwsgiServer_OLD {
    std::unordered_map<std::string, Connection*> connections_;
    std::mutex connectionsMutex_;  // Contention point!
    
public:
    bool sendResponse(const std::string& connId, const HttpResponse& response) {
        std::lock_guard<std::mutex> lock(connectionsMutex_);  // BLOCKS
        
        auto it = connections_.find(connId);
        if (it == connections_.end()) return false;
        
        // Heavy serialization under lock
        std::string serialized = response.build();
        it->second->send(serialized);
        
        return true;
    }
};

} // namespace uwsgi_before

// ============================================================================
// AFTER: Optimized uwsgi_core.cpp with lock-free queue
// ============================================================================

namespace uwsgi_after {

class UwsgiServer_NEW {
    std::unordered_map<std::string, Connection*> connections_;
    std::mutex connectionsMutex_;  // Still needed for connection map
    uwsgi::ResponseQueue responseQueue_;  // NEW: Lock-free queue
    
public:
    // Non-blocking response submission
    bool sendResponse(const std::string& connId, const HttpResponse& response) {
        QZ_PROFILE_NAMED("uwsgi.sendResponse");
        
        // No lock, no blocking - just enqueue
        return responseQueue_.submit(connId, response);
    }
    
    // Process responses in event loop (single thread, no contention)
    int poll(int timeoutMs) {
        QZ_PROFILE_NAMED("uwsgi.poll");
        
        // ... epoll/kqueue event processing ...
        
        // Process pending responses (batch for efficiency)
        {
            QZ_PROFILE_SCOPE(process_responses);
            
            responseQueue_.processPending(
                [this](const std::string& connId, const HttpResponse& resp) {
                    // No lock needed - single-threaded in poll()
                    auto it = connections_.find(connId);
                    if (it != connections_.end()) {
                        std::string serialized = resp.build();
                        it->second->send(serialized);
                    }
                },
                32  // Process up to 32 responses per poll
            );
        }
        
        return eventsProcessed;
    }
};

} // namespace uwsgi_after

// ============================================================================
// MIGRATION GUIDE
// ============================================================================

/*

STEP-BY-STEP MIGRATION:

1. Add includes at top of file:
   #include "qz/lockfree_queue.h"
   #include "qz/extension_profiler.h"
   #include "http_worker_pool.h"  // If using worker pool

2. Initialize worker pool at extension load:
   void init_http_extension() {
       auto& pool = http::getWorkerPool();
       pool.start();  // Start with default workers
   }

3. Add profiling to extension functions:
   - Add QZ_PROFILE_FUNCTION() at start of function
   - Add QZ_PROFILE_SCOPE(name) for nested operations
   - Use QZ_PROFILE_NAMED("custom.name") for custom names

4. Optimize dict building:
   - Use .reserve() to preallocate capacity
   - Use std::move() to avoid string copies
   - Use .emplace() instead of operator[]
   - Pass by rvalue reference (T&&) when possible

5. For heavy operations (>10μs):
   - Submit to worker pool instead of inline
   - Provide callback for async completion
   - Handle queue-full fallback

6. Replace mutexes with lock-free queues:
   - For single consumer: use SPSCQueue
   - For multiple producers: use MPSCQueue
   - Process queue in event loop/main thread
   - Keep mutex for data structures that need it

7. Enable profiling in tests:
   qz::ExtensionProfiler::instance().setEnabled(true);
   
   // Run tests...
   
   auto stats = qz::ExtensionProfiler::instance().getAllStats();
   // Check for slow functions

8. Benchmark before and after:
   - Use benchmark/ scripts
   - Compare throughput and latency
   - Check CPU usage
   - Monitor queue depths

PERFORMANCE CHECKLIST:

[ ] Profiling added to all extension functions
[ ] Dict building uses move semantics
[ ] Heavy operations offloaded to workers
[ ] Lock contention eliminated where possible
[ ] Queue depths monitored
[ ] Benchmarks show improvement
[ ] No regressions in functionality
[ ] Documentation updated

*/

// ============================================================================
// EXAMPLE: Complete optimized extension registration
// ============================================================================

void register_optimized_http_extension(FunctionRegistry& reg) {
    // Initialize worker pool
    auto& pool = http::getWorkerPool();
    pool.start(2);  // 2 worker threads
    
    // Register profiling control functions
    reg.registerFunction("system.net.http.profiling.enable", 
        [](const std::vector<Value>&) -> Value {
            qz::ExtensionProfiler::instance().setEnabled(true);
            return Value(true);
        });
    
    reg.registerFunction("system.net.http.profiling.stats",
        [](const std::vector<Value>&) -> Value {
            auto stats = qz::ExtensionProfiler::instance().getTopSlowest(10);
            // ... convert to Quartz dict ...
            return Value(0);  // Placeholder
        });
    
    // Register optimized HTTP functions
    reg.registerFunction("system.net.http.request.post", http_after::http_request_post_NEW);
    
    // ... register other functions ...
}

// ============================================================================
// QUARTZ USAGE EXAMPLE
// ============================================================================

/*

// Enable profiling
system.net.http.profiling.enable()

// Run workload
for i in 1..10000 {
    let req = system.net.http.request.post("http://api.example.com/data", "payload")
    // ... use req ...
}

// Check performance stats
let stats = system.net.http.profiling.stats()
for name, data in stats {
    print(name, "avg:", data.avgTimeNs, "ns")
}

// Get slowest functions
let slowest = system.net.http.profiling.topSlowest(5)
for entry in slowest {
    print(entry.name, entry.avgTimeNs, "ns")
}

*/
