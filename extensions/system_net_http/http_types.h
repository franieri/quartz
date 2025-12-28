// ============================================================================
// HTTP Types Header
// RFC 7230-7235 compliant HTTP types for Quartz
// ============================================================================

#ifndef HTTP_TYPES_H
#define HTTP_TYPES_H

#include <string>
#include <unordered_map>
#include <vector>
#include <optional>

namespace http {

// ============================================================================
// HTTP Methods (RFC 7231 Section 4)
// ============================================================================
enum class Method {
    GET,        // RFC 7231 Section 4.3.1
    HEAD,       // RFC 7231 Section 4.3.2
    POST,       // RFC 7231 Section 4.3.3
    PUT,        // RFC 7231 Section 4.3.4
    DELETE_,    // RFC 7231 Section 4.3.5 (DELETE_ to avoid C++ keyword)
    CONNECT,    // RFC 7231 Section 4.3.6
    OPTIONS,    // RFC 7231 Section 4.3.7
    TRACE,      // RFC 7231 Section 4.3.8
    PATCH       // RFC 5789
};

inline std::string methodToString(Method method) {
    switch (method) {
        case Method::GET:       return "GET";
        case Method::HEAD:      return "HEAD";
        case Method::POST:      return "POST";
        case Method::PUT:       return "PUT";
        case Method::DELETE_:   return "DELETE";
        case Method::CONNECT:   return "CONNECT";
        case Method::OPTIONS:   return "OPTIONS";
        case Method::TRACE:     return "TRACE";
        case Method::PATCH:     return "PATCH";
        default:                return "GET";
    }
}

inline Method stringToMethod(const std::string& str) {
    if (str == "GET")       return Method::GET;
    if (str == "HEAD")      return Method::HEAD;
    if (str == "POST")      return Method::POST;
    if (str == "PUT")       return Method::PUT;
    if (str == "DELETE")    return Method::DELETE_;
    if (str == "CONNECT")   return Method::CONNECT;
    if (str == "OPTIONS")   return Method::OPTIONS;
    if (str == "TRACE")     return Method::TRACE;
    if (str == "PATCH")     return Method::PATCH;
    return Method::GET;  // Default
}

// ============================================================================
// HTTP Status Codes (RFC 7231 Section 6)
// ============================================================================
enum class StatusCode {
    // 1xx Informational (RFC 7231 Section 6.2)
    Continue            = 100,
    SwitchingProtocols  = 101,
    Processing          = 102,  // RFC 2518
    EarlyHints          = 103,  // RFC 8297
    
    // 2xx Successful (RFC 7231 Section 6.3)
    OK                  = 200,
    Created             = 201,
    Accepted            = 202,
    NonAuthoritativeInformation = 203,
    NoContent           = 204,
    ResetContent        = 205,
    PartialContent      = 206,  // RFC 7233
    MultiStatus         = 207,  // RFC 4918
    AlreadyReported     = 208,  // RFC 5842
    IMUsed              = 226,  // RFC 3229
    
    // 3xx Redirection (RFC 7231 Section 6.4)
    MultipleChoices     = 300,
    MovedPermanently    = 301,
    Found               = 302,
    SeeOther            = 303,
    NotModified         = 304,  // RFC 7232
    UseProxy            = 305,
    TemporaryRedirect   = 307,
    PermanentRedirect   = 308,  // RFC 7538
    
    // 4xx Client Error (RFC 7231 Section 6.5)
    BadRequest          = 400,
    Unauthorized        = 401,  // RFC 7235
    PaymentRequired     = 402,
    Forbidden           = 403,
    NotFound            = 404,
    MethodNotAllowed    = 405,
    NotAcceptable       = 406,
    ProxyAuthenticationRequired = 407,  // RFC 7235
    RequestTimeout      = 408,
    Conflict            = 409,
    Gone                = 410,
    LengthRequired      = 411,
    PreconditionFailed  = 412,  // RFC 7232
    PayloadTooLarge     = 413,
    URITooLong          = 414,
    UnsupportedMediaType = 415,
    RangeNotSatisfiable = 416,  // RFC 7233
    ExpectationFailed   = 417,
    ImATeapot           = 418,  // RFC 2324, RFC 7168
    MisdirectedRequest  = 421,  // RFC 7540
    UnprocessableEntity = 422,  // RFC 4918
    Locked              = 423,  // RFC 4918
    FailedDependency    = 424,  // RFC 4918
    TooEarly            = 425,  // RFC 8470
    UpgradeRequired     = 426,
    PreconditionRequired = 428,  // RFC 6585
    TooManyRequests     = 429,  // RFC 6585
    RequestHeaderFieldsTooLarge = 431,  // RFC 6585
    UnavailableForLegalReasons  = 451,  // RFC 7725
    
    // 5xx Server Error (RFC 7231 Section 6.6)
    InternalServerError = 500,
    NotImplemented      = 501,
    BadGateway          = 502,
    ServiceUnavailable  = 503,
    GatewayTimeout      = 504,
    HTTPVersionNotSupported = 505,
    VariantAlsoNegotiates   = 506,  // RFC 2295
    InsufficientStorage     = 507,  // RFC 4918
    LoopDetected            = 508,  // RFC 5842
    NotExtended             = 510,  // RFC 2774
    NetworkAuthenticationRequired = 511  // RFC 6585
};

inline std::string statusCodeToReason(StatusCode code) {
    switch (code) {
        // 1xx
        case StatusCode::Continue:              return "Continue";
        case StatusCode::SwitchingProtocols:    return "Switching Protocols";
        case StatusCode::Processing:            return "Processing";
        case StatusCode::EarlyHints:            return "Early Hints";
        // 2xx
        case StatusCode::OK:                    return "OK";
        case StatusCode::Created:               return "Created";
        case StatusCode::Accepted:              return "Accepted";
        case StatusCode::NonAuthoritativeInformation: return "Non-Authoritative Information";
        case StatusCode::NoContent:             return "No Content";
        case StatusCode::ResetContent:          return "Reset Content";
        case StatusCode::PartialContent:        return "Partial Content";
        case StatusCode::MultiStatus:           return "Multi-Status";
        case StatusCode::AlreadyReported:       return "Already Reported";
        case StatusCode::IMUsed:                return "IM Used";
        // 3xx
        case StatusCode::MultipleChoices:       return "Multiple Choices";
        case StatusCode::MovedPermanently:      return "Moved Permanently";
        case StatusCode::Found:                 return "Found";
        case StatusCode::SeeOther:              return "See Other";
        case StatusCode::NotModified:           return "Not Modified";
        case StatusCode::UseProxy:              return "Use Proxy";
        case StatusCode::TemporaryRedirect:     return "Temporary Redirect";
        case StatusCode::PermanentRedirect:     return "Permanent Redirect";
        // 4xx
        case StatusCode::BadRequest:            return "Bad Request";
        case StatusCode::Unauthorized:          return "Unauthorized";
        case StatusCode::PaymentRequired:       return "Payment Required";
        case StatusCode::Forbidden:             return "Forbidden";
        case StatusCode::NotFound:              return "Not Found";
        case StatusCode::MethodNotAllowed:      return "Method Not Allowed";
        case StatusCode::NotAcceptable:         return "Not Acceptable";
        case StatusCode::ProxyAuthenticationRequired: return "Proxy Authentication Required";
        case StatusCode::RequestTimeout:        return "Request Timeout";
        case StatusCode::Conflict:              return "Conflict";
        case StatusCode::Gone:                  return "Gone";
        case StatusCode::LengthRequired:        return "Length Required";
        case StatusCode::PreconditionFailed:    return "Precondition Failed";
        case StatusCode::PayloadTooLarge:       return "Payload Too Large";
        case StatusCode::URITooLong:            return "URI Too Long";
        case StatusCode::UnsupportedMediaType:  return "Unsupported Media Type";
        case StatusCode::RangeNotSatisfiable:   return "Range Not Satisfiable";
        case StatusCode::ExpectationFailed:     return "Expectation Failed";
        case StatusCode::ImATeapot:             return "I'm a teapot";
        case StatusCode::MisdirectedRequest:    return "Misdirected Request";
        case StatusCode::UnprocessableEntity:   return "Unprocessable Entity";
        case StatusCode::Locked:                return "Locked";
        case StatusCode::FailedDependency:      return "Failed Dependency";
        case StatusCode::TooEarly:              return "Too Early";
        case StatusCode::UpgradeRequired:       return "Upgrade Required";
        case StatusCode::PreconditionRequired:  return "Precondition Required";
        case StatusCode::TooManyRequests:       return "Too Many Requests";
        case StatusCode::RequestHeaderFieldsTooLarge: return "Request Header Fields Too Large";
        case StatusCode::UnavailableForLegalReasons:  return "Unavailable For Legal Reasons";
        // 5xx
        case StatusCode::InternalServerError:   return "Internal Server Error";
        case StatusCode::NotImplemented:        return "Not Implemented";
        case StatusCode::BadGateway:            return "Bad Gateway";
        case StatusCode::ServiceUnavailable:    return "Service Unavailable";
        case StatusCode::GatewayTimeout:        return "Gateway Timeout";
        case StatusCode::HTTPVersionNotSupported:     return "HTTP Version Not Supported";
        case StatusCode::VariantAlsoNegotiates:       return "Variant Also Negotiates";
        case StatusCode::InsufficientStorage:         return "Insufficient Storage";
        case StatusCode::LoopDetected:                return "Loop Detected";
        case StatusCode::NotExtended:                 return "Not Extended";
        case StatusCode::NetworkAuthenticationRequired: return "Network Authentication Required";
        default:                                return "Unknown";
    }
}

inline bool isInformational(int code) { return code >= 100 && code < 200; }
inline bool isSuccessful(int code)    { return code >= 200 && code < 300; }
inline bool isRedirection(int code)   { return code >= 300 && code < 400; }
inline bool isClientError(int code)   { return code >= 400 && code < 500; }
inline bool isServerError(int code)   { return code >= 500 && code < 600; }

// ============================================================================
// HTTP Version (RFC 7230 Section 2.6)
// ============================================================================
enum class Version {
    HTTP_1_0,
    HTTP_1_1,
    HTTP_2_0,
    HTTP_3_0
};

inline std::string versionToString(Version version) {
    switch (version) {
        case Version::HTTP_1_0: return "HTTP/1.0";
        case Version::HTTP_1_1: return "HTTP/1.1";
        case Version::HTTP_2_0: return "HTTP/2";
        case Version::HTTP_3_0: return "HTTP/3";
        default:                return "HTTP/1.1";
    }
}

inline Version stringToVersion(const std::string& str) {
    if (str == "HTTP/1.0" || str == "1.0") return Version::HTTP_1_0;
    if (str == "HTTP/1.1" || str == "1.1") return Version::HTTP_1_1;
    if (str == "HTTP/2" || str == "HTTP/2.0" || str == "2" || str == "2.0") return Version::HTTP_2_0;
    if (str == "HTTP/3" || str == "HTTP/3.0" || str == "3" || str == "3.0") return Version::HTTP_3_0;
    return Version::HTTP_1_1;  // Default
}

// ============================================================================
// Content Types (RFC 7231 Section 3.1.1.5)
// ============================================================================
struct MediaType {
    std::string type;       // e.g., "application"
    std::string subtype;    // e.g., "json"
    std::unordered_map<std::string, std::string> parameters;  // e.g., charset=utf-8
    
    std::string toString() const {
        std::string result = type + "/" + subtype;
        for (const auto& [key, value] : parameters) {
            result += "; " + key + "=" + value;
        }
        return result;
    }
    
    static MediaType parse(const std::string& str);
};

// Common MIME types
namespace mime {
    const std::string ApplicationJson = "application/json";
    const std::string ApplicationXml = "application/xml";
    const std::string ApplicationFormUrlEncoded = "application/x-www-form-urlencoded";
    const std::string ApplicationOctetStream = "application/octet-stream";
    const std::string TextPlain = "text/plain";
    const std::string TextHtml = "text/html";
    const std::string TextCss = "text/css";
    const std::string TextJavascript = "text/javascript";
    const std::string ImagePng = "image/png";
    const std::string ImageJpeg = "image/jpeg";
    const std::string ImageGif = "image/gif";
    const std::string ImageWebp = "image/webp";
    const std::string ImageSvg = "image/svg+xml";
    const std::string MultipartFormData = "multipart/form-data";
}

// ============================================================================
// HTTP Headers (Case-insensitive as per RFC 7230 Section 3.2)
// ============================================================================
class Headers {
public:
    void set(const std::string& name, const std::string& value);
    void append(const std::string& name, const std::string& value);
    void remove(const std::string& name);
    std::optional<std::string> get(const std::string& name) const;
    std::vector<std::string> getAll(const std::string& name) const;
    bool has(const std::string& name) const;
    std::vector<std::pair<std::string, std::string>> entries() const;
    void clear();
    size_t size() const;
    
private:
    // Store headers with original casing, but lookup is case-insensitive
    std::vector<std::pair<std::string, std::string>> headers_;
    
    static std::string toLower(const std::string& str);
};

// Common header names (RFC 7231, RFC 7232, RFC 7233, RFC 7234, RFC 7235)
namespace header {
    // General Headers
    const std::string CacheControl = "Cache-Control";
    const std::string Connection = "Connection";
    const std::string Date = "Date";
    const std::string Pragma = "Pragma";
    const std::string Trailer = "Trailer";
    const std::string TransferEncoding = "Transfer-Encoding";
    const std::string Upgrade = "Upgrade";
    const std::string Via = "Via";
    const std::string Warning = "Warning";
    
    // Request Headers (RFC 7231 Section 5)
    const std::string Accept = "Accept";
    const std::string AcceptCharset = "Accept-Charset";
    const std::string AcceptEncoding = "Accept-Encoding";
    const std::string AcceptLanguage = "Accept-Language";
    const std::string Authorization = "Authorization";
    const std::string Expect = "Expect";
    const std::string From = "From";
    const std::string Host = "Host";
    const std::string IfMatch = "If-Match";
    const std::string IfModifiedSince = "If-Modified-Since";
    const std::string IfNoneMatch = "If-None-Match";
    const std::string IfRange = "If-Range";
    const std::string IfUnmodifiedSince = "If-Unmodified-Since";
    const std::string MaxForwards = "Max-Forwards";
    const std::string ProxyAuthorization = "Proxy-Authorization";
    const std::string Range = "Range";
    const std::string Referer = "Referer";
    const std::string TE = "TE";
    const std::string UserAgent = "User-Agent";
    
    // Response Headers (RFC 7231 Section 7)
    const std::string AcceptRanges = "Accept-Ranges";
    const std::string Age = "Age";
    const std::string Allow = "Allow";
    const std::string ETag = "ETag";
    const std::string Location = "Location";
    const std::string ProxyAuthenticate = "Proxy-Authenticate";
    const std::string RetryAfter = "Retry-After";
    const std::string Server = "Server";
    const std::string Vary = "Vary";
    const std::string WWWAuthenticate = "WWW-Authenticate";
    
    // Entity Headers
    const std::string ContentDisposition = "Content-Disposition";
    const std::string ContentEncoding = "Content-Encoding";
    const std::string ContentLanguage = "Content-Language";
    const std::string ContentLength = "Content-Length";
    const std::string ContentLocation = "Content-Location";
    const std::string ContentMD5 = "Content-MD5";
    const std::string ContentRange = "Content-Range";
    const std::string ContentType = "Content-Type";
    const std::string Expires = "Expires";
    const std::string LastModified = "Last-Modified";
    
    // Cookie Headers (RFC 6265)
    const std::string Cookie = "Cookie";
    const std::string SetCookie = "Set-Cookie";
    
    // CORS Headers (Fetch Standard)
    const std::string Origin = "Origin";
    const std::string AccessControlAllowOrigin = "Access-Control-Allow-Origin";
    const std::string AccessControlAllowCredentials = "Access-Control-Allow-Credentials";
    const std::string AccessControlAllowHeaders = "Access-Control-Allow-Headers";
    const std::string AccessControlAllowMethods = "Access-Control-Allow-Methods";
    const std::string AccessControlExposeHeaders = "Access-Control-Expose-Headers";
    const std::string AccessControlMaxAge = "Access-Control-Max-Age";
    const std::string AccessControlRequestHeaders = "Access-Control-Request-Headers";
    const std::string AccessControlRequestMethod = "Access-Control-Request-Method";
}

// ============================================================================
// URL/URI Components (RFC 3986)
// ============================================================================
struct URL {
    std::string scheme;     // "http" or "https"
    std::string username;
    std::string password;
    std::string host;
    int port = 0;           // 0 means use default for scheme
    std::string path;
    std::string query;
    std::string fragment;
    
    std::string toString() const;
    std::string hostWithPort() const;
    static URL parse(const std::string& urlString);
    
    int effectivePort() const {
        if (port != 0) return port;
        if (scheme == "https") return 443;
        return 80;  // Default HTTP
    }
};

// ============================================================================
// HTTP Request
// ============================================================================
struct HttpRequest {
    Method method = Method::GET;
    URL url;
    Version version = Version::HTTP_1_1;
    Headers headers;
    std::string body;
    
    // Request options
    int timeoutMs = 30000;          // 30 second default timeout
    bool followRedirects = true;
    int maxRedirects = 10;
    bool validateCertificates = true;
    
    // Build request line (RFC 7230 Section 3.1.1)
    std::string requestLine() const;
    
    // Serialize the entire request
    std::string serialize() const;
    
    // Builder pattern helpers
    HttpRequest& setMethod(Method m) { method = m; return *this; }
    HttpRequest& setUrl(const std::string& u) { url = URL::parse(u); return *this; }
    HttpRequest& setHeader(const std::string& name, const std::string& value) { 
        headers.set(name, value); 
        return *this; 
    }
    HttpRequest& setBody(const std::string& b) { body = b; return *this; }
    HttpRequest& setBody(const std::string& b, const std::string& contentType) { 
        body = b;
        headers.set(header::ContentType, contentType);
        return *this; 
    }
    HttpRequest& setTimeout(int ms) { timeoutMs = ms; return *this; }
};

// ============================================================================
// HTTP Response
// ============================================================================
struct HttpResponse {
    Version version = Version::HTTP_1_1;
    int statusCode = 0;
    std::string reasonPhrase;
    Headers headers;
    std::string body;
    
    // Response metadata
    int elapsedTimeMs = 0;          // Time to receive response
    std::string finalUrl;           // URL after any redirects
    int redirectCount = 0;          // Number of redirects followed
    
    // Status helpers (RFC 7231)
    bool isInformational() const { return http::isInformational(statusCode); }
    bool isSuccessful() const { return http::isSuccessful(statusCode); }
    bool isRedirection() const { return http::isRedirection(statusCode); }
    bool isClientError() const { return http::isClientError(statusCode); }
    bool isServerError() const { return http::isServerError(statusCode); }
    bool isError() const { return isClientError() || isServerError(); }
    
    // Status line (RFC 7230 Section 3.1.2)
    std::string statusLine() const;
    
    // Convenience accessors
    std::optional<std::string> getHeader(const std::string& name) const {
        return headers.get(name);
    }
    
    std::optional<int> contentLength() const {
        auto cl = headers.get(header::ContentLength);
        if (cl) {
            try { return std::stoi(*cl); } catch (...) {}
        }
        return std::nullopt;
    }
    
    std::optional<std::string> contentType() const {
        return headers.get(header::ContentType);
    }
};

// ============================================================================
// Cookie (RFC 6265)
// ============================================================================
struct Cookie {
    std::string name;
    std::string value;
    std::string domain;
    std::string path = "/";
    std::string expires;            // Date string
    int maxAge = -1;                // -1 means session cookie
    bool secure = false;
    bool httpOnly = false;
    std::string sameSite;           // "Strict", "Lax", "None"
    
    std::string toString() const;
    static Cookie parse(const std::string& setCookieHeader);
};

// ============================================================================
// Authentication Types (RFC 7235)
// ============================================================================
enum class AuthScheme {
    Basic,
    Bearer,
    Digest,
    HOBA,
    Mutual,
    Negotiate,
    OAuth,
    SCRAM_SHA_1,
    SCRAM_SHA_256,
    Vapid
};

struct BasicAuth {
    std::string username;
    std::string password;
    
    std::string toHeader() const;
};

struct BearerAuth {
    std::string token;
    
    std::string toHeader() const { return "Bearer " + token; }
};

}  // namespace http

#endif // HTTP_TYPES_H
