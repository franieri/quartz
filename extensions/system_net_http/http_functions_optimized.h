// ============================================================================
// Optimized HTTP Functions - Example Implementation
// Demonstrates FFI boundary optimization patterns
// ============================================================================

#ifndef HTTP_FUNCTIONS_OPTIMIZED_H
#define HTTP_FUNCTIONS_OPTIMIZED_H

#include "function_registry.h"
#include "runtime.h"
#include "http_types.h"
#include "http_worker_pool.h"
#include "qz/extension_profiler.h"
#include "qz/lockfree_queue.h"
#include <sstream>

namespace http {

// ============================================================================
// Optimized Dict Building with Profiling
// ============================================================================

// Optimized version with move semantics and profiling
static Value buildHttpRequestDictOptimized(http::HttpRequest&& req, Runtime* runtime) {
    QZ_PROFILE_NAMED("buildHttpRequestDict");
    
    if (!runtime) return Value(0);
    
    std::unordered_map<std::string, Value> dict;
    dict.reserve(8);  // Preallocate for known fields
    
    // Use move semantics to avoid copies
    dict.emplace("method", Value(http::methodToString(req.method)));
    dict.emplace("url", Value(req.url.toString()));
    dict.emplace("version", Value(http::versionToString(req.version)));
    dict.emplace("body", Value(std::move(req.body)));
    dict.emplace("timeoutMs", Value(req.timeoutMs));
    dict.emplace("followRedirects", Value(req.followRedirects));
    dict.emplace("maxRedirects", Value(req.maxRedirects));
    
    {
        QZ_PROFILE_SCOPE(headers_conversion);
        std::unordered_map<std::string, Value> headers;
        headers.reserve(req.headers.size());
        for (auto& [name, value] : req.headers.entries()) {
            headers.emplace(name, Value(std::move(value)));
        }
        dict.emplace("headers", runtime->makeDict(std::move(headers)));
    }
    
    return runtime->makeDict(std::move(dict));
}

// Async version using worker pool
static void buildHttpRequestDictAsync(
    const http::HttpRequest& req,
    Runtime* runtime,
    std::function<void(Value)> callback)
{
    QZ_PROFILE_NAMED("buildHttpRequestDictAsync");
    
    auto& pool = http::getWorkerPool();
    
    // Make a copy for the worker thread
    auto reqCopy = std::make_shared<http::HttpRequest>(req);
    
    http::WorkItem item;
    item.callback = [reqCopy, runtime, callback = std::move(callback)]() {
        // This runs on worker thread
        auto dict = buildHttpRequestDictOptimized(std::move(*reqCopy), runtime);
        callback(dict);
    };
    
    if (!pool.submit(std::move(item))) {
        // Fallback to inline if queue is full
        callback(buildHttpRequestDictOptimized(http::HttpRequest(req), runtime));
    }
}

// ============================================================================
// Optimized Response Dict Building
// ============================================================================

static Value buildHttpResponseDictOptimized(http::HttpResponse&& resp, Runtime* runtime) {
    QZ_PROFILE_NAMED("buildHttpResponseDict");
    
    if (!runtime) return Value(0);
    
    std::unordered_map<std::string, Value> dict;
    dict.reserve(14);  // All known fields
    
    // Move strings to avoid copies
    dict.emplace("version", Value(http::versionToString(resp.version)));
    dict.emplace("statusCode", Value(resp.statusCode));
    dict.emplace("statusText", Value(std::move(resp.reasonPhrase)));
    dict.emplace("body", Value(std::move(resp.body)));
    dict.emplace("elapsedTimeMs", Value(resp.elapsedTimeMs));
    dict.emplace("finalUrl", Value(std::move(resp.finalUrl)));
    dict.emplace("redirectCount", Value(resp.redirectCount));
    
    // Status helpers (no allocation, small overhead)
    dict.emplace("ok", Value(resp.isSuccessful()));
    dict.emplace("isInformational", Value(resp.isInformational()));
    dict.emplace("isSuccessful", Value(resp.isSuccessful()));
    dict.emplace("isRedirection", Value(resp.isRedirection()));
    dict.emplace("isClientError", Value(resp.isClientError()));
    dict.emplace("isServerError", Value(resp.isServerError()));
    dict.emplace("isError", Value(resp.isError()));
    
    {
        QZ_PROFILE_SCOPE(headers_conversion);
        std::unordered_map<std::string, Value> headers;
        headers.reserve(resp.headers.size());
        for (auto& [name, value] : resp.headers.entries()) {
            headers.emplace(name, Value(value));
        }
        dict.emplace("headers", runtime->makeDict(std::move(headers)));
    }
    
    return runtime->makeDict(std::move(dict));
}

// ============================================================================
// Optimized Request Creation Functions
// ============================================================================

static void registerOptimizedRequestFunctions(FunctionRegistry& reg) {
    
    // system.net.http.request.get - Optimized version
    reg.registerFunction("system.net.http.request.get", [](const std::vector<Value>& args) -> Value {
        QZ_PROFILE_FUNCTION();
        
        if (!global_runtime_ptr || args.empty() || !std::holds_alternative<std::string>(args[0])) {
            throw LanguageException("ArgumentError", "Invalid arguments");
        }
        
        http::HttpRequest req;
        req.method = http::Method::GET;
        req.url = http::URL::parse(std::get<std::string>(args[0]));
        
        // Preallocate headers
        req.headers.set(http::header::UserAgent, "Quartz/1.0");
        req.headers.set(http::header::Accept, "*/*");
        
        // Use optimized builder with move
        return buildHttpRequestDictOptimized(std::move(req), global_runtime_ptr);
    });
    
    // system.net.http.request.post - With profiling
    reg.registerFunction("system.net.http.request.post", [](const std::vector<Value>& args) -> Value {
        QZ_PROFILE_FUNCTION();
        
        if (!global_runtime_ptr || args.empty() || !std::holds_alternative<std::string>(args[0])) {
            throw LanguageException("ArgumentError", "Invalid arguments");
        }
        
        http::HttpRequest req;
        req.method = http::Method::POST;
        req.url = http::URL::parse(std::get<std::string>(args[0]));
        req.headers.set(http::header::UserAgent, "Quartz/1.0");
        req.headers.set(http::header::Accept, "*/*");
        
        if (args.size() > 1 && std::holds_alternative<std::string>(args[1])) {
            // Move body string
            req.body = std::get<std::string>(args[1]);
            req.headers.set(http::header::ContentLength, std::to_string(req.body.size()));
        }
        
        return buildHttpRequestDictOptimized(std::move(req), global_runtime_ptr);
    });
}

// ============================================================================
// Serialization Functions with Offloading
// ============================================================================

static void registerOptimizedSerializationFunctions(FunctionRegistry& reg) {
    
    // system.net.http.request.serialize - Offloaded version
    reg.registerFunction("system.net.http.request.serialize", [](const std::vector<Value>& args) -> Value {
        QZ_PROFILE_FUNCTION();
        
        if (!global_runtime_ptr || args.empty()) {
            throw LanguageException("ArgumentError", "Invalid arguments");
        }
        
        // For heavy serialization, consider offloading
        // Check size threshold
        const size_t OFFLOAD_THRESHOLD = 8192;  // 8KB
        
        // For now, inline small requests
        // TODO: Implement async version for large payloads
        
        return Value(std::string("serialized"));  // Placeholder
    });
}

// ============================================================================
// Performance Monitoring Functions
// ============================================================================

static void registerProfilingFunctions(FunctionRegistry& reg) {
    
    // system.net.http.profiling.enable
    reg.registerFunction("system.net.http.profiling.enable", [](const std::vector<Value>&) -> Value {
        qz::ExtensionProfiler::instance().setEnabled(true);
        return Value(true);
    });
    
    // system.net.http.profiling.disable
    reg.registerFunction("system.net.http.profiling.disable", [](const std::vector<Value>&) -> Value {
        qz::ExtensionProfiler::instance().setEnabled(false);
        return Value(true);
    });
    
    // system.net.http.profiling.stats
    reg.registerFunction("system.net.http.profiling.stats", [](const std::vector<Value>&) -> Value {
        if (!global_runtime_ptr) return Value(0);
        
        auto stats = qz::ExtensionProfiler::instance().getAllStats();
        
        std::unordered_map<std::string, Value> dict;
        for (const auto& [name, funcStats] : stats) {
            std::unordered_map<std::string, Value> statsDict;
            statsDict["callCount"] = Value(static_cast<int>(funcStats.callCount.load()));
            statsDict["avgTimeNs"] = Value(static_cast<int>(funcStats.avgTimeNs()));
            statsDict["minTimeNs"] = Value(static_cast<int>(funcStats.minTimeNs.load()));
            statsDict["maxTimeNs"] = Value(static_cast<int>(funcStats.maxTimeNs.load()));
            
            dict[name] = global_runtime_ptr->makeDict(std::move(statsDict));
        }
        
        return global_runtime_ptr->makeDict(std::move(dict));
    });
    
    // system.net.http.profiling.reset
    reg.registerFunction("system.net.http.profiling.reset", [](const std::vector<Value>&) -> Value {
        qz::ExtensionProfiler::instance().reset();
        return Value(true);
    });
    
    // system.net.http.profiling.topSlowest
    reg.registerFunction("system.net.http.profiling.topSlowest", [](const std::vector<Value>& args) -> Value {
        if (!global_runtime_ptr) return Value(0);
        
        size_t n = 10;
        if (!args.empty() && std::holds_alternative<int>(args[0])) {
            n = std::get<int>(args[0]);
        }
        
        auto top = qz::ExtensionProfiler::instance().getTopSlowest(n);
        
        std::vector<Value> results;
        for (const auto& [name, stats] : top) {
            std::unordered_map<std::string, Value> item;
            item["name"] = Value(name);
            item["avgTimeNs"] = Value(static_cast<int>(stats.avgTimeNs()));
            item["callCount"] = Value(static_cast<int>(stats.callCount.load()));
            results.push_back(global_runtime_ptr->makeDict(std::move(item)));
        }
        
        return global_runtime_ptr->makeArray(std::move(results));
    });
}

} // namespace http

#endif // HTTP_FUNCTIONS_OPTIMIZED_H
