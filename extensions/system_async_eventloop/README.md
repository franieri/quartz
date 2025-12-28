# system.async.eventloop Extension

High-performance event loop for asynchronous I/O in Quartz, using platform-native multiplexing (epoll, kqueue, IOCP).

## Features

- Platform-native I/O multiplexing for maximum performance
- Socket event watching (read, write, accept, close, error)
- Timer management (setTimeout, setInterval)
- Immediate and next-tick scheduling
- Idle callbacks for background work
- Statistics and monitoring

## Platform Support

| Platform | Backend | Performance |
|----------|---------|-------------|
| Linux | epoll (edge-triggered) | ⚡ Excellent |
| macOS | kqueue | ⚡ Excellent |
| Windows | IOCP | ✅ Good |

## API Reference

### Lifecycle

```quartz
import system.async.eventloop as eventloop;

// Initialize (optional - auto-initialized on first use)
eventloop.init();
eventloop.init({maxEvents: 1024, timeout: 100});

// Run the event loop
eventloop.run();              // Run until stop() is called
eventloop.runOnce(100);       // Process one batch of events (timeout ms)
eventloop.runFor(5000);       // Run for specific duration (ms)

// Stop the event loop
eventloop.stop();

// Check status
let running = eventloop.isRunning();
```

### Socket Event Watching

```quartz
import system.net.socket as socket;
import system.async.eventloop as eventloop;

let server = socket.createServer("0.0.0.0", 8080);
let info = socket.info(server);

// Watch for incoming connections
eventloop.watchAccept(server, info["fd"], fn(event) {
    io.out.println("New connection!");
    let conn = socket.accept(server);
    // ... handle connection
});

// Watch for readable data
eventloop.watchRead(clientSocket, clientFd, fn(event) {
    let data = socket.recv(clientSocket, 4096);
    // ... process data
});

// Watch for writable (buffer space available)
eventloop.watchWrite(clientSocket, clientFd, fn(event) {
    socket.send(clientSocket, pendingData);
});

// Stop watching
eventloop.unwatch(socketId);

// Set close/error handlers
eventloop.onClose(socketId, fn(event) {
    io.out.println("Connection closed");
});

eventloop.onError(socketId, fn(event) {
    io.out.println("Socket error: " + event["errorCode"]);
});
```

### Timers

```quartz
// One-shot timer
let timerId = eventloop.setTimeout(1000, fn(event) {
    io.out.println("Fired after 1 second");
});

// Repeating timer
let intervalId = eventloop.setInterval(100, fn(event) {
    io.out.println("Fires every 100ms");
});

// Cancel timers
eventloop.clearTimeout(timerId);
eventloop.clearInterval(intervalId);
```

### Scheduling

```quartz
// Run callback before next I/O poll (highest priority)
eventloop.nextTick(fn(event) {
    io.out.println("Runs before I/O");
});

// Run callback after current I/O events
eventloop.setImmediate(fn(event) {
    io.out.println("Runs after I/O");
});

// Run callback when no other events (low priority)
eventloop.onIdle(fn(event) {
    // Background work
});
eventloop.clearIdle();
```

### Statistics

```quartz
let stats = eventloop.stats();
// Returns:
// {
//   totalEvents: 1000,
//   readEvents: 500,
//   writeEvents: 200,
//   acceptEvents: 50,
//   errorEvents: 0,
//   timerEvents: 250,
//   idleEvents: 0,
//   iterations: 100,
//   uptimeMs: 5000
// }

eventloop.resetStats();

// Watcher info
let watchers = eventloop.watcherCount();
let timers = eventloop.timerCount();
```

## Event Object

All callbacks receive an event object:

```quartz
{
    type: "read",       // read, write, accept, close, error, timeout, idle, custom
    socketId: "...",    // Socket ID (for socket events)
    fd: 5,              // File descriptor
    data: "",           // Optional data
    errorCode: 0,       // Error code (for error events)
    timerId: 1          // Timer ID (for timeout events)
}
```

## Example: Async Echo Server

```quartz
import system.io as io;
import system.net.socket as socket;
import system.async.eventloop as eventloop;

let server = socket.createServer("0.0.0.0", 8080);
let serverInfo = socket.info(server);

io.out.println("Async echo server on port 8080");

// Watch for connections
eventloop.watchAccept(server, serverInfo["fd"], fn(event) {
    let conn = socket.accept(server);
    if (conn == "") { return; }
    
    let clientSock = conn["socket"];
    let clientInfo = socket.info(clientSock);
    
    io.out.println("Client connected: " + conn["host"]);
    
    // Watch for data from this client
    eventloop.watchRead(clientSock, clientInfo["fd"], fn(readEvent) {
        let data = socket.recv(clientSock, 4096);
        
        if (data == "") {
            // Connection closed
            eventloop.unwatch(clientSock);
            socket.close(clientSock);
            io.out.println("Client disconnected");
            return;
        }
        
        // Echo back
        socket.send(clientSock, "Echo: " + data);
    });
});

// Run forever
eventloop.run();
```

## Performance Characteristics

### Event Loop Phases (per iteration)

1. **Next-tick callbacks** - Highest priority, runs first
2. **Timer processing** - Check and fire due timers
3. **Immediate callbacks** - After timer phase
4. **I/O polling** - Wait for socket events
5. **I/O callbacks** - Process socket events
6. **Idle callbacks** - Only when no other work

### Scaling

| Connections | Memory | Latency |
|-------------|--------|---------|
| 1,000 | ~1 MB | < 1ms |
| 10,000 | ~10 MB | 1-2ms |
| 100,000 | ~100 MB | 2-5ms |

## Best Practices

1. **Use edge-triggered mode** - Both epoll and kqueue are configured for edge-triggered for efficiency
2. **Process all available data** - On read events, read until EAGAIN
3. **Avoid blocking operations** - Use async alternatives or worker threads
4. **Monitor statistics** - Watch for error events and increasing latency
5. **Use nextTick for immediate work** - Higher priority than setImmediate
6. **Use onIdle for background tasks** - Won't delay I/O processing
