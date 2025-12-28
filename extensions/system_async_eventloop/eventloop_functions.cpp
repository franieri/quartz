// ============================================================================
// system.async.eventloop - Quartz Function Bindings
// Exposes event loop functionality to the Quartz language
// ============================================================================

#include "function_registry.h"
#include "runtime.h"
#include "eventloop_types.h"
#include <sstream>

using namespace eventloop;

static inline Runtime* rt() {
    return global_runtime_ptr;
}

static inline bool isString(const Value& v) {
    return std::holds_alternative<std::string>(v);
}

static inline bool isInt(const Value& v) {
    return std::holds_alternative<int>(v);
}

static inline bool isBool(const Value& v) {
    return std::holds_alternative<bool>(v);
}

static inline bool isLambda(const Value& v) {
    // In Quartz, lambdas are passed as strings with prefix "__lambda_"
    if (!std::holds_alternative<std::string>(v)) return false;
    const std::string& s = std::get<std::string>(v);
    return s.rfind("__lambda_", 0) == 0;
}

// ============================================================================
// Callback Management
// Store Quartz lambdas and invoke them from C++
// ============================================================================

static std::unordered_map<std::string, Value> g_callbacks;
static std::mutex g_callbackMutex;
static uint64_t g_callbackIdCounter = 0;

static std::string storeCallback(const Value& lambdaVal) {
    std::lock_guard<std::mutex> lock(g_callbackMutex);
    std::string id = "cb_" + std::to_string(g_callbackIdCounter++);
    g_callbacks[id] = lambdaVal;
    return id;
}

static Value getCallback(const std::string& id) {
    std::lock_guard<std::mutex> lock(g_callbackMutex);
    auto it = g_callbacks.find(id);
    if (it != g_callbacks.end()) {
        return it->second;
    }
    return Value(std::string(""));
}

static void removeCallback(const std::string& id) {
    std::lock_guard<std::mutex> lock(g_callbackMutex);
    g_callbacks.erase(id);
}

// Clear all callbacks (called on eventloop shutdown)
static void clearAllCallbacks() {
    std::lock_guard<std::mutex> lock(g_callbackMutex);
    g_callbacks.clear();
    g_callbackIdCounter = 0;
}

// Build event info as a Quartz dict
static Value buildEventDict(const Event& event, Runtime* runtime) {
    if (!runtime) return Value(0);
    
    std::unordered_map<std::string, Value> dict;
    
    std::string typeStr;
    switch (event.type) {
        case EventType::READ: typeStr = "read"; break;
        case EventType::WRITE: typeStr = "write"; break;
        case EventType::ACCEPT: typeStr = "accept"; break;
        case EventType::CLOSE: typeStr = "close"; break;
        case EventType::ERROR_EVENT: typeStr = "error"; break;
        case EventType::TIMEOUT: typeStr = "timeout"; break;
        case EventType::IDLE: typeStr = "idle"; break;
        case EventType::SIGNAL: typeStr = "signal"; break;
        case EventType::CUSTOM: typeStr = "custom"; break;
    }
    
    dict["type"] = Value(typeStr);
    dict["socketId"] = Value(event.socketId);
    dict["fd"] = Value(event.fd);
    dict["data"] = Value(event.data);
    dict["errorCode"] = Value(event.errorCode);
    dict["timerId"] = Value(static_cast<int>(event.timerId));
    
    return runtime->makeDict(std::move(dict));
}

// ============================================================================
// Event Loop Lifecycle Functions
// ============================================================================

void register_eventloop_lifecycle_functions(FunctionRegistry& reg) {
    
    // ------------------------------------------------------------------------
    // system.async.eventloop.init() -> bool
    // Initialize the event loop (called automatically, but can be explicit)
    // ------------------------------------------------------------------------
    reg.registerFunction("system.async.eventloop.init", [](const std::vector<Value>& args) -> Value {
        EventLoopConfig config;
        
        // Optional config from dict argument
        if (!args.empty() && std::holds_alternative<DictRef>(args[0]) && rt()) {
            auto* dict = rt()->getDict(std::get<DictRef>(args[0]));
            if (dict) {
                if (dict->count("maxEvents") && isInt((*dict)["maxEvents"])) {
                    config.maxEvents = std::get<int>((*dict)["maxEvents"]);
                }
                if (dict->count("timeout") && isInt((*dict)["timeout"])) {
                    config.defaultTimeoutMs = std::get<int>((*dict)["timeout"]);
                }
                if (dict->count("enableStats") && isBool((*dict)["enableStats"])) {
                    config.enableStats = std::get<bool>((*dict)["enableStats"]);
                }
            }
        }
        
        return Value(initEventLoop(config));
    });
    
    // ------------------------------------------------------------------------
    // system.async.eventloop.run() -> void
    // Run the event loop until stop() is called
    // ------------------------------------------------------------------------
    reg.registerFunction("system.async.eventloop.run", [](const std::vector<Value>& args) -> Value {
        // Set up callback invoker
        getEventLoop().setCallbackInvoker([](const std::string& callbackId, const Event& event) {
            Value callback = getCallback(callbackId);
            if (!rt()) return;
            
            Value eventDict = buildEventDict(event, rt());
            std::vector<Value> cbArgs = {eventDict};
            
            try {
                rt()->invokeLambdaValue(callback, cbArgs);
            } catch (...) {
                // Silently handle callback errors
            }
        });
        
        // Set up callback cleanup (removes callback from storage when no longer needed)
        getEventLoop().setCallbackCleanup([](const std::string& callbackId) {
            removeCallback(callbackId);
        });
        
        getEventLoop().run();
        return Value(true);
    });
    
    // ------------------------------------------------------------------------
    // system.async.eventloop.runOnce(timeout?: int) -> bool
    // Process one batch of events
    // ------------------------------------------------------------------------
    reg.registerFunction("system.async.eventloop.runOnce", [](const std::vector<Value>& args) -> Value {
        int timeout = -1;
        if (!args.empty() && isInt(args[0])) {
            timeout = std::get<int>(args[0]);
        }
        
        getEventLoop().setCallbackInvoker([](const std::string& callbackId, const Event& event) {
            Value callback = getCallback(callbackId);
            if (!rt()) return;
            
            Value eventDict = buildEventDict(event, rt());
            std::vector<Value> cbArgs = {eventDict};
            
            try {
                rt()->invokeLambdaValue(callback, cbArgs);
            } catch (...) {
            }
        });
        
        // Set up callback cleanup
        getEventLoop().setCallbackCleanup([](const std::string& callbackId) {
            removeCallback(callbackId);
        });
        
        getEventLoop().runOnce(timeout);
        return Value(true);
    });
    
    // ------------------------------------------------------------------------
    // system.async.eventloop.runFor(durationMs: int) -> bool
    // Run for a specific duration
    // ------------------------------------------------------------------------
    reg.registerFunction("system.async.eventloop.runFor", [](const std::vector<Value>& args) -> Value {
        if (args.empty() || !isInt(args[0])) {
            throw LanguageException("TypeError", 
                "system.async.eventloop.runFor: requires duration in milliseconds");
        }
        
        getEventLoop().setCallbackInvoker([](const std::string& callbackId, const Event& event) {
            Value callback = getCallback(callbackId);
            if (!rt()) return;
            
            Value eventDict = buildEventDict(event, rt());
            std::vector<Value> cbArgs = {eventDict};
            
            try {
                rt()->invokeLambdaValue(callback, cbArgs);
            } catch (...) {
            }
        });
        
        // Set up callback cleanup
        getEventLoop().setCallbackCleanup([](const std::string& callbackId) {
            removeCallback(callbackId);
        });
        
        int duration = std::get<int>(args[0]);
        getEventLoop().runFor(duration);
        return Value(true);
    });
    
    // ------------------------------------------------------------------------
    // system.async.eventloop.stop() -> bool
    // Stop the event loop
    // ------------------------------------------------------------------------
    reg.registerFunction("system.async.eventloop.stop", [](const std::vector<Value>& args) -> Value {
        getEventLoop().stop();
        // Clean up all stored callbacks to prevent memory leaks
        clearAllCallbacks();
        return Value(true);
    });
    
    // ------------------------------------------------------------------------
    // system.async.eventloop.isRunning() -> bool
    // Check if event loop is running
    // ------------------------------------------------------------------------
    reg.registerFunction("system.async.eventloop.isRunning", [](const std::vector<Value>& args) -> Value {
        return Value(getEventLoop().isRunning());
    });
}

// ============================================================================
// I/O Watcher Functions
// ============================================================================

void register_eventloop_io_functions(FunctionRegistry& reg) {
    
    // ------------------------------------------------------------------------
    // system.async.eventloop.watchRead(socketId: string, fd: int, callback: fn) -> bool
    // Watch a socket for read events
    // ------------------------------------------------------------------------
    reg.registerFunction("system.async.eventloop.watchRead", [](const std::vector<Value>& args) -> Value {
        if (args.size() < 3 || !isString(args[0]) || !isInt(args[1]) || !isLambda(args[2])) {
            throw LanguageException("TypeError", 
                "system.async.eventloop.watchRead: requires (socketId, fd, callback)");
        }
        
        const std::string& socketId = std::get<std::string>(args[0]);
        int fd = std::get<int>(args[1]);
        std::string callbackId = storeCallback(args[2]);
        
        return Value(getEventLoop().watchRead(socketId, fd, callbackId));
    });
    
    // ------------------------------------------------------------------------
    // system.async.eventloop.watchWrite(socketId: string, fd: int, callback: fn) -> bool
    // Watch a socket for write events
    // ------------------------------------------------------------------------
    reg.registerFunction("system.async.eventloop.watchWrite", [](const std::vector<Value>& args) -> Value {
        if (args.size() < 3 || !isString(args[0]) || !isInt(args[1]) || !isLambda(args[2])) {
            throw LanguageException("TypeError", 
                "system.async.eventloop.watchWrite: requires (socketId, fd, callback)");
        }
        
        const std::string& socketId = std::get<std::string>(args[0]);
        int fd = std::get<int>(args[1]);
        std::string callbackId = storeCallback(args[2]);
        
        return Value(getEventLoop().watchWrite(socketId, fd, callbackId));
    });
    
    // ------------------------------------------------------------------------
    // system.async.eventloop.watchAccept(socketId: string, fd: int, callback: fn) -> bool
    // Watch a server socket for incoming connections
    // ------------------------------------------------------------------------
    reg.registerFunction("system.async.eventloop.watchAccept", [](const std::vector<Value>& args) -> Value {
        if (args.size() < 3 || !isString(args[0]) || !isInt(args[1]) || !isLambda(args[2])) {
            throw LanguageException("TypeError", 
                "system.async.eventloop.watchAccept: requires (socketId, fd, callback)");
        }
        
        const std::string& socketId = std::get<std::string>(args[0]);
        int fd = std::get<int>(args[1]);
        std::string callbackId = storeCallback(args[2]);
        
        return Value(getEventLoop().watchAccept(socketId, fd, callbackId));
    });
    
    // ------------------------------------------------------------------------
    // system.async.eventloop.unwatch(socketId: string) -> bool
    // Stop watching a socket
    // ------------------------------------------------------------------------
    reg.registerFunction("system.async.eventloop.unwatch", [](const std::vector<Value>& args) -> Value {
        if (args.empty() || !isString(args[0])) {
            throw LanguageException("TypeError", 
                "system.async.eventloop.unwatch: requires socketId");
        }
        
        return Value(getEventLoop().unwatch(std::get<std::string>(args[0])));
    });
    
    // ------------------------------------------------------------------------
    // system.async.eventloop.onClose(socketId: string, callback: fn) -> bool
    // Set close event callback
    // ------------------------------------------------------------------------
    reg.registerFunction("system.async.eventloop.onClose", [](const std::vector<Value>& args) -> Value {
        if (args.size() < 2 || !isString(args[0]) || !isLambda(args[1])) {
            throw LanguageException("TypeError", 
                "system.async.eventloop.onClose: requires (socketId, callback)");
        }
        
        const std::string& socketId = std::get<std::string>(args[0]);
        std::string callbackId = storeCallback(args[1]);
        
        return Value(getEventLoop().onClose(socketId, callbackId));
    });
    
    // ------------------------------------------------------------------------
    // system.async.eventloop.onError(socketId: string, callback: fn) -> bool
    // Set error event callback
    // ------------------------------------------------------------------------
    reg.registerFunction("system.async.eventloop.onError", [](const std::vector<Value>& args) -> Value {
        if (args.size() < 2 || !isString(args[0]) || !isLambda(args[1])) {
            throw LanguageException("TypeError", 
                "system.async.eventloop.onError: requires (socketId, callback)");
        }
        
        const std::string& socketId = std::get<std::string>(args[0]);
        std::string callbackId = storeCallback(args[1]);
        
        return Value(getEventLoop().onError(socketId, callbackId));
    });
}

// ============================================================================
// Timer Functions
// ============================================================================

void register_eventloop_timer_functions(FunctionRegistry& reg) {
    
    // ------------------------------------------------------------------------
    // system.async.eventloop.setTimeout(delayMs: int, callback: fn) -> int
    // Schedule a one-time callback after delay
    // ------------------------------------------------------------------------
    reg.registerFunction("system.async.eventloop.setTimeout", [](const std::vector<Value>& args) -> Value {
        if (args.size() < 2 || !isInt(args[0]) || !isLambda(args[1])) {
            throw LanguageException("TypeError", 
                "system.async.eventloop.setTimeout: requires (delayMs, callback)");
        }
        
        int delayMs = std::get<int>(args[0]);
        std::string callbackId = storeCallback(args[1]);
        
        uint64_t timerId = getEventLoop().setTimeout(delayMs, callbackId);
        return Value(static_cast<int>(timerId));
    });
    
    // ------------------------------------------------------------------------
    // system.async.eventloop.setInterval(intervalMs: int, callback: fn) -> int
    // Schedule a repeating callback
    // ------------------------------------------------------------------------
    reg.registerFunction("system.async.eventloop.setInterval", [](const std::vector<Value>& args) -> Value {
        if (args.size() < 2 || !isInt(args[0]) || !isLambda(args[1])) {
            throw LanguageException("TypeError", 
                "system.async.eventloop.setInterval: requires (intervalMs, callback)");
        }
        
        int intervalMs = std::get<int>(args[0]);
        std::string callbackId = storeCallback(args[1]);
        
        uint64_t timerId = getEventLoop().setInterval(intervalMs, callbackId);
        return Value(static_cast<int>(timerId));
    });
    
    // ------------------------------------------------------------------------
    // system.async.eventloop.clearTimeout(timerId: int) -> bool
    // Cancel a timeout
    // ------------------------------------------------------------------------
    reg.registerFunction("system.async.eventloop.clearTimeout", [](const std::vector<Value>& args) -> Value {
        if (args.empty() || !isInt(args[0])) {
            throw LanguageException("TypeError", 
                "system.async.eventloop.clearTimeout: requires timerId");
        }
        
        return Value(getEventLoop().clearTimer(std::get<int>(args[0])));
    });
    
    // ------------------------------------------------------------------------
    // system.async.eventloop.clearInterval(timerId: int) -> bool
    // Cancel an interval
    // ------------------------------------------------------------------------
    reg.registerFunction("system.async.eventloop.clearInterval", [](const std::vector<Value>& args) -> Value {
        if (args.empty() || !isInt(args[0])) {
            throw LanguageException("TypeError", 
                "system.async.eventloop.clearInterval: requires timerId");
        }
        
        return Value(getEventLoop().clearTimer(std::get<int>(args[0])));
    });
}

// ============================================================================
// Immediate/NextTick/Idle Functions
// ============================================================================

void register_eventloop_scheduling_functions(FunctionRegistry& reg) {
    
    // ------------------------------------------------------------------------
    // system.async.eventloop.setImmediate(callback: fn) -> void
    // Schedule callback to run after current I/O events
    // ------------------------------------------------------------------------
    reg.registerFunction("system.async.eventloop.setImmediate", [](const std::vector<Value>& args) -> Value {
        if (args.empty() || !isLambda(args[0])) {
            throw LanguageException("TypeError", 
                "system.async.eventloop.setImmediate: requires callback");
        }
        
        std::string callbackId = storeCallback(args[0]);
        getEventLoop().setImmediate(callbackId);
        return Value(true);
    });
    
    // ------------------------------------------------------------------------
    // system.async.eventloop.nextTick(callback: fn) -> void
    // Schedule callback to run before next I/O poll (highest priority)
    // ------------------------------------------------------------------------
    reg.registerFunction("system.async.eventloop.nextTick", [](const std::vector<Value>& args) -> Value {
        if (args.empty() || !isLambda(args[0])) {
            throw LanguageException("TypeError", 
                "system.async.eventloop.nextTick: requires callback");
        }
        
        std::string callbackId = storeCallback(args[0]);
        getEventLoop().nextTick(callbackId);
        return Value(true);
    });
    
    // ------------------------------------------------------------------------
    // system.async.eventloop.onIdle(callback: fn) -> void
    // Schedule callback to run when no other events
    // ------------------------------------------------------------------------
    reg.registerFunction("system.async.eventloop.onIdle", [](const std::vector<Value>& args) -> Value {
        if (args.empty() || !isLambda(args[0])) {
            throw LanguageException("TypeError", 
                "system.async.eventloop.onIdle: requires callback");
        }
        
        std::string callbackId = storeCallback(args[0]);
        getEventLoop().onIdle(callbackId);
        return Value(true);
    });
    
    // ------------------------------------------------------------------------
    // system.async.eventloop.clearIdle() -> void
    // Clear all idle callbacks
    // ------------------------------------------------------------------------
    reg.registerFunction("system.async.eventloop.clearIdle", [](const std::vector<Value>& args) -> Value {
        getEventLoop().clearIdle();
        return Value(true);
    });
}

// ============================================================================
// Statistics Functions
// ============================================================================

void register_eventloop_stats_functions(FunctionRegistry& reg) {
    
    // ------------------------------------------------------------------------
    // system.async.eventloop.stats() -> dict
    // Get event loop statistics
    // ------------------------------------------------------------------------
    reg.registerFunction("system.async.eventloop.stats", [](const std::vector<Value>& args) -> Value {
        if (!rt()) return Value(0);
        
        const EventLoopStats& stats = getEventLoop().stats();
        
        std::unordered_map<std::string, Value> dict;
        dict["totalEvents"] = Value(static_cast<int>(stats.totalEvents.load()));
        dict["readEvents"] = Value(static_cast<int>(stats.readEvents.load()));
        dict["writeEvents"] = Value(static_cast<int>(stats.writeEvents.load()));
        dict["acceptEvents"] = Value(static_cast<int>(stats.acceptEvents.load()));
        dict["errorEvents"] = Value(static_cast<int>(stats.errorEvents.load()));
        dict["timerEvents"] = Value(static_cast<int>(stats.timerEvents.load()));
        dict["idleEvents"] = Value(static_cast<int>(stats.idleEvents.load()));
        dict["iterations"] = Value(static_cast<int>(stats.iterations.load()));
        dict["uptimeMs"] = Value(static_cast<int>(stats.uptimeMs()));
        
        return rt()->makeDict(std::move(dict));
    });
    
    // ------------------------------------------------------------------------
    // system.async.eventloop.resetStats() -> void
    // Reset statistics counters
    // ------------------------------------------------------------------------
    reg.registerFunction("system.async.eventloop.resetStats", [](const std::vector<Value>& args) -> Value {
        getEventLoop().resetStats();
        return Value(true);
    });
    
    // ------------------------------------------------------------------------
    // system.async.eventloop.watcherCount() -> int
    // Get number of active I/O watchers
    // ------------------------------------------------------------------------
    reg.registerFunction("system.async.eventloop.watcherCount", [](const std::vector<Value>& args) -> Value {
        return Value(static_cast<int>(getEventLoop().watcherCount()));
    });
    
    // ------------------------------------------------------------------------
    // system.async.eventloop.timerCount() -> int
    // Get number of active timers
    // ------------------------------------------------------------------------
    reg.registerFunction("system.async.eventloop.timerCount", [](const std::vector<Value>& args) -> Value {
        return Value(static_cast<int>(getEventLoop().timerCount()));
    });
}

// ============================================================================
// Extension Entry Point
// ============================================================================

extern "C" __attribute__((visibility("default")))
void init_extension(FunctionRegistry& reg) {
    // Initialize the event loop
    initEventLoop();
    
    // Register all function groups
    register_eventloop_lifecycle_functions(reg);
    register_eventloop_io_functions(reg);
    register_eventloop_timer_functions(reg);
    register_eventloop_scheduling_functions(reg);
    register_eventloop_stats_functions(reg);
}
