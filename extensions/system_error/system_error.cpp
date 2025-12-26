#include "function_registry.h"
#include "types.h"
#include <iostream>
#include <string>
#include <stdexcept>

// ============================================================================
// system.error Extension
// Provides error handling utilities and exception type functions
// ============================================================================

void register_error_functions(FunctionRegistry& reg) {
    // error.message(exception) - Get the message from an exception
    // This is mainly for introspection of caught exceptions
    auto getMessage = [](const std::vector<Value>& args) -> Value {
        if (args.empty()) return Value(std::string(""));
        return std::visit([](auto&& arg) -> Value {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, std::string>) {
                return Value(arg);
            }
            return Value(std::string(""));
        }, args[0]);
    };
    reg.registerFunction("system.error.getMessage", getMessage);
    
    // error.type(exception) - Get the type name of an exception
    auto getType = [](const std::vector<Value>& args) -> Value {
        if (args.empty()) return Value(std::string("Exception"));
        return std::visit([](auto&& arg) -> Value {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, std::string>) {
                // Parse type from format "Type:message"
                std::string s = arg;
                auto pos = s.find(':');
                if (pos != std::string::npos) {
                    return Value(s.substr(0, pos));
                }
                return Value(std::string("Exception"));
            }
            return Value(std::string("Exception"));
        }, args[0]);
    };
    reg.registerFunction("system.error.getType", getType);
    
    // error.panic(message) - Immediately abort with error message
    auto panic = [](const std::vector<Value>& args) -> Value {
        std::string message = "panic!";
        if (!args.empty()) {
            message = std::visit([](auto&& arg) -> std::string {
                using T = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<T, std::string>) return arg;
                else if constexpr (std::is_same_v<T, int>) return std::to_string(arg);
                else if constexpr (std::is_same_v<T, double>) return std::to_string(arg);
                else if constexpr (std::is_same_v<T, bool>) return arg ? "true" : "false";
                else return "unknown error";
            }, args[0]);
        }
        std::cerr << "[PANIC] " << message << std::endl;
        std::exit(1);
        return Value(0);  // Never reached
    };
    reg.registerFunction("system.error.panic", panic);
    
    // error.assert(condition, message) - Assert a condition, panic if false
    auto assertFn = [](const std::vector<Value>& args) -> Value {
        if (args.empty()) return Value(true);
        
        bool condition = std::visit([](auto&& arg) -> bool {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, bool>) return arg;
            else if constexpr (std::is_arithmetic_v<T>) return arg != 0;
            else if constexpr (std::is_same_v<T, std::string>) return !arg.empty();
            return false;
        }, args[0]);
        
        if (!condition) {
            std::string message = "Assertion failed";
            if (args.size() > 1) {
                message = std::visit([](auto&& arg) -> std::string {
                    using T = std::decay_t<decltype(arg)>;
                    if constexpr (std::is_same_v<T, std::string>) return arg;
                    else return "Assertion failed";
                }, args[1]);
            }
            std::cerr << "[ASSERT FAILED] " << message << std::endl;
            std::exit(1);
        }
        return Value(true);
    };
    reg.registerFunction("system.error.assert", assertFn);
    
    // error.stackTrace() - Print current stack trace (stub for now)
    auto stackTrace = [](const std::vector<Value>& args) -> Value {
        std::cerr << "[Stack Trace]" << std::endl;
        std::cerr << "  (stack trace not available in current version)" << std::endl;
        return Value(std::string(""));
    };
    reg.registerFunction("system.error.stackTrace", stackTrace);
}

extern "C" __attribute__((visibility("default"))) void init_extension(FunctionRegistry& reg) {
    register_error_functions(reg);
}
