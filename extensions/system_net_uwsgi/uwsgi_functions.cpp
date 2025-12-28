// ============================================================================
// system.net.uwsgi - Quartz Function Bindings
// High-performance uWSGI server for nginx integration
// Integrates with system.async.eventloop for async request handling
// ============================================================================

#include "function_registry.h"
#include "runtime.h"
#include "uwsgi_types.h"
#include <sstream>

using namespace uwsgi;

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

static inline bool isDictRef(const Value& v) {
    return std::holds_alternative<DictRef>(v);
}

static inline bool isLambda(const Value& v) {
    if (!std::holds_alternative<std::string>(v)) return false;
    const std::string& s = std::get<std::string>(v);
    return s.rfind("__lambda_", 0) == 0;
}

// ============================================================================
// Server Reference Type
// Format: "uwsgi:server_123"
// ============================================================================

static std::string makeServerRef(const std::string& id) {
    return "uwsgi:" + id;
}

static std::string extractServerId(const std::string& ref) {
    if (ref.substr(0, 6) == "uwsgi:") {
        return ref.substr(6);
    }
    return ref;
}

static bool isServerRef(const Value& v) {
    if (!isString(v)) return false;
    const std::string& s = std::get<std::string>(v);
    return s.substr(0, 6) == "uwsgi:";
}

// ============================================================================
// Callback Storage for Async Handlers
// ============================================================================

static std::unordered_map<std::string, Value> g_handlers;
static std::mutex g_handlerMutex;

static void storeHandler(const std::string& serverId, const Value& lambdaVal) {
    std::lock_guard<std::mutex> lock(g_handlerMutex);
    g_handlers[serverId] = lambdaVal;
}

static Value getHandler(const std::string& serverId) {
    std::lock_guard<std::mutex> lock(g_handlerMutex);
    auto it = g_handlers.find(serverId);
    return it != g_handlers.end() ? it->second : Value(std::string(""));
}

static void removeHandler(const std::string& serverId) {
    std::lock_guard<std::mutex> lock(g_handlerMutex);
    g_handlers.erase(serverId);
}

// ============================================================================
// Dict Conversion Helpers
// ============================================================================

static Value buildRequestDict(const HttpRequest& req, Runtime* runtime) {
    if (!runtime) return Value(0);
    
    std::unordered_map<std::string, Value> dict;
    
    dict["method"] = Value(req.method);
    dict["uri"] = Value(req.uri);
    dict["path"] = Value(req.path);
    dict["queryString"] = Value(req.queryString);
    dict["protocol"] = Value(req.protocol);
    dict["host"] = Value(req.host);
    dict["remoteAddr"] = Value(req.remoteAddr);
    dict["remotePort"] = Value(req.remotePort);
    dict["contentType"] = Value(req.contentType);
    dict["contentLength"] = Value(static_cast<int>(req.contentLength));
    dict["body"] = Value(req.body);
    dict["elapsedMs"] = Value(static_cast<int>(req.elapsedMs()));
    
    // Headers as nested dict
    std::unordered_map<std::string, Value> headersDict;
    for (const auto& [key, value] : req.headers) {
        headersDict[key] = Value(value);
    }
    dict["headers"] = runtime->makeDict(std::move(headersDict));
    
    // Query params as nested dict (if any)
    if (!req.queryString.empty()) {
        auto params = parseQueryString(req.queryString);
        std::unordered_map<std::string, Value> paramsDict;
        for (const auto& [key, value] : params) {
            paramsDict[key] = Value(value);
        }
        dict["params"] = runtime->makeDict(std::move(paramsDict));
    }
    
    // Raw uWSGI vars as nested dict
    std::unordered_map<std::string, Value> varsDict;
    for (const auto& [key, value] : req.uwsgiVars) {
        varsDict[key] = Value(value);
    }
    dict["uwsgiVars"] = runtime->makeDict(std::move(varsDict));
    
    return runtime->makeDict(std::move(dict));
}

static HttpResponse dictToResponse(const Value& v, Runtime* runtime) {
    HttpResponse resp;
    
    if (!isDictRef(v) || !runtime) {
        resp.statusCode = 200;
        resp.statusText = "OK";
        return resp;
    }
    
    auto* dict = runtime->getDict(std::get<DictRef>(v));
    if (!dict) return resp;
    
    // Status code
    if (dict->count("status") && isInt((*dict)["status"])) {
        resp.statusCode = std::get<int>((*dict)["status"]);
        resp.statusText = getStatusText(resp.statusCode);
    } else if (dict->count("statusCode") && isInt((*dict)["statusCode"])) {
        resp.statusCode = std::get<int>((*dict)["statusCode"]);
        resp.statusText = getStatusText(resp.statusCode);
    }
    
    // Status text override
    if (dict->count("statusText") && isString((*dict)["statusText"])) {
        resp.statusText = std::get<std::string>((*dict)["statusText"]);
    }
    
    // Body
    if (dict->count("body") && isString((*dict)["body"])) {
        resp.body = std::get<std::string>((*dict)["body"]);
    }
    
    // Headers
    if (dict->count("headers") && isDictRef((*dict)["headers"])) {
        auto* headersDict = runtime->getDict(std::get<DictRef>((*dict)["headers"]));
        if (headersDict) {
            for (const auto& [key, value] : *headersDict) {
                if (isString(value)) {
                    resp.headers[key] = std::get<std::string>(value);
                }
            }
        }
    }
    
    return resp;
}

static Value buildStatsDict(const ServerStats& stats, Runtime* runtime) {
    if (!runtime) return Value(0);
    
    std::unordered_map<std::string, Value> dict;
    
    dict["totalConnections"] = Value(static_cast<int>(stats.totalConnections.load()));
    dict["activeConnections"] = Value(static_cast<int>(stats.activeConnections.load()));
    dict["totalRequests"] = Value(static_cast<int>(stats.totalRequests.load()));
    dict["successResponses"] = Value(static_cast<int>(stats.successResponses.load()));
    dict["clientErrors"] = Value(static_cast<int>(stats.clientErrors.load()));
    dict["serverErrors"] = Value(static_cast<int>(stats.serverErrors.load()));
    dict["bytesReceived"] = Value(static_cast<int>(stats.bytesReceived.load()));
    dict["bytesSent"] = Value(static_cast<int>(stats.bytesSent.load()));
    dict["parseErrors"] = Value(static_cast<int>(stats.parseErrors.load()));
    dict["timeouts"] = Value(static_cast<int>(stats.timeouts.load()));
    dict["uptimeMs"] = Value(static_cast<int>(stats.uptimeMs()));
    dict["requestsPerSecond"] = Value(static_cast<int>(stats.requestsPerSecond()));
    
    return runtime->makeDict(std::move(dict));
}

// Overload for StatsSnapshot (copyable stats)
static Value buildStatsDict(const StatsSnapshot& stats, Runtime* runtime) {
    if (!runtime) return Value(0);
    
    std::unordered_map<std::string, Value> dict;
    
    dict["totalConnections"] = Value(static_cast<int>(stats.totalConnections));
    dict["activeConnections"] = Value(static_cast<int>(stats.activeConnections));
    dict["totalRequests"] = Value(static_cast<int>(stats.totalRequests));
    dict["successResponses"] = Value(static_cast<int>(stats.successResponses));
    dict["clientErrors"] = Value(static_cast<int>(stats.clientErrors));
    dict["serverErrors"] = Value(static_cast<int>(stats.serverErrors));
    dict["bytesReceived"] = Value(static_cast<int>(stats.bytesReceived));
    dict["bytesSent"] = Value(static_cast<int>(stats.bytesSent));
    dict["parseErrors"] = Value(static_cast<int>(stats.parseErrors));
    dict["timeouts"] = Value(static_cast<int>(stats.timeouts));
    dict["uptimeMs"] = Value(static_cast<int>(stats.uptimeMs));
    dict["requestsPerSecond"] = Value(static_cast<int>(stats.requestsPerSecond));
    
    return runtime->makeDict(std::move(dict));
}

static ServerConfig dictToConfig(const Value& v, Runtime* runtime) {
    ServerConfig config;
    
    if (!isDictRef(v) || !runtime) return config;
    
    auto* dict = runtime->getDict(std::get<DictRef>(v));
    if (!dict) return config;
    
    // Network settings
    if (dict->count("host") && isString((*dict)["host"])) {
        config.bindAddress = std::get<std::string>((*dict)["host"]);
    }
    if (dict->count("address") && isString((*dict)["address"])) {
        config.bindAddress = std::get<std::string>((*dict)["address"]);
    }
    if (dict->count("port") && isInt((*dict)["port"])) {
        config.port = std::get<int>((*dict)["port"]);
    }
    if (dict->count("socket") && isString((*dict)["socket"])) {
        config.socketPath = std::get<std::string>((*dict)["socket"]);
        config.useUnixSocket = true;
    }
    if (dict->count("backlog") && isInt((*dict)["backlog"])) {
        config.backlog = std::get<int>((*dict)["backlog"]);
    }
    
    // Connection settings
    if (dict->count("maxConnections") && isInt((*dict)["maxConnections"])) {
        config.maxConnections = std::get<int>((*dict)["maxConnections"]);
    }
    if (dict->count("timeout") && isInt((*dict)["timeout"])) {
        config.connectionTimeoutMs = std::get<int>((*dict)["timeout"]);
    }
    if (dict->count("keepAliveTimeout") && isInt((*dict)["keepAliveTimeout"])) {
        config.keepAliveTimeoutMs = std::get<int>((*dict)["keepAliveTimeout"]);
    }
    if (dict->count("keepAlive")) {
        if (isBool((*dict)["keepAlive"])) {
            config.enableKeepAlive = std::get<bool>((*dict)["keepAlive"]);
        } else if (isInt((*dict)["keepAlive"])) {
            config.enableKeepAlive = std::get<int>((*dict)["keepAlive"]) != 0;
        }
    }
    
    // Performance settings
    if (dict->count("tcpNoDelay")) {
        if (isBool((*dict)["tcpNoDelay"])) {
            config.tcpNoDelay = std::get<bool>((*dict)["tcpNoDelay"]);
        } else if (isInt((*dict)["tcpNoDelay"])) {
            config.tcpNoDelay = std::get<int>((*dict)["tcpNoDelay"]) != 0;
        }
    }
    if (dict->count("reuseAddr")) {
        if (isBool((*dict)["reuseAddr"])) {
            config.reuseAddr = std::get<bool>((*dict)["reuseAddr"]);
        } else if (isInt((*dict)["reuseAddr"])) {
            config.reuseAddr = std::get<int>((*dict)["reuseAddr"]) != 0;
        }
    }
    if (dict->count("reusePort")) {
        if (isBool((*dict)["reusePort"])) {
            config.reusePort = std::get<bool>((*dict)["reusePort"]);
        } else if (isInt((*dict)["reusePort"])) {
            config.reusePort = std::get<int>((*dict)["reusePort"]) != 0;
        }
    }
    
    // Buffer settings
    if (dict->count("maxBodySize") && isInt((*dict)["maxBodySize"])) {
        config.maxBodySize = std::get<int>((*dict)["maxBodySize"]);
    }
    
    // Debug
    if (dict->count("verbose")) {
        if (isBool((*dict)["verbose"])) {
            config.verbose = std::get<bool>((*dict)["verbose"]);
        } else if (isInt((*dict)["verbose"])) {
            config.verbose = std::get<int>((*dict)["verbose"]) != 0;
        }
    }
    
    return config;
}

// ============================================================================
// Server Lifecycle Functions
// ============================================================================

void register_uwsgi_server_functions(FunctionRegistry& reg) {
    
    // ------------------------------------------------------------------------
    // system.net.uwsgi.create(config?: dict) -> server
    // Creates a new uWSGI server
    // Config: {host, port, socket, backlog, maxConnections, timeout, keepAlive, etc.}
    // ------------------------------------------------------------------------
    reg.registerFunction("system.net.uwsgi.create", [](const std::vector<Value>& args) -> Value {
        ServerConfig config;
        
        if (!args.empty() && isDictRef(args[0]) && rt()) {
            config = dictToConfig(args[0], rt());
        }
        
        UwsgiServer* server = ServerManager::instance().createServer();
        if (!server) {
            throw LanguageException("UwsgiError", "Failed to create uWSGI server");
        }
        
        server->configure(config);
        
        return Value(makeServerRef(server->id()));
    });
    
    // ------------------------------------------------------------------------
    // system.net.uwsgi.configure(server: server, config: dict) -> bool
    // Configure server settings (before start)
    // ------------------------------------------------------------------------
    reg.registerFunction("system.net.uwsgi.configure", [](const std::vector<Value>& args) -> Value {
        if (args.size() < 2 || !isServerRef(args[0]) || !isDictRef(args[1])) {
            throw LanguageException("TypeError", 
                "system.net.uwsgi.configure: requires (server, config)");
        }
        
        std::string id = extractServerId(std::get<std::string>(args[0]));
        UwsgiServer* server = ServerManager::instance().getServer(id);
        if (!server) {
            throw LanguageException("UwsgiError", "Invalid server reference");
        }
        
        ServerConfig config = dictToConfig(args[1], rt());
        return Value(server->configure(config));
    });
    
    // ------------------------------------------------------------------------
    // system.net.uwsgi.start(server: server) -> bool
    // Starts the server
    // ------------------------------------------------------------------------
    reg.registerFunction("system.net.uwsgi.start", [](const std::vector<Value>& args) -> Value {
        if (args.empty() || !isServerRef(args[0])) {
            throw LanguageException("TypeError", 
                "system.net.uwsgi.start: requires server argument");
        }
        
        std::string id = extractServerId(std::get<std::string>(args[0]));
        UwsgiServer* server = ServerManager::instance().getServer(id);
        if (!server) {
            throw LanguageException("UwsgiError", "Invalid server reference");
        }
        
        if (!server->start()) {
            throw LanguageException("UwsgiError", "Failed to start server");
        }
        
        return Value(true);
    });
    
    // ------------------------------------------------------------------------
    // system.net.uwsgi.stop(server: server) -> bool
    // Stops the server
    // ------------------------------------------------------------------------
    reg.registerFunction("system.net.uwsgi.stop", [](const std::vector<Value>& args) -> Value {
        if (args.empty() || !isServerRef(args[0])) {
            throw LanguageException("TypeError", 
                "system.net.uwsgi.stop: requires server argument");
        }
        
        std::string id = extractServerId(std::get<std::string>(args[0]));
        UwsgiServer* server = ServerManager::instance().getServer(id);
        if (!server) {
            throw LanguageException("UwsgiError", "Invalid server reference");
        }
        
        server->stop();
        removeHandler(id);
        
        return Value(true);
    });
    
    // ------------------------------------------------------------------------
    // system.net.uwsgi.destroy(server: server) -> bool
    // Destroys the server completely
    // ------------------------------------------------------------------------
    reg.registerFunction("system.net.uwsgi.destroy", [](const std::vector<Value>& args) -> Value {
        if (args.empty() || !isServerRef(args[0])) {
            throw LanguageException("TypeError", 
                "system.net.uwsgi.destroy: requires server argument");
        }
        
        std::string id = extractServerId(std::get<std::string>(args[0]));
        removeHandler(id);
        return Value(ServerManager::instance().removeServer(id));
    });
    
    // ------------------------------------------------------------------------
    // system.net.uwsgi.isRunning(server: server) -> bool
    // Check if server is running
    // ------------------------------------------------------------------------
    reg.registerFunction("system.net.uwsgi.isRunning", [](const std::vector<Value>& args) -> Value {
        if (args.empty() || !isServerRef(args[0])) {
            throw LanguageException("TypeError", 
                "system.net.uwsgi.isRunning: requires server argument");
        }
        
        std::string id = extractServerId(std::get<std::string>(args[0]));
        UwsgiServer* server = ServerManager::instance().getServer(id);
        if (!server) {
            return Value(false);
        }
        
        return Value(server->isRunning());
    });
}

// ============================================================================
// Request Handler Functions
// ============================================================================

void register_uwsgi_handler_functions(FunctionRegistry& reg) {
    
    // ------------------------------------------------------------------------
    // system.net.uwsgi.onRequest(server: server, handler: fn) -> bool
    // Set request handler. Handler receives request dict and returns response dict.
    // Handler: fn(request) -> {status, headers, body}
    // ------------------------------------------------------------------------
    reg.registerFunction("system.net.uwsgi.onRequest", [](const std::vector<Value>& args) -> Value {
        if (args.size() < 2 || !isServerRef(args[0]) || !isLambda(args[1])) {
            throw LanguageException("TypeError", 
                "system.net.uwsgi.onRequest: requires (server, handler)");
        }
        
        std::string id = extractServerId(std::get<std::string>(args[0]));
        UwsgiServer* server = ServerManager::instance().getServer(id);
        if (!server) {
            throw LanguageException("UwsgiError", "Invalid server reference");
        }
        
        // Store handler lambda
        storeHandler(id, args[1]);
        
        // Set sync handler that invokes the Quartz lambda
        server->setHandler([id](const HttpRequest& req) -> HttpResponse {
            Value handler = getHandler(id);
            if (!rt()) {
                return HttpResponse::serverError("Runtime not available");
            }
            
            // Build request dict
            Value reqDict = buildRequestDict(req, rt());
            std::vector<Value> handlerArgs = {reqDict};
            
            try {
                // Invoke Quartz handler
                Value result = rt()->invokeLambdaValue(handler, handlerArgs);
                
                // Convert result to HttpResponse
                return dictToResponse(result, rt());
            } catch (const std::exception& e) {
                return HttpResponse::serverError(std::string("Handler error: ") + e.what());
            }
        });
        
        return Value(true);
    });
    
    // ------------------------------------------------------------------------
    // system.net.uwsgi.onRequestAsync(server: server, handler: fn) -> bool
    // Set async request handler. Handler receives (request, connectionId).
    // Must call system.net.uwsgi.respond() to send response.
    // Handler: fn(request, connId) -> void
    // ------------------------------------------------------------------------
    reg.registerFunction("system.net.uwsgi.onRequestAsync", [](const std::vector<Value>& args) -> Value {
        if (args.size() < 2 || !isServerRef(args[0]) || !isLambda(args[1])) {
            throw LanguageException("TypeError", 
                "system.net.uwsgi.onRequestAsync: requires (server, handler)");
        }
        
        std::string id = extractServerId(std::get<std::string>(args[0]));
        UwsgiServer* server = ServerManager::instance().getServer(id);
        if (!server) {
            throw LanguageException("UwsgiError", "Invalid server reference");
        }
        
        // Store handler lambda
        storeHandler(id, args[1]);
        
        // Set async handler
        server->setAsyncHandler([id](const HttpRequest& req, const std::string& connId) {
            Value handler = getHandler(id);
            if (!rt()) return;
            
            // Build request dict
            Value reqDict = buildRequestDict(req, rt());
            std::vector<Value> handlerArgs = {reqDict, Value(connId)};
            
            try {
                rt()->invokeLambdaValue(handler, handlerArgs);
            } catch (...) {
                // Async handler errors are silently ignored
            }
        });
        
        return Value(true);
    });
    
    // ------------------------------------------------------------------------
    // system.net.uwsgi.respond(server: server, connId: string, response: dict) -> bool
    // Send response for async request
    // ------------------------------------------------------------------------
    reg.registerFunction("system.net.uwsgi.respond", [](const std::vector<Value>& args) -> Value {
        if (args.size() < 3 || !isServerRef(args[0]) || !isString(args[1]) || !isDictRef(args[2])) {
            throw LanguageException("TypeError", 
                "system.net.uwsgi.respond: requires (server, connId, response)");
        }
        
        std::string serverId = extractServerId(std::get<std::string>(args[0]));
        UwsgiServer* server = ServerManager::instance().getServer(serverId);
        if (!server) {
            throw LanguageException("UwsgiError", "Invalid server reference");
        }
        
        const std::string& connId = std::get<std::string>(args[1]);
        HttpResponse response = dictToResponse(args[2], rt());
        
        return Value(server->sendResponse(connId, response));
    });
}

// ============================================================================
// Event Loop Integration Functions
// ============================================================================

void register_uwsgi_eventloop_functions(FunctionRegistry& reg) {
    
    // ------------------------------------------------------------------------
    // system.net.uwsgi.poll(server: server, timeout?: int) -> int
    // Process pending I/O events. Returns number of events processed.
    // Use this with system.async.eventloop for async operation.
    // ------------------------------------------------------------------------
    reg.registerFunction("system.net.uwsgi.poll", [](const std::vector<Value>& args) -> Value {
        if (args.empty() || !isServerRef(args[0])) {
            throw LanguageException("TypeError", 
                "system.net.uwsgi.poll: requires server argument");
        }
        
        std::string id = extractServerId(std::get<std::string>(args[0]));
        UwsgiServer* server = ServerManager::instance().getServer(id);
        if (!server) {
            throw LanguageException("UwsgiError", "Invalid server reference");
        }
        
        int timeout = 0;  // Non-blocking by default
        if (args.size() > 1 && isInt(args[1])) {
            timeout = std::get<int>(args[1]);
        }
        
        return Value(server->poll(timeout));
    });
    
    // ------------------------------------------------------------------------
    // system.net.uwsgi.fd(server: server) -> int
    // Get the server socket file descriptor.
    // Useful for integrating with system.async.eventloop.
    // ------------------------------------------------------------------------
    reg.registerFunction("system.net.uwsgi.fd", [](const std::vector<Value>& args) -> Value {
        if (args.empty() || !isServerRef(args[0])) {
            throw LanguageException("TypeError", 
                "system.net.uwsgi.fd: requires server argument");
        }
        
        std::string id = extractServerId(std::get<std::string>(args[0]));
        UwsgiServer* server = ServerManager::instance().getServer(id);
        if (!server) {
            throw LanguageException("UwsgiError", "Invalid server reference");
        }
        
        return Value(static_cast<int>(server->serverFd()));
    });
    
    // ------------------------------------------------------------------------
    // system.net.uwsgi.run(server: server) -> void
    // Run server in blocking mode (simple use case)
    // For production, use poll() with event loop instead.
    // ------------------------------------------------------------------------
    reg.registerFunction("system.net.uwsgi.run", [](const std::vector<Value>& args) -> Value {
        if (args.empty() || !isServerRef(args[0])) {
            throw LanguageException("TypeError", 
                "system.net.uwsgi.run: requires server argument");
        }
        
        std::string id = extractServerId(std::get<std::string>(args[0]));
        UwsgiServer* server = ServerManager::instance().getServer(id);
        if (!server) {
            throw LanguageException("UwsgiError", "Invalid server reference");
        }
        
        // Simple blocking run loop
        while (server->isRunning()) {
            server->poll(100);  // 100ms timeout
        }
        
        return Value(true);
    });
}

// ============================================================================
// Statistics Functions
// ============================================================================

void register_uwsgi_stats_functions(FunctionRegistry& reg) {
    
    // ------------------------------------------------------------------------
    // system.net.uwsgi.stats(server: server) -> dict
    // Get server statistics
    // ------------------------------------------------------------------------
    reg.registerFunction("system.net.uwsgi.stats", [](const std::vector<Value>& args) -> Value {
        if (!rt() || args.empty() || !isServerRef(args[0])) {
            throw LanguageException("TypeError", 
                "system.net.uwsgi.stats: requires server argument");
        }
        
        std::string id = extractServerId(std::get<std::string>(args[0]));
        UwsgiServer* server = ServerManager::instance().getServer(id);
        if (!server) {
            throw LanguageException("UwsgiError", "Invalid server reference");
        }
        
        return buildStatsDict(server->stats(), rt());
    });
    
    // ------------------------------------------------------------------------
    // system.net.uwsgi.resetStats(server: server) -> void
    // Reset server statistics
    // ------------------------------------------------------------------------
    reg.registerFunction("system.net.uwsgi.resetStats", [](const std::vector<Value>& args) -> Value {
        if (args.empty() || !isServerRef(args[0])) {
            throw LanguageException("TypeError", 
                "system.net.uwsgi.resetStats: requires server argument");
        }
        
        std::string id = extractServerId(std::get<std::string>(args[0]));
        UwsgiServer* server = ServerManager::instance().getServer(id);
        if (!server) {
            throw LanguageException("UwsgiError", "Invalid server reference");
        }
        
        server->resetStats();
        return Value(true);
    });
    
    // ------------------------------------------------------------------------
    // system.net.uwsgi.activeConnections(server: server) -> int
    // Get number of active connections
    // ------------------------------------------------------------------------
    reg.registerFunction("system.net.uwsgi.activeConnections", [](const std::vector<Value>& args) -> Value {
        if (args.empty() || !isServerRef(args[0])) {
            throw LanguageException("TypeError", 
                "system.net.uwsgi.activeConnections: requires server argument");
        }
        
        std::string id = extractServerId(std::get<std::string>(args[0]));
        UwsgiServer* server = ServerManager::instance().getServer(id);
        if (!server) {
            return Value(0);
        }
        
        return Value(static_cast<int>(server->activeConnections()));
    });
}

// ============================================================================
// Response Helper Functions
// ============================================================================

void register_uwsgi_response_functions(FunctionRegistry& reg) {
    
    // ------------------------------------------------------------------------
    // system.net.uwsgi.response.ok(body: string, contentType?: string) -> dict
    // Create a 200 OK response
    // ------------------------------------------------------------------------
    reg.registerFunction("system.net.uwsgi.response.ok", [](const std::vector<Value>& args) -> Value {
        if (!rt() || args.empty() || !isString(args[0])) {
            throw LanguageException("TypeError", 
                "system.net.uwsgi.response.ok: requires body string");
        }
        
        const std::string& body = std::get<std::string>(args[0]);
        std::string contentType = "text/plain";
        if (args.size() > 1 && isString(args[1])) {
            contentType = std::get<std::string>(args[1]);
        }
        
        std::unordered_map<std::string, Value> dict;
        dict["status"] = Value(200);
        dict["body"] = Value(body);
        
        std::unordered_map<std::string, Value> headers;
        headers["Content-Type"] = Value(contentType);
        headers["Content-Length"] = Value(std::to_string(body.size()));
        dict["headers"] = rt()->makeDict(std::move(headers));
        
        return rt()->makeDict(std::move(dict));
    });
    
    // ------------------------------------------------------------------------
    // system.net.uwsgi.response.json(body: string) -> dict
    // Create a JSON response
    // ------------------------------------------------------------------------
    reg.registerFunction("system.net.uwsgi.response.json", [](const std::vector<Value>& args) -> Value {
        if (!rt() || args.empty() || !isString(args[0])) {
            throw LanguageException("TypeError", 
                "system.net.uwsgi.response.json: requires body string");
        }
        
        const std::string& body = std::get<std::string>(args[0]);
        
        std::unordered_map<std::string, Value> dict;
        dict["status"] = Value(200);
        dict["body"] = Value(body);
        
        std::unordered_map<std::string, Value> headers;
        headers["Content-Type"] = Value("application/json");
        headers["Content-Length"] = Value(std::to_string(body.size()));
        dict["headers"] = rt()->makeDict(std::move(headers));
        
        return rt()->makeDict(std::move(dict));
    });
    
    // ------------------------------------------------------------------------
    // system.net.uwsgi.response.html(body: string) -> dict
    // Create an HTML response
    // ------------------------------------------------------------------------
    reg.registerFunction("system.net.uwsgi.response.html", [](const std::vector<Value>& args) -> Value {
        if (!rt() || args.empty() || !isString(args[0])) {
            throw LanguageException("TypeError", 
                "system.net.uwsgi.response.html: requires body string");
        }
        
        const std::string& body = std::get<std::string>(args[0]);
        
        std::unordered_map<std::string, Value> dict;
        dict["status"] = Value(200);
        dict["body"] = Value(body);
        
        std::unordered_map<std::string, Value> headers;
        headers["Content-Type"] = Value("text/html; charset=utf-8");
        headers["Content-Length"] = Value(std::to_string(body.size()));
        dict["headers"] = rt()->makeDict(std::move(headers));
        
        return rt()->makeDict(std::move(dict));
    });
    
    // ------------------------------------------------------------------------
    // system.net.uwsgi.response.error(status: int, message?: string) -> dict
    // Create an error response
    // ------------------------------------------------------------------------
    reg.registerFunction("system.net.uwsgi.response.error", [](const std::vector<Value>& args) -> Value {
        if (!rt() || args.empty() || !isInt(args[0])) {
            throw LanguageException("TypeError", 
                "system.net.uwsgi.response.error: requires status code");
        }
        
        int status = std::get<int>(args[0]);
        std::string message = getStatusText(status);
        if (args.size() > 1 && isString(args[1])) {
            message = std::get<std::string>(args[1]);
        }
        
        std::unordered_map<std::string, Value> dict;
        dict["status"] = Value(status);
        dict["body"] = Value(message);
        
        std::unordered_map<std::string, Value> headers;
        headers["Content-Type"] = Value("text/plain");
        headers["Content-Length"] = Value(std::to_string(message.size()));
        dict["headers"] = rt()->makeDict(std::move(headers));
        
        return rt()->makeDict(std::move(dict));
    });
    
    // ------------------------------------------------------------------------
    // system.net.uwsgi.response.redirect(location: string, status?: int) -> dict
    // Create a redirect response
    // ------------------------------------------------------------------------
    reg.registerFunction("system.net.uwsgi.response.redirect", [](const std::vector<Value>& args) -> Value {
        if (!rt() || args.empty() || !isString(args[0])) {
            throw LanguageException("TypeError", 
                "system.net.uwsgi.response.redirect: requires location");
        }
        
        const std::string& location = std::get<std::string>(args[0]);
        int status = 302;
        if (args.size() > 1 && isInt(args[1])) {
            status = std::get<int>(args[1]);
        }
        
        std::unordered_map<std::string, Value> dict;
        dict["status"] = Value(status);
        dict["body"] = Value("");
        
        std::unordered_map<std::string, Value> headers;
        headers["Location"] = Value(location);
        dict["headers"] = rt()->makeDict(std::move(headers));
        
        return rt()->makeDict(std::move(dict));
    });
}

// ============================================================================
// Utility Functions
// ============================================================================

void register_uwsgi_util_functions(FunctionRegistry& reg) {
    
    // ------------------------------------------------------------------------
    // system.net.uwsgi.urlDecode(str: string) -> string
    // URL decode a string
    // ------------------------------------------------------------------------
    reg.registerFunction("system.net.uwsgi.urlDecode", [](const std::vector<Value>& args) -> Value {
        if (args.empty() || !isString(args[0])) {
            throw LanguageException("TypeError", 
                "system.net.uwsgi.urlDecode: requires string argument");
        }
        
        return Value(urlDecode(std::get<std::string>(args[0])));
    });
    
    // ------------------------------------------------------------------------
    // system.net.uwsgi.urlEncode(str: string) -> string
    // URL encode a string
    // ------------------------------------------------------------------------
    reg.registerFunction("system.net.uwsgi.urlEncode", [](const std::vector<Value>& args) -> Value {
        if (args.empty() || !isString(args[0])) {
            throw LanguageException("TypeError", 
                "system.net.uwsgi.urlEncode: requires string argument");
        }
        
        return Value(urlEncode(std::get<std::string>(args[0])));
    });
    
    // ------------------------------------------------------------------------
    // system.net.uwsgi.parseQuery(query: string) -> dict
    // Parse query string into dict
    // ------------------------------------------------------------------------
    reg.registerFunction("system.net.uwsgi.parseQuery", [](const std::vector<Value>& args) -> Value {
        if (!rt() || args.empty() || !isString(args[0])) {
            throw LanguageException("TypeError", 
                "system.net.uwsgi.parseQuery: requires string argument");
        }
        
        auto params = parseQueryString(std::get<std::string>(args[0]));
        std::unordered_map<std::string, Value> dict;
        
        for (const auto& [key, value] : params) {
            dict[key] = Value(value);
        }
        
        return rt()->makeDict(std::move(dict));
    });
    
    // ------------------------------------------------------------------------
    // system.net.uwsgi.mimeType(extension: string) -> string
    // Get MIME type for file extension
    // ------------------------------------------------------------------------
    reg.registerFunction("system.net.uwsgi.mimeType", [](const std::vector<Value>& args) -> Value {
        if (args.empty() || !isString(args[0])) {
            throw LanguageException("TypeError", 
                "system.net.uwsgi.mimeType: requires string argument");
        }
        
        std::string ext = std::get<std::string>(args[0]);
        if (!ext.empty() && ext[0] != '.') {
            ext = "." + ext;
        }
        
        return Value(getMimeType(ext));
    });
    
    // ------------------------------------------------------------------------
    // system.net.uwsgi.statusText(code: int) -> string
    // Get status text for HTTP status code
    // ------------------------------------------------------------------------
    reg.registerFunction("system.net.uwsgi.statusText", [](const std::vector<Value>& args) -> Value {
        if (args.empty() || !isInt(args[0])) {
            throw LanguageException("TypeError", 
                "system.net.uwsgi.statusText: requires integer argument");
        }
        
        return Value(getStatusText(std::get<int>(args[0])));
    });
}

// ============================================================================
// Manager Functions
// ============================================================================

void register_uwsgi_manager_functions(FunctionRegistry& reg) {
    
    // ------------------------------------------------------------------------
    // system.net.uwsgi.servers() -> array
    // Get list of all server IDs
    // ------------------------------------------------------------------------
    reg.registerFunction("system.net.uwsgi.servers", [](const std::vector<Value>& args) -> Value {
        if (!rt()) return Value(0);
        
        auto ids = ServerManager::instance().getServerIds();
        std::vector<Value> refs;
        
        for (const auto& id : ids) {
            refs.push_back(Value(makeServerRef(id)));
        }
        
        return rt()->makeArray(std::move(refs));
    });
    
    // ------------------------------------------------------------------------
    // system.net.uwsgi.stopAll() -> void
    // Stop all servers
    // ------------------------------------------------------------------------
    reg.registerFunction("system.net.uwsgi.stopAll", [](const std::vector<Value>& args) -> Value {
        ServerManager::instance().stopAll();
        return Value(true);
    });
    
    // ------------------------------------------------------------------------
    // system.net.uwsgi.totalStats() -> dict
    // Get aggregate statistics for all servers
    // ------------------------------------------------------------------------
    reg.registerFunction("system.net.uwsgi.totalStats", [](const std::vector<Value>& args) -> Value {
        if (!rt()) return Value(0);
        
        return buildStatsDict(ServerManager::instance().aggregateStats(), rt());
    });
}

// ============================================================================
// Extension Entry Point
// ============================================================================

extern "C" __attribute__((visibility("default")))
void init_extension(FunctionRegistry& reg) {
    // Register all function groups
    register_uwsgi_server_functions(reg);
    register_uwsgi_handler_functions(reg);
    register_uwsgi_eventloop_functions(reg);
    register_uwsgi_stats_functions(reg);
    register_uwsgi_response_functions(reg);
    register_uwsgi_util_functions(reg);
    register_uwsgi_manager_functions(reg);
}
