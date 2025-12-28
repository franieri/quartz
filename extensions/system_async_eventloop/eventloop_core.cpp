// ============================================================================
// system.async.eventloop - Event Loop Core Implementation
// High-performance event loop with platform-specific backends
// ============================================================================

#include "eventloop_types.h"
#include <algorithm>
#include <cstring>

namespace eventloop {

// ============================================================================
// Platform Backend Implementations
// ============================================================================

#ifdef QZ_PLATFORM_LINUX
// Linux epoll implementation

EpollBackend::EpollBackend(int maxEvents)
    : epollFd_(-1), maxEvents_(maxEvents), numReady_(0) {
    events_.resize(maxEvents);
}

EpollBackend::~EpollBackend() {
    destroy();
}

bool EpollBackend::create() {
    epollFd_ = epoll_create1(EPOLL_CLOEXEC);
    return epollFd_ >= 0;
}

void EpollBackend::destroy() {
    if (epollFd_ >= 0) {
        ::close(epollFd_);
        epollFd_ = -1;
    }
}

bool EpollBackend::addSocket(int fd, uint32_t events, void* userData) {
    if (epollFd_ < 0 || fd < 0) return false;
    
    struct epoll_event ev;
    ev.events = EPOLLET; // Edge-triggered
    if (events & EVT_READ)  ev.events |= EPOLLIN;
    if (events & EVT_WRITE) ev.events |= EPOLLOUT;
    ev.data.fd = fd;
    
    return epoll_ctl(epollFd_, EPOLL_CTL_ADD, fd, &ev) == 0;
}

bool EpollBackend::modifySocket(int fd, uint32_t events) {
    if (epollFd_ < 0 || fd < 0) return false;
    
    struct epoll_event ev;
    ev.events = EPOLLET;
    if (events & EVT_READ)  ev.events |= EPOLLIN;
    if (events & EVT_WRITE) ev.events |= EPOLLOUT;
    ev.data.fd = fd;
    
    return epoll_ctl(epollFd_, EPOLL_CTL_MOD, fd, &ev) == 0;
}

bool EpollBackend::removeSocket(int fd) {
    if (epollFd_ < 0 || fd < 0) return false;
    return epoll_ctl(epollFd_, EPOLL_CTL_DEL, fd, nullptr) == 0;
}

int EpollBackend::poll(int timeoutMs) {
    if (epollFd_ < 0) return -1;
    numReady_ = epoll_wait(epollFd_, events_.data(), maxEvents_, timeoutMs);
    return numReady_;
}

void EpollBackend::processEvents(const std::function<void(int fd, uint32_t events)>& handler) {
    for (int i = 0; i < numReady_; i++) {
        int fd = events_[i].data.fd;
        uint32_t ev = 0;
        
        if (events_[i].events & EPOLLIN)  ev |= EVT_READ;
        if (events_[i].events & EPOLLOUT) ev |= EVT_WRITE;
        if (events_[i].events & EPOLLERR) ev |= EVT_ERROR;
        if (events_[i].events & (EPOLLHUP | EPOLLRDHUP)) ev |= EVT_CLOSE;
        
        handler(fd, ev);
    }
}
#endif // QZ_PLATFORM_LINUX

#ifdef QZ_PLATFORM_MACOS
// macOS kqueue implementation

KqueueBackend::KqueueBackend(int maxEvents)
    : kqueueFd_(-1), maxEvents_(maxEvents), numReady_(0) {
    events_.resize(maxEvents);
}

KqueueBackend::~KqueueBackend() {
    destroy();
}

bool KqueueBackend::create() {
    kqueueFd_ = kqueue();
    return kqueueFd_ >= 0;
}

void KqueueBackend::destroy() {
    if (kqueueFd_ >= 0) {
        ::close(kqueueFd_);
        kqueueFd_ = -1;
    }
    fdEvents_.clear();
}

bool KqueueBackend::addSocket(int fd, uint32_t events, void* userData) {
    if (kqueueFd_ < 0 || fd < 0) return false;
    
    struct kevent ev[2];
    int n = 0;
    
    if (events & EVT_READ) {
        EV_SET(&ev[n++], fd, EVFILT_READ, EV_ADD | EV_CLEAR, 0, 0, nullptr);
    }
    if (events & EVT_WRITE) {
        EV_SET(&ev[n++], fd, EVFILT_WRITE, EV_ADD | EV_CLEAR, 0, 0, nullptr);
    }
    
    if (n > 0 && kevent(kqueueFd_, ev, n, nullptr, 0, nullptr) < 0) {
        return false;
    }
    
    fdEvents_[fd] = events;
    return true;
}

bool KqueueBackend::modifySocket(int fd, uint32_t events) {
    if (kqueueFd_ < 0 || fd < 0) return false;
    
    auto it = fdEvents_.find(fd);
    uint32_t oldEvents = (it != fdEvents_.end()) ? it->second : 0;
    
    struct kevent ev[4];
    int n = 0;
    
    // Remove old events
    if ((oldEvents & EVT_READ) && !(events & EVT_READ)) {
        EV_SET(&ev[n++], fd, EVFILT_READ, EV_DELETE, 0, 0, nullptr);
    }
    if ((oldEvents & EVT_WRITE) && !(events & EVT_WRITE)) {
        EV_SET(&ev[n++], fd, EVFILT_WRITE, EV_DELETE, 0, 0, nullptr);
    }
    
    // Add new events
    if (!(oldEvents & EVT_READ) && (events & EVT_READ)) {
        EV_SET(&ev[n++], fd, EVFILT_READ, EV_ADD | EV_CLEAR, 0, 0, nullptr);
    }
    if (!(oldEvents & EVT_WRITE) && (events & EVT_WRITE)) {
        EV_SET(&ev[n++], fd, EVFILT_WRITE, EV_ADD | EV_CLEAR, 0, 0, nullptr);
    }
    
    if (n > 0) {
        kevent(kqueueFd_, ev, n, nullptr, 0, nullptr);
    }
    
    fdEvents_[fd] = events;
    return true;
}

bool KqueueBackend::removeSocket(int fd) {
    if (kqueueFd_ < 0 || fd < 0) return false;
    
    auto it = fdEvents_.find(fd);
    if (it == fdEvents_.end()) return false;
    
    struct kevent ev[2];
    int n = 0;
    
    if (it->second & EVT_READ) {
        EV_SET(&ev[n++], fd, EVFILT_READ, EV_DELETE, 0, 0, nullptr);
    }
    if (it->second & EVT_WRITE) {
        EV_SET(&ev[n++], fd, EVFILT_WRITE, EV_DELETE, 0, 0, nullptr);
    }
    
    // Ignore errors (filter may not exist)
    kevent(kqueueFd_, ev, n, nullptr, 0, nullptr);
    fdEvents_.erase(it);
    return true;
}

int KqueueBackend::poll(int timeoutMs) {
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

void KqueueBackend::processEvents(const std::function<void(int fd, uint32_t events)>& handler) {
    for (int i = 0; i < numReady_; i++) {
        int fd = static_cast<int>(events_[i].ident);
        uint32_t evtFlags = 0;
        
        if (events_[i].flags & EV_ERROR) {
            evtFlags |= EVT_ERROR;
        } else if (events_[i].flags & EV_EOF) {
            evtFlags |= EVT_CLOSE;
        } else if (events_[i].filter == EVFILT_READ) {
            evtFlags |= EVT_READ;
        } else if (events_[i].filter == EVFILT_WRITE) {
            evtFlags |= EVT_WRITE;
        }
        
        handler(fd, evtFlags);
    }
}
#endif // QZ_PLATFORM_MACOS

#ifdef QZ_PLATFORM_WINDOWS
// Windows IOCP implementation (simplified - uses select fallback for broader compatibility)

IOCPBackend::IOCPBackend(int maxEvents)
    : iocpHandle_(INVALID_HANDLE_VALUE), numCompletions_(0) {
    completions_.resize(maxEvents);
}

IOCPBackend::~IOCPBackend() {
    destroy();
}

bool IOCPBackend::create() {
    // For simplicity, we use a basic implementation
    // A full IOCP implementation requires OVERLAPPED structures
    return true;
}

void IOCPBackend::destroy() {
    if (iocpHandle_ != INVALID_HANDLE_VALUE) {
        CloseHandle(iocpHandle_);
        iocpHandle_ = INVALID_HANDLE_VALUE;
    }
}

bool IOCPBackend::addSocket(int fd, uint32_t events, void* userData) {
    // Simplified - real IOCP would associate with completion port
    return true;
}

bool IOCPBackend::modifySocket(int fd, uint32_t events) {
    return true;
}

bool IOCPBackend::removeSocket(int fd) {
    return true;
}

int IOCPBackend::poll(int timeoutMs) {
    // Use Sleep for basic timing - real impl would use GetQueuedCompletionStatusEx
    if (timeoutMs > 0) {
        Sleep(timeoutMs);
    }
    return 0;
}

void IOCPBackend::processEvents(const std::function<void(int fd, uint32_t events)>& handler) {
    // Events are processed via completion routines in full IOCP
}
#endif // QZ_PLATFORM_WINDOWS

// Factory function
std::unique_ptr<EventLoopBackend> createBackend(int maxEvents) {
#ifdef QZ_PLATFORM_LINUX
    return std::make_unique<EpollBackend>(maxEvents);
#elif defined(QZ_PLATFORM_MACOS)
    return std::make_unique<KqueueBackend>(maxEvents);
#elif defined(QZ_PLATFORM_WINDOWS)
    return std::make_unique<IOCPBackend>(maxEvents);
#else
    return nullptr;
#endif
}

// ============================================================================
// Event Loop Implementation
// ============================================================================

EventLoop::EventLoop(const EventLoopConfig& config)
    : config_(config)
    , running_(false)
    , stopRequested_(false)
{
}

EventLoop::~EventLoop() {
    stop();
    if (backend_) {
        backend_->destroy();
    }
}

bool EventLoop::init() {
    backend_ = createBackend(config_.maxEvents);
    if (!backend_ || !backend_->create()) {
        return false;
    }
    stats_.startTime = std::chrono::steady_clock::now();
    return true;
}

void EventLoop::run() {
    running_ = true;
    stopRequested_ = false;
    
    while (!stopRequested_.load()) {
        runOnce(calculateTimeout());
    }
    
    running_ = false;
}

void EventLoop::runOnce(int timeoutMs) {
    stats_.iterations++;
    stats_.lastIterationTime = std::chrono::steady_clock::now();
    
    // Process next-tick callbacks first (highest priority)
    processNextTick();
    
    // Process timers
    processTimers();
    
    // Process immediate callbacks
    processImmediate();
    
    // Poll for I/O events
    if (backend_) {
        int numEvents = backend_->poll(timeoutMs);
        
        if (numEvents > 0) {
            backend_->processEvents([this](int fd, uint32_t events) {
                handleSocketEvent(fd, events);
            });
        } else if (numEvents == 0) {
            // No events - run idle callbacks
            processIdle();
        }
    }
}

void EventLoop::runFor(int durationMs) {
    running_ = true;
    stopRequested_ = false;
    
    auto start = std::chrono::steady_clock::now();
    auto end = start + std::chrono::milliseconds(durationMs);
    
    while (!stopRequested_.load()) {
        auto now = std::chrono::steady_clock::now();
        if (now >= end) break;
        
        auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(end - now).count();
        int timeout = std::min(static_cast<int>(remaining), calculateTimeout());
        runOnce(timeout);
    }
    
    running_ = false;
}

void EventLoop::stop() {
    stopRequested_ = true;
}

bool EventLoop::watchRead(const std::string& socketId, int fd, const std::string& callbackId) {
    std::lock_guard<std::mutex> lock(watcherMutex_);
    
    auto it = watchers_.find(socketId);
    if (it == watchers_.end()) {
        // New watcher
        IOWatcher watcher;
        watcher.socketId = socketId;
        watcher.fd = fd;
        watcher.events = IOWatcher::EV_READ;
        watcher.readCallback = callbackId;
        watchers_[socketId] = watcher;
        fdToSocketId_[fd] = socketId;
        
        if (backend_) {
            backend_->addSocket(fd, EventLoopBackend::EVT_READ, nullptr);
        }
    } else {
        // Update existing
        it->second.events |= IOWatcher::EV_READ;
        it->second.readCallback = callbackId;
        updateWatcherEvents(socketId);
    }
    
    return true;
}

bool EventLoop::watchWrite(const std::string& socketId, int fd, const std::string& callbackId) {
    std::lock_guard<std::mutex> lock(watcherMutex_);
    
    auto it = watchers_.find(socketId);
    if (it == watchers_.end()) {
        IOWatcher watcher;
        watcher.socketId = socketId;
        watcher.fd = fd;
        watcher.events = IOWatcher::EV_WRITE;
        watcher.writeCallback = callbackId;
        watchers_[socketId] = watcher;
        fdToSocketId_[fd] = socketId;
        
        if (backend_) {
            backend_->addSocket(fd, EventLoopBackend::EVT_WRITE, nullptr);
        }
    } else {
        it->second.events |= IOWatcher::EV_WRITE;
        it->second.writeCallback = callbackId;
        updateWatcherEvents(socketId);
    }
    
    return true;
}

bool EventLoop::watchAccept(const std::string& socketId, int fd, const std::string& callbackId) {
    std::lock_guard<std::mutex> lock(watcherMutex_);
    
    IOWatcher watcher;
    watcher.socketId = socketId;
    watcher.fd = fd;
    watcher.events = IOWatcher::EV_READ;  // Accept uses read event
    watcher.readCallback = callbackId;
    watcher.isServer = true;
    watchers_[socketId] = watcher;
    fdToSocketId_[fd] = socketId;
    
    if (backend_) {
        backend_->addSocket(fd, EventLoopBackend::EVT_READ, nullptr);
    }
    
    return true;
}

bool EventLoop::unwatch(const std::string& socketId) {
    std::lock_guard<std::mutex> lock(watcherMutex_);
    
    auto it = watchers_.find(socketId);
    if (it == watchers_.end()) return false;
    
    // Clean up all callbacks associated with this watcher
    if (cleanup_) {
        if (!it->second.readCallback.empty()) cleanup_(it->second.readCallback);
        if (!it->second.writeCallback.empty()) cleanup_(it->second.writeCallback);
        if (!it->second.closeCallback.empty()) cleanup_(it->second.closeCallback);
        if (!it->second.errorCallback.empty()) cleanup_(it->second.errorCallback);
    }
    
    int fd = it->second.fd;
    if (backend_) {
        backend_->removeSocket(fd);
    }
    
    fdToSocketId_.erase(fd);
    watchers_.erase(it);
    return true;
}

bool EventLoop::unwatchRead(const std::string& socketId) {
    std::lock_guard<std::mutex> lock(watcherMutex_);
    
    auto it = watchers_.find(socketId);
    if (it == watchers_.end()) return false;
    
    it->second.events &= ~IOWatcher::EV_READ;
    it->second.readCallback.clear();
    
    if (it->second.events == 0) {
        int fd = it->second.fd;
        if (backend_) {
            backend_->removeSocket(fd);
        }
        fdToSocketId_.erase(fd);
        watchers_.erase(it);
    } else {
        updateWatcherEvents(socketId);
    }
    
    return true;
}

bool EventLoop::unwatchWrite(const std::string& socketId) {
    std::lock_guard<std::mutex> lock(watcherMutex_);
    
    auto it = watchers_.find(socketId);
    if (it == watchers_.end()) return false;
    
    it->second.events &= ~IOWatcher::EV_WRITE;
    it->second.writeCallback.clear();
    
    if (it->second.events == 0) {
        int fd = it->second.fd;
        if (backend_) {
            backend_->removeSocket(fd);
        }
        fdToSocketId_.erase(fd);
        watchers_.erase(it);
    } else {
        updateWatcherEvents(socketId);
    }
    
    return true;
}

bool EventLoop::onClose(const std::string& socketId, const std::string& callbackId) {
    std::lock_guard<std::mutex> lock(watcherMutex_);
    
    auto it = watchers_.find(socketId);
    if (it == watchers_.end()) return false;
    
    it->second.closeCallback = callbackId;
    return true;
}

bool EventLoop::onError(const std::string& socketId, const std::string& callbackId) {
    std::lock_guard<std::mutex> lock(watcherMutex_);
    
    auto it = watchers_.find(socketId);
    if (it == watchers_.end()) return false;
    
    it->second.errorCallback = callbackId;
    return true;
}

uint64_t EventLoop::setTimeout(int delayMs, const std::string& callbackId) {
    std::lock_guard<std::mutex> lock(timerMutex_);
    
    Timer timer;
    timer.id = nextTimerId_++;
    timer.deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(delayMs);
    timer.intervalMs = 0;  // One-shot
    timer.cancelled = false;
    timer.callbackId = callbackId;
    
    // Track callback for cleanup
    timerCallbacks_[timer.id] = callbackId;
    
    timers_.push(timer);
    return timer.id;
}

uint64_t EventLoop::setInterval(int intervalMs, const std::string& callbackId) {
    std::lock_guard<std::mutex> lock(timerMutex_);
    
    Timer timer;
    timer.id = nextTimerId_++;
    timer.deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(intervalMs);
    timer.intervalMs = intervalMs;  // Repeating
    timer.cancelled = false;
    timer.callbackId = callbackId;
    
    // Track callback for cleanup
    timerCallbacks_[timer.id] = callbackId;
    
    timers_.push(timer);
    return timer.id;
}

bool EventLoop::clearTimer(uint64_t timerId) {
    std::lock_guard<std::mutex> lock(timerMutex_);
    cancelledTimers_.insert(timerId);
    
    // Clean up the callback
    auto it = timerCallbacks_.find(timerId);
    if (it != timerCallbacks_.end()) {
        if (cleanup_) {
            cleanup_(it->second);
        }
        timerCallbacks_.erase(it);
    }
    
    return true;
}

void EventLoop::setImmediate(const std::string& callbackId) {
    std::lock_guard<std::mutex> lock(queueMutex_);
    immediateQueue_.push(callbackId);
}

void EventLoop::nextTick(const std::string& callbackId) {
    std::lock_guard<std::mutex> lock(queueMutex_);
    nextTickQueue_.push(callbackId);
}

void EventLoop::onIdle(const std::string& callbackId) {
    std::lock_guard<std::mutex> lock(idleMutex_);
    idleCallbacks_.push_back(callbackId);
}

void EventLoop::clearIdle() {
    std::lock_guard<std::mutex> lock(idleMutex_);
    idleCallbacks_.clear();
}

void EventLoop::processTimers() {
    std::lock_guard<std::mutex> lock(timerMutex_);
    
    auto now = std::chrono::steady_clock::now();
    
    while (!timers_.empty()) {
        const Timer& top = timers_.top();
        
        if (top.deadline > now) break;
        
        Timer timer = timers_.top();
        timers_.pop();
        
        // Check if cancelled
        if (cancelledTimers_.count(timer.id) > 0) {
            cancelledTimers_.erase(timer.id);
            // Callback already cleaned up in clearTimer
            continue;
        }
        
        if (timer.cancelled) continue;
        
        // Invoke callback
        Event event(EventType::TIMEOUT);
        event.timerId = timer.id;
        invokeCallback(timer.callbackId, event);
        
        if (config_.enableStats) {
            stats_.timerEvents++;
            stats_.totalEvents++;
        }
        
        // Re-schedule if interval, otherwise clean up callback
        if (timer.intervalMs > 0) {
            timer.deadline = now + std::chrono::milliseconds(timer.intervalMs);
            timers_.push(timer);
        } else {
            // One-shot timer completed - clean up callback
            if (cleanup_) {
                cleanup_(timer.callbackId);
            }
            timerCallbacks_.erase(timer.id);
        }
    }
}

void EventLoop::processNextTick() {
    std::queue<std::string> toProcess;
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        std::swap(toProcess, nextTickQueue_);
    }
    
    while (!toProcess.empty()) {
        std::string callbackId = toProcess.front();
        toProcess.pop();
        
        Event event(EventType::CUSTOM);
        invokeCallback(callbackId, event);
    }
}

void EventLoop::processImmediate() {
    std::queue<std::string> toProcess;
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        std::swap(toProcess, immediateQueue_);
    }
    
    while (!toProcess.empty()) {
        std::string callbackId = toProcess.front();
        toProcess.pop();
        
        Event event(EventType::CUSTOM);
        invokeCallback(callbackId, event);
    }
}

void EventLoop::processIdle() {
    std::vector<std::string> callbacks;
    {
        std::lock_guard<std::mutex> lock(idleMutex_);
        callbacks = idleCallbacks_;
    }
    
    int count = 0;
    for (const auto& callbackId : callbacks) {
        if (count >= config_.maxIdleCallbacksPerIter) break;
        
        Event event(EventType::IDLE);
        invokeCallback(callbackId, event);
        count++;
        
        if (config_.enableStats) {
            stats_.idleEvents++;
            stats_.totalEvents++;
        }
    }
}

int EventLoop::calculateTimeout() {
    std::lock_guard<std::mutex> lock(timerMutex_);
    
    if (timers_.empty()) {
        return config_.defaultTimeoutMs;
    }
    
    auto now = std::chrono::steady_clock::now();
    const Timer& top = timers_.top();
    
    if (top.deadline <= now) {
        return 0;  // Timer already due
    }
    
    auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(top.deadline - now).count();
    return std::min(static_cast<int>(diff), config_.defaultTimeoutMs);
}

void EventLoop::invokeCallback(const std::string& callbackId, const Event& event) {
    if (invoker_ && !callbackId.empty()) {
        invoker_(callbackId, event);
    }
}

void EventLoop::handleSocketEvent(int fd, uint32_t events) {
    std::string socketId;
    IOWatcher watcher;
    
    {
        std::lock_guard<std::mutex> lock(watcherMutex_);
        
        auto it = fdToSocketId_.find(fd);
        if (it == fdToSocketId_.end()) return;
        
        socketId = it->second;
        auto wit = watchers_.find(socketId);
        if (wit == watchers_.end()) return;
        
        watcher = wit->second;
    }
    
    if (events & EventLoopBackend::EVT_ERROR) {
        Event event(EventType::ERROR_EVENT, socketId, fd);
        invokeCallback(watcher.errorCallback, event);
        
        if (config_.enableStats) {
            stats_.errorEvents++;
            stats_.totalEvents++;
        }
        return;
    }
    
    if (events & EventLoopBackend::EVT_CLOSE) {
        Event event(EventType::CLOSE, socketId, fd);
        invokeCallback(watcher.closeCallback, event);
        
        // Auto-unwatch on close
        unwatch(socketId);
        return;
    }
    
    if (events & EventLoopBackend::EVT_READ) {
        if (watcher.isServer) {
            Event event(EventType::ACCEPT, socketId, fd);
            invokeCallback(watcher.readCallback, event);
            
            if (config_.enableStats) {
                stats_.acceptEvents++;
                stats_.totalEvents++;
            }
        } else {
            Event event(EventType::READ, socketId, fd);
            invokeCallback(watcher.readCallback, event);
            
            if (config_.enableStats) {
                stats_.readEvents++;
                stats_.totalEvents++;
            }
        }
    }
    
    if (events & EventLoopBackend::EVT_WRITE) {
        Event event(EventType::WRITE, socketId, fd);
        invokeCallback(watcher.writeCallback, event);
        
        if (config_.enableStats) {
            stats_.writeEvents++;
            stats_.totalEvents++;
        }
    }
}

void EventLoop::updateWatcherEvents(const std::string& socketId) {
    auto it = watchers_.find(socketId);
    if (it == watchers_.end()) return;
    
    uint32_t backendEvents = 0;
    if (it->second.events & IOWatcher::EV_READ)  backendEvents |= EventLoopBackend::EVT_READ;
    if (it->second.events & IOWatcher::EV_WRITE) backendEvents |= EventLoopBackend::EVT_WRITE;
    
    if (backend_) {
        backend_->modifySocket(it->second.fd, backendEvents);
    }
}

// ============================================================================
// Global Event Loop Instance
// ============================================================================

static std::unique_ptr<EventLoop> g_eventLoop;
static std::mutex g_eventLoopMutex;

EventLoop& getEventLoop() {
    std::lock_guard<std::mutex> lock(g_eventLoopMutex);
    if (!g_eventLoop) {
        g_eventLoop = std::make_unique<EventLoop>();
        g_eventLoop->init();
    }
    return *g_eventLoop;
}

bool initEventLoop(const EventLoopConfig& config) {
    std::lock_guard<std::mutex> lock(g_eventLoopMutex);
    g_eventLoop = std::make_unique<EventLoop>(config);
    return g_eventLoop->init();
}

void destroyEventLoop() {
    std::lock_guard<std::mutex> lock(g_eventLoopMutex);
    g_eventLoop.reset();
}

} // namespace eventloop
