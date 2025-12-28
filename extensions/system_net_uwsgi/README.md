# system.net.uwsgi - High-Performance uWSGI Server

A high-performance uWSGI protocol server extension for Quartz, designed to work with nginx as a frontend reverse proxy. Features non-blocking I/O with epoll (Linux) / kqueue (macOS) for maximum performance.

## Features

- **uWSGI Protocol**: Full implementation of the uWSGI binary protocol
- **High Performance**: Uses epoll/kqueue for efficient I/O multiplexing
- **Async Support**: Integrates with `system.async.eventloop` for async request handling
- **Unix Sockets**: Support for both TCP and Unix domain sockets
- **Connection Pooling**: Efficient connection management with keep-alive support
- **Statistics**: Built-in request/connection metrics

## Quick Start

### Simple Synchronous Server

```quartz
import system.net.uwsgi.*

// Create server
let server = create({
    "port": 3031,
    "host": "127.0.0.1"
})

// Set request handler
onRequest(server, fn(req) {
    return response.json('{"message": "Hello from Quartz!"}')
})

// Start and run
start(server)
run(server)
```

### With Event Loop (Async)

```quartz
import system.net.uwsgi.*
import system.async.eventloop as ev

// Create and configure server
let server = create({
    "socket": "/tmp/quartz.sock",  // Unix socket for nginx
    "backlog": 4096,
    "maxConnections": 10000
})

// Async request handler
onRequestAsync(server, fn(req, connId) {
    // Process request asynchronously
    ev.setTimeout(10, fn(e) {
        let body = '{"path": "' + req["path"] + '"}'
        respond(server, connId, response.json(body))
    })
})

start(server)

// Integrate with event loop
ev.init()
ev.setInterval(1, fn(e) {
    poll(server, 0)  // Non-blocking poll
})
ev.run()
```

## nginx Configuration

```nginx
upstream quartz_backend {
    server unix:/tmp/quartz.sock;
    # Or TCP: server 127.0.0.1:3031;
}

server {
    listen 80;
    server_name example.com;
    
    location / {
        uwsgi_pass quartz_backend;
        include uwsgi_params;
        
        # Performance tuning
        uwsgi_buffering on;
        uwsgi_buffer_size 4k;
        uwsgi_buffers 8 4k;
    }
}
```

## API Reference

### Server Lifecycle

| Function | Description |
|----------|-------------|
| `create(config?)` | Create a new uWSGI server |
| `configure(server, config)` | Configure server settings |
| `start(server)` | Start the server |
| `stop(server)` | Stop the server |
| `destroy(server)` | Destroy the server |
| `isRunning(server)` | Check if server is running |
| `run(server)` | Run server in blocking mode |

### Request Handling

| Function | Description |
|----------|-------------|
| `onRequest(server, handler)` | Set sync request handler |
| `onRequestAsync(server, handler)` | Set async request handler |
| `respond(server, connId, response)` | Send async response |

### Event Loop Integration

| Function | Description |
|----------|-------------|
| `poll(server, timeout?)` | Process I/O events |
| `fd(server)` | Get server socket fd |

### Response Helpers

| Function | Description |
|----------|-------------|
| `response.ok(body, contentType?)` | 200 OK response |
| `response.json(body)` | JSON response |
| `response.html(body)` | HTML response |
| `response.error(status, message?)` | Error response |
| `response.redirect(location, status?)` | Redirect response |

### Statistics

| Function | Description |
|----------|-------------|
| `stats(server)` | Get server statistics |
| `resetStats(server)` | Reset statistics |
| `activeConnections(server)` | Get active connection count |
| `totalStats()` | Aggregate stats for all servers |

### Utilities

| Function | Description |
|----------|-------------|
| `urlDecode(str)` | URL decode string |
| `urlEncode(str)` | URL encode string |
| `parseQuery(query)` | Parse query string |
| `mimeType(extension)` | Get MIME type |
| `statusText(code)` | Get HTTP status text |

## Configuration Options

```quartz
{
    // Network
    "host": "127.0.0.1",      // Bind address
    "port": 3031,              // TCP port
    "socket": "/tmp/app.sock", // Unix socket path
    "backlog": 4096,           // Listen backlog
    
    // Connections
    "maxConnections": 10000,   // Max concurrent connections
    "timeout": 30000,          // Connection timeout (ms)
    "keepAliveTimeout": 5000,  // Keep-alive timeout (ms)
    "keepAlive": true,         // Enable keep-alive
    
    // Performance
    "tcpNoDelay": true,        // Disable Nagle's algorithm
    "reuseAddr": true,         // SO_REUSEADDR
    "reusePort": false,        // SO_REUSEPORT (for multi-process)
    "maxBodySize": 67108864,   // Max request body (64MB)
    
    // Debug
    "verbose": false           // Enable verbose logging
}
```

## Request Object

The request object passed to handlers contains:

```quartz
{
    "method": "GET",
    "uri": "/api/users?id=123",
    "path": "/api/users",
    "queryString": "id=123",
    "protocol": "HTTP/1.1",
    "host": "example.com",
    "remoteAddr": "192.168.1.1",
    "remotePort": 54321,
    "contentType": "application/json",
    "contentLength": 0,
    "body": "",
    "elapsedMs": 0,
    "headers": {
        "User-Agent": "...",
        "Accept": "*/*"
    },
    "params": {
        "id": "123"
    },
    "uwsgiVars": {
        "REQUEST_METHOD": "GET",
        "REQUEST_URI": "/api/users?id=123",
        ...
    }
}
```

## Response Object

Response dictionaries have this structure:

```quartz
{
    "status": 200,
    "statusText": "OK",  // optional
    "headers": {
        "Content-Type": "application/json",
        "X-Custom": "value"
    },
    "body": '{"data": "value"}'
}
```

## Performance Tips

1. **Use Unix Sockets**: For local nginx, Unix sockets have lower latency than TCP
2. **Enable TCP_NODELAY**: Already enabled by default, reduces latency
3. **Set High Backlog**: Use 4096+ for high-traffic applications
4. **Use Async Handlers**: For I/O-bound operations, use `onRequestAsync` with the event loop
5. **SO_REUSEPORT**: Enable for multi-process deployments

## Benchmarks

Typical performance on modern hardware:
- ~50,000+ requests/second (simple JSON response)
- ~100,000+ connections concurrent (with event loop)
- Sub-millisecond latency for simple responses

## Dependencies

- `system.async.eventloop` (optional, for async operation)
