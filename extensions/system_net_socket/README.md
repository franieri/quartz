# system.net.socket Extension

High-performance cross-platform TCP/UDP socket library for Quartz.

## Features

- Non-blocking I/O with platform-native backends
- TCP and UDP socket support
- Socket options (SO_REUSEADDR, TCP_NODELAY, SO_KEEPALIVE, etc.)
- High-performance buffer management
- Connection statistics and monitoring
- IPv4 and IPv6 support

## Platform Support

| Platform | Backend | Status |
|----------|---------|--------|
| Linux | epoll | ✅ Full support |
| macOS | kqueue | ✅ Full support |
| Windows | IOCP | ✅ Basic support |

## API Reference

### Socket Creation

```quartz
import system.net.socket as socket;

// Create raw sockets
let tcpSocket = socket.tcp();
let udpSocket = socket.udp();

// High-level helpers
let server = socket.createServer("0.0.0.0", 8080);    // TCP server
let client = socket.createClient("example.com", 80);  // TCP client
```

### Socket Operations

```quartz
// Bind and listen (server)
socket.bind(sock, "0.0.0.0", 8080);
socket.listen(sock, 1024);  // backlog

// Accept connections
let conn = socket.accept(serverSock);
// Returns: {socket: "socket:...", host: "...", port: ...}

// Connect (client)
socket.connect(sock, "example.com", 80);
```

### I/O Operations

```quartz
// Send data
let bytesSent = socket.send(sock, "Hello");
socket.sendAll(sock, "Complete message");  // Blocks until all sent

// Receive data
let data = socket.recv(sock, 4096);  // maxLength
let line = socket.recvLine(sock);    // Read until newline

// UDP
socket.sendTo(sock, "data", "host", port);
let result = socket.recvFrom(sock);  // {data, host, port}
```

### Socket Options

```quartz
socket.setNonBlocking(sock, true);
socket.setReuseAddr(sock, true);
socket.setNoDelay(sock, true);           // TCP_NODELAY
socket.setBufferSizes(sock, 65536, 65536);  // recv, send
```

### Socket Info

```quartz
let info = socket.info(sock);
// Returns:
// {
//   id: "socket:sock_0",
//   type: "tcp",
//   state: "connected",  // closed, created, bound, listening, connected, error
//   localHost: "0.0.0.0",
//   localPort: 8080,
//   remoteHost: "...",
//   remotePort: ...,
//   bytesReceived: 0,
//   bytesSent: 0,
//   ...
// }

socket.close(sock);
```

### Utilities

```quartz
let ip = socket.resolve("example.com");
let addrs = socket.localAddresses();
let isValid = socket.isSocket(value);
let count = socket.socketCount();
```

## Example: Simple Echo Server

```quartz
import system.net.socket as socket;
import system.io as io;

let server = socket.createServer("0.0.0.0", 8080);
io.out.println("Echo server listening on port 8080");

while (true) {
    let conn = socket.accept(server);
    if (conn != "") {
        let clientSock = conn["socket"];
        let data = socket.recv(clientSock, 1024);
        
        if (data != "") {
            socket.send(clientSock, "Echo: " + data);
        }
        
        socket.close(clientSock);
    }
}
```

## Performance Tips

1. Use non-blocking sockets with the event loop for high concurrency
2. Enable TCP_NODELAY for low-latency applications
3. Set appropriate buffer sizes for your workload
4. Use SO_REUSEADDR for fast server restarts
5. Consider SO_REUSEPORT (Linux) for load balancing across processes
