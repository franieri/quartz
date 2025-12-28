// ============================================================================
// system.async.eventloop - Event Loop Types and Platform Abstraction
// High-performance cross-platform event loop for async I/O
// ============================================================================

#ifndef EVENTLOOP_TYPES_H
#define EVENTLOOP_TYPES_H

#include <string>
#include <vector>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <functional>
#include <atomic>
#include <mutex>
#include <chrono>
#include <thread>

// Platform detection (same as socket_types.h)
#if defined(_WIN32) || defined(_WIN64)
    #define QZ_PLATFORM_WINDOWS 1
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <winsock2.h>
    #include <ws2tcpip.h>
    using socket_t = SOCKET;
    #define INVALID_SOCKET_VALUE INVALID_SOCKET
#elif defined(__APPLE__) || defined(__MACH__)
    #define QZ_PLATFORM_MACOS 1
    #include <sys/event.h>
    #include <sys/time.h>
    #include <unistd.h>
    using socket_t = int;
    #define INVALID_SOCKET_VALUE (-1)
#elif defined(__linux__)
    #define QZ_PLATFORM_LINUX 1
    #include <sys/epoll.h>
    #include <unistd.h>
    using socket_t = int;
    #define INVALID_SOCKET_VALUE (-1)
#endif

namespace eventloop {

// ============================================================================
// Event Types
// ============================================================================

enum class EventType {
    READ,           // Socket is readable
    WRITE,          // Socket is writable
    ACCEPT,         // New connection available
    CLOSE,          // Connection closed
    ERROR_EVENT,    // Error occurred
    TIMEOUT,        // Timer expired
    IDLE,           // Idle callback
    SIGNAL,         // Signal received
    CUSTOM          // Custom user event
};

// ============================================================================
// Event Data Structure
// ============================================================================

struct Event {
    EventType type;
    std::string socketId;       // Socket reference if applicable
    int fd;                     // File descriptor
    std::string data;           // Optional event data
    int errorCode;              // Error code if type == ERROR_EVENT
    uint64_t timerId;           // Timer ID if type == TIMEOUT
    
    Event() : type(EventType::CUSTOM), fd(-1), errorCode(0), timerId(0) {}
    Event(EventType t) : type(t), fd(-1), errorCode(0), timerId(0) {}
    Event(EventType t, const std::string& sid, int f) 
        : type(t), socketId(sid), fd(f), errorCode(0), timerId(0) {}
};

// ============================================================================
// Timer Structure
// ============================================================================

struct Timer {
    uint64_t id;
    std::chrono::steady_clock::time_point deadline;
    int intervalMs;         // 0 for one-shot, >0 for repeating
    bool cancelled;
    std::string callbackId; // Quartz lambda ID
    
    Timer() : id(0), intervalMs(0), cancelled(false) {}
    
    bool operator>(const Timer& other) const {
        return deadline > other.deadline;
    }
};

// ============================================================================
// I/O Watcher - Tracks registered sockets
// ============================================================================

struct IOWatcher {
    std::string socketId;
    int fd;
    uint32_t events;            // Bit flags: READ | WRITE
    std::string readCallback;   // Quartz lambda ID for read events
    std::string writeCallback;  // Quartz lambda ID for write events
    std::string closeCallback;  // Quartz lambda ID for close events
    std::string errorCallback;  // Quartz lambda ID for error events
    bool isServer;              // True if this is a listening socket
    
    static constexpr uint32_t EV_READ  = 0x01;
    static constexpr uint32_t EV_WRITE = 0x02;
    
    IOWatcher() : fd(-1), events(0), isServer(false) {}
};

// ============================================================================
// Event Loop Statistics
// ============================================================================

struct EventLoopStats {
    std::atomic<uint64_t> totalEvents{0};
    std::atomic<uint64_t> readEvents{0};
    std::atomic<uint64_t> writeEvents{0};
    std::atomic<uint64_t> acceptEvents{0};
    std::atomic<uint64_t> errorEvents{0};
    std::atomic<uint64_t> timerEvents{0};
    std::atomic<uint64_t> idleEvents{0};
    std::atomic<uint64_t> iterations{0};
    std::chrono::steady_clock::time_point startTime;
    std::chrono::steady_clock::time_point lastIterationTime;
    
    EventLoopStats() : startTime(std::chrono::steady_clock::now()),
                       lastIterationTime(std::chrono::steady_clock::now()) {}
    
    void reset() {
        totalEvents = 0;
        readEvents = 0;
        writeEvents = 0;
        acceptEvents = 0;
        errorEvents = 0;
        timerEvents = 0;
        idleEvents = 0;
        iterations = 0;
        startTime = std::chrono::steady_clock::now();
    }
    
    uint64_t uptimeMs() const {
        auto now = std::chrono::steady_clock::now();
        return std::chrono::duration_cast<std::chrono::milliseconds>(now - startTime).count();
    }
};

// ============================================================================
// Event Loop Configuration
// ============================================================================

struct EventLoopConfig {
    int maxEvents = 1024;               // Max events per poll
    int defaultTimeoutMs = 100;         // Default poll timeout
    int maxIdleCallbacksPerIter = 100;  // Max idle callbacks per iteration
    bool enableStats = true;            // Track statistics
    
    // Performance tuning
    bool useBusyWait = false;           // Busy wait instead of blocking
    int busyWaitSpinCount = 1000;       // Spins before yielding
};

// ============================================================================
// Platform-Specific Event Loop Backend (Abstract)
// ============================================================================

class EventLoopBackend {
public:
    virtual ~EventLoopBackend() = default;
    
    virtual bool create() = 0;
    virtual void destroy() = 0;
    
    virtual bool addSocket(int fd, uint32_t events, void* userData) = 0;
    virtual bool modifySocket(int fd, uint32_t events) = 0;
    virtual bool removeSocket(int fd) = 0;
    
    // Poll for events, returns number of events
    virtual int poll(int timeoutMs) = 0;
    
    // Process events and call the handler
    virtual void processEvents(const std::function<void(int fd, uint32_t events)>& handler) = 0;
    
    // Event flags (prefixed to avoid conflicts with system headers)
    static constexpr uint32_t EVT_READ  = 0x01;
    static constexpr uint32_t EVT_WRITE = 0x02;
    static constexpr uint32_t EVT_ERROR = 0x04;
    static constexpr uint32_t EVT_CLOSE = 0x08;
};

#ifdef QZ_PLATFORM_LINUX
// ============================================================================
// Linux epoll Backend
// ============================================================================
class EpollBackend : public EventLoopBackend {
public:
    EpollBackend(int maxEvents = 1024);
    ~EpollBackend() override;
    
    bool create() override;
    void destroy() override;
    bool addSocket(int fd, uint32_t events, void* userData) override;
    bool modifySocket(int fd, uint32_t events) override;
    bool removeSocket(int fd) override;
    int poll(int timeoutMs) override;
    void processEvents(const std::function<void(int fd, uint32_t events)>& handler) override;
    
private:
    int epollFd_;
    int maxEvents_;
    std::vector<struct epoll_event> events_;
    int numReady_;
};
#endif

#ifdef QZ_PLATFORM_MACOS
// ============================================================================
// macOS kqueue Backend
// ============================================================================
class KqueueBackend : public EventLoopBackend {
public:
    KqueueBackend(int maxEvents = 1024);
    ~KqueueBackend() override;
    
    bool create() override;
    void destroy() override;
    bool addSocket(int fd, uint32_t events, void* userData) override;
    bool modifySocket(int fd, uint32_t events) override;
    bool removeSocket(int fd) override;
    int poll(int timeoutMs) override;
    void processEvents(const std::function<void(int fd, uint32_t events)>& handler) override;
    
private:
    int kqueueFd_;
    int maxEvents_;
    std::vector<struct kevent> events_;
    int numReady_;
    std::unordered_map<int, uint32_t> fdEvents_;
};
#endif

#ifdef QZ_PLATFORM_WINDOWS
// ============================================================================
// Windows IOCP Backend
// ============================================================================
class IOCPBackend : public EventLoopBackend {
public:
    IOCPBackend(int maxEvents = 1024);
    ~IOCPBackend() override;
    
    bool create() override;
    void destroy() override;
    bool addSocket(int fd, uint32_t events, void* userData) override;
    bool modifySocket(int fd, uint32_t events) override;
    bool removeSocket(int fd) override;
    int poll(int timeoutMs) override;
    void processEvents(const std::function<void(int fd, uint32_t events)>& handler) override;
    
private:
    HANDLE iocpHandle_;
    std::vector<OVERLAPPED_ENTRY> completions_;
    ULONG numCompletions_;
};
#endif

// Factory function
std::unique_ptr<EventLoopBackend> createBackend(int maxEvents = 1024);

// ============================================================================
// Main Event Loop Class
// ============================================================================

class EventLoop {
public:
    EventLoop(const EventLoopConfig& config = EventLoopConfig());
    ~EventLoop();
    
    // Lifecycle
    bool init();
    void run();                          // Run until stopped
    void runOnce(int timeoutMs = -1);    // Process one batch of events
    void runFor(int durationMs);         // Run for a specific duration
    void stop();                         // Signal loop to stop
    bool isRunning() const { return running_.load(); }
    
    // Socket I/O registration
    bool watchRead(const std::string& socketId, int fd, const std::string& callbackId);
    bool watchWrite(const std::string& socketId, int fd, const std::string& callbackId);
    bool watchAccept(const std::string& socketId, int fd, const std::string& callbackId);
    bool unwatch(const std::string& socketId);
    bool unwatchRead(const std::string& socketId);
    bool unwatchWrite(const std::string& socketId);
    
    // Set close/error callbacks
    bool onClose(const std::string& socketId, const std::string& callbackId);
    bool onError(const std::string& socketId, const std::string& callbackId);
    
    // Timers
    uint64_t setTimeout(int delayMs, const std::string& callbackId);
    uint64_t setInterval(int intervalMs, const std::string& callbackId);
    bool clearTimer(uint64_t timerId);
    
    // Immediate/Next tick
    void setImmediate(const std::string& callbackId);
    void nextTick(const std::string& callbackId);
    
    // Idle callbacks (called when no other events)
    void onIdle(const std::string& callbackId);
    void clearIdle();
    
    // Statistics
    const EventLoopStats& stats() const { return stats_; }
    void resetStats() { stats_.reset(); }
    
    // Get active watchers count
    size_t watcherCount() const { return watchers_.size(); }
    size_t timerCount() const { return timers_.size(); }
    
    // Callback invoker (set by extension to invoke Quartz lambdas)
    using CallbackInvoker = std::function<void(const std::string& callbackId, const Event& event)>;
    void setCallbackInvoker(CallbackInvoker invoker) { invoker_ = std::move(invoker); }
    
    // Callback cleanup (set by extension to clean up callback storage when no longer needed)
    using CallbackCleanup = std::function<void(const std::string& callbackId)>;
    void setCallbackCleanup(CallbackCleanup cleanup) { cleanup_ = std::move(cleanup); }
    
private:
    EventLoopConfig config_;
    std::unique_ptr<EventLoopBackend> backend_;
    
    std::atomic<bool> running_;
    std::atomic<bool> stopRequested_;
    
    // I/O watchers
    std::unordered_map<std::string, IOWatcher> watchers_;  // socketId -> watcher
    std::unordered_map<int, std::string> fdToSocketId_;    // fd -> socketId
    mutable std::mutex watcherMutex_;
    
    // Timers (min-heap by deadline)
    std::priority_queue<Timer, std::vector<Timer>, std::greater<Timer>> timers_;
    std::unordered_set<uint64_t> cancelledTimers_;
    std::unordered_map<uint64_t, std::string> timerCallbacks_;  // timerId -> callbackId for cleanup
    std::atomic<uint64_t> nextTimerId_{1};
    mutable std::mutex timerMutex_;
    
    // Immediate/next-tick queues
    std::queue<std::string> immediateQueue_;
    std::queue<std::string> nextTickQueue_;
    mutable std::mutex queueMutex_;
    
    // Idle callbacks
    std::vector<std::string> idleCallbacks_;
    mutable std::mutex idleMutex_;
    
    // Statistics
    EventLoopStats stats_;
    
    // Callback invoker
    CallbackInvoker invoker_;
    
    // Callback cleanup
    CallbackCleanup cleanup_;
    
    // Internal methods
    void processTimers();
    void processNextTick();
    void processImmediate();
    void processIdle();
    int calculateTimeout();
    void invokeCallback(const std::string& callbackId, const Event& event);
    void handleSocketEvent(int fd, uint32_t events);
    void updateWatcherEvents(const std::string& socketId);
};

// ============================================================================
// Global Event Loop Instance
// ============================================================================

EventLoop& getEventLoop();
bool initEventLoop(const EventLoopConfig& config = EventLoopConfig());
void destroyEventLoop();

} // namespace eventloop

#endif // EVENTLOOP_TYPES_H
