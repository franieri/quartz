#include "function_registry.h"
#include "runtime.h"
#include "types.h"
#include <algorithm>
#include <cctype>

static inline Runtime* rt() {
    return global_runtime_ptr;
}

void register_string_functions(FunctionRegistry& reg) {
    // system.string.concat(str1, str2, ...) -> string - concatenates all arguments
    reg.registerFunction("system.string.concat", [](const std::vector<Value>& args) -> Value {
        std::string result;
        for (const auto& arg : args) {
            if (std::holds_alternative<std::string>(arg)) {
                result += std::get<std::string>(arg);
            } else if (std::holds_alternative<int>(arg)) {
                result += std::to_string(std::get<int>(arg));
            } else if (std::holds_alternative<double>(arg)) {
                result += std::to_string(std::get<double>(arg));
            } else if (std::holds_alternative<bool>(arg)) {
                result += std::get<bool>(arg) ? "true" : "false";
            }
        }
        return Value(result);
    });
    
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
    
    // system.string.split(str, delimiter) -> array of strings
    auto split_func = [](const std::vector<Value>& args) -> Value {
        if (!rt()) return Value("");
        if (args.size() < 2) return rt()->makeArray({});
        if (!std::holds_alternative<std::string>(args[0]) || 
            !std::holds_alternative<std::string>(args[1])) {
            return rt()->makeArray({});
        }
        std::string str = std::get<std::string>(args[0]);
        std::string delim = std::get<std::string>(args[1]);
        std::vector<Value> result;
        
        if (delim.empty()) {
            // Split into individual characters
            for (char c : str) {
                result.push_back(Value(std::string(1, c)));
            }
        } else {
            size_t pos = 0;
            size_t prev = 0;
            while ((pos = str.find(delim, prev)) != std::string::npos) {
                result.push_back(Value(str.substr(prev, pos - prev)));
                prev = pos + delim.length();
            }
            result.push_back(Value(str.substr(prev)));
        }
        return rt()->makeArray(std::move(result));
    };
    reg.registerFunction("system.string.split", split_func);
    
    // system.string.indexOf(str, substr) -> int (-1 if not found)
    auto indexOf_func = [](const std::vector<Value>& args) -> Value {
        if (args.size() < 2) return Value(-1);
        if (!std::holds_alternative<std::string>(args[0]) || 
            !std::holds_alternative<std::string>(args[1])) {
            return Value(-1);
        }
        std::string str = std::get<std::string>(args[0]);
        std::string substr = std::get<std::string>(args[1]);
        size_t pos = str.find(substr);
        if (pos == std::string::npos) {
            return Value(-1);
        }
        return Value(static_cast<int>(pos));
    };
    reg.registerFunction("system.string.indexOf", indexOf_func);
    
    // system.string.chr(code) -> string - returns character for ASCII code
    reg.registerFunction("system.string.chr", [](const std::vector<Value>& args) -> Value {
        if (args.empty() || !std::holds_alternative<int>(args[0])) {
            return Value("");
        }
        int code = std::get<int>(args[0]);
        if (code < 0 || code > 255) return Value("");
        return Value(std::string(1, static_cast<char>(code)));
    });
    
    // system.string.CRLF - constant for \r\n
    reg.registerFunction("system.string.CRLF", [](const std::vector<Value>& args) -> Value {
        return Value(std::string("\r\n"));
    });
    
    // system.string.LF - constant for \n
    reg.registerFunction("system.string.LF", [](const std::vector<Value>& args) -> Value {
        return Value(std::string("\n"));
    });
    
    // system.string.CR - constant for \r
    reg.registerFunction("system.string.CR", [](const std::vector<Value>& args) -> Value {
        return Value(std::string("\r"));
    });
}
