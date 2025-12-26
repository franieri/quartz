#include "function_registry.h"
#include "types.h"
#include <algorithm>
#include <cctype>

void register_string_functions(FunctionRegistry& reg) {
    auto length_func = [](const std::vector<Value>& args) -> Value {
        if (args.empty()) return Value(0);
        if (std::holds_alternative<std::string>(args[0])) {
            return Value(static_cast<int>(std::get<std::string>(args[0]).length()));
        }
        return Value(0);
    };
    reg.registerFunction("system.string.length", length_func);
    
    auto uppercase_func = [](const std::vector<Value>& args) -> Value {
        if (args.empty()) return Value("");
        if (std::holds_alternative<std::string>(args[0])) {
            std::string str = std::get<std::string>(args[0]);
            std::transform(str.begin(), str.end(), str.begin(), ::toupper);
            return Value(str);
        }
        return Value("");
    };
    reg.registerFunction("system.string.uppercase", uppercase_func);
    reg.registerFunction("system.string.toUpperCase", uppercase_func);
    
    auto lowercase_func = [](const std::vector<Value>& args) -> Value {
        if (args.empty()) return Value("");
        if (std::holds_alternative<std::string>(args[0])) {
            std::string str = std::get<std::string>(args[0]);
            std::transform(str.begin(), str.end(), str.begin(), ::tolower);
            return Value(str);
        }
        return Value("");
    };
    reg.registerFunction("system.string.lowercase", lowercase_func);
    reg.registerFunction("system.string.toLowerCase", lowercase_func);
    
    auto trim_func = [](const std::vector<Value>& args) -> Value {
        if (args.empty()) return Value("");
        if (std::holds_alternative<std::string>(args[0])) {
            std::string str = std::get<std::string>(args[0]);
            auto start = str.begin();
            while (start != str.end() && std::isspace(*start)) ++start;
            auto end = str.end();
            do { --end; } while (std::distance(start, end) > 0 && std::isspace(*end));
            return Value(std::string(start, end + 1));
        }
        return Value("");
    };
    reg.registerFunction("system.string.trim", trim_func);
    
    auto contains_func = [](const std::vector<Value>& args) -> Value {
        if (args.size() < 2) return Value(false);
        if (std::holds_alternative<std::string>(args[0]) && 
            std::holds_alternative<std::string>(args[1])) {
            std::string str = std::get<std::string>(args[0]);
            std::string substr = std::get<std::string>(args[1]);
            return Value(str.find(substr) != std::string::npos);
        }
        return Value(false);
    };
    reg.registerFunction("system.string.contains", contains_func);
    
    auto startsWith_func = [](const std::vector<Value>& args) -> Value {
        if (args.size() < 2) return Value(false);
        if (std::holds_alternative<std::string>(args[0]) && 
            std::holds_alternative<std::string>(args[1])) {
            std::string str = std::get<std::string>(args[0]);
            std::string prefix = std::get<std::string>(args[1]);
            return Value(str.substr(0, prefix.length()) == prefix);
        }
        return Value(false);
    };
    reg.registerFunction("system.string.startsWith", startsWith_func);
    
    auto endsWith_func = [](const std::vector<Value>& args) -> Value {
        if (args.size() < 2) return Value(false);
        if (std::holds_alternative<std::string>(args[0]) && 
            std::holds_alternative<std::string>(args[1])) {
            std::string str = std::get<std::string>(args[0]);
            std::string suffix = std::get<std::string>(args[1]);
            if (suffix.length() > str.length()) return Value(false);
            return Value(str.substr(str.length() - suffix.length()) == suffix);
        }
        return Value(false);
    };
    reg.registerFunction("system.string.endsWith", endsWith_func);
    
    auto substring_func = [](const std::vector<Value>& args) -> Value {
        if (args.size() < 2) return Value("");
        if (std::holds_alternative<std::string>(args[0])) {
            std::string str = std::get<std::string>(args[0]);
            int start = std::holds_alternative<int>(args[1]) ? std::get<int>(args[1]) : 0;
            if (args.size() >= 3 && std::holds_alternative<int>(args[2])) {
                int end = std::get<int>(args[2]);
                if (start >= 0 && end <= (int)str.length() && start <= end) {
                    return Value(str.substr(start, end - start));
                }
            } else {
                if (start >= 0 && start <= (int)str.length()) {
                    return Value(str.substr(start));
                }
            }
        }
        return Value("");
    };
    reg.registerFunction("system.string.substring", substring_func);
    
    auto replace_func = [](const std::vector<Value>& args) -> Value {
        if (args.size() < 3) return Value("");
        if (std::holds_alternative<std::string>(args[0]) && 
            std::holds_alternative<std::string>(args[1]) &&
            std::holds_alternative<std::string>(args[2])) {
            std::string str = std::get<std::string>(args[0]);
            std::string from = std::get<std::string>(args[1]);
            std::string to = std::get<std::string>(args[2]);
            size_t pos = 0;
            while ((pos = str.find(from, pos)) != std::string::npos) {
                str.replace(pos, from.length(), to);
                pos += to.length();
            }
            return Value(str);
        }
        return Value("");
    };
    reg.registerFunction("system.string.replace", replace_func);
}
