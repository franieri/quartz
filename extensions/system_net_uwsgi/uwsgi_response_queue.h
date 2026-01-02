// ============================================================================
// Lock-Free Response Queue for uWSGI Server
// Enables non-blocking response submission from async handlers
// Cross-platform: Windows, Linux, macOS
// ============================================================================

#ifndef UWSGI_RESPONSE_QUEUE_H
#define UWSGI_RESPONSE_QUEUE_H

#include "qz/lockfree_queue.h"
#include "uwsgi_types.h"
#include <string>
#include <memory>

namespace uwsgi {

// ============================================================================
// Pending Response Item
// ============================================================================

struct PendingResponse {
    std::string connectionId;
    HttpResponse response;
    
    PendingResponse() = default;
    
    PendingResponse(std::string connId, HttpResponse resp)
        : connectionId(std::move(connId))
        , response(std::move(resp)) {}
    
    PendingResponse(const PendingResponse&) = default;
    PendingResponse(PendingResponse&&) = default;
    PendingResponse& operator=(const PendingResponse&) = default;
    PendingResponse& operator=(PendingResponse&&) = default;
};

// ============================================================================
// Response Queue Manager
// - Multiple async handlers can submit responses (MPSC queue)
// - IO thread consumes responses during poll()
// - No blocking on submission - fails fast if queue full
// ============================================================================

class ResponseQueue {
public:
    ResponseQueue() = default;
    
    // Submit a response (called from async handler/FFI boundary)
    // Returns false if queue is full - caller should handle fallback
    bool submit(std::string connectionId, HttpResponse response) {
        return queue_.try_enqueue(PendingResponse(
            std::move(connectionId), 
            std::move(response)
        ));
    }
    
    // Process pending responses (called from IO thread)
    // Returns number of responses processed
    template<typename Handler>
    size_t processPending(Handler&& handler, size_t maxBatch = 32) {
        size_t processed = 0;
        
        while (processed < maxBatch) {
            auto item = queue_.try_dequeue();
            if (!item) break;
            
            handler(item->connectionId, item->response);
            ++processed;
        }
        
        return processed;
    }
    
    // Check queue status
    size_t pendingCount() const {
        return queue_.approximate_size();
    }
    
    bool isEmpty() const {
        return queue_.empty();
    }
    
    bool isFull() const {
        return queue_.full();
    }
    
    static constexpr size_t capacity() {
        return qz::MPSCQueue<PendingResponse, 4096>::capacity();
    }
    
private:
    qz::MPSCQueue<PendingResponse, 4096> queue_;
};

} // namespace uwsgi

#endif // UWSGI_RESPONSE_QUEUE_H
