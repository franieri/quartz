// ============================================================================
// system.net.uwsgi - Core Implementation
// High-performance uWSGI protocol server
// ============================================================================

#include "uwsgi_types.h"
#include <algorithm>
#include <cstring>
#include <sstream>
#include <iomanip>
#ifndef QZ_PLATFORM_WINDOWS
#include <sys/stat.h>
#endif

namespace uwsgi {

// ============================================================================
// Static ID Counters
// ============================================================================

std::atomic<uint64_t> Connection::nextId_{0};
std::atomic<uint64_t> UwsgiServer::nextServerId_{0};

// ============================================================================
// HTTP Status Text Lookup
// ============================================================================

std::string getStatusText(int code) {
    switch (code) {
        case 100: return "Continue";
        case 101: return "Switching Protocols";
        case 200: return "OK";
        case 201: return "Created";
        case 202: return "Accepted";
        case 204: return "No Content";
        case 206: return "Partial Content";
        case 301: return "Moved Permanently";
        case 302: return "Found";
        case 303: return "See Other";
        case 304: return "Not Modified";
        case 307: return "Temporary Redirect";
        case 308: return "Permanent Redirect";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 406: return "Not Acceptable";
        case 408: return "Request Timeout";
        case 409: return "Conflict";
        case 410: return "Gone";
        case 411: return "Length Required";
        case 413: return "Payload Too Large";
        case 414: return "URI Too Long";
        case 415: return "Unsupported Media Type";
        case 416: return "Range Not Satisfiable";
        case 422: return "Unprocessable Entity";
        case 429: return "Too Many Requests";
        case 500: return "Internal Server Error";
        case 501: return "Not Implemented";
        case 502: return "Bad Gateway";
        case 503: return "Service Unavailable";
        case 504: return "Gateway Timeout";
        default: return "Unknown";
    }
}

// ============================================================================
// URL Encoding/Decoding
// ============================================================================

std::string urlDecode(const std::string& encoded) {
    std::string result;
    result.reserve(encoded.size());
    
    for (size_t i = 0; i < encoded.size(); ++i) {
        if (encoded[i] == '%' && i + 2 < encoded.size()) {
            int value;
            std::istringstream iss(encoded.substr(i + 1, 2));
            if (iss >> std::hex >> value) {
                result += static_cast<char>(value);
                i += 2;
                continue;
            }
        } else if (encoded[i] == '+') {
            result += ' ';
            continue;
        }
        result += encoded[i];
    }
    return result;
}

std::string urlEncode(const std::string& str) {
    std::ostringstream encoded;
    encoded.fill('0');
    encoded << std::hex;
    
    for (unsigned char c : str) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            encoded << c;
        } else {
            encoded << '%' << std::setw(2) << (int)c;
        }
    }
    return encoded.str();
}

std::unordered_map<std::string, std::string> parseQueryString(const std::string& query) {
    std::unordered_map<std::string, std::string> result;
    
    std::istringstream stream(query);
    std::string pair;
    
    while (std::getline(stream, pair, '&')) {
        size_t eqPos = pair.find('=');
        if (eqPos != std::string::npos) {
            std::string key = urlDecode(pair.substr(0, eqPos));
            std::string value = urlDecode(pair.substr(eqPos + 1));
            result[key] = value;
        } else if (!pair.empty()) {
            result[urlDecode(pair)] = "";
        }
    }
    return result;
}

std::string getMimeType(const std::string& extension) {
    static const std::unordered_map<std::string, std::string> mimeTypes = {
        {".html", "text/html"},
        {".htm", "text/html"},
        {".css", "text/css"},
        {".js", "application/javascript"},
        {".json", "application/json"},
        {".xml", "application/xml"},
        {".txt", "text/plain"},
        {".png", "image/png"},
        {".jpg", "image/jpeg"},
        {".jpeg", "image/jpeg"},
        {".gif", "image/gif"},
        {".svg", "image/svg+xml"},
        {".ico", "image/x-icon"},
        {".pdf", "application/pdf"},
        {".zip", "application/zip"},
        {".gz", "application/gzip"},
        {".woff", "font/woff"},
        {".woff2", "font/woff2"},
        {".ttf", "font/ttf"},
        {".mp3", "audio/mpeg"},
        {".mp4", "video/mp4"},
        {".webm", "video/webm"},
        {".wasm", "application/wasm"},
    };
    
    auto it = mimeTypes.find(extension);
    return it != mimeTypes.end() ? it->second : "application/octet-stream";
}

std::string cgiToHttpHeader(const std::string& cgiName) {
    // Remove HTTP_ prefix and convert to HTTP header format
    std::string name = cgiName;
    if (name.substr(0, 5) == "HTTP_") {
        name = name.substr(5);
    }
    
    std::string result;
    bool capitalizeNext = true;
    
    for (char c : name) {
        if (c == '_') {
            result += '-';
            capitalizeNext = true;
        } else {
            result += capitalizeNext ? std::toupper(c) : std::tolower(c);
            capitalizeNext = false;
        }
    }
    return result;
}

// ============================================================================
// HttpRequest Implementation
// ============================================================================

std::string HttpRequest::getHeader(const std::string& name) const {
    // Case-insensitive search
    std::string lowerName = name;
    std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
    
    for (const auto& [key, value] : headers) {
        std::string lowerKey = key;
        std::transform(lowerKey.begin(), lowerKey.end(), lowerKey.begin(), ::tolower);
        if (lowerKey == lowerName) {
            return value;
        }
    }
    return "";
}

std::string HttpRequest::getVar(const std::string& name) const {
    auto it = uwsgiVars.find(name);
    return it != uwsgiVars.end() ? it->second : "";
}

int64_t HttpRequest::elapsedMs() const {
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now - startTime).count();
}

// ============================================================================
// HttpResponse Implementation
// ============================================================================

void HttpResponse::status(int code, const std::string& text) {
    statusCode = code;
    statusText = text.empty() ? getStatusText(code) : text;
}

void HttpResponse::setHeader(const std::string& name, const std::string& value) {
    headers[name] = value;
}

void HttpResponse::setContentType(const std::string& type) {
    headers["Content-Type"] = type;
}

void HttpResponse::setContentLength(size_t length) {
    headers["Content-Length"] = std::to_string(length);
}

std::string HttpResponse::build() const {
    std::ostringstream ss;
    
    // Status line
    ss << "HTTP/1.1 " << statusCode << " " << statusText << "\r\n";
    
    // Headers
    for (const auto& [name, value] : headers) {
        ss << name << ": " << value << "\r\n";
    }
    
    // Add Content-Length if not present and we have a body
    if (headers.find("Content-Length") == headers.end() && !body.empty()) {
        ss << "Content-Length: " << body.size() << "\r\n";
    }
    
    // End headers
    ss << "\r\n";
    
    // Body
    if (!body.empty()) {
        ss << body;
    }
    
    return ss.str();
}

HttpResponse HttpResponse::ok(const std::string& body, const std::string& contentType) {
    HttpResponse resp;
    resp.statusCode = 200;
    resp.statusText = "OK";
    resp.body = body;
    resp.headers["Content-Type"] = contentType;
    resp.headers["Content-Length"] = std::to_string(body.size());
    return resp;
}

HttpResponse HttpResponse::json(const std::string& jsonBody) {
    return ok(jsonBody, "application/json");
}

HttpResponse HttpResponse::html(const std::string& htmlBody) {
    return ok(htmlBody, "text/html; charset=utf-8");
}

HttpResponse HttpResponse::notFound(const std::string& message) {
    HttpResponse resp;
    resp.statusCode = 404;
    resp.statusText = "Not Found";
    resp.body = message;
    resp.headers["Content-Type"] = "text/plain";
    return resp;
}

HttpResponse HttpResponse::serverError(const std::string& message) {
    HttpResponse resp;
    resp.statusCode = 500;
    resp.statusText = "Internal Server Error";
    resp.body = message;
    resp.headers["Content-Type"] = "text/plain";
    return resp;
}

HttpResponse HttpResponse::redirect(const std::string& location, int code) {
    HttpResponse resp;
    resp.statusCode = code;
    resp.statusText = getStatusText(code);
    resp.headers["Location"] = location;
    return resp;
}

// ============================================================================
// Connection Implementation
// ============================================================================

Connection::Connection(socket_t fd, const std::string& remoteAddr, int remotePort)
    : fd_(fd)
    , remoteAddr_(remoteAddr)
    , remotePort_(remotePort)
    , state_(ConnectionState::READING_HEADER)
    , recvOffset_(0)
    , sendOffset_(0)
    , bytesReceived_(0)
    , bytesSent_(0)
{
    id_ = "conn_" + std::to_string(nextId_++);
    recvBuffer_.resize(RECV_BUFFER_SIZE);
    lastActivity_ = std::chrono::steady_clock::now();
}

Connection::~Connection() {
    if (fd_ != INVALID_SOCKET_VALUE) {
        close_socket(fd_);
    }
}

int Connection::readData() {
    if (fd_ == INVALID_SOCKET_VALUE) return -1;
    
    // Ensure buffer has space
    if (recvOffset_ >= recvBuffer_.size()) {
        if (recvBuffer_.size() >= MAX_BODY_SIZE) {
            return -1;  // Buffer overflow
        }
        recvBuffer_.resize(std::min(recvBuffer_.size() * 2, MAX_BODY_SIZE));
    }
    
    ssize_t n = ::recv(fd_, recvBuffer_.data() + recvOffset_, 
                       recvBuffer_.size() - recvOffset_, 0);
    
    if (n > 0) {
        recvOffset_ += n;
        bytesReceived_ += n;
        lastActivity_ = std::chrono::steady_clock::now();
        return static_cast<int>(n);
    } else if (n == 0) {
        return -1;  // Connection closed
    } else {
#ifdef QZ_PLATFORM_WINDOWS
        int err = WSAGetLastError();
        if (err == WSAEWOULDBLOCK) return 0;
#else
        if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
#endif
        return -1;  // Error
    }
}

int Connection::writeData(const char* data, size_t length) {
    if (fd_ == INVALID_SOCKET_VALUE) return -1;
    
    ssize_t n = ::send(fd_, data, length, 0);
    
    if (n > 0) {
        bytesSent_ += n;
        lastActivity_ = std::chrono::steady_clock::now();
        return static_cast<int>(n);
    } else if (n == 0) {
        return 0;
    } else {
#ifdef QZ_PLATFORM_WINDOWS
        int err = WSAGetLastError();
        if (err == WSAEWOULDBLOCK) return 0;
#else
        if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
#endif
        return -1;
    }
}

int Connection::writeData(const std::string& data) {
    return writeData(data.c_str(), data.size());
}

int Connection::flushSendBuffer() {
    if (sendBuffer_.empty() || sendOffset_ >= sendBuffer_.size()) {
        return 0;
    }
    
    int written = writeData(sendBuffer_.data() + sendOffset_, 
                            sendBuffer_.size() - sendOffset_);
    
    if (written > 0) {
        sendOffset_ += written;
        if (sendOffset_ >= sendBuffer_.size()) {
            // All data sent, clear buffer
            sendBuffer_.clear();
            sendOffset_ = 0;
        }
    }
    return written;
}

bool Connection::parseHeader() {
    // Need 4 bytes for uWSGI header
    if (recvOffset_ < 4) return false;
    
    std::memcpy(&uwsgiHeader_, recvBuffer_.data(), 4);
    
    // Validate header (datasize is uint16_t, so max is 64KB which is within our limit)
    // The check is mostly for documentation - any uint16_t value is valid
    if (static_cast<size_t>(uwsgiHeader_.datasize) > MAX_UWSGI_PACKET_SIZE) {
        state_ = ConnectionState::CLOSING;
        return false;
    }
    
    // Remove header from buffer
    std::memmove(recvBuffer_.data(), recvBuffer_.data() + 4, recvOffset_ - 4);
    recvOffset_ -= 4;
    
    state_ = ConnectionState::READING_VARS;
    return true;
}

bool Connection::parseVars() {
    // Need full vars block
    if (recvOffset_ < uwsgiHeader_.datasize) return false;
    
    // Parse uWSGI vars (key-value pairs with 2-byte length prefixes)
    size_t offset = 0;
    const char* data = recvBuffer_.data();
    
    while (offset + 4 <= uwsgiHeader_.datasize) {
        // Key length (2 bytes, little-endian)
        uint16_t keyLen = *reinterpret_cast<const uint16_t*>(data + offset);
        offset += 2;
        
        if (offset + keyLen + 2 > uwsgiHeader_.datasize) break;
        
        std::string key(data + offset, keyLen);
        offset += keyLen;
        
        // Value length (2 bytes, little-endian)
        uint16_t valLen = *reinterpret_cast<const uint16_t*>(data + offset);
        offset += 2;
        
        if (offset + valLen > uwsgiHeader_.datasize) break;
        
        std::string value(data + offset, valLen);
        offset += valLen;
        
        // Store variable
        request_.uwsgiVars[key] = value;
        
        // Map common CGI vars to request fields
        if (key == "REQUEST_METHOD") {
            request_.method = value;
        } else if (key == "REQUEST_URI") {
            request_.uri = value;
            // Parse path and query string
            size_t qPos = value.find('?');
            if (qPos != std::string::npos) {
                request_.path = value.substr(0, qPos);
                request_.queryString = value.substr(qPos + 1);
            } else {
                request_.path = value;
            }
        } else if (key == "PATH_INFO") {
            if (request_.path.empty()) request_.path = value;
        } else if (key == "QUERY_STRING") {
            request_.queryString = value;
        } else if (key == "SERVER_PROTOCOL") {
            request_.protocol = value;
        } else if (key == "HTTP_HOST" || key == "SERVER_NAME") {
            if (request_.host.empty()) request_.host = value;
        } else if (key == "REMOTE_ADDR") {
            request_.remoteAddr = value;
        } else if (key == "REMOTE_PORT") {
            request_.remotePort = std::stoi(value);
        } else if (key == "CONTENT_TYPE") {
            request_.contentType = value;
            request_.headers["Content-Type"] = value;
        } else if (key == "CONTENT_LENGTH") {
            request_.contentLength = std::stoull(value);
            request_.headers["Content-Length"] = value;
        } else if (key.substr(0, 5) == "HTTP_") {
            // Convert HTTP_X_Y to X-Y header
            std::string headerName = cgiToHttpHeader(key);
            request_.headers[headerName] = value;
        }
    }
    
    // Remove vars from buffer
    std::memmove(recvBuffer_.data(), recvBuffer_.data() + uwsgiHeader_.datasize, 
                 recvOffset_ - uwsgiHeader_.datasize);
    recvOffset_ -= uwsgiHeader_.datasize;
    
    // Check if there's a body to read
    if (request_.contentLength > 0) {
        state_ = ConnectionState::READING_BODY;
    } else {
        state_ = ConnectionState::PROCESSING;
    }
    
    return true;
}

bool Connection::parseBody() {
    // Wait for full body
    if (recvOffset_ < request_.contentLength) return false;
    
    // Extract body
    request_.body.assign(recvBuffer_.data(), request_.contentLength);
    
    // Remove body from buffer
    std::memmove(recvBuffer_.data(), recvBuffer_.data() + request_.contentLength,
                 recvOffset_ - request_.contentLength);
    recvOffset_ -= request_.contentLength;
    
    state_ = ConnectionState::PROCESSING;
    return true;
}

void Connection::setResponse(const HttpResponse& response) {
    sendBuffer_ = response.build();
    sendOffset_ = 0;
    state_ = ConnectionState::SENDING_RESPONSE;
}

void Connection::setResponse(const std::string& rawResponse) {
    sendBuffer_ = rawResponse;
    sendOffset_ = 0;
    state_ = ConnectionState::SENDING_RESPONSE;
}

void Connection::reset() {
    // Reset for keep-alive
    recvOffset_ = 0;
    sendBuffer_.clear();
    sendOffset_ = 0;
    request_ = HttpRequest();
    uwsgiHeader_ = UwsgiHeader();
    state_ = ConnectionState::READING_HEADER;
    lastActivity_ = std::chrono::steady_clock::now();
}

bool Connection::isTimedOut(int timeoutMs) const {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastActivity_).count();
    return elapsed > timeoutMs;
}

// ============================================================================
// UwsgiServer Implementation
// ============================================================================

UwsgiServer::UwsgiServer()
    : running_(false)
    , serverFd_(INVALID_SOCKET_VALUE)
    , useAsyncHandler_(false)
#ifdef QZ_PLATFORM_LINUX
    , epollFd_(-1)
#elif defined(QZ_PLATFORM_MACOS)
    , kqueueFd_(-1)
#endif
{
    id_ = "uwsgi_" + std::to_string(nextServerId_++);
}

UwsgiServer::~UwsgiServer() {
    stop();
}

bool UwsgiServer::configure(const ServerConfig& config) {
    if (running_) return false;
    config_ = config;
    return true;
}

void UwsgiServer::setHandler(RequestHandler handler) {
    handler_ = std::move(handler);
    useAsyncHandler_ = false;
}

void UwsgiServer::setAsyncHandler(AsyncRequestHandler handler) {
    asyncHandler_ = std::move(handler);
    useAsyncHandler_ = true;
}

bool UwsgiServer::createSocket() {
    // Create TCP socket
    serverFd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (serverFd_ == INVALID_SOCKET_VALUE) {
        return false;
    }
    
    // Set socket options
    if (config_.reuseAddr) {
        int opt = 1;
        setsockopt(serverFd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    }
    
#ifdef SO_REUSEPORT
    if (config_.reusePort) {
        int opt = 1;
        setsockopt(serverFd_, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
    }
#endif
    
    // Set non-blocking
    if (!setNonBlocking(serverFd_)) {
        close_socket(serverFd_);
        serverFd_ = INVALID_SOCKET_VALUE;
        return false;
    }
    
    // Bind
    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(config_.port);
    
    if (config_.bindAddress == "0.0.0.0" || config_.bindAddress.empty()) {
        addr.sin_addr.s_addr = INADDR_ANY;
    } else {
        inet_pton(AF_INET, config_.bindAddress.c_str(), &addr.sin_addr);
    }
    
    if (::bind(serverFd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close_socket(serverFd_);
        serverFd_ = INVALID_SOCKET_VALUE;
        return false;
    }
    
    // Listen
    if (::listen(serverFd_, config_.backlog) < 0) {
        close_socket(serverFd_);
        serverFd_ = INVALID_SOCKET_VALUE;
        return false;
    }
    
    return true;
}

bool UwsgiServer::createUnixSocket() {
#ifndef QZ_PLATFORM_WINDOWS
    serverFd_ = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (serverFd_ == INVALID_SOCKET_VALUE) {
        return false;
    }
    
    // Remove existing socket file
    unlink(config_.socketPath.c_str());
    
    // Set non-blocking
    if (!setNonBlocking(serverFd_)) {
        close_socket(serverFd_);
        serverFd_ = INVALID_SOCKET_VALUE;
        return false;
    }
    
    // Bind
    struct sockaddr_un addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, config_.socketPath.c_str(), sizeof(addr.sun_path) - 1);
    
    if (::bind(serverFd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close_socket(serverFd_);
        serverFd_ = INVALID_SOCKET_VALUE;
        return false;
    }
    
    // Set socket permissions (world-readable/writable for nginx)
    chmod(config_.socketPath.c_str(), 0666);
    
    // Listen
    if (::listen(serverFd_, config_.backlog) < 0) {
        close_socket(serverFd_);
        serverFd_ = INVALID_SOCKET_VALUE;
        return false;
    }
    
    return true;
#else
    return false;  // Unix sockets not supported on Windows
#endif
}

bool UwsgiServer::setNonBlocking(socket_t fd) {
#ifdef QZ_PLATFORM_WINDOWS
    u_long mode = 1;
    return ioctlsocket(fd, FIONBIO, &mode) == 0;
#else
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return false;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) >= 0;
#endif
}

bool UwsgiServer::setupMultiplexer() {
#ifdef QZ_PLATFORM_LINUX
    epollFd_ = epoll_create1(0);
    if (epollFd_ < 0) return false;
    
    // Add server socket
    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLET;  // Edge-triggered for performance
    ev.data.ptr = nullptr;  // nullptr indicates server socket
    
    if (epoll_ctl(epollFd_, EPOLL_CTL_ADD, serverFd_, &ev) < 0) {
        close(epollFd_);
        epollFd_ = -1;
        return false;
    }
    return true;
    
#elif defined(QZ_PLATFORM_MACOS)
    kqueueFd_ = kqueue();
    if (kqueueFd_ < 0) return false;
    
    // Add server socket
    struct kevent ev;
    EV_SET(&ev, serverFd_, EVFILT_READ, EV_ADD | EV_CLEAR, 0, 0, nullptr);
    
    if (kevent(kqueueFd_, &ev, 1, nullptr, 0, nullptr) < 0) {
        close(kqueueFd_);
        kqueueFd_ = -1;
        return false;
    }
    return true;
    
#else
    return false;
#endif
}

void UwsgiServer::cleanupMultiplexer() {
#ifdef QZ_PLATFORM_LINUX
    if (epollFd_ >= 0) {
        close(epollFd_);
        epollFd_ = -1;
    }
#elif defined(QZ_PLATFORM_MACOS)
    if (kqueueFd_ >= 0) {
        close(kqueueFd_);
        kqueueFd_ = -1;
    }
#endif
}

bool UwsgiServer::addToMultiplexer(socket_t fd, uint32_t events, void* userData) {
#ifdef QZ_PLATFORM_LINUX
    struct epoll_event ev;
    ev.events = EPOLLET;  // Edge-triggered
    if (events & EV_READ) ev.events |= EPOLLIN;
    if (events & EV_WRITE) ev.events |= EPOLLOUT;
    ev.data.ptr = userData;
    
    return epoll_ctl(epollFd_, EPOLL_CTL_ADD, fd, &ev) >= 0;
    
#elif defined(QZ_PLATFORM_MACOS)
    struct kevent evs[2];
    int nevs = 0;
    
    if (events & EV_READ) {
        EV_SET(&evs[nevs++], fd, EVFILT_READ, EV_ADD | EV_CLEAR, 0, 0, userData);
    }
    if (events & EV_WRITE) {
        EV_SET(&evs[nevs++], fd, EVFILT_WRITE, EV_ADD | EV_CLEAR, 0, 0, userData);
    }
    
    return kevent(kqueueFd_, evs, nevs, nullptr, 0, nullptr) >= 0;
#else
    return false;
#endif
}

bool UwsgiServer::modifyInMultiplexer(socket_t fd, uint32_t events) {
#ifdef QZ_PLATFORM_LINUX
    struct epoll_event ev;
    ev.events = EPOLLET;
    if (events & EV_READ) ev.events |= EPOLLIN;
    if (events & EV_WRITE) ev.events |= EPOLLOUT;
    ev.data.fd = fd;
    
    return epoll_ctl(epollFd_, EPOLL_CTL_MOD, fd, &ev) >= 0;
    
#elif defined(QZ_PLATFORM_MACOS)
    // kqueue handles this via EV_ADD with existing filter
    return true;
#else
    return false;
#endif
}

bool UwsgiServer::removeFromMultiplexer(socket_t fd) {
#ifdef QZ_PLATFORM_LINUX
    return epoll_ctl(epollFd_, EPOLL_CTL_DEL, fd, nullptr) >= 0;
    
#elif defined(QZ_PLATFORM_MACOS)
    struct kevent evs[2];
    EV_SET(&evs[0], fd, EVFILT_READ, EV_DELETE, 0, 0, nullptr);
    EV_SET(&evs[1], fd, EVFILT_WRITE, EV_DELETE, 0, 0, nullptr);
    kevent(kqueueFd_, evs, 2, nullptr, 0, nullptr);
    return true;
#else
    return false;
#endif
}

bool UwsgiServer::start() {
    if (running_) return false;
    
    // Create socket
    bool created;
    if (config_.useUnixSocket && !config_.socketPath.empty()) {
        created = createUnixSocket();
    } else {
        created = createSocket();
    }
    
    if (!created) return false;
    
    // Setup I/O multiplexer
    if (!setupMultiplexer()) {
        close_socket(serverFd_);
        serverFd_ = INVALID_SOCKET_VALUE;
        return false;
    }
    
    running_ = true;
    stats_.reset();
    
    return true;
}

void UwsgiServer::stop() {
    if (!running_) return;
    
    running_ = false;
    
    // Close all connections
    {
        std::lock_guard<std::mutex> lock(connectionsMutex_);
        connections_.clear();
    }
    
    // Cleanup multiplexer
    cleanupMultiplexer();
    
    // Close server socket
    if (serverFd_ != INVALID_SOCKET_VALUE) {
        close_socket(serverFd_);
        serverFd_ = INVALID_SOCKET_VALUE;
    }
    
    // Remove Unix socket file
    if (config_.useUnixSocket && !config_.socketPath.empty()) {
#ifndef QZ_PLATFORM_WINDOWS
        unlink(config_.socketPath.c_str());
#endif
    }
}

void UwsgiServer::acceptConnections() {
    while (true) {
        struct sockaddr_in clientAddr;
        socklen_t addrLen = sizeof(clientAddr);
        
        socket_t clientFd = ::accept(serverFd_, (struct sockaddr*)&clientAddr, &addrLen);
        
        if (clientFd == INVALID_SOCKET_VALUE) {
#ifdef QZ_PLATFORM_WINDOWS
            if (WSAGetLastError() == WSAEWOULDBLOCK) break;
#else
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
#endif
            break;  // Error
        }
        
        // Check connection limit
        if (connections_.size() >= static_cast<size_t>(config_.maxConnections)) {
            close_socket(clientFd);
            continue;
        }
        
        // Set non-blocking
        setNonBlocking(clientFd);
        
        // Set TCP_NODELAY for low latency
        if (config_.tcpNoDelay) {
            int opt = 1;
            setsockopt(clientFd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));
        }
        
        // Get client address
        char addrStr[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &clientAddr.sin_addr, addrStr, sizeof(addrStr));
        int port = ntohs(clientAddr.sin_port);
        
        // Create connection
        auto conn = std::make_unique<Connection>(clientFd, addrStr, port);
        const std::string& connId = conn->id();
        
        // Add to multiplexer
        addToMultiplexer(clientFd, EV_READ, conn.get());
        
        // Store connection
        {
            std::lock_guard<std::mutex> lock(connectionsMutex_);
            connections_[connId] = std::move(conn);
        }
        
        stats_.totalConnections++;
        stats_.activeConnections++;
    }
}

void UwsgiServer::handleConnection(Connection* conn, uint32_t events) {
    if (!conn) return;
    
    // Handle readable
    if (events & EV_READ) {
        int n = conn->readData();
        
        if (n < 0) {
            // Error or closed
            removeConnection(conn->id());
            return;
        }
        
        // Process based on state
        while (true) {
            switch (conn->state()) {
                case ConnectionState::READING_HEADER:
                    if (!conn->parseHeader()) goto done_reading;
                    break;
                    
                case ConnectionState::READING_VARS:
                    if (!conn->parseVars()) goto done_reading;
                    break;
                    
                case ConnectionState::READING_BODY:
                    if (!conn->parseBody()) goto done_reading;
                    break;
                    
                case ConnectionState::PROCESSING:
                    processRequest(conn);
                    goto done_reading;
                    
                case ConnectionState::CLOSING:
                    removeConnection(conn->id());
                    return;
                    
                default:
                    goto done_reading;
            }
        }
        done_reading:;
    }
    
    // Handle writable
    if ((events & EV_WRITE) || conn->state() == ConnectionState::SENDING_RESPONSE) {
        if (conn->hasPendingSend()) {
            int written = conn->flushSendBuffer();
            
            if (written < 0) {
                removeConnection(conn->id());
                return;
            }
            
            if (!conn->hasPendingSend()) {
                // Response sent
                stats_.bytesSent += conn->bytesSent();
                
                if (config_.enableKeepAlive) {
                    conn->reset();
                    conn->setState(ConnectionState::KEEP_ALIVE);
                } else {
                    removeConnection(conn->id());
                }
            }
        }
    }
}

void UwsgiServer::processRequest(Connection* conn) {
    stats_.totalRequests++;
    stats_.bytesReceived += conn->bytesReceived();
    
    HttpResponse response;
    
    if (useAsyncHandler_ && asyncHandler_) {
        // Async handler - will call sendResponse later
        asyncHandler_(conn->request(), conn->id());
        return;
    } else if (handler_) {
        try {
            response = handler_(conn->request());
        } catch (const std::exception& e) {
            response = HttpResponse::serverError(e.what());
        }
    } else {
        response = HttpResponse::serverError("No handler configured");
    }
    
    // Update stats
    if (response.statusCode >= 200 && response.statusCode < 300) {
        stats_.successResponses++;
    } else if (response.statusCode >= 400 && response.statusCode < 500) {
        stats_.clientErrors++;
    } else if (response.statusCode >= 500) {
        stats_.serverErrors++;
    }
    
    conn->setResponse(response);
    conn->flushSendBuffer();
}

bool UwsgiServer::sendResponse(const std::string& connId, const HttpResponse& response) {
    std::lock_guard<std::mutex> lock(connectionsMutex_);
    
    auto it = connections_.find(connId);
    if (it == connections_.end()) return false;
    
    Connection* conn = it->second.get();
    
    // Update stats
    if (response.statusCode >= 200 && response.statusCode < 300) {
        stats_.successResponses++;
    } else if (response.statusCode >= 400 && response.statusCode < 500) {
        stats_.clientErrors++;
    } else if (response.statusCode >= 500) {
        stats_.serverErrors++;
    }
    
    conn->setResponse(response);
    conn->flushSendBuffer();
    
    return true;
}

void UwsgiServer::removeConnection(const std::string& connId) {
    std::lock_guard<std::mutex> lock(connectionsMutex_);
    
    auto it = connections_.find(connId);
    if (it != connections_.end()) {
        removeFromMultiplexer(it->second->fd());
        connections_.erase(it);
        stats_.activeConnections--;
    }
}

void UwsgiServer::checkTimeouts() {
    std::vector<std::string> toRemove;
    
    {
        std::lock_guard<std::mutex> lock(connectionsMutex_);
        
        for (auto& [id, conn] : connections_) {
            int timeout = conn->state() == ConnectionState::KEEP_ALIVE 
                ? config_.keepAliveTimeoutMs 
                : config_.connectionTimeoutMs;
            
            if (conn->isTimedOut(timeout)) {
                toRemove.push_back(id);
                stats_.timeouts++;
            }
        }
    }
    
    for (const auto& id : toRemove) {
        removeConnection(id);
    }
}

int UwsgiServer::poll(int timeoutMs) {
    if (!running_) return 0;
    
    int eventCount = 0;
    
#ifdef QZ_PLATFORM_LINUX
    struct epoll_event events[1024];
    int nfds = epoll_wait(epollFd_, events, 1024, timeoutMs);
    
    for (int i = 0; i < nfds; ++i) {
        if (events[i].data.ptr == nullptr) {
            // Server socket - accept connections
            acceptConnections();
        } else {
            // Client connection
            Connection* conn = static_cast<Connection*>(events[i].data.ptr);
            uint32_t evts = 0;
            if (events[i].events & EPOLLIN) evts |= EV_READ;
            if (events[i].events & EPOLLOUT) evts |= EV_WRITE;
            handleConnection(conn, evts);
        }
        eventCount++;
    }
    
#elif defined(QZ_PLATFORM_MACOS)
    struct kevent events[1024];
    struct timespec ts;
    ts.tv_sec = timeoutMs / 1000;
    ts.tv_nsec = (timeoutMs % 1000) * 1000000;
    
    int nfds = kevent(kqueueFd_, nullptr, 0, events, 1024, 
                      timeoutMs >= 0 ? &ts : nullptr);
    
    for (int i = 0; i < nfds; ++i) {
        if (events[i].udata == nullptr) {
            // Server socket - accept connections
            acceptConnections();
        } else {
            // Client connection
            Connection* conn = static_cast<Connection*>(events[i].udata);
            uint32_t evts = 0;
            if (events[i].filter == EVFILT_READ) evts |= EV_READ;
            if (events[i].filter == EVFILT_WRITE) evts |= EV_WRITE;
            handleConnection(conn, evts);
        }
        eventCount++;
    }
#endif
    
    // Periodic timeout check (every ~100ms)
    static auto lastTimeoutCheck = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastTimeoutCheck).count() > 100) {
        checkTimeouts();
        lastTimeoutCheck = now;
    }
    
    return eventCount;
}

// ============================================================================
// ServerManager Implementation
// ============================================================================

ServerManager& ServerManager::instance() {
    static ServerManager instance;
    return instance;
}

ServerManager::~ServerManager() {
    stopAll();
}

UwsgiServer* ServerManager::createServer(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto server = std::make_unique<UwsgiServer>();
    std::string serverId = id.empty() ? server->id() : id;
    
    UwsgiServer* ptr = server.get();
    servers_[serverId] = std::move(server);
    
    return ptr;
}

UwsgiServer* ServerManager::getServer(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = servers_.find(id);
    return it != servers_.end() ? it->second.get() : nullptr;
}

bool ServerManager::removeServer(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = servers_.find(id);
    if (it != servers_.end()) {
        it->second->stop();
        servers_.erase(it);
        return true;
    }
    return false;
}

std::vector<std::string> ServerManager::getServerIds() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<std::string> ids;
    ids.reserve(servers_.size());
    
    for (const auto& [id, server] : servers_) {
        ids.push_back(id);
    }
    return ids;
}

void ServerManager::stopAll() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    for (auto& [id, server] : servers_) {
        server->stop();
    }
    servers_.clear();
}

StatsSnapshot ServerManager::aggregateStats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    StatsSnapshot aggregate;
    
    for (const auto& [id, server] : servers_) {
        const auto& stats = server->stats();
        aggregate.totalConnections += stats.totalConnections.load();
        aggregate.activeConnections += stats.activeConnections.load();
        aggregate.totalRequests += stats.totalRequests.load();
        aggregate.successResponses += stats.successResponses.load();
        aggregate.clientErrors += stats.clientErrors.load();
        aggregate.serverErrors += stats.serverErrors.load();
        aggregate.bytesReceived += stats.bytesReceived.load();
        aggregate.bytesSent += stats.bytesSent.load();
        aggregate.parseErrors += stats.parseErrors.load();
        aggregate.timeouts += stats.timeouts.load();
        // uptimeMs and requestsPerSecond don't make sense aggregated
    }
    
    return aggregate;
}

} // namespace uwsgi
