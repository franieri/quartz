# system.net.http Extension

HTTP client library for Quartz providing RFC-compliant HTTP request/response types and utilities.

## Overview

This extension provides comprehensive HTTP functionality based on RFC 7230-7235, RFC 6265 (Cookies), and RFC 3986 (URLs). It includes:

- **HttpRequest** - Build and serialize HTTP requests
- **HttpResponse** - Parse and work with HTTP responses  
- **URL** - Parse and build URLs
- **Headers** - HTTP header management (case-insensitive)
- **Cookies** - Cookie parsing and serialization
- **Status Codes** - Full RFC 7231 status code support
- **MIME Types** - Common content type constants
- **Authentication** - Basic and Bearer auth helpers

## Usage

```quartz
import system.net.http as http;
import system.io as io;

// Create a GET request
let request = http.request.get("https://api.example.com/users");

// Create a POST request with JSON body
let postReq = http.request.post("https://api.example.com/users", "{\"name\": \"John\"}");

// Check status code categories
let code = 404;
io.out.println("Is client error:", http.status.isClientError(code));
io.out.println("Status text:", http.status.text(code));

// Parse a URL
let url = http.url.parse("https://user:pass@example.com:8080/path?q=search#anchor");
io.out.println("Host:", url["host"]);
io.out.println("Port:", url["port"]);
io.out.println("Path:", url["path"]);

// URL encoding
let encoded = http.url.encode("hello world & more");
io.out.println("Encoded:", encoded);

// Create auth headers
let basicAuth = http.auth.basic("username", "password");
let bearerAuth = http.auth.bearer("my-jwt-token");

// Use MIME type constants
let contentType = http.mime.JSON();
io.out.println("Content-Type:", contentType);

// Use HTTP method constants
let method = http.method.POST();
io.out.println("Method:", method);

// Parse cookies
let cookie = http.cookie.parse("session=abc123; Path=/; HttpOnly; Secure");
io.out.println("Cookie name:", cookie["name"]);
io.out.println("HttpOnly:", cookie["httpOnly"]);
```

## API Reference

### Request Functions

| Function | Description |
|----------|-------------|
| `http.request.create(method, url)` | Create a request with specified method and URL |
| `http.request.get(url)` | Create a GET request |
| `http.request.post(url, body?)` | Create a POST request |
| `http.request.put(url, body?)` | Create a PUT request |
| `http.request.delete(url)` | Create a DELETE request |
| `http.request.patch(url, body?)` | Create a PATCH request |
| `http.request.head(url)` | Create a HEAD request |
| `http.request.options(url)` | Create an OPTIONS request |
| `http.request.serialize(request)` | Serialize request to HTTP wire format |

### Response Functions

| Function | Description |
|----------|-------------|
| `http.response.create(statusCode, body?)` | Create a response (for testing) |
| `http.response.isOk(response)` | Check if response is successful (2xx) |
| `http.response.getHeader(response, name)` | Get a header value (case-insensitive) |

### Status Functions

| Function | Description |
|----------|-------------|
| `http.status.text(code)` | Get reason phrase for status code |
| `http.status.isInformational(code)` | Check if 1xx status |
| `http.status.isSuccessful(code)` | Check if 2xx status |
| `http.status.isRedirection(code)` | Check if 3xx status |
| `http.status.isClientError(code)` | Check if 4xx status |
| `http.status.isServerError(code)` | Check if 5xx status |
| `http.status.isError(code)` | Check if 4xx or 5xx status |

### URL Functions

| Function | Description |
|----------|-------------|
| `http.url.parse(urlString)` | Parse URL into components |
| `http.url.build(components)` | Build URL from components dict |
| `http.url.encode(str)` | URL-encode a string |
| `http.url.decode(str)` | URL-decode a string |

### Headers Functions

| Function | Description |
|----------|-------------|
| `http.headers.create()` | Create new headers dict |
| `http.headers.set(headers, name, value)` | Set a header |
| `http.headers.get(headers, name)` | Get a header (case-insensitive) |
| `http.headers.has(headers, name)` | Check if header exists |

### Cookie Functions

| Function | Description |
|----------|-------------|
| `http.cookie.parse(setCookieHeader)` | Parse Set-Cookie header |
| `http.cookie.create(name, value)` | Create a new cookie |
| `http.cookie.toString(cookie)` | Serialize cookie to string |

### Auth Functions

| Function | Description |
|----------|-------------|
| `http.auth.basic(username, password)` | Generate Basic auth header |
| `http.auth.bearer(token)` | Generate Bearer auth header |

### Method Constants

| Constant | Value |
|----------|-------|
| `http.method.GET()` | "GET" |
| `http.method.POST()` | "POST" |
| `http.method.PUT()` | "PUT" |
| `http.method.DELETE()` | "DELETE" |
| `http.method.PATCH()` | "PATCH" |
| `http.method.HEAD()` | "HEAD" |
| `http.method.OPTIONS()` | "OPTIONS" |
| `http.method.TRACE()` | "TRACE" |
| `http.method.CONNECT()` | "CONNECT" |

### MIME Type Constants

| Constant | Value |
|----------|-------|
| `http.mime.JSON()` | "application/json" |
| `http.mime.XML()` | "application/xml" |
| `http.mime.FORM()` | "application/x-www-form-urlencoded" |
| `http.mime.MULTIPART()` | "multipart/form-data" |
| `http.mime.TEXT()` | "text/plain" |
| `http.mime.HTML()` | "text/html" |
| `http.mime.CSS()` | "text/css" |
| `http.mime.JS()` | "text/javascript" |
| `http.mime.PNG()` | "image/png" |
| `http.mime.JPEG()` | "image/jpeg" |
| `http.mime.GIF()` | "image/gif" |
| `http.mime.OCTET_STREAM()` | "application/octet-stream" |

### Header Name Constants

| Constant | Value |
|----------|-------|
| `http.header.ACCEPT()` | "Accept" |
| `http.header.AUTHORIZATION()` | "Authorization" |
| `http.header.CONTENT_TYPE()` | "Content-Type" |
| `http.header.CONTENT_LENGTH()` | "Content-Length" |
| `http.header.HOST()` | "Host" |
| `http.header.USER_AGENT()` | "User-Agent" |
| `http.header.COOKIE()` | "Cookie" |
| `http.header.SET_COOKIE()` | "Set-Cookie" |
| ... and more |

## HttpRequest Object Structure

```quartz
{
    "method": "GET",           // HTTP method
    "url": "https://...",      // Full URL string
    "version": "HTTP/1.1",     // HTTP version
    "headers": { ... },        // Headers dict
    "body": "",                // Request body
    "timeoutMs": 30000,        // Timeout in milliseconds
    "followRedirects": true,   // Follow redirects?
    "maxRedirects": 10         // Max redirect count
}
```

## HttpResponse Object Structure

```quartz
{
    "version": "HTTP/1.1",      // HTTP version
    "statusCode": 200,          // Status code
    "statusText": "OK",         // Reason phrase
    "headers": { ... },         // Headers dict
    "body": "...",              // Response body
    "elapsedTimeMs": 123,       // Request duration
    "finalUrl": "https://...",  // URL after redirects
    "redirectCount": 0,         // Number of redirects
    "ok": true,                 // Is 2xx?
    "isSuccessful": true,       // Is 2xx?
    "isClientError": false,     // Is 4xx?
    "isServerError": false,     // Is 5xx?
    "isError": false            // Is 4xx or 5xx?
}
```

## URL Object Structure

```quartz
{
    "scheme": "https",
    "host": "example.com",
    "port": 443,
    "path": "/api/users",
    "query": "page=1",
    "fragment": "section",
    "username": "",
    "password": "",
    "href": "https://example.com/api/users?page=1#section"
}
```

## Cookie Object Structure

```quartz
{
    "name": "session",
    "value": "abc123",
    "domain": ".example.com",
    "path": "/",
    "expires": "",
    "maxAge": -1,           // -1 = session cookie
    "secure": true,
    "httpOnly": true,
    "sameSite": "Lax"
}
```

## RFC Compliance

This library implements types according to:

- **RFC 7230** - HTTP/1.1 Message Syntax and Routing
- **RFC 7231** - HTTP/1.1 Semantics and Content
- **RFC 7232** - HTTP/1.1 Conditional Requests
- **RFC 7233** - HTTP/1.1 Range Requests
- **RFC 7234** - HTTP/1.1 Caching
- **RFC 7235** - HTTP/1.1 Authentication
- **RFC 6265** - HTTP State Management Mechanism (Cookies)
- **RFC 3986** - Uniform Resource Identifier (URI)
- **RFC 5789** - PATCH Method for HTTP
