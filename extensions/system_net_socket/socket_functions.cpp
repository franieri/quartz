// ============================================================================
// system.net.socket - Quartz Function Bindings
// Exposes socket functionality to the Quartz language
// ============================================================================

#include "function_registry.h"
#include "runtime.h"
#include "socket_types.h"
#include <sstream>

using namespace qz_socket;

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

// ============================================================================
// Resource Limits
// ============================================================================

static constexpr size_t MAX_RECV_SIZE = 16 * 1024 * 1024;  // 16MB max receive buffer
static constexpr size_t MAX_SOCKETS = 10000;               // Max concurrent sockets

// ============================================================================
// Socket Ref Type (stored as string in Quartz)
// Format: "socket:sock_123"
// ============================================================================

static std::string makeSocketRef(const std::string& id) {
    return "socket:" + id;
}

static std::string extractSocketId(const std::string& ref) {
    if (ref.substr(0, 7) == "socket:") {
        return ref.substr(7);
    }
    return ref;
}

static bool isSocketRef(const Value& v) {
    if (!isString(v)) return false;
    const std::string& s = std::get<std::string>(v);
    return s.substr(0, 7) == "socket:";
}

// Build socket info as a Quartz dict
static Value buildSocketInfoDict(Socket* sock, Runtime* runtime) {
    if (!runtime || !sock) return Value(0);
    
    std::unordered_map<std::string, Value> dict;
    dict["id"] = Value(makeSocketRef(sock->id()));
    dict["type"] = Value(sock->type() == SocketType::TCP ? "tcp" : "udp");
    
    std::string stateStr;
    switch (sock->state()) {
        case SocketState::CLOSED: stateStr = "closed"; break;
        case SocketState::CREATED: stateStr = "created"; break;
        case SocketState::BOUND: stateStr = "bound"; break;
        case SocketState::LISTENING: stateStr = "listening"; break;
        case SocketState::CONNECTED: stateStr = "connected"; break;
        case SocketState::ERROR_STATE: stateStr = "error"; break;
    }
    dict["state"] = Value(stateStr);
    
    dict["localHost"] = Value(sock->localAddress().host);
    dict["localPort"] = Value(sock->localAddress().port);
    dict["remoteHost"] = Value(sock->remoteAddress().host);
    dict["remotePort"] = Value(sock->remoteAddress().port);
    dict["isValid"] = Value(sock->isValid());
    dict["nonBlocking"] = Value(sock->isNonBlocking());
    
    // Stats
    const auto& stats = sock->stats();
    dict["bytesReceived"] = Value(static_cast<int>(stats.bytesReceived.load()));
    dict["bytesSent"] = Value(static_cast<int>(stats.bytesSent.load()));
    dict["packetsReceived"] = Value(static_cast<int>(stats.packetsReceived.load()));
    dict["packetsSent"] = Value(static_cast<int>(stats.packetsSent.load()));
    dict["acceptedConnections"] = Value(static_cast<int>(stats.acceptedConnections.load()));
    dict["errors"] = Value(static_cast<int>(stats.errors.load()));
    dict["uptimeMs"] = Value(static_cast<int>(stats.uptimeMs()));
    dict["idleMs"] = Value(static_cast<int>(stats.idleMs()));
    
    return runtime->makeDict(std::move(dict));
}

// ============================================================================
// Socket Creation Functions
// ============================================================================

void register_socket_create_functions(FunctionRegistry& reg) {
    
    // ------------------------------------------------------------------------
    // system.net.socket.tcp() -> socket
    // Creates a new TCP socket
    // ------------------------------------------------------------------------
    reg.registerFunction("system.net.socket.tcp", [](const std::vector<Value>& args) -> Value {
        Socket* sock = SocketManager::instance().createSocket(SocketType::TCP);
        if (!sock) {
            throw LanguageException("SocketError", "Failed to create TCP socket");
        }
        return Value(makeSocketRef(sock->id()));
    });
    
    // ------------------------------------------------------------------------
    // system.net.socket.udp() -> socket
    // Creates a new UDP socket
    // ------------------------------------------------------------------------
    reg.registerFunction("system.net.socket.udp", [](const std::vector<Value>& args) -> Value {
        Socket* sock = SocketManager::instance().createSocket(SocketType::UDP);
        if (!sock) {
            throw LanguageException("SocketError", "Failed to create UDP socket");
        }
        return Value(makeSocketRef(sock->id()));
    });
    
    // ------------------------------------------------------------------------
    // system.net.socket.close(sock: socket) -> bool
    // Closes a socket
    // ------------------------------------------------------------------------
    reg.registerFunction("system.net.socket.close", [](const std::vector<Value>& args) -> Value {
        if (args.empty() || !isSocketRef(args[0])) {
            throw LanguageException("TypeError", "system.net.socket.close: argument must be a socket");
        }
        std::string id = extractSocketId(std::get<std::string>(args[0]));
        Socket* sock = SocketManager::instance().getSocket(id);
        if (!sock) {
            return Value(false);
        }
        sock->close();
        SocketManager::instance().removeSocket(id);
        return Value(true);
    });
    
    // ------------------------------------------------------------------------
    // system.net.socket.info(sock: socket) -> dict
    // Returns socket information
    // ------------------------------------------------------------------------
    reg.registerFunction("system.net.socket.info", [](const std::vector<Value>& args) -> Value {
        if (!rt() || args.empty() || !isSocketRef(args[0])) {
            throw LanguageException("TypeError", "system.net.socket.info: argument must be a socket");
        }
        std::string id = extractSocketId(std::get<std::string>(args[0]));
        Socket* sock = SocketManager::instance().getSocket(id);
        if (!sock) {
            throw LanguageException("SocketError", "Invalid socket");
        }
        return buildSocketInfoDict(sock, rt());
    });
}

// ============================================================================
// Socket Options Functions
// ============================================================================

void register_socket_option_functions(FunctionRegistry& reg) {
    
    // ------------------------------------------------------------------------
    // system.net.socket.setNonBlocking(sock: socket, nonBlocking: bool) -> bool
    // ------------------------------------------------------------------------
    reg.registerFunction("system.net.socket.setNonBlocking", [](const std::vector<Value>& args) -> Value {
        if (args.size() < 2 || !isSocketRef(args[0])) {
            throw LanguageException("TypeError", 
                "system.net.socket.setNonBlocking: requires (socket, bool)");
        }
        bool nonBlocking = false;
        if (isBool(args[1])) {
            nonBlocking = std::get<bool>(args[1]);
        } else if (isInt(args[1])) {
            nonBlocking = std::get<int>(args[1]) != 0;
        } else {
            throw LanguageException("TypeError", 
                "system.net.socket.setNonBlocking: second argument must be bool");
        }
        std::string id = extractSocketId(std::get<std::string>(args[0]));
        Socket* sock = SocketManager::instance().getSocket(id);
        if (!sock) {
            throw LanguageException("SocketError", "Invalid socket");
        }
        return Value(sock->setNonBlocking(nonBlocking));
    });
    
    // ------------------------------------------------------------------------
    // system.net.socket.setReuseAddr(sock: socket, reuse: bool) -> bool
    // ------------------------------------------------------------------------
    reg.registerFunction("system.net.socket.setReuseAddr", [](const std::vector<Value>& args) -> Value {
        if (args.size() < 2 || !isSocketRef(args[0])) {
            throw LanguageException("TypeError", 
                "system.net.socket.setReuseAddr: requires (socket, bool)");
        }
        bool reuse = false;
        if (isBool(args[1])) {
            reuse = std::get<bool>(args[1]);
        } else if (isInt(args[1])) {
            reuse = std::get<int>(args[1]) != 0;
        } else {
            throw LanguageException("TypeError", 
                "system.net.socket.setReuseAddr: second argument must be bool");
        }
        std::string id = extractSocketId(std::get<std::string>(args[0]));
        Socket* sock = SocketManager::instance().getSocket(id);
        if (!sock) {
            throw LanguageException("SocketError", "Invalid socket");
        }
        return Value(sock->setReuseAddr(reuse));
    });
    
    // ------------------------------------------------------------------------
    // system.net.socket.setNoDelay(sock: socket, noDelay: bool) -> bool
    // Disables Nagle's algorithm for TCP sockets
    // ------------------------------------------------------------------------
    reg.registerFunction("system.net.socket.setNoDelay", [](const std::vector<Value>& args) -> Value {
        if (args.size() < 2 || !isSocketRef(args[0])) {
            throw LanguageException("TypeError", 
                "system.net.socket.setNoDelay: requires (socket, bool)");
        }
        bool noDelay = false;
        if (isBool(args[1])) {
            noDelay = std::get<bool>(args[1]);
        } else if (isInt(args[1])) {
            noDelay = std::get<int>(args[1]) != 0;
        } else {
            throw LanguageException("TypeError", 
                "system.net.socket.setNoDelay: second argument must be bool");
        }
        std::string id = extractSocketId(std::get<std::string>(args[0]));
        Socket* sock = SocketManager::instance().getSocket(id);
        if (!sock) {
            throw LanguageException("SocketError", "Invalid socket");
        }
        return Value(sock->setNoDelay(noDelay));
    });
    
    // ------------------------------------------------------------------------
    // system.net.socket.setBufferSizes(sock: socket, recvSize: int, sendSize: int) -> bool
    // ------------------------------------------------------------------------
    reg.registerFunction("system.net.socket.setBufferSizes", [](const std::vector<Value>& args) -> Value {
        if (args.size() < 3 || !isSocketRef(args[0]) || !isInt(args[1]) || !isInt(args[2])) {
            throw LanguageException("TypeError", 
                "system.net.socket.setBufferSizes: requires (socket, recvSize, sendSize)");
        }
        std::string id = extractSocketId(std::get<std::string>(args[0]));
        Socket* sock = SocketManager::instance().getSocket(id);
        if (!sock) {
            throw LanguageException("SocketError", "Invalid socket");
        }
        return Value(sock->setBufferSizes(std::get<int>(args[1]), std::get<int>(args[2])));
    });
    
    // ------------------------------------------------------------------------
    // system.net.socket.setTimeouts(sock: socket, sendMs: int, recvMs: int) -> bool
    // Sets send and receive timeouts in milliseconds. 0 = no timeout.
    // Helps prevent Slowloris-style attacks.
    // ------------------------------------------------------------------------
    reg.registerFunction("system.net.socket.setTimeouts", [](const std::vector<Value>& args) -> Value {
        if (args.size() < 3 || !isSocketRef(args[0]) || !isInt(args[1]) || !isInt(args[2])) {
            throw LanguageException("TypeError", 
                "system.net.socket.setTimeouts: requires (socket, sendTimeoutMs, recvTimeoutMs)");
        }
        std::string id = extractSocketId(std::get<std::string>(args[0]));
        Socket* sock = SocketManager::instance().getSocket(id);
        if (!sock) {
            throw LanguageException("SocketError", "Invalid socket");
        }
        return Value(sock->setTimeouts(std::get<int>(args[1]), std::get<int>(args[2])));
    });
}

// ============================================================================
// TCP Server Functions
// ============================================================================

void register_socket_server_functions(FunctionRegistry& reg) {
    
    // ------------------------------------------------------------------------
    // system.net.socket.bind(sock: socket, host: string, port: int) -> bool
    // ------------------------------------------------------------------------
    reg.registerFunction("system.net.socket.bind", [](const std::vector<Value>& args) -> Value {
        if (args.size() < 3 || !isSocketRef(args[0]) || !isString(args[1]) || !isInt(args[2])) {
            throw LanguageException("TypeError", 
                "system.net.socket.bind: requires (socket, host, port)");
        }
        std::string id = extractSocketId(std::get<std::string>(args[0]));
        Socket* sock = SocketManager::instance().getSocket(id);
        if (!sock) {
            throw LanguageException("SocketError", "Invalid socket");
        }
        
        const std::string& host = std::get<std::string>(args[1]);
        int port = std::get<int>(args[2]);
        
        if (!sock->bind(host, port)) {
            throw LanguageException("SocketError", 
                "Failed to bind to " + host + ":" + std::to_string(port) + " - " + sock->lastErrorString());
        }
        return Value(true);
    });
    
    // ------------------------------------------------------------------------
    // system.net.socket.listen(sock: socket, backlog?: int) -> bool
    // Default backlog is 1024
    // ------------------------------------------------------------------------
    reg.registerFunction("system.net.socket.listen", [](const std::vector<Value>& args) -> Value {
        if (args.empty() || !isSocketRef(args[0])) {
            throw LanguageException("TypeError", 
                "system.net.socket.listen: requires (socket, backlog?)");
        }
        std::string id = extractSocketId(std::get<std::string>(args[0]));
        Socket* sock = SocketManager::instance().getSocket(id);
        if (!sock) {
            throw LanguageException("SocketError", "Invalid socket");
        }
        
        int backlog = 1024;
        if (args.size() > 1 && isInt(args[1])) {
            backlog = std::get<int>(args[1]);
        }
        
        if (!sock->listen(backlog)) {
            throw LanguageException("SocketError", 
                "Failed to listen - " + sock->lastErrorString());
        }
        return Value(true);
    });
    
    // ------------------------------------------------------------------------
    // system.net.socket.accept(sock: socket) -> dict
    // Returns a dict with {socket: socket, host: string, port: int}
    // Returns an empty dict if no connection is pending (non-blocking mode)
    // ------------------------------------------------------------------------
    reg.registerFunction("system.net.socket.accept", [](const std::vector<Value>& args) -> Value {
        if (!rt() || args.empty() || !isSocketRef(args[0])) {
            throw LanguageException("TypeError", 
                "system.net.socket.accept: requires (socket)");
        }
        std::string id = extractSocketId(std::get<std::string>(args[0]));
        Socket* sock = SocketManager::instance().getSocket(id);
        if (!sock) {
            throw LanguageException("SocketError", "Invalid socket");
        }
        
        SocketAddress clientAddr;
        Socket* client = sock->accept(clientAddr);
        
        if (!client) {
            // No connection pending or error - return empty dict
            std::unordered_map<std::string, Value> empty;
            return rt()->makeDict(std::move(empty));
        }
        
        // Store the new client socket
        std::string clientId = SocketManager::instance().storeSocket(std::unique_ptr<Socket>(client));
        
        std::unordered_map<std::string, Value> dict;
        dict["socket"] = Value(makeSocketRef(clientId));
        dict["host"] = Value(clientAddr.host);
        dict["port"] = Value(clientAddr.port);
        
        return rt()->makeDict(std::move(dict));
    });
}

// ============================================================================
// TCP Client Functions
// ============================================================================

void register_socket_client_functions(FunctionRegistry& reg) {
    
    // ------------------------------------------------------------------------
    // system.net.socket.connect(sock: socket, host: string, port: int) -> bool
    // ------------------------------------------------------------------------
    reg.registerFunction("system.net.socket.connect", [](const std::vector<Value>& args) -> Value {
        if (args.size() < 3 || !isSocketRef(args[0]) || !isString(args[1]) || !isInt(args[2])) {
            throw LanguageException("TypeError", 
                "system.net.socket.connect: requires (socket, host, port)");
        }
        std::string id = extractSocketId(std::get<std::string>(args[0]));
        Socket* sock = SocketManager::instance().getSocket(id);
        if (!sock) {
            throw LanguageException("SocketError", "Invalid socket");
        }
        
        const std::string& host = std::get<std::string>(args[1]);
        int port = std::get<int>(args[2]);
        
        if (!sock->connect(host, port)) {
            throw LanguageException("SocketError", 
                "Failed to connect to " + host + ":" + std::to_string(port) + " - " + sock->lastErrorString());
        }
        return Value(true);
    });
}

// ============================================================================
// I/O Functions
// ============================================================================

void register_socket_io_functions(FunctionRegistry& reg) {
    
    // ------------------------------------------------------------------------
    // system.net.socket.send(sock: socket, data: string) -> int
    // Returns number of bytes sent, or -1 on error
    // Returns 0 if would block (non-blocking mode)
    // ------------------------------------------------------------------------
    reg.registerFunction("system.net.socket.send", [](const std::vector<Value>& args) -> Value {
        if (args.size() < 2 || !isSocketRef(args[0]) || !isString(args[1])) {
            throw LanguageException("TypeError", 
                "system.net.socket.send: requires (socket, data)");
        }
        std::string id = extractSocketId(std::get<std::string>(args[0]));
        Socket* sock = SocketManager::instance().getSocket(id);
        if (!sock) {
            throw LanguageException("SocketError", "Invalid socket");
        }
        
        const std::string& data = std::get<std::string>(args[1]);
        int sent = sock->send(data);
        
        return Value(sent);
    });
    
    // ------------------------------------------------------------------------
    // system.net.socket.recv(sock: socket, maxLen?: int) -> string
    // Default maxLen is 4096
    // Returns empty string on error or connection closed
    // ------------------------------------------------------------------------
    reg.registerFunction("system.net.socket.recv", [](const std::vector<Value>& args) -> Value {
        if (args.empty() || !isSocketRef(args[0])) {
            throw LanguageException("TypeError", 
                "system.net.socket.recv: requires (socket, maxLen?)");
        }
        std::string id = extractSocketId(std::get<std::string>(args[0]));
        Socket* sock = SocketManager::instance().getSocket(id);
        if (!sock) {
            throw LanguageException("SocketError", "Invalid socket");
        }
        
        size_t maxLen = 4096;
        if (args.size() > 1 && isInt(args[1])) {
            maxLen = static_cast<size_t>(std::get<int>(args[1]));
        }
        
        // Apply resource limit
        if (maxLen > MAX_RECV_SIZE) {
            maxLen = MAX_RECV_SIZE;
        }
        
        return Value(sock->recv(maxLen));
    });
    
    // ------------------------------------------------------------------------
    // system.net.socket.sendAll(sock: socket, data: string) -> bool
    // Sends all data, looping until complete. Returns true on success.
    // ------------------------------------------------------------------------
    reg.registerFunction("system.net.socket.sendAll", [](const std::vector<Value>& args) -> Value {
        if (args.size() < 2 || !isSocketRef(args[0]) || !isString(args[1])) {
            throw LanguageException("TypeError", 
                "system.net.socket.sendAll: requires (socket, data)");
        }
        std::string id = extractSocketId(std::get<std::string>(args[0]));
        Socket* sock = SocketManager::instance().getSocket(id);
        if (!sock) {
            throw LanguageException("SocketError", "Invalid socket");
        }
        
        const std::string& data = std::get<std::string>(args[1]);
        size_t totalSent = 0;
        
        while (totalSent < data.length()) {
            int sent = sock->send(data.c_str() + totalSent, data.length() - totalSent);
            if (sent < 0) {
                return Value(false);
            }
            if (sent == 0) {
                // Would block - for blocking sockets this shouldn't happen
                // For non-blocking, caller should use event loop
                continue;
            }
            totalSent += sent;
        }
        
        return Value(true);
    });
    
    // ------------------------------------------------------------------------
    // system.net.socket.recvLine(sock: socket, maxLen?: int) -> string
    // Reads until newline or maxLen reached
    // ------------------------------------------------------------------------
    reg.registerFunction("system.net.socket.recvLine", [](const std::vector<Value>& args) -> Value {
        if (args.empty() || !isSocketRef(args[0])) {
            throw LanguageException("TypeError", 
                "system.net.socket.recvLine: requires (socket, maxLen?)");
        }
        std::string id = extractSocketId(std::get<std::string>(args[0]));
        Socket* sock = SocketManager::instance().getSocket(id);
        if (!sock) {
            throw LanguageException("SocketError", "Invalid socket");
        }
        
        size_t maxLen = 4096;
        if (args.size() > 1 && isInt(args[1])) {
            maxLen = static_cast<size_t>(std::get<int>(args[1]));
        }
        
        // Apply resource limit
        if (maxLen > MAX_RECV_SIZE) {
            maxLen = MAX_RECV_SIZE;
        }
        
        std::string line;
        line.reserve(256);
        char c;
        
        while (line.length() < maxLen) {
            int received = sock->recv(&c, 1);
            if (received <= 0) break;
            if (c == '\n') break;
            line += c;
        }
        
        // Remove trailing \r if present
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        
        return Value(line);
    });
}

// ============================================================================
// UDP Functions
// ============================================================================

void register_socket_udp_functions(FunctionRegistry& reg) {
    
    // ------------------------------------------------------------------------
    // system.net.socket.sendTo(sock: socket, data: string, host: string, port: int) -> int
    // ------------------------------------------------------------------------
    reg.registerFunction("system.net.socket.sendTo", [](const std::vector<Value>& args) -> Value {
        if (args.size() < 4 || !isSocketRef(args[0]) || !isString(args[1]) || 
            !isString(args[2]) || !isInt(args[3])) {
            throw LanguageException("TypeError", 
                "system.net.socket.sendTo: requires (socket, data, host, port)");
        }
        std::string id = extractSocketId(std::get<std::string>(args[0]));
        Socket* sock = SocketManager::instance().getSocket(id);
        if (!sock) {
            throw LanguageException("SocketError", "Invalid socket");
        }
        
        const std::string& data = std::get<std::string>(args[1]);
        SocketAddress addr(std::get<std::string>(args[2]), std::get<int>(args[3]));
        
        int sent = sock->sendTo(data.c_str(), data.length(), addr);
        return Value(sent);
    });
    
    // ------------------------------------------------------------------------
    // system.net.socket.recvFrom(sock: socket, maxLen?: int) -> dict
    // Returns {data: string, host: string, port: int}
    // ------------------------------------------------------------------------
    reg.registerFunction("system.net.socket.recvFrom", [](const std::vector<Value>& args) -> Value {
        if (!rt() || args.empty() || !isSocketRef(args[0])) {
            throw LanguageException("TypeError", 
                "system.net.socket.recvFrom: requires (socket, maxLen?)");
        }
        std::string id = extractSocketId(std::get<std::string>(args[0]));
        Socket* sock = SocketManager::instance().getSocket(id);
        if (!sock) {
            throw LanguageException("SocketError", "Invalid socket");
        }
        
        size_t maxLen = 4096;
        if (args.size() > 1 && isInt(args[1])) {
            maxLen = static_cast<size_t>(std::get<int>(args[1]));
        }
        
        // Apply resource limit
        if (maxLen > MAX_RECV_SIZE) {
            maxLen = MAX_RECV_SIZE;
        }
        
        std::vector<char> buffer(maxLen);
        SocketAddress srcAddr;
        
        int received = sock->recvFrom(buffer.data(), maxLen, srcAddr);
        
        std::unordered_map<std::string, Value> dict;
        if (received > 0) {
            dict["data"] = Value(std::string(buffer.data(), received));
            dict["host"] = Value(srcAddr.host);
            dict["port"] = Value(srcAddr.port);
        } else {
            dict["data"] = Value(std::string(""));
            dict["host"] = Value(std::string(""));
            dict["port"] = Value(0);
        }
        
        return rt()->makeDict(std::move(dict));
    });
}

// ============================================================================
// Utility Functions
// ============================================================================

void register_socket_util_functions(FunctionRegistry& reg) {
    
    // ------------------------------------------------------------------------
    // system.net.socket.resolve(hostname: string) -> string
    // Resolves hostname to IP address
    // ------------------------------------------------------------------------
    reg.registerFunction("system.net.socket.resolve", [](const std::vector<Value>& args) -> Value {
        if (args.empty() || !isString(args[0])) {
            throw LanguageException("TypeError", 
                "system.net.socket.resolve: requires hostname string");
        }
        return Value(resolveHostname(std::get<std::string>(args[0])));
    });
    
    // ------------------------------------------------------------------------
    // system.net.socket.localAddresses() -> array
    // Returns array of local IP addresses
    // ------------------------------------------------------------------------
    reg.registerFunction("system.net.socket.localAddresses", [](const std::vector<Value>& args) -> Value {
        if (!rt()) return Value(0);
        
        auto addresses = getLocalAddresses();
        std::vector<Value> arr;
        for (const auto& addr : addresses) {
            arr.push_back(Value(addr));
        }
        return rt()->makeArray(std::move(arr));
    });
    
    // ------------------------------------------------------------------------
    // system.net.socket.isSocket(value: any) -> bool
    // Checks if a value is a socket reference
    // ------------------------------------------------------------------------
    reg.registerFunction("system.net.socket.isSocket", [](const std::vector<Value>& args) -> Value {
        if (args.empty()) return Value(false);
        return Value(isSocketRef(args[0]));
    });
    
    // ------------------------------------------------------------------------
    // system.net.socket.socketCount() -> int
    // Returns the number of active sockets
    // ------------------------------------------------------------------------
    reg.registerFunction("system.net.socket.socketCount", [](const std::vector<Value>& args) -> Value {
        return Value(static_cast<int>(SocketManager::instance().socketCount()));
    });
}

// ============================================================================
// Quick Server Helper
// ============================================================================

void register_socket_server_helper_functions(FunctionRegistry& reg) {
    
    // ------------------------------------------------------------------------
    // system.net.socket.createServer(host: string, port: int) -> socket
    // Creates a TCP server socket, binds, and starts listening
    // Convenience function combining tcp() + bind() + listen()
    // ------------------------------------------------------------------------
    reg.registerFunction("system.net.socket.createServer", [](const std::vector<Value>& args) -> Value {
        if (args.size() < 2 || !isString(args[0]) || !isInt(args[1])) {
            throw LanguageException("TypeError", 
                "system.net.socket.createServer: requires (host, port)");
        }
        
        const std::string& host = std::get<std::string>(args[0]);
        int port = std::get<int>(args[1]);
        int backlog = 1024;
        if (args.size() > 2 && isInt(args[2])) {
            backlog = std::get<int>(args[2]);
        }
        
        Socket* sock = SocketManager::instance().createSocket(SocketType::TCP);
        if (!sock) {
            throw LanguageException("SocketError", "Failed to create socket");
        }
        
        sock->setReuseAddr(true);
        sock->setNonBlocking(true);
        sock->setNoDelay(true);
        
        if (!sock->bind(host, port)) {
            std::string error = sock->lastErrorString();
            SocketManager::instance().removeSocket(sock->id());
            throw LanguageException("SocketError", 
                "Failed to bind to " + host + ":" + std::to_string(port) + " - " + error);
        }
        
        if (!sock->listen(backlog)) {
            std::string error = sock->lastErrorString();
            SocketManager::instance().removeSocket(sock->id());
            throw LanguageException("SocketError", "Failed to listen - " + error);
        }
        
        return Value(makeSocketRef(sock->id()));
    });
    
    // ------------------------------------------------------------------------
    // system.net.socket.createClient(host: string, port: int) -> socket
    // Creates a TCP client socket and connects
    // ------------------------------------------------------------------------
    reg.registerFunction("system.net.socket.createClient", [](const std::vector<Value>& args) -> Value {
        if (args.size() < 2 || !isString(args[0]) || !isInt(args[1])) {
            throw LanguageException("TypeError", 
                "system.net.socket.createClient: requires (host, port)");
        }
        
        const std::string& host = std::get<std::string>(args[0]);
        int port = std::get<int>(args[1]);
        
        Socket* sock = SocketManager::instance().createSocket(SocketType::TCP);
        if (!sock) {
            throw LanguageException("SocketError", "Failed to create socket");
        }
        
        sock->setNoDelay(true);
        
        if (!sock->connect(host, port)) {
            std::string error = sock->lastErrorString();
            SocketManager::instance().removeSocket(sock->id());
            throw LanguageException("SocketError", 
                "Failed to connect to " + host + ":" + std::to_string(port) + " - " + error);
        }
        
        return Value(makeSocketRef(sock->id()));
    });
}

// ============================================================================
// Extension Entry Point
// ============================================================================

extern "C" __attribute__((visibility("default")))
void init_extension(FunctionRegistry& reg) {
    // Initialize socket library
    qz_socket::initializeSocketLibrary();
    
    // Register all function groups
    register_socket_create_functions(reg);
    register_socket_option_functions(reg);
    register_socket_server_functions(reg);
    register_socket_client_functions(reg);
    register_socket_io_functions(reg);
    register_socket_udp_functions(reg);
    register_socket_util_functions(reg);
    register_socket_server_helper_functions(reg);
}
