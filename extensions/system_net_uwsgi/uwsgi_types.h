// ============================================================================
// system.net.uwsgi - uWSGI Protocol Types and Server Implementation
// High-performance uWSGI server for nginx frontend integration
// ============================================================================

#ifndef UWSGI_TYPES_H
#define UWSGI_TYPES_H

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <functional>
#include <atomic>
#include <mutex>
#include <queue>
#include <chrono>
#include <thread>

// Platform detection
#if defined(_WIN32) || defined(_WIN64)
    #define QZ_PLATFORM_WINDOWS 1
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <winsock2.h>
    #include <ws2tcpip.h>
    using socket_t = SOCKET;
    #define INVALID_SOCKET_VALUE INVALID_SOCKET
    #define close_socket closesocket
#elif defined(__APPLE__) || defined(__MACH__)
    #define QZ_PLATFORM_MACOS 1
    #include <sys/socket.h>
    #include <sys/types.h>
    #include <sys/event.h>
    #include <sys/un.h>
    #include <netinet/in.h>
    #include <netinet/tcp.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <fcntl.h>
    #include <errno.h>
    using socket_t = int;
    #define INVALID_SOCKET_VALUE (-1)
    #define close_socket ::close
#elif defined(__linux__)
    #define QZ_PLATFORM_LINUX 1
    #include <sys/socket.h>
    #include <sys/types.h>
    #include <sys/epoll.h>
    #include <sys/un.h>
    #include <netinet/in.h>
    #include <netinet/tcp.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <fcntl.h>
    #include <errno.h>
    using socket_t = int;
    #define INVALID_SOCKET_VALUE (-1)
    #define close_socket ::close
#endif

namespace uwsgi {

// ============================================================================
// uWSGI Protocol Constants
// ============================================================================

// uWSGI packet types
constexpr uint8_t UWSGI_MODIFIER1_WSGI = 0;        // Standard WSGI request
constexpr uint8_t UWSGI_MODIFIER1_PING = 100;      // Ping (healthcheck)
constexpr uint8_t UWSGI_MODIFIER1_CACHE = 111;     // Cache request
constexpr uint8_t UWSGI_MODIFIER2_END = 0;         // End of block

// Maximum sizes for safety
constexpr size_t MAX_UWSGI_PACKET_SIZE = 64 * 1024;       // 64KB max packet
constexpr size_t MAX_HEADER_SIZE = 16 * 1024;             // 16KB max single header
constexpr size_t MAX_BODY_SIZE = 64 * 1024 * 1024;        // 64MB max body
constexpr size_t MAX_CONNECTIONS = 10000;                  // Max concurrent connections
constexpr size_t RECV_BUFFER_SIZE = 65536;                // 64KB receive buffer
constexpr size_t SEND_BUFFER_SIZE = 65536;                // 64KB send buffer

// ============================================================================
// uWSGI Packet Header (4 bytes)
// ============================================================================

#pragma pack(push, 1)
struct UwsgiHeader {
    uint8_t modifier1;
    uint16_t datasize;      // Little-endian
    uint8_t modifier2;
    
    UwsgiHeader() : modifier1(0), datasize(0), modifier2(0) {}
    UwsgiHeader(uint8_t m1, uint16_t size, uint8_t m2) 
        : modifier1(m1), datasize(size), modifier2(m2) {}
};
#pragma pack(pop)

static_assert(sizeof(UwsgiHeader) == 4, "UwsgiHeader must be 4 bytes");

// ============================================================================
// HTTP Request (parsed from uWSGI vars)
// ============================================================================

struct HttpRequest {
    std::string method;
    std::string uri;
    std::string path;
    std::string queryString;
    std::string protocol;
    std::string host;
    std::string remoteAddr;
    int remotePort;
    std::string contentType;
    size_t contentLength;
    std::string body;
    std::unordered_map<std::string, std::string> headers;
    std::unordered_map<std::string, std::string> uwsgiVars;
    
    // Request timing
    std::chrono::steady_clock::time_point startTime;
    
    HttpRequest() : remotePort(0), contentLength(0) {
        startTime = std::chrono::steady_clock::now();
    }
    
    // Get header (case-insensitive)
    std::string getHeader(const std::string& name) const;
    
    // Get uwsgi var
    std::string getVar(const std::string& name) const;
    
    // Check if request has body
    bool hasBody() const { return contentLength > 0; }
    
    // Elapsed time since request started (ms)
    int64_t elapsedMs() const;
};

// ============================================================================
// HTTP Response Builder
// ============================================================================

struct HttpResponse {
    int statusCode;
    std::string statusText;
    std::unordered_map<std::string, std::string> headers;
    std::string body;
    bool headersSent;
    bool finished;
    
    HttpResponse() : statusCode(200), statusText("OK"), headersSent(false), finished(false) {}
    
    // Set status
    void status(int code, const std::string& text = "");
    
    // Set header
    void setHeader(const std::string& name, const std::string& value);
    
    // Common headers
    void setContentType(const std::string& type);
    void setContentLength(size_t length);
    
    // Build HTTP response string
    std::string build() const;
    
    // Quick responses
    static HttpResponse ok(const std::string& body, const std::string& contentType = "text/plain");
    static HttpResponse json(const std::string& jsonBody);
    static HttpResponse html(const std::string& htmlBody);
    static HttpResponse notFound(const std::string& message = "Not Found");
    static HttpResponse serverError(const std::string& message = "Internal Server Error");
    static HttpResponse redirect(const std::string& location, int code = 302);
};

// ============================================================================
// Connection State Machine
// ============================================================================

enum class ConnectionState {
    READING_HEADER,       // Reading 4-byte uWSGI header
    READING_VARS,         // Reading uWSGI vars block
    READING_BODY,         // Reading request body (if any)
    PROCESSING,           // Processing request
    SENDING_RESPONSE,     // Sending response
    KEEP_ALIVE,           // Waiting for next request (if keep-alive)
    CLOSING,              // Connection closing
    CLOSED                // Connection closed
};

// ============================================================================
// Client Connection
// ============================================================================

class Connection {
public:
    Connection(socket_t fd, const std::string& remoteAddr, int remotePort);
    ~Connection();
    
    // Prevent copying
    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;
    
    // Connection ID
    const std::string& id() const { return id_; }
    socket_t fd() const { return fd_; }
    ConnectionState state() const { return state_; }
    
    // Remote info
    const std::string& remoteAddr() const { return remoteAddr_; }
    int remotePort() const { return remotePort_; }
    
    // Read data from socket into internal buffer
    // Returns: >0 bytes read, 0 = would block, <0 = error/closed
    int readData();
    
    // Write data to socket
    // Returns: >0 bytes written, 0 = would block, <0 = error
    int writeData(const char* data, size_t length);
    int writeData(const std::string& data);
    
    // Flush pending send buffer
    int flushSendBuffer();
    
    // Check if there's pending data to send
    bool hasPendingSend() const { return sendOffset_ < sendBuffer_.size(); }
    
    // Parse uWSGI protocol
    bool parseHeader();
    bool parseVars();
    bool parseBody();
    
    // Get parsed request (valid after parsing completes)
    HttpRequest& request() { return request_; }
    const HttpRequest& request() const { return request_; }
    
    // Set response to send
    void setResponse(const HttpResponse& response);
    void setResponse(const std::string& rawResponse);
    
    // State transitions
    void setState(ConnectionState newState) { state_ = newState; }
    
    // Reset for keep-alive
    void reset();
    
    // Check timeouts
    bool isTimedOut(int timeoutMs) const;
    
    // Stats
    size_t bytesReceived() const { return bytesReceived_; }
    size_t bytesSent() const { return bytesSent_; }
    
private:
    std::string id_;
    socket_t fd_;
    std::string remoteAddr_;
    int remotePort_;
    ConnectionState state_;
    
    // Receive buffer
    std::vector<char> recvBuffer_;
    size_t recvOffset_;
    
    // Send buffer
    std::string sendBuffer_;
    size_t sendOffset_;
    
    // Parsed data
    UwsgiHeader uwsgiHeader_;
    HttpRequest request_;
    
    // Timing
    std::chrono::steady_clock::time_point lastActivity_;
    
    // Stats
    size_t bytesReceived_;
    size_t bytesSent_;
    
    static std::atomic<uint64_t> nextId_;
};

// ============================================================================
// Request Handler Callback Type
// ============================================================================

// Synchronous handler: receives request, returns response
using RequestHandler = std::function<HttpResponse(const HttpRequest&)>;

// Async handler: receives request and connection ID, calls response callback
using AsyncRequestHandler = std::function<void(const HttpRequest&, const std::string& connId)>;

// Response callback for async handlers
using ResponseCallback = std::function<void(const std::string& connId, const HttpResponse&)>;

// ============================================================================
// uWSGI Server Statistics (Copyable Snapshot)
// ============================================================================

struct StatsSnapshot {
    uint64_t totalConnections{0};
    uint64_t activeConnections{0};
    uint64_t totalRequests{0};
    uint64_t successResponses{0};
    uint64_t clientErrors{0};
    uint64_t serverErrors{0};
    uint64_t bytesReceived{0};
    uint64_t bytesSent{0};
    uint64_t parseErrors{0};
    uint64_t timeouts{0};
    uint64_t uptimeMs{0};
    double requestsPerSecond{0.0};
};

// ============================================================================
// uWSGI Server Statistics (Atomic for thread-safety)
// ============================================================================

struct ServerStats {
    std::atomic<uint64_t> totalConnections{0};
    std::atomic<uint64_t> activeConnections{0};
    std::atomic<uint64_t> totalRequests{0};
    std::atomic<uint64_t> successResponses{0};     // 2xx
    std::atomic<uint64_t> clientErrors{0};         // 4xx
    std::atomic<uint64_t> serverErrors{0};         // 5xx
    std::atomic<uint64_t> bytesReceived{0};
    std::atomic<uint64_t> bytesSent{0};
    std::atomic<uint64_t> parseErrors{0};
    std::atomic<uint64_t> timeouts{0};
    std::chrono::steady_clock::time_point startTime;
    
    ServerStats() : startTime(std::chrono::steady_clock::now()) {}
    
    void reset() {
        totalConnections = 0;
        activeConnections = 0;
        totalRequests = 0;
        successResponses = 0;
        clientErrors = 0;
        serverErrors = 0;
        bytesReceived = 0;
        bytesSent = 0;
        parseErrors = 0;
        timeouts = 0;
        startTime = std::chrono::steady_clock::now();
    }
    
    uint64_t uptimeMs() const {
        auto now = std::chrono::steady_clock::now();
        return std::chrono::duration_cast<std::chrono::milliseconds>(now - startTime).count();
    }
    
    double requestsPerSecond() const {
        uint64_t ms = uptimeMs();
        if (ms == 0) return 0.0;
        return (double)totalRequests.load() * 1000.0 / (double)ms;
    }
    
    // Create a copyable snapshot
    StatsSnapshot snapshot() const {
        StatsSnapshot snap;
        snap.totalConnections = totalConnections.load();
        snap.activeConnections = activeConnections.load();
        snap.totalRequests = totalRequests.load();
        snap.successResponses = successResponses.load();
        snap.clientErrors = clientErrors.load();
        snap.serverErrors = serverErrors.load();
        snap.bytesReceived = bytesReceived.load();
        snap.bytesSent = bytesSent.load();
        snap.parseErrors = parseErrors.load();
        snap.timeouts = timeouts.load();
        snap.uptimeMs = uptimeMs();
        snap.requestsPerSecond = requestsPerSecond();
        return snap;
    }
};

// ============================================================================
// Server Configuration
// ============================================================================

struct ServerConfig {
    // Network settings
    std::string bindAddress = "127.0.0.1";
    int port = 3031;
    std::string socketPath;                    // Unix socket path (alternative to TCP)
    bool useUnixSocket = false;
    int backlog = 4096;                        // Listen backlog (high for performance)
    
    // Connection settings
    int maxConnections = 10000;
    int connectionTimeoutMs = 30000;           // 30 seconds
    int keepAliveTimeoutMs = 5000;             // 5 seconds for keep-alive
    bool enableKeepAlive = true;
    
    // Buffer settings
    size_t recvBufferSize = RECV_BUFFER_SIZE;
    size_t sendBufferSize = SEND_BUFFER_SIZE;
    size_t maxBodySize = MAX_BODY_SIZE;
    
    // Threading (for worker pool)
    int numWorkers = 0;                        // 0 = single-threaded (event loop based)
    
    // Performance tuning
    bool tcpNoDelay = true;                    // Disable Nagle's algorithm
    bool reuseAddr = true;
    bool reusePort = false;                    // SO_REUSEPORT for multi-process
    
    // Debugging
    bool verbose = false;
};

// ============================================================================
// uWSGI Server
// ============================================================================

class UwsgiServer {
public:
    UwsgiServer();
    ~UwsgiServer();
    
    // Prevent copying
    UwsgiServer(const UwsgiServer&) = delete;
    UwsgiServer& operator=(const UwsgiServer&) = delete;
    
    // Configuration
    bool configure(const ServerConfig& config);
    const ServerConfig& config() const { return config_; }
    
    // Set request handler
    void setHandler(RequestHandler handler);
    void setAsyncHandler(AsyncRequestHandler handler);
    
    // Lifecycle
    bool start();
    void stop();
    bool isRunning() const { return running_.load(); }
    
    // For async handlers: send response to connection
    bool sendResponse(const std::string& connId, const HttpResponse& response);
    
    // Non-blocking poll (for integration with event loop)
    // Returns number of events processed
    int poll(int timeoutMs = 0);
    
    // Get statistics
    const ServerStats& stats() const { return stats_; }
    void resetStats() { stats_.reset(); }
    
    // Get active connections
    size_t activeConnections() const { return connections_.size(); }
    
    // Get server socket fd (for event loop integration)
    socket_t serverFd() const { return serverFd_; }
    
    // Server ID
    const std::string& id() const { return id_; }
    
private:
    std::string id_;
    ServerConfig config_;
    std::atomic<bool> running_;
    socket_t serverFd_;
    
    // Connections
    std::unordered_map<std::string, std::unique_ptr<Connection>> connections_;
    std::mutex connectionsMutex_;
    
    // Handlers
    RequestHandler handler_;
    AsyncRequestHandler asyncHandler_;
    bool useAsyncHandler_;
    
    // Stats
    ServerStats stats_;
    
    // Platform-specific I/O multiplexer
#ifdef QZ_PLATFORM_LINUX
    int epollFd_;
#elif defined(QZ_PLATFORM_MACOS)
    int kqueueFd_;
#endif
    
    // Internal methods
    bool createSocket();
    bool createUnixSocket();
    bool setupMultiplexer();
    void cleanupMultiplexer();
    
    void acceptConnections();
    void handleConnection(Connection* conn, uint32_t events);
    void processRequest(Connection* conn);
    void removeConnection(const std::string& connId);
    void checkTimeouts();
    
    // Set socket non-blocking
    bool setNonBlocking(socket_t fd);
    
    // Add/remove fd from multiplexer
    bool addToMultiplexer(socket_t fd, uint32_t events, void* userData);
    bool modifyInMultiplexer(socket_t fd, uint32_t events);
    bool removeFromMultiplexer(socket_t fd);
    
    // Event flags
    static constexpr uint32_t EV_READ = 0x01;
    static constexpr uint32_t EV_WRITE = 0x02;
    
    static std::atomic<uint64_t> nextServerId_;
};

// ============================================================================
// Server Manager (Singleton)
// ============================================================================

class ServerManager {
public:
    static ServerManager& instance();
    
    // Create/get/remove servers
    UwsgiServer* createServer(const std::string& id = "");
    UwsgiServer* getServer(const std::string& id);
    bool removeServer(const std::string& id);
    
    // Get all server IDs
    std::vector<std::string> getServerIds() const;
    
    // Stop all servers
    void stopAll();
    
    // Aggregate stats (returns copyable snapshot)
    StatsSnapshot aggregateStats() const;
    
private:
    ServerManager() = default;
    ~ServerManager();
    
    std::unordered_map<std::string, std::unique_ptr<UwsgiServer>> servers_;
    mutable std::mutex mutex_;
};

// ============================================================================
// Utility Functions
// ============================================================================

// Get HTTP status text
std::string getStatusText(int code);

// URL decode
std::string urlDecode(const std::string& encoded);

// URL encode
std::string urlEncode(const std::string& str);

// Parse query string into map
std::unordered_map<std::string, std::string> parseQueryString(const std::string& query);

// MIME type lookup
std::string getMimeType(const std::string& extension);

// Convert CGI header name to HTTP header name
// e.g., "HTTP_CONTENT_TYPE" -> "Content-Type"
std::string cgiToHttpHeader(const std::string& cgiName);

} // namespace uwsgi

#endif // UWSGI_TYPES_H
