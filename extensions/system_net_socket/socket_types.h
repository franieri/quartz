// ============================================================================
// system.net.socket - Socket Types and Platform Abstraction
// High-performance cross-platform socket library
// ============================================================================

#ifndef SOCKET_TYPES_H
#define SOCKET_TYPES_H

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <functional>
#include <atomic>
#include <mutex>
#include <chrono>

// ============================================================================
// Platform Detection and Includes
// ============================================================================

#if defined(_WIN32) || defined(_WIN64)
    #define QZ_PLATFORM_WINDOWS 1
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <mswsock.h>
    #pragma comment(lib, "ws2_32.lib")
    #pragma comment(lib, "mswsock.lib")
    
    using socket_t = SOCKET;
    #define INVALID_SOCKET_VALUE INVALID_SOCKET
    #define SOCKET_ERROR_VALUE SOCKET_ERROR
    #define close_socket closesocket
    
#elif defined(__APPLE__) || defined(__MACH__)
    #define QZ_PLATFORM_MACOS 1
    #include <sys/socket.h>
    #include <sys/types.h>
    #include <sys/event.h>
    #include <sys/time.h>
    #include <netinet/in.h>
    #include <netinet/tcp.h>
    #include <arpa/inet.h>
    #include <netdb.h>
    #include <unistd.h>
    #include <fcntl.h>
    #include <errno.h>
    
    using socket_t = int;
    #define INVALID_SOCKET_VALUE (-1)
    #define SOCKET_ERROR_VALUE (-1)
    #define close_socket ::close
    
#elif defined(__linux__)
    #define QZ_PLATFORM_LINUX 1
    #include <sys/socket.h>
    #include <sys/types.h>
    #include <sys/epoll.h>
    #include <netinet/in.h>
    #include <netinet/tcp.h>
    #include <arpa/inet.h>
    #include <netdb.h>
    #include <unistd.h>
    #include <fcntl.h>
    #include <errno.h>
    
    using socket_t = int;
    #define INVALID_SOCKET_VALUE (-1)
    #define SOCKET_ERROR_VALUE (-1)
    #define close_socket ::close
    
#else
    #error "Unsupported platform"
#endif

namespace qz_socket {

// ============================================================================
// Socket Types and Enums
// ============================================================================

enum class SocketType {
    TCP,
    UDP
};

enum class SocketState {
    CLOSED,
    CREATED,
    BOUND,
    LISTENING,
    CONNECTED,
    ERROR_STATE
};

enum class SocketEvent {
    READABLE,
    WRITABLE,
    ERROR_EVENT,
    CLOSE,
    ACCEPT
};

// ============================================================================
// Socket Address
// ============================================================================

struct SocketAddress {
    std::string host;
    int port;
    bool isIPv6;
    
    SocketAddress() : host("0.0.0.0"), port(0), isIPv6(false) {}
    SocketAddress(const std::string& h, int p, bool v6 = false) 
        : host(h), port(p), isIPv6(v6) {}
    
    std::string toString() const {
        if (isIPv6) {
            return "[" + host + "]:" + std::to_string(port);
        }
        return host + ":" + std::to_string(port);
    }
};

// ============================================================================
// Socket Options
// ============================================================================

struct SocketOptions {
    bool reuseAddr = true;
    bool reusePort = false;       // Linux SO_REUSEPORT for load balancing
    bool noDelay = true;          // TCP_NODELAY (disable Nagle)
    bool keepAlive = false;
    int recvBufferSize = 65536;   // 64KB
    int sendBufferSize = 65536;   // 64KB
    int backlog = 1024;           // Listen backlog
    int recvTimeout = 0;          // 0 = no timeout
    int sendTimeout = 0;
    bool nonBlocking = true;      // Default to non-blocking for event loop
    
    // Keep-alive options (if keepAlive is true)
    int keepAliveIdle = 60;       // Seconds before first probe
    int keepAliveInterval = 10;   // Seconds between probes
    int keepAliveCount = 3;       // Number of probes before disconnect
};

// ============================================================================
// Socket Statistics
// ============================================================================

struct SocketStats {
    std::atomic<uint64_t> bytesReceived{0};
    std::atomic<uint64_t> bytesSent{0};
    std::atomic<uint64_t> packetsReceived{0};
    std::atomic<uint64_t> packetsSent{0};
    std::atomic<uint64_t> acceptedConnections{0};
    std::atomic<uint64_t> errors{0};
    std::chrono::steady_clock::time_point createdAt;
    std::chrono::steady_clock::time_point lastActivityAt;
    
    SocketStats() : createdAt(std::chrono::steady_clock::now()),
                    lastActivityAt(std::chrono::steady_clock::now()) {}
    
    void updateActivity() {
        lastActivityAt = std::chrono::steady_clock::now();
    }
    
    uint64_t uptimeMs() const {
        auto now = std::chrono::steady_clock::now();
        return std::chrono::duration_cast<std::chrono::milliseconds>(now - createdAt).count();
    }
    
    uint64_t idleMs() const {
        auto now = std::chrono::steady_clock::now();
        return std::chrono::duration_cast<std::chrono::milliseconds>(now - lastActivityAt).count();
    }
};

// ============================================================================
// Socket Handle - Core socket wrapper
// ============================================================================

class Socket {
public:
    Socket();
    Socket(SocketType type);
    Socket(socket_t existingSocket, SocketType type);
    ~Socket();
    
    // Prevent copying
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
    
    // Allow moving
    Socket(Socket&& other) noexcept;
    Socket& operator=(Socket&& other) noexcept;
    
    // Core operations
    bool create(SocketType type, bool ipv6 = false);
    bool bind(const std::string& host, int port);
    bool listen(int backlog = 1024);
    Socket* accept(SocketAddress& clientAddr);
    bool connect(const std::string& host, int port);
    void close();
    
    // I/O operations
    int send(const char* data, size_t length);
    int send(const std::string& data);
    int recv(char* buffer, size_t maxLength);
    std::string recv(size_t maxLength = 4096);
    
    // UDP operations
    int sendTo(const char* data, size_t length, const SocketAddress& addr);
    int recvFrom(char* buffer, size_t maxLength, SocketAddress& addr);
    
    // Socket options
    bool setOption(const SocketOptions& opts);
    bool setNonBlocking(bool nonBlocking);
    bool setReuseAddr(bool reuse);
    bool setNoDelay(bool noDelay);
    bool setKeepAlive(bool keepAlive, int idle = 60, int interval = 10, int count = 3);
    bool setBufferSizes(int recvSize, int sendSize);
    bool setTimeouts(int sendTimeoutMs, int recvTimeoutMs);
    
    // Accessors
    socket_t handle() const { return fd_; }
    SocketType type() const { return type_; }
    SocketState state() const { return state_; }
    const SocketAddress& localAddress() const { return localAddr_; }
    const SocketAddress& remoteAddress() const { return remoteAddr_; }
    const SocketStats& stats() const { return stats_; }
    bool isValid() const { return fd_ != INVALID_SOCKET_VALUE; }
    bool isNonBlocking() const { return nonBlocking_; }
    int lastError() const { return lastError_; }
    std::string lastErrorString() const;
    
    // Unique ID for this socket
    const std::string& id() const { return id_; }
    
private:
    socket_t fd_;
    SocketType type_;
    SocketState state_;
    SocketAddress localAddr_;
    SocketAddress remoteAddr_;
    SocketStats stats_;
    SocketOptions options_;
    bool nonBlocking_;
    int lastError_;
    std::string id_;
    
    static std::atomic<uint64_t> nextId_;
    
    void setLastError();
    static std::string generateId();
};

// ============================================================================
// Platform-specific I/O Multiplexer Backends
// ============================================================================

// Event callback type
using EventCallback = std::function<void(Socket*, SocketEvent)>;

// Abstract multiplexer interface
class IOMultiplexer {
public:
    virtual ~IOMultiplexer() = default;
    
    virtual bool create() = 0;
    virtual void destroy() = 0;
    
    virtual bool add(Socket* socket, uint32_t events, void* userData = nullptr) = 0;
    virtual bool modify(Socket* socket, uint32_t events) = 0;
    virtual bool remove(Socket* socket) = 0;
    
    // Poll for events, returns number of ready sockets
    virtual int poll(int timeoutMs = -1) = 0;
    
    // Get events for processing after poll()
    virtual void processEvents(const EventCallback& callback) = 0;
    
    // Event flags for add/modify
    static constexpr uint32_t EVENT_READ  = 0x01;
    static constexpr uint32_t EVENT_WRITE = 0x02;
    static constexpr uint32_t EVENT_ERROR = 0x04;
};

#ifdef QZ_PLATFORM_LINUX
// ============================================================================
// Linux epoll Backend
// ============================================================================
class EpollMultiplexer : public IOMultiplexer {
public:
    EpollMultiplexer(int maxEvents = 1024);
    ~EpollMultiplexer() override;
    
    bool create() override;
    void destroy() override;
    bool add(Socket* socket, uint32_t events, void* userData) override;
    bool modify(Socket* socket, uint32_t events) override;
    bool remove(Socket* socket) override;
    int poll(int timeoutMs) override;
    void processEvents(const EventCallback& callback) override;
    
private:
    int epollFd_;
    int maxEvents_;
    std::vector<struct epoll_event> events_;
    int numReady_;
    std::unordered_map<socket_t, Socket*> sockets_;
};
#endif

#ifdef QZ_PLATFORM_MACOS
// ============================================================================
// macOS kqueue Backend
// ============================================================================
class KqueueMultiplexer : public IOMultiplexer {
public:
    KqueueMultiplexer(int maxEvents = 1024);
    ~KqueueMultiplexer() override;
    
    bool create() override;
    void destroy() override;
    bool add(Socket* socket, uint32_t events, void* userData) override;
    bool modify(Socket* socket, uint32_t events) override;
    bool remove(Socket* socket) override;
    int poll(int timeoutMs) override;
    void processEvents(const EventCallback& callback) override;
    
private:
    int kqueueFd_;
    int maxEvents_;
    std::vector<struct kevent> events_;
    std::vector<struct kevent> changes_;
    int numReady_;
    std::unordered_map<socket_t, Socket*> sockets_;
};
#endif

#ifdef QZ_PLATFORM_WINDOWS
// ============================================================================
// Windows IOCP Backend (I/O Completion Ports)
// ============================================================================
class IOCPMultiplexer : public IOMultiplexer {
public:
    IOCPMultiplexer(int concurrency = 0);
    ~IOCPMultiplexer() override;
    
    bool create() override;
    void destroy() override;
    bool add(Socket* socket, uint32_t events, void* userData) override;
    bool modify(Socket* socket, uint32_t events) override;
    bool remove(Socket* socket) override;
    int poll(int timeoutMs) override;
    void processEvents(const EventCallback& callback) override;
    
private:
    HANDLE iocpHandle_;
    int concurrency_;
    std::vector<OVERLAPPED_ENTRY> completions_;
    ULONG numCompletions_;
    std::unordered_map<socket_t, Socket*> sockets_;
};
#endif

// ============================================================================
// Factory function to create platform-appropriate multiplexer
// ============================================================================
std::unique_ptr<IOMultiplexer> createMultiplexer(int maxEvents = 1024);

// ============================================================================
// Platform utilities
// ============================================================================

// Initialize platform socket library (WSAStartup on Windows)
bool initializeSocketLibrary();

// Cleanup platform socket library (WSACleanup on Windows)
void cleanupSocketLibrary();

// Resolve hostname to IP address
std::string resolveHostname(const std::string& hostname, bool preferIPv6 = false);

// Get local IP addresses
std::vector<std::string> getLocalAddresses(bool includeIPv6 = false);

// Socket reference for Quartz runtime
struct SocketRef {
    std::string id;
};

// Global socket storage (managed by the extension)
class SocketManager {
public:
    static SocketManager& instance();
    
    // Create and store a new socket
    Socket* createSocket(SocketType type);
    
    // Store an existing socket (e.g., from accept())
    std::string storeSocket(std::unique_ptr<Socket> socket);
    
    // Get socket by ID
    Socket* getSocket(const std::string& id);
    
    // Remove and destroy socket
    bool removeSocket(const std::string& id);
    
    // Get all socket IDs
    std::vector<std::string> getAllSocketIds() const;
    
    // Stats
    size_t socketCount() const;
    
private:
    SocketManager() = default;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::unique_ptr<Socket>> sockets_;
};

} // namespace qz_socket

#endif // SOCKET_TYPES_H
