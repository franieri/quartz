// ============================================================================
// system.net.http Extension - Main Entry Point
// ============================================================================

#include "function_registry.h"

// Forward declarations for all function registration modules
void register_http_request_functions(FunctionRegistry& reg);
void register_http_response_functions(FunctionRegistry& reg);
void register_http_status_functions(FunctionRegistry& reg);
void register_http_url_functions(FunctionRegistry& reg);
void register_http_headers_functions(FunctionRegistry& reg);
void register_http_cookie_functions(FunctionRegistry& reg);
void register_http_method_functions(FunctionRegistry& reg);
void register_http_mime_functions(FunctionRegistry& reg);
void register_http_auth_functions(FunctionRegistry& reg);
void register_http_header_constants(FunctionRegistry& reg);

extern "C" __attribute__((visibility("default"))) void init_extension(FunctionRegistry& reg) {
    // Register all HTTP-related functions
    register_http_request_functions(reg);
    register_http_response_functions(reg);
    register_http_status_functions(reg);
    register_http_url_functions(reg);
    register_http_headers_functions(reg);
    register_http_cookie_functions(reg);
    register_http_method_functions(reg);
    register_http_mime_functions(reg);
    register_http_auth_functions(reg);
    register_http_header_constants(reg);
}
