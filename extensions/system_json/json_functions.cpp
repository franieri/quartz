#include "function_registry.h"
#include "runtime.h"
#include <nlohmann/json.hpp>
#include <stdexcept>

using json = nlohmann::json;

static inline Runtime* rt() {
    return global_runtime_ptr;
}

// Convert Quartz Value to nlohmann::json
static json valueToJson(const Value& val, Runtime* runtime) {
    return std::visit([runtime](auto&& arg) -> json {
        using T = std::decay_t<decltype(arg)>;
        
        if constexpr (std::is_same_v<T, int>) {
            return json(arg);
        } else if constexpr (std::is_same_v<T, double>) {
            return json(arg);
        } else if constexpr (std::is_same_v<T, std::string>) {
            return json(arg);
        } else if constexpr (std::is_same_v<T, bool>) {
            return json(arg);
        } else if constexpr (std::is_same_v<T, ArrayRef>) {
            if (!runtime) return json::array();
            auto* vec = runtime->getArray(arg);
            if (!vec) return json::array();
            json arr = json::array();
            for (const auto& v : *vec) {
                arr.push_back(valueToJson(v, runtime));
            }
            return arr;
        } else if constexpr (std::is_same_v<T, DictRef>) {
            if (!runtime) return json::object();
            auto* dict = runtime->getDict(arg);
            if (!dict) return json::object();
            json obj = json::object();
            for (const auto& [key, value] : *dict) {
                obj[key] = valueToJson(value, runtime);
            }
            return obj;
        } else {
            return json(nullptr);
        }
    }, val);
}

// Convert nlohmann::json to Quartz Value
static Value jsonToValue(const json& j, Runtime* runtime) {
    if (j.is_null()) {
        return Value(0);  // or could use an optional type
    } else if (j.is_boolean()) {
        return Value(j.get<bool>());
    } else if (j.is_number_integer()) {
        return Value(j.get<int>());
    } else if (j.is_number_float()) {
        return Value(j.get<double>());
    } else if (j.is_string()) {
        return Value(j.get<std::string>());
    } else if (j.is_array()) {
        if (!runtime) return Value(0);
        std::vector<Value> arr;
        arr.reserve(j.size());
        for (const auto& item : j) {
            arr.push_back(jsonToValue(item, runtime));
        }
        return runtime->makeArray(std::move(arr));
    } else if (j.is_object()) {
        if (!runtime) return Value(0);
        std::unordered_map<std::string, Value> dict;
        for (auto it = j.begin(); it != j.end(); ++it) {
            dict[it.key()] = jsonToValue(it.value(), runtime);
        }
        return runtime->makeDict(std::move(dict));
    }
    return Value(0);
}

void register_json_functions(FunctionRegistry& reg) {
    // system.json.parse(str) -> value
    // Parse JSON string and return corresponding Quartz value
    reg.registerFunction("system.json.parse", [](const std::vector<Value>& args) -> Value {
        if (!rt() || args.empty()) return Value(0);
        if (!std::holds_alternative<std::string>(args[0])) {
            return Value(0);
        }
        
        const std::string& jsonStr = std::get<std::string>(args[0]);
        try {
            json j = json::parse(jsonStr);
            return jsonToValue(j, rt());
        } catch (const json::parse_error& e) {
            // Return error indicator (could be improved with proper error handling)
            return Value(0);
        } catch (const std::exception& e) {
            return Value(0);
        }
    });
    
    // system.json.stringify(value, indent?) -> string
    // Convert Quartz value to JSON string
    reg.registerFunction("system.json.stringify", [](const std::vector<Value>& args) -> Value {
        if (!rt() || args.empty()) return Value(std::string("null"));
        
        int indent = -1;  // -1 means compact, >=0 means pretty-print with that many spaces
        if (args.size() >= 2 && std::holds_alternative<int>(args[1])) {
            indent = std::get<int>(args[1]);
        }
        
        try {
            json j = valueToJson(args[0], rt());
            std::string result = indent >= 0 ? j.dump(indent) : j.dump();
            return Value(result);
        } catch (const std::exception& e) {
            return Value(std::string("null"));
        }
    });
    
    // system.json.isValid(str) -> bool
    // Check if a string is valid JSON
    reg.registerFunction("system.json.isValid", [](const std::vector<Value>& args) -> Value {
        if (args.empty()) return Value(false);
        if (!std::holds_alternative<std::string>(args[0])) {
            return Value(false);
        }
        
        const std::string& jsonStr = std::get<std::string>(args[0]);
        try {
            [[maybe_unused]] auto parsed = json::parse(jsonStr);
            return Value(true);
        } catch (const json::parse_error&) {
            return Value(false);
        } catch (const std::exception&) {
            return Value(false);
        }
    });
    
    // system.json.prettify(str, indent?) -> string
    // Pretty-print a JSON string
    reg.registerFunction("system.json.prettify", [](const std::vector<Value>& args) -> Value {
        if (args.empty()) return Value(std::string(""));
        if (!std::holds_alternative<std::string>(args[0])) {
            return Value(std::string(""));
        }
        
        int indent = 2;  // Default indent of 2 spaces
        if (args.size() >= 2 && std::holds_alternative<int>(args[1])) {
            indent = std::get<int>(args[1]);
            if (indent < 0) indent = 2;
        }
        
        const std::string& jsonStr = std::get<std::string>(args[0]);
        try {
            json j = json::parse(jsonStr);
            return Value(j.dump(indent));
        } catch (const json::parse_error&) {
            return Value(jsonStr);  // Return original if invalid
        } catch (const std::exception&) {
            return Value(jsonStr);
        }
    });
    
    // system.json.minify(str) -> string
    // Minify a JSON string (remove whitespace)
    reg.registerFunction("system.json.minify", [](const std::vector<Value>& args) -> Value {
        if (args.empty()) return Value(std::string(""));
        if (!std::holds_alternative<std::string>(args[0])) {
            return Value(std::string(""));
        }
        
        const std::string& jsonStr = std::get<std::string>(args[0]);
        try {
            json j = json::parse(jsonStr);
            return Value(j.dump());
        } catch (const json::parse_error&) {
            return Value(jsonStr);  // Return original if invalid
        } catch (const std::exception&) {
            return Value(jsonStr);
        }
    });
    
    // system.json.type(str) -> string
    // Get the type of a JSON value ("null", "boolean", "number", "string", "array", "object", "invalid")
    reg.registerFunction("system.json.type", [](const std::vector<Value>& args) -> Value {
        if (args.empty()) return Value(std::string("invalid"));
        if (!std::holds_alternative<std::string>(args[0])) {
            return Value(std::string("invalid"));
        }
        
        const std::string& jsonStr = std::get<std::string>(args[0]);
        try {
            json j = json::parse(jsonStr);
            if (j.is_null()) return Value(std::string("null"));
            if (j.is_boolean()) return Value(std::string("boolean"));
            if (j.is_number()) return Value(std::string("number"));
            if (j.is_string()) return Value(std::string("string"));
            if (j.is_array()) return Value(std::string("array"));
            if (j.is_object()) return Value(std::string("object"));
            return Value(std::string("unknown"));
        } catch (const json::parse_error&) {
            return Value(std::string("invalid"));
        } catch (const std::exception&) {
            return Value(std::string("invalid"));
        }
    });
    
    // system.json.merge(obj1, obj2) -> obj
    // Merge two JSON objects (obj2 overwrites obj1 for duplicate keys)
    reg.registerFunction("system.json.merge", [](const std::vector<Value>& args) -> Value {
        if (!rt() || args.size() < 2) return Value(0);
        
        try {
            json j1 = valueToJson(args[0], rt());
            json j2 = valueToJson(args[1], rt());
            
            if (!j1.is_object() || !j2.is_object()) {
                return Value(0);
            }
            
            // Merge j2 into j1
            j1.update(j2);
            return jsonToValue(j1, rt());
        } catch (const std::exception&) {
            return Value(0);
        }
    });
    
    // system.json.get(obj, path) -> value
    // Get value at JSON pointer path (e.g., "/foo/bar")
    reg.registerFunction("system.json.get", [](const std::vector<Value>& args) -> Value {
        if (!rt() || args.size() < 2) return Value(0);
        
        try {
            json j = valueToJson(args[0], rt());
            
            if (!std::holds_alternative<std::string>(args[1])) {
                return Value(0);
            }
            
            const std::string& path = std::get<std::string>(args[1]);
            json::json_pointer ptr(path);
            
            if (j.contains(ptr)) {
                return jsonToValue(j[ptr], rt());
            }
            return Value(0);
        } catch (const std::exception&) {
            return Value(0);
        }
    });
    
    // system.json.set(obj, path, value) -> obj
    // Set value at JSON pointer path
    reg.registerFunction("system.json.set", [](const std::vector<Value>& args) -> Value {
        if (!rt() || args.size() < 3) return Value(0);
        
        try {
            json j = valueToJson(args[0], rt());
            
            if (!std::holds_alternative<std::string>(args[1])) {
                return Value(0);
            }
            
            const std::string& path = std::get<std::string>(args[1]);
            json::json_pointer ptr(path);
            
            json newVal = valueToJson(args[2], rt());
            j[ptr] = newVal;
            
            return jsonToValue(j, rt());
        } catch (const std::exception&) {
            return Value(0);
        }
    });
}
