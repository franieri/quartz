// ============================================================================
// system.net.socket - Core Socket Implementation
// Cross-platform socket operations with platform-specific backends
// ============================================================================

#include "socket_types.h"
#include <sstream>
#include <cstring>
#include <algorithm>

namespace qz_socket {

// ============================================================================
// Static members
// ============================================================================

std::atomic<uint64_t> Socket::nextId_{0};

// ============================================================================
// Platform Library Initialization
// ============================================================================

static bool g_socketLibraryInitialized = false;

bool initializeSocketLibrary() {
    if (g_socketLibraryInitialized) return true;
    
#ifdef QZ_PLATFORM_WINDOWS
    WSADATA wsaData;
    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != 0) {
        return false;
    }
#endif
    
    g_socketLibraryInitialized = true;
    return true;
}

void cleanupSocketLibrary() {
    if (!g_socketLibraryInitialized) return;
    
#ifdef QZ_PLATFORM_WINDOWS
    WSACleanup();
#endif
    
    g_socketLibraryInitialized = false;
}

// ============================================================================
// Socket Implementation
// ============================================================================

std::string Socket::generateId() {
    return "sock_" + std::to_string(nextId_++);
}

Socket::Socket() 
    : fd_(INVALID_SOCKET_VALUE)
    , type_(SocketType::TCP)
    , state_(SocketState::CLOSED)
    , nonBlocking_(false)
    , lastError_(0)
    , id_(generateId()) 
{
}

Socket::Socket(SocketType type)
    : fd_(INVALID_SOCKET_VALUE)
    , type_(type)
    , state_(SocketState::CLOSED)
    , nonBlocking_(false)
    , lastError_(0)
    , id_(generateId())
{
}

Socket::Socket(socket_t existingSocket, SocketType type)
    : fd_(existingSocket)
    , type_(type)
    , state_(existingSocket != INVALID_SOCKET_VALUE ? SocketState::CONNECTED : SocketState::CLOSED)
    , nonBlocking_(false)
    , lastError_(0)
    , id_(generateId())
{
}

Socket::~Socket() {
    close();
}

Socket::Socket(Socket&& other) noexcept
    : fd_(other.fd_)
    , type_(other.type_)
    , state_(other.state_)
    , localAddr_(std::move(other.localAddr_))
    , remoteAddr_(std::move(other.remoteAddr_))
    , options_(other.options_)
    , nonBlocking_(other.nonBlocking_)
    , lastError_(other.lastError_)
    , id_(std::move(other.id_))
{
    other.fd_ = INVALID_SOCKET_VALUE;
    other.state_ = SocketState::CLOSED;
}

Socket& Socket::operator=(Socket&& other) noexcept {
    if (this != &other) {
        close();
        fd_ = other.fd_;
        type_ = other.type_;
        state_ = other.state_;
        localAddr_ = std::move(other.localAddr_);
        remoteAddr_ = std::move(other.remoteAddr_);
        options_ = other.options_;
        nonBlocking_ = other.nonBlocking_;
        lastError_ = other.lastError_;
        id_ = std::move(other.id_);
        
        other.fd_ = INVALID_SOCKET_VALUE;
        other.state_ = SocketState::CLOSED;
    }
    return *this;
}

void Socket::setLastError() {
#ifdef QZ_PLATFORM_WINDOWS
    lastError_ = WSAGetLastError();
#else
    lastError_ = errno;
#endif
}

std::string Socket::lastErrorString() const {
#ifdef QZ_PLATFORM_WINDOWS
    char* msg = nullptr;
    FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, lastError_, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPSTR)&msg, 0, nullptr
    );
    std::string result = msg ? msg : "Unknown error";
    LocalFree(msg);
    return result;
#else
    return strerror(lastError_);
#endif
}

bool Socket::create(SocketType type, bool ipv6) {
    if (fd_ != INVALID_SOCKET_VALUE) {
        close();
    }
    
    initializeSocketLibrary();
    
    type_ = type;
    int domain = ipv6 ? AF_INET6 : AF_INET;
    int sockType = (type == SocketType::TCP) ? SOCK_STREAM : SOCK_DGRAM;
    int protocol = (type == SocketType::TCP) ? IPPROTO_TCP : IPPROTO_UDP;
    
    fd_ = ::socket(domain, sockType, protocol);
    if (fd_ == INVALID_SOCKET_VALUE) {
        setLastError();
        state_ = SocketState::ERROR_STATE;
        return false;
    }
    
    state_ = SocketState::CREATED;
    localAddr_.isIPv6 = ipv6;
    return true;
}

bool Socket::bind(const std::string& host, int port) {
    if (fd_ == INVALID_SOCKET_VALUE) {
        lastError_ = EINVAL;
        return false;
    }
    
    struct sockaddr_storage addr;
    socklen_t addrLen;
    std::memset(&addr, 0, sizeof(addr));
    
    if (localAddr_.isIPv6) {
        auto* addr6 = reinterpret_cast<struct sockaddr_in6*>(&addr);
        addr6->sin6_family = AF_INET6;
        addr6->sin6_port = htons(static_cast<uint16_t>(port));
        if (host.empty() || host == "::" || host == "0.0.0.0") {
            addr6->sin6_addr = in6addr_any;
        } else {
            if (inet_pton(AF_INET6, host.c_str(), &addr6->sin6_addr) != 1) {
                setLastError();
                return false;
            }
        }
        addrLen = sizeof(struct sockaddr_in6);
    } else {
        auto* addr4 = reinterpret_cast<struct sockaddr_in*>(&addr);
        addr4->sin_family = AF_INET;
        addr4->sin_port = htons(static_cast<uint16_t>(port));
        if (host.empty() || host == "0.0.0.0") {
            addr4->sin_addr.s_addr = INADDR_ANY;
        } else {
            if (inet_pton(AF_INET, host.c_str(), &addr4->sin_addr) != 1) {
                setLastError();
                return false;
            }
        }
        addrLen = sizeof(struct sockaddr_in);
    }
    
    if (::bind(fd_, reinterpret_cast<struct sockaddr*>(&addr), addrLen) == SOCKET_ERROR_VALUE) {
        setLastError();
        state_ = SocketState::ERROR_STATE;
        return false;
    }
    
    localAddr_.host = host.empty() ? "0.0.0.0" : host;
    localAddr_.port = port;
    state_ = SocketState::BOUND;
    return true;
}

bool Socket::listen(int backlog) {
    if (fd_ == INVALID_SOCKET_VALUE || type_ != SocketType::TCP) {
        lastError_ = EINVAL;
        return false;
    }
    
    if (::listen(fd_, backlog) == SOCKET_ERROR_VALUE) {
        setLastError();
        state_ = SocketState::ERROR_STATE;
        return false;
    }
    
    state_ = SocketState::LISTENING;
    return true;
}

Socket* Socket::accept(SocketAddress& clientAddr) {
    if (fd_ == INVALID_SOCKET_VALUE || state_ != SocketState::LISTENING) {
        lastError_ = EINVAL;
        return nullptr;
    }
    
    struct sockaddr_storage addr;
    socklen_t addrLen = sizeof(addr);
    
    socket_t clientFd = ::accept(fd_, reinterpret_cast<struct sockaddr*>(&addr), &addrLen);
    if (clientFd == INVALID_SOCKET_VALUE) {
        setLastError();
        // EAGAIN/EWOULDBLOCK is not an error for non-blocking sockets
#ifdef QZ_PLATFORM_WINDOWS
        if (lastError_ == WSAEWOULDBLOCK) return nullptr;
#else
        if (lastError_ == EAGAIN || lastError_ == EWOULDBLOCK) return nullptr;
#endif
        return nullptr;
    }
    
    // Extract client address
    char ipStr[INET6_ADDRSTRLEN];
    if (addr.ss_family == AF_INET6) {
        auto* addr6 = reinterpret_cast<struct sockaddr_in6*>(&addr);
        inet_ntop(AF_INET6, &addr6->sin6_addr, ipStr, sizeof(ipStr));
        clientAddr.host = ipStr;
        clientAddr.port = ntohs(addr6->sin6_port);
        clientAddr.isIPv6 = true;
    } else {
        auto* addr4 = reinterpret_cast<struct sockaddr_in*>(&addr);
        inet_ntop(AF_INET, &addr4->sin_addr, ipStr, sizeof(ipStr));
        clientAddr.host = ipStr;
        clientAddr.port = ntohs(addr4->sin_port);
        clientAddr.isIPv6 = false;
    }
    
    Socket* client = new Socket(clientFd, SocketType::TCP);
    client->remoteAddr_ = clientAddr;
    client->state_ = SocketState::CONNECTED;
    
    // Inherit non-blocking mode
    if (nonBlocking_) {
        client->setNonBlocking(true);
    }
    
    stats_.acceptedConnections++;
    stats_.updateActivity();
    
    return client;
}

bool Socket::connect(const std::string& host, int port) {
    if (fd_ == INVALID_SOCKET_VALUE) {
        lastError_ = EINVAL;
        return false;
    }
    
    // Resolve hostname
    struct addrinfo hints, *result;
    std::memset(&hints, 0, sizeof(hints));
    hints.ai_family = localAddr_.isIPv6 ? AF_INET6 : AF_INET;
    hints.ai_socktype = (type_ == SocketType::TCP) ? SOCK_STREAM : SOCK_DGRAM;
    
    std::string portStr = std::to_string(port);
    int status = getaddrinfo(host.c_str(), portStr.c_str(), &hints, &result);
    if (status != 0) {
        lastError_ = status;
        return false;
    }
    
    bool connected = false;
    for (struct addrinfo* p = result; p != nullptr; p = p->ai_next) {
        if (::connect(fd_, p->ai_addr, static_cast<socklen_t>(p->ai_addrlen)) == 0) {
            connected = true;
            break;
        }
        
#ifdef QZ_PLATFORM_WINDOWS
        int err = WSAGetLastError();
        if (err == WSAEWOULDBLOCK || err == WSAEINPROGRESS) {
            connected = true; // Connection in progress for non-blocking
            break;
        }
#else
        if (errno == EINPROGRESS) {
            connected = true; // Connection in progress for non-blocking
            break;
        }
#endif
    }
    
    freeaddrinfo(result);
    
    if (!connected) {
        setLastError();
        state_ = SocketState::ERROR_STATE;
        return false;
    }
    
    remoteAddr_.host = host;
    remoteAddr_.port = port;
    state_ = SocketState::CONNECTED;
    return true;
}

void Socket::close() {
    if (fd_ != INVALID_SOCKET_VALUE) {
        close_socket(fd_);
        fd_ = INVALID_SOCKET_VALUE;
    }
    state_ = SocketState::CLOSED;
}

int Socket::send(const char* data, size_t length) {
    if (fd_ == INVALID_SOCKET_VALUE) {
        lastError_ = EINVAL;
        return -1;
    }
    
    int flags = 0;
#ifdef QZ_PLATFORM_LINUX
    flags = MSG_NOSIGNAL; // Prevent SIGPIPE on Linux
#endif
    
    int sent = ::send(fd_, data, static_cast<int>(length), flags);
    if (sent == SOCKET_ERROR_VALUE) {
        setLastError();
#ifdef QZ_PLATFORM_WINDOWS
        if (lastError_ == WSAEWOULDBLOCK) return 0;
#else
        if (lastError_ == EAGAIN || lastError_ == EWOULDBLOCK) return 0;
#endif
        stats_.errors++;
        return -1;
    }
    
    stats_.bytesSent += sent;
    stats_.packetsSent++;
    stats_.updateActivity();
    return sent;
}

int Socket::send(const std::string& data) {
    return send(data.c_str(), data.length());
}

int Socket::recv(char* buffer, size_t maxLength) {
    if (fd_ == INVALID_SOCKET_VALUE) {
        lastError_ = EINVAL;
        return -1;
    }
    
    int received = ::recv(fd_, buffer, static_cast<int>(maxLength), 0);
    if (received == SOCKET_ERROR_VALUE) {
        setLastError();
#ifdef QZ_PLATFORM_WINDOWS
        if (lastError_ == WSAEWOULDBLOCK) return 0;
#else
        if (lastError_ == EAGAIN || lastError_ == EWOULDBLOCK) return 0;
#endif
        stats_.errors++;
        return -1;
    }
    
    if (received > 0) {
        stats_.bytesReceived += received;
        stats_.packetsReceived++;
        stats_.updateActivity();
    }
    
    return received; // 0 means connection closed by peer
}

std::string Socket::recv(size_t maxLength) {
    std::vector<char> buffer(maxLength);
    int received = recv(buffer.data(), maxLength);
    if (received > 0) {
        return std::string(buffer.data(), received);
    }
    return "";
}

int Socket::sendTo(const char* data, size_t length, const SocketAddress& addr) {
    if (fd_ == INVALID_SOCKET_VALUE) {
        lastError_ = EINVAL;
        return -1;
    }
    
    struct sockaddr_storage destAddr;
    socklen_t addrLen;
    std::memset(&destAddr, 0, sizeof(destAddr));
    
    if (addr.isIPv6) {
        auto* addr6 = reinterpret_cast<struct sockaddr_in6*>(&destAddr);
        addr6->sin6_family = AF_INET6;
        addr6->sin6_port = htons(static_cast<uint16_t>(addr.port));
        inet_pton(AF_INET6, addr.host.c_str(), &addr6->sin6_addr);
        addrLen = sizeof(struct sockaddr_in6);
    } else {
        auto* addr4 = reinterpret_cast<struct sockaddr_in*>(&destAddr);
        addr4->sin_family = AF_INET;
        addr4->sin_port = htons(static_cast<uint16_t>(addr.port));
        inet_pton(AF_INET, addr.host.c_str(), &addr4->sin_addr);
        addrLen = sizeof(struct sockaddr_in);
    }
    
    int sent = ::sendto(fd_, data, static_cast<int>(length), 0,
                        reinterpret_cast<struct sockaddr*>(&destAddr), addrLen);
    if (sent == SOCKET_ERROR_VALUE) {
        setLastError();
        return -1;
    }
    
    stats_.bytesSent += sent;
    stats_.packetsSent++;
    return sent;
}

int Socket::recvFrom(char* buffer, size_t maxLength, SocketAddress& addr) {
    if (fd_ == INVALID_SOCKET_VALUE) {
        lastError_ = EINVAL;
        return -1;
    }
    
    struct sockaddr_storage srcAddr;
    socklen_t addrLen = sizeof(srcAddr);
    
    int received = ::recvfrom(fd_, buffer, static_cast<int>(maxLength), 0,
                              reinterpret_cast<struct sockaddr*>(&srcAddr), &addrLen);
    if (received == SOCKET_ERROR_VALUE) {
        setLastError();
        return -1;
    }
    
    // Extract source address
    char ipStr[INET6_ADDRSTRLEN];
    if (srcAddr.ss_family == AF_INET6) {
        auto* addr6 = reinterpret_cast<struct sockaddr_in6*>(&srcAddr);
        inet_ntop(AF_INET6, &addr6->sin6_addr, ipStr, sizeof(ipStr));
        addr.host = ipStr;
        addr.port = ntohs(addr6->sin6_port);
        addr.isIPv6 = true;
    } else {
        auto* addr4 = reinterpret_cast<struct sockaddr_in*>(&srcAddr);
        inet_ntop(AF_INET, &addr4->sin_addr, ipStr, sizeof(ipStr));
        addr.host = ipStr;
        addr.port = ntohs(addr4->sin_port);
        addr.isIPv6 = false;
    }
    
    stats_.bytesReceived += received;
    stats_.packetsReceived++;
    return received;
}

bool Socket::setOption(const SocketOptions& opts) {
    options_ = opts;
    
    if (!setReuseAddr(opts.reuseAddr)) return false;
    if (!setNonBlocking(opts.nonBlocking)) return false;
    
    if (type_ == SocketType::TCP) {
        if (!setNoDelay(opts.noDelay)) return false;
        if (!setKeepAlive(opts.keepAlive, opts.keepAliveIdle, 
                          opts.keepAliveInterval, opts.keepAliveCount)) return false;
    }
    
    if (!setBufferSizes(opts.recvBufferSize, opts.sendBufferSize)) return false;
    
#ifdef QZ_PLATFORM_LINUX
    if (opts.reusePort) {
        int val = 1;
        if (setsockopt(fd_, SOL_SOCKET, SO_REUSEPORT, &val, sizeof(val)) < 0) {
            setLastError();
            return false;
        }
    }
#endif
    
    return true;
}

bool Socket::setNonBlocking(bool nonBlocking) {
    if (fd_ == INVALID_SOCKET_VALUE) return false;
    
#ifdef QZ_PLATFORM_WINDOWS
    u_long mode = nonBlocking ? 1 : 0;
    if (ioctlsocket(fd_, FIONBIO, &mode) != 0) {
        setLastError();
        return false;
    }
#else
    int flags = fcntl(fd_, F_GETFL, 0);
    if (flags < 0) {
        setLastError();
        return false;
    }
    
    flags = nonBlocking ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK);
    if (fcntl(fd_, F_SETFL, flags) < 0) {
        setLastError();
        return false;
    }
#endif
    
    nonBlocking_ = nonBlocking;
    return true;
}

bool Socket::setReuseAddr(bool reuse) {
    if (fd_ == INVALID_SOCKET_VALUE) return false;
    
    int val = reuse ? 1 : 0;
#ifdef QZ_PLATFORM_WINDOWS
    if (setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, (const char*)&val, sizeof(val)) != 0) {
#else
    if (setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val)) < 0) {
#endif
        setLastError();
        return false;
    }
    return true;
}

bool Socket::setNoDelay(bool noDelay) {
    if (fd_ == INVALID_SOCKET_VALUE || type_ != SocketType::TCP) return false;
    
    int val = noDelay ? 1 : 0;
#ifdef QZ_PLATFORM_WINDOWS
    if (setsockopt(fd_, IPPROTO_TCP, TCP_NODELAY, (const char*)&val, sizeof(val)) != 0) {
#else
    if (setsockopt(fd_, IPPROTO_TCP, TCP_NODELAY, &val, sizeof(val)) < 0) {
#endif
        setLastError();
        return false;
    }
    return true;
}

bool Socket::setKeepAlive(bool keepAlive, int idle, int interval, int count) {
    if (fd_ == INVALID_SOCKET_VALUE || type_ != SocketType::TCP) return false;
    
    int val = keepAlive ? 1 : 0;
#ifdef QZ_PLATFORM_WINDOWS
    if (setsockopt(fd_, SOL_SOCKET, SO_KEEPALIVE, (const char*)&val, sizeof(val)) != 0) {
        setLastError();
        return false;
    }
    
    if (keepAlive) {
        struct tcp_keepalive ka;
        ka.onoff = 1;
        ka.keepalivetime = idle * 1000;      // ms
        ka.keepaliveinterval = interval * 1000; // ms
        DWORD bytesReturned;
        if (WSAIoctl(fd_, SIO_KEEPALIVE_VALS, &ka, sizeof(ka), nullptr, 0, &bytesReturned, nullptr, nullptr) != 0) {
            setLastError();
            return false;
        }
    }
#else
    if (setsockopt(fd_, SOL_SOCKET, SO_KEEPALIVE, &val, sizeof(val)) < 0) {
        setLastError();
        return false;
    }
    
    if (keepAlive) {
#ifdef QZ_PLATFORM_LINUX
        if (setsockopt(fd_, IPPROTO_TCP, TCP_KEEPIDLE, &idle, sizeof(idle)) < 0) {
            setLastError();
            return false;
        }
        if (setsockopt(fd_, IPPROTO_TCP, TCP_KEEPINTVL, &interval, sizeof(interval)) < 0) {
            setLastError();
            return false;
        }
        if (setsockopt(fd_, IPPROTO_TCP, TCP_KEEPCNT, &count, sizeof(count)) < 0) {
            setLastError();
            return false;
        }
#elif defined(QZ_PLATFORM_MACOS)
        // macOS uses TCP_KEEPALIVE instead of TCP_KEEPIDLE
        if (setsockopt(fd_, IPPROTO_TCP, TCP_KEEPALIVE, &idle, sizeof(idle)) < 0) {
            setLastError();
            return false;
        }
        // macOS doesn't support TCP_KEEPINTVL and TCP_KEEPCNT at socket level
#endif
    }
#endif
    
    return true;
}

bool Socket::setBufferSizes(int recvSize, int sendSize) {
    if (fd_ == INVALID_SOCKET_VALUE) return false;
    
#ifdef QZ_PLATFORM_WINDOWS
    if (setsockopt(fd_, SOL_SOCKET, SO_RCVBUF, (const char*)&recvSize, sizeof(recvSize)) != 0) {
        setLastError();
        return false;
    }
    if (setsockopt(fd_, SOL_SOCKET, SO_SNDBUF, (const char*)&sendSize, sizeof(sendSize)) != 0) {
        setLastError();
        return false;
    }
#else
    if (setsockopt(fd_, SOL_SOCKET, SO_RCVBUF, &recvSize, sizeof(recvSize)) < 0) {
        setLastError();
        return false;
    }
    if (setsockopt(fd_, SOL_SOCKET, SO_SNDBUF, &sendSize, sizeof(sendSize)) < 0) {
        setLastError();
        return false;
    }
#endif
    
    return true;
}

bool Socket::setTimeouts(int sendTimeoutMs, int recvTimeoutMs) {
    if (fd_ == INVALID_SOCKET_VALUE) return false;
    
#ifdef QZ_PLATFORM_WINDOWS
    // Windows uses DWORD milliseconds
    DWORD sendTimeout = static_cast<DWORD>(sendTimeoutMs);
    DWORD recvTimeout = static_cast<DWORD>(recvTimeoutMs);
    
    if (sendTimeoutMs > 0) {
        if (setsockopt(fd_, SOL_SOCKET, SO_SNDTIMEO, (const char*)&sendTimeout, sizeof(sendTimeout)) != 0) {
            setLastError();
            return false;
        }
    }
    if (recvTimeoutMs > 0) {
        if (setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, (const char*)&recvTimeout, sizeof(recvTimeout)) != 0) {
            setLastError();
            return false;
        }
    }
#else
    // POSIX uses struct timeval
    if (sendTimeoutMs > 0) {
        struct timeval tv;
        tv.tv_sec = sendTimeoutMs / 1000;
        tv.tv_usec = (sendTimeoutMs % 1000) * 1000;
        if (setsockopt(fd_, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) < 0) {
            setLastError();
            return false;
        }
    }
    if (recvTimeoutMs > 0) {
        struct timeval tv;
        tv.tv_sec = recvTimeoutMs / 1000;
        tv.tv_usec = (recvTimeoutMs % 1000) * 1000;
        if (setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
            setLastError();
            return false;
        }
    }
#endif
    
    options_.sendTimeout = sendTimeoutMs;
    options_.recvTimeout = recvTimeoutMs;
    return true;
}

// ============================================================================
// Platform-specific Multiplexer Implementations
// ============================================================================

#ifdef QZ_PLATFORM_LINUX
// Linux epoll implementation

EpollMultiplexer::EpollMultiplexer(int maxEvents)
    : epollFd_(-1), maxEvents_(maxEvents), numReady_(0) {
    events_.resize(maxEvents);
}

EpollMultiplexer::~EpollMultiplexer() {
    destroy();
}

bool EpollMultiplexer::create() {
    epollFd_ = epoll_create1(EPOLL_CLOEXEC);
    return epollFd_ >= 0;
}

void EpollMultiplexer::destroy() {
    if (epollFd_ >= 0) {
        ::close(epollFd_);
        epollFd_ = -1;
    }
    sockets_.clear();
}

bool EpollMultiplexer::add(Socket* socket, uint32_t events, void* userData) {
    if (epollFd_ < 0 || !socket || !socket->isValid()) return false;
    
    struct epoll_event ev;
    ev.events = 0;
    if (events & EVENT_READ)  ev.events |= EPOLLIN;
    if (events & EVENT_WRITE) ev.events |= EPOLLOUT;
    ev.events |= EPOLLET; // Edge-triggered for high performance
    ev.data.ptr = socket;
    
    if (epoll_ctl(epollFd_, EPOLL_CTL_ADD, socket->handle(), &ev) < 0) {
        return false;
    }
    
    sockets_[socket->handle()] = socket;
    return true;
}

bool EpollMultiplexer::modify(Socket* socket, uint32_t events) {
    if (epollFd_ < 0 || !socket || !socket->isValid()) return false;
    
    struct epoll_event ev;
    ev.events = 0;
    if (events & EVENT_READ)  ev.events |= EPOLLIN;
    if (events & EVENT_WRITE) ev.events |= EPOLLOUT;
    ev.events |= EPOLLET;
    ev.data.ptr = socket;
    
    return epoll_ctl(epollFd_, EPOLL_CTL_MOD, socket->handle(), &ev) == 0;
}

bool EpollMultiplexer::remove(Socket* socket) {
    if (epollFd_ < 0 || !socket || !socket->isValid()) return false;
    
    sockets_.erase(socket->handle());
    return epoll_ctl(epollFd_, EPOLL_CTL_DEL, socket->handle(), nullptr) == 0;
}

int EpollMultiplexer::poll(int timeoutMs) {
    if (epollFd_ < 0) return -1;
    numReady_ = epoll_wait(epollFd_, events_.data(), maxEvents_, timeoutMs);
    return numReady_;
}

void EpollMultiplexer::processEvents(const EventCallback& callback) {
    for (int i = 0; i < numReady_; i++) {
        Socket* socket = static_cast<Socket*>(events_[i].data.ptr);
        uint32_t ev = events_[i].events;
        
        if (ev & EPOLLERR) {
            callback(socket, SocketEvent::ERROR_EVENT);
        } else if (ev & EPOLLHUP) {
            callback(socket, SocketEvent::CLOSE);
        } else {
            if (ev & EPOLLIN) {
                if (socket->state() == SocketState::LISTENING) {
                    callback(socket, SocketEvent::ACCEPT);
                } else {
                    callback(socket, SocketEvent::READABLE);
                }
            }
            if (ev & EPOLLOUT) {
                callback(socket, SocketEvent::WRITABLE);
            }
        }
    }
}
#endif // QZ_PLATFORM_LINUX

#ifdef QZ_PLATFORM_MACOS
// macOS kqueue implementation

KqueueMultiplexer::KqueueMultiplexer(int maxEvents)
    : kqueueFd_(-1), maxEvents_(maxEvents), numReady_(0) {
    events_.resize(maxEvents);
}

KqueueMultiplexer::~KqueueMultiplexer() {
    destroy();
}

bool KqueueMultiplexer::create() {
    kqueueFd_ = kqueue();
    return kqueueFd_ >= 0;
}

void KqueueMultiplexer::destroy() {
    if (kqueueFd_ >= 0) {
        ::close(kqueueFd_);
        kqueueFd_ = -1;
    }
    sockets_.clear();
    changes_.clear();
}

bool KqueueMultiplexer::add(Socket* socket, uint32_t events, void* userData) {
    if (kqueueFd_ < 0 || !socket || !socket->isValid()) return false;
    
    struct kevent ev[2];
    int n = 0;
    
    if (events & EVENT_READ) {
        EV_SET(&ev[n++], socket->handle(), EVFILT_READ, EV_ADD | EV_CLEAR, 0, 0, socket);
    }
    if (events & EVENT_WRITE) {
        EV_SET(&ev[n++], socket->handle(), EVFILT_WRITE, EV_ADD | EV_CLEAR, 0, 0, socket);
    }
    
    if (kevent(kqueueFd_, ev, n, nullptr, 0, nullptr) < 0) {
        return false;
    }
    
    sockets_[socket->handle()] = socket;
    return true;
}

bool KqueueMultiplexer::modify(Socket* socket, uint32_t events) {
    if (kqueueFd_ < 0 || !socket || !socket->isValid()) return false;
    
    // Remove existing and re-add with new events
    struct kevent ev[4];
    int n = 0;
    
    // Remove both filters first
    EV_SET(&ev[n++], socket->handle(), EVFILT_READ, EV_DELETE, 0, 0, nullptr);
    EV_SET(&ev[n++], socket->handle(), EVFILT_WRITE, EV_DELETE, 0, 0, nullptr);
    
    // Ignore errors from delete (filter may not exist)
    kevent(kqueueFd_, ev, n, nullptr, 0, nullptr);
    
    n = 0;
    if (events & EVENT_READ) {
        EV_SET(&ev[n++], socket->handle(), EVFILT_READ, EV_ADD | EV_CLEAR, 0, 0, socket);
    }
    if (events & EVENT_WRITE) {
        EV_SET(&ev[n++], socket->handle(), EVFILT_WRITE, EV_ADD | EV_CLEAR, 0, 0, socket);
    }
    
    if (n > 0) {
        return kevent(kqueueFd_, ev, n, nullptr, 0, nullptr) >= 0;
    }
    return true;
}

bool KqueueMultiplexer::remove(Socket* socket) {
    if (kqueueFd_ < 0 || !socket || !socket->isValid()) return false;
    
    struct kevent ev[2];
    EV_SET(&ev[0], socket->handle(), EVFILT_READ, EV_DELETE, 0, 0, nullptr);
    EV_SET(&ev[1], socket->handle(), EVFILT_WRITE, EV_DELETE, 0, 0, nullptr);
    
    // Ignore errors (filters may not exist)
    kevent(kqueueFd_, ev, 2, nullptr, 0, nullptr);
    sockets_.erase(socket->handle());
    return true;
}

int KqueueMultiplexer::poll(int timeoutMs) {
    if (kqueueFd_ < 0) return -1;
    
    struct timespec ts;
    struct timespec* tsPtr = nullptr;
    
    if (timeoutMs >= 0) {
        ts.tv_sec = timeoutMs / 1000;
        ts.tv_nsec = (timeoutMs % 1000) * 1000000;
        tsPtr = &ts;
    }
    
    numReady_ = kevent(kqueueFd_, nullptr, 0, events_.data(), maxEvents_, tsPtr);
    return numReady_;
}

void KqueueMultiplexer::processEvents(const EventCallback& callback) {
    for (int i = 0; i < numReady_; i++) {
        Socket* socket = static_cast<Socket*>(events_[i].udata);
        
        if (events_[i].flags & EV_ERROR) {
            callback(socket, SocketEvent::ERROR_EVENT);
        } else if (events_[i].flags & EV_EOF) {
            callback(socket, SocketEvent::CLOSE);
        } else if (events_[i].filter == EVFILT_READ) {
            if (socket->state() == SocketState::LISTENING) {
                callback(socket, SocketEvent::ACCEPT);
            } else {
                callback(socket, SocketEvent::READABLE);
            }
        } else if (events_[i].filter == EVFILT_WRITE) {
            callback(socket, SocketEvent::WRITABLE);
        }
    }
}
#endif // QZ_PLATFORM_MACOS

#ifdef QZ_PLATFORM_WINDOWS
// Windows IOCP implementation

IOCPMultiplexer::IOCPMultiplexer(int concurrency)
    : iocpHandle_(INVALID_HANDLE_VALUE), concurrency_(concurrency), numCompletions_(0) {
    completions_.resize(256);
}

IOCPMultiplexer::~IOCPMultiplexer() {
    destroy();
}

bool IOCPMultiplexer::create() {
    iocpHandle_ = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, concurrency_);
    return iocpHandle_ != INVALID_HANDLE_VALUE;
}

void IOCPMultiplexer::destroy() {
    if (iocpHandle_ != INVALID_HANDLE_VALUE) {
        CloseHandle(iocpHandle_);
        iocpHandle_ = INVALID_HANDLE_VALUE;
    }
    sockets_.clear();
}

bool IOCPMultiplexer::add(Socket* socket, uint32_t events, void* userData) {
    if (iocpHandle_ == INVALID_HANDLE_VALUE || !socket || !socket->isValid()) return false;
    
    HANDLE result = CreateIoCompletionPort(
        (HANDLE)socket->handle(),
        iocpHandle_,
        (ULONG_PTR)socket,
        0
    );
    
    if (result == nullptr) return false;
    
    sockets_[socket->handle()] = socket;
    return true;
}

bool IOCPMultiplexer::modify(Socket* socket, uint32_t events) {
    // IOCP doesn't require modification - events are driven by operations
    return true;
}

bool IOCPMultiplexer::remove(Socket* socket) {
    if (!socket || !socket->isValid()) return false;
    sockets_.erase(socket->handle());
    // IOCP association is removed when socket is closed
    return true;
}

int IOCPMultiplexer::poll(int timeoutMs) {
    if (iocpHandle_ == INVALID_HANDLE_VALUE) return -1;
    
    DWORD timeout = (timeoutMs < 0) ? INFINITE : static_cast<DWORD>(timeoutMs);
    
    BOOL result = GetQueuedCompletionStatusEx(
        iocpHandle_,
        completions_.data(),
        static_cast<ULONG>(completions_.size()),
        &numCompletions_,
        timeout,
        FALSE
    );
    
    if (!result) {
        if (GetLastError() == WAIT_TIMEOUT) {
            numCompletions_ = 0;
            return 0;
        }
        return -1;
    }
    
    return static_cast<int>(numCompletions_);
}

void IOCPMultiplexer::processEvents(const EventCallback& callback) {
    for (ULONG i = 0; i < numCompletions_; i++) {
        Socket* socket = reinterpret_cast<Socket*>(completions_[i].lpCompletionKey);
        // Note: For full IOCP support, you'd need OVERLAPPED structures
        // This is a simplified version
        callback(socket, SocketEvent::READABLE);
    }
}
#endif // QZ_PLATFORM_WINDOWS

// ============================================================================
// Factory function
// ============================================================================

std::unique_ptr<IOMultiplexer> createMultiplexer(int maxEvents) {
#ifdef QZ_PLATFORM_LINUX
    return std::make_unique<EpollMultiplexer>(maxEvents);
#elif defined(QZ_PLATFORM_MACOS)
    return std::make_unique<KqueueMultiplexer>(maxEvents);
#elif defined(QZ_PLATFORM_WINDOWS)
    return std::make_unique<IOCPMultiplexer>();
#else
    return nullptr;
#endif
}

// ============================================================================
// Hostname Resolution
// ============================================================================

std::string resolveHostname(const std::string& hostname, bool preferIPv6) {
    initializeSocketLibrary();
    
    struct addrinfo hints, *result;
    std::memset(&hints, 0, sizeof(hints));
    hints.ai_family = preferIPv6 ? AF_INET6 : AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    
    if (getaddrinfo(hostname.c_str(), nullptr, &hints, &result) != 0) {
        return "";
    }
    
    char ipStr[INET6_ADDRSTRLEN];
    if (result->ai_family == AF_INET6) {
        auto* addr = reinterpret_cast<struct sockaddr_in6*>(result->ai_addr);
        inet_ntop(AF_INET6, &addr->sin6_addr, ipStr, sizeof(ipStr));
    } else {
        auto* addr = reinterpret_cast<struct sockaddr_in*>(result->ai_addr);
        inet_ntop(AF_INET, &addr->sin_addr, ipStr, sizeof(ipStr));
    }
    
    freeaddrinfo(result);
    return ipStr;
}

std::vector<std::string> getLocalAddresses(bool includeIPv6) {
    std::vector<std::string> addresses;
    
    // Get hostname
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) != 0) {
        return addresses;
    }
    
    struct addrinfo hints, *result;
    std::memset(&hints, 0, sizeof(hints));
    hints.ai_family = includeIPv6 ? AF_UNSPEC : AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    
    if (getaddrinfo(hostname, nullptr, &hints, &result) != 0) {
        return addresses;
    }
    
    for (struct addrinfo* p = result; p != nullptr; p = p->ai_next) {
        char ipStr[INET6_ADDRSTRLEN];
        if (p->ai_family == AF_INET6) {
            auto* addr = reinterpret_cast<struct sockaddr_in6*>(p->ai_addr);
            inet_ntop(AF_INET6, &addr->sin6_addr, ipStr, sizeof(ipStr));
        } else {
            auto* addr = reinterpret_cast<struct sockaddr_in*>(p->ai_addr);
            inet_ntop(AF_INET, &addr->sin_addr, ipStr, sizeof(ipStr));
        }
        addresses.push_back(ipStr);
    }
    
    freeaddrinfo(result);
    return addresses;
}

// ============================================================================
// Socket Manager Implementation
// ============================================================================

SocketManager& SocketManager::instance() {
    static SocketManager instance;
    return instance;
}

Socket* SocketManager::createSocket(SocketType type) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto socket = std::make_unique<Socket>(type);
    if (!socket->create(type)) {
        return nullptr;
    }
    std::string id = socket->id();
    Socket* ptr = socket.get();
    sockets_[id] = std::move(socket);
    return ptr;
}

std::string SocketManager::storeSocket(std::unique_ptr<Socket> socket) {
    if (!socket) return "";
    std::lock_guard<std::mutex> lock(mutex_);
    std::string id = socket->id();
    sockets_[id] = std::move(socket);
    return id;
}

Socket* SocketManager::getSocket(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sockets_.find(id);
    if (it == sockets_.end()) return nullptr;
    return it->second.get();
}

bool SocketManager::removeSocket(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    return sockets_.erase(id) > 0;
}

std::vector<std::string> SocketManager::getAllSocketIds() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> ids;
    ids.reserve(sockets_.size());
    for (const auto& [id, _] : sockets_) {
        ids.push_back(id);
    }
    return ids;
}

size_t SocketManager::socketCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return sockets_.size();
}

} // namespace qz_socket
