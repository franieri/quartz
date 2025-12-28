// ============================================================================
// HTTP Types Implementation
// ============================================================================

#include "http_types.h"
#include <algorithm>
#include <cctype>
#include <sstream>
#include <regex>

namespace http {

// ============================================================================
// Headers Implementation
// ============================================================================

std::string Headers::toLower(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return result;
}

void Headers::set(const std::string& name, const std::string& value) {
    std::string lowerName = toLower(name);
    
    // Remove any existing headers with this name
    headers_.erase(
        std::remove_if(headers_.begin(), headers_.end(),
            [&lowerName, this](const auto& pair) {
                return toLower(pair.first) == lowerName;
            }),
        headers_.end()
    );
    
    // Add the new header
    headers_.emplace_back(name, value);
}

void Headers::append(const std::string& name, const std::string& value) {
    headers_.emplace_back(name, value);
}

void Headers::remove(const std::string& name) {
    std::string lowerName = toLower(name);
    headers_.erase(
        std::remove_if(headers_.begin(), headers_.end(),
            [&lowerName, this](const auto& pair) {
                return toLower(pair.first) == lowerName;
            }),
        headers_.end()
    );
}

std::optional<std::string> Headers::get(const std::string& name) const {
    std::string lowerName = toLower(name);
    for (const auto& [key, value] : headers_) {
        if (toLower(key) == lowerName) {
            return value;
        }
    }
    return std::nullopt;
}

std::vector<std::string> Headers::getAll(const std::string& name) const {
    std::string lowerName = toLower(name);
    std::vector<std::string> result;
    for (const auto& [key, value] : headers_) {
        if (toLower(key) == lowerName) {
            result.push_back(value);
        }
    }
    return result;
}

bool Headers::has(const std::string& name) const {
    return get(name).has_value();
}

std::vector<std::pair<std::string, std::string>> Headers::entries() const {
    return headers_;
}

void Headers::clear() {
    headers_.clear();
}

size_t Headers::size() const {
    return headers_.size();
}

// ============================================================================
// MediaType Implementation
// ============================================================================

MediaType MediaType::parse(const std::string& str) {
    MediaType result;
    
    // Split by semicolon to get type/subtype and parameters
    size_t semicolon = str.find(';');
    std::string typeStr = (semicolon != std::string::npos) ? str.substr(0, semicolon) : str;
    
    // Trim whitespace
    typeStr.erase(0, typeStr.find_first_not_of(" \t"));
    typeStr.erase(typeStr.find_last_not_of(" \t") + 1);
    
    // Split type/subtype
    size_t slash = typeStr.find('/');
    if (slash != std::string::npos) {
        result.type = typeStr.substr(0, slash);
        result.subtype = typeStr.substr(slash + 1);
    } else {
        result.type = typeStr;
    }
    
    // Parse parameters
    if (semicolon != std::string::npos) {
        std::string params = str.substr(semicolon + 1);
        std::istringstream iss(params);
        std::string param;
        while (std::getline(iss, param, ';')) {
            // Trim
            param.erase(0, param.find_first_not_of(" \t"));
            param.erase(param.find_last_not_of(" \t") + 1);
            
            size_t eq = param.find('=');
            if (eq != std::string::npos) {
                std::string key = param.substr(0, eq);
                std::string value = param.substr(eq + 1);
                // Remove quotes if present
                if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
                    value = value.substr(1, value.size() - 2);
                }
                result.parameters[key] = value;
            }
        }
    }
    
    return result;
}

// ============================================================================
// URL Implementation
// ============================================================================

URL URL::parse(const std::string& urlString) {
    URL result;
    std::string remaining = urlString;
    
    // Parse scheme
    size_t schemeEnd = remaining.find("://");
    if (schemeEnd != std::string::npos) {
        result.scheme = remaining.substr(0, schemeEnd);
        std::transform(result.scheme.begin(), result.scheme.end(), 
                       result.scheme.begin(), ::tolower);
        remaining = remaining.substr(schemeEnd + 3);
    } else {
        result.scheme = "http";  // Default
    }
    
    // Parse fragment
    size_t fragPos = remaining.find('#');
    if (fragPos != std::string::npos) {
        result.fragment = remaining.substr(fragPos + 1);
        remaining = remaining.substr(0, fragPos);
    }
    
    // Parse query
    size_t queryPos = remaining.find('?');
    if (queryPos != std::string::npos) {
        result.query = remaining.substr(queryPos + 1);
        remaining = remaining.substr(0, queryPos);
    }
    
    // Parse path
    size_t pathPos = remaining.find('/');
    if (pathPos != std::string::npos) {
        result.path = remaining.substr(pathPos);
        remaining = remaining.substr(0, pathPos);
    } else {
        result.path = "/";
    }
    
    // Parse userinfo
    size_t atPos = remaining.find('@');
    if (atPos != std::string::npos) {
        std::string userinfo = remaining.substr(0, atPos);
        remaining = remaining.substr(atPos + 1);
        
        size_t colonPos = userinfo.find(':');
        if (colonPos != std::string::npos) {
            result.username = userinfo.substr(0, colonPos);
            result.password = userinfo.substr(colonPos + 1);
        } else {
            result.username = userinfo;
        }
    }
    
    // Parse host and port
    size_t portPos = remaining.rfind(':');
    // Check if it's an IPv6 address
    if (remaining.find('[') != std::string::npos) {
        size_t bracketEnd = remaining.find(']');
        if (bracketEnd != std::string::npos && portPos > bracketEnd) {
            result.host = remaining.substr(0, portPos);
            try {
                result.port = std::stoi(remaining.substr(portPos + 1));
            } catch (...) {
                result.port = 0;
            }
        } else {
            result.host = remaining;
        }
    } else if (portPos != std::string::npos) {
        result.host = remaining.substr(0, portPos);
        try {
            result.port = std::stoi(remaining.substr(portPos + 1));
        } catch (...) {
            result.port = 0;
        }
    } else {
        result.host = remaining;
    }
    
    return result;
}

std::string URL::toString() const {
    std::ostringstream oss;
    
    if (!scheme.empty()) {
        oss << scheme << "://";
    }
    
    if (!username.empty()) {
        oss << username;
        if (!password.empty()) {
            oss << ":" << password;
        }
        oss << "@";
    }
    
    oss << host;
    
    if (port != 0) {
        bool defaultPort = (scheme == "http" && port == 80) ||
                          (scheme == "https" && port == 443);
        if (!defaultPort) {
            oss << ":" << port;
        }
    }
    
    oss << path;
    
    if (!query.empty()) {
        oss << "?" << query;
    }
    
    if (!fragment.empty()) {
        oss << "#" << fragment;
    }
    
    return oss.str();
}

std::string URL::hostWithPort() const {
    std::ostringstream oss;
    oss << host;
    if (port != 0) {
        oss << ":" << port;
    }
    return oss.str();
}

// ============================================================================
// HttpRequest Implementation
// ============================================================================

std::string HttpRequest::requestLine() const {
    std::ostringstream oss;
    oss << methodToString(method) << " ";
    
    // Use path + query for request line
    oss << url.path;
    if (!url.query.empty()) {
        oss << "?" << url.query;
    }
    
    oss << " " << versionToString(version);
    return oss.str();
}

std::string HttpRequest::serialize() const {
    std::ostringstream oss;
    
    // Request line
    oss << requestLine() << "\r\n";
    
    // Ensure Host header is present
    Headers hdrs = headers;  // Copy
    if (!hdrs.has(header::Host)) {
        hdrs.set(header::Host, url.hostWithPort());
    }
    
    // Content-Length if body present
    if (!body.empty() && !hdrs.has(header::ContentLength)) {
        hdrs.set(header::ContentLength, std::to_string(body.size()));
    }
    
    // Headers
    for (const auto& [name, value] : hdrs.entries()) {
        oss << name << ": " << value << "\r\n";
    }
    
    // Empty line
    oss << "\r\n";
    
    // Body
    if (!body.empty()) {
        oss << body;
    }
    
    return oss.str();
}

// ============================================================================
// HttpResponse Implementation
// ============================================================================

std::string HttpResponse::statusLine() const {
    std::ostringstream oss;
    oss << versionToString(version) << " " << statusCode << " " << reasonPhrase;
    return oss.str();
}

// ============================================================================
// Cookie Implementation
// ============================================================================

std::string Cookie::toString() const {
    std::ostringstream oss;
    oss << name << "=" << value;
    
    if (!domain.empty()) {
        oss << "; Domain=" << domain;
    }
    if (!path.empty() && path != "/") {
        oss << "; Path=" << path;
    }
    if (!expires.empty()) {
        oss << "; Expires=" << expires;
    }
    if (maxAge >= 0) {
        oss << "; Max-Age=" << maxAge;
    }
    if (secure) {
        oss << "; Secure";
    }
    if (httpOnly) {
        oss << "; HttpOnly";
    }
    if (!sameSite.empty()) {
        oss << "; SameSite=" << sameSite;
    }
    
    return oss.str();
}

Cookie Cookie::parse(const std::string& setCookieHeader) {
    Cookie cookie;
    
    std::istringstream iss(setCookieHeader);
    std::string part;
    bool first = true;
    
    while (std::getline(iss, part, ';')) {
        // Trim
        part.erase(0, part.find_first_not_of(" \t"));
        part.erase(part.find_last_not_of(" \t") + 1);
        
        size_t eq = part.find('=');
        
        if (first) {
            // First part is name=value
            if (eq != std::string::npos) {
                cookie.name = part.substr(0, eq);
                cookie.value = part.substr(eq + 1);
            }
            first = false;
            continue;
        }
        
        // Attributes
        std::string attrName = (eq != std::string::npos) ? part.substr(0, eq) : part;
        std::string attrValue = (eq != std::string::npos) ? part.substr(eq + 1) : "";
        
        // Case-insensitive comparison
        std::string lowerAttr = attrName;
        std::transform(lowerAttr.begin(), lowerAttr.end(), lowerAttr.begin(), ::tolower);
        
        if (lowerAttr == "domain") {
            cookie.domain = attrValue;
        } else if (lowerAttr == "path") {
            cookie.path = attrValue;
        } else if (lowerAttr == "expires") {
            cookie.expires = attrValue;
        } else if (lowerAttr == "max-age") {
            try { cookie.maxAge = std::stoi(attrValue); } catch (...) {}
        } else if (lowerAttr == "secure") {
            cookie.secure = true;
        } else if (lowerAttr == "httponly") {
            cookie.httpOnly = true;
        } else if (lowerAttr == "samesite") {
            cookie.sameSite = attrValue;
        }
    }
    
    return cookie;
}

// ============================================================================
// BasicAuth Implementation
// ============================================================================

// Base64 encoding for Basic Auth
static const char* base64Chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static std::string base64Encode(const std::string& input) {
    std::string result;
    int val = 0, valb = -6;
    for (unsigned char c : input) {
        val = (val << 8) + c;
        valb += 8;
        while (valb >= 0) {
            result.push_back(base64Chars[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }
    if (valb > -6) {
        result.push_back(base64Chars[((val << 8) >> (valb + 8)) & 0x3F]);
    }
    while (result.size() % 4) {
        result.push_back('=');
    }
    return result;
}

std::string BasicAuth::toHeader() const {
    return "Basic " + base64Encode(username + ":" + password);
}

}  // namespace http
