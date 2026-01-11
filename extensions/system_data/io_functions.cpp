#include "function_registry.h"
#include "runtime.h"
#include "dataframe.h"
#include <fstream>
#include <sstream>

using namespace qz_data;

static inline Runtime* rt() {
    return global_runtime_ptr;
}

// Helper to get DataFrame ID from Value
static inline size_t getDfId(const Value& v) {
    if (std::holds_alternative<int>(v)) {
        return static_cast<size_t>(std::get<int>(v));
    }
    return 0;
}

void register_io_functions(FunctionRegistry& reg) {
    auto& registry = DataFrameRegistry::instance();
    
    // ========================================================================
    // CSV I/O
    // ========================================================================
    
    // data.io.readCSV(filename, delimiter?, hasHeader?) -> dfId
    reg.registerFunction("system.data.io.readCSV", [&registry](const std::vector<Value>& args) -> Value {
        if (args.empty() || !std::holds_alternative<std::string>(args[0])) {
            return Value(-1);
        }
        
        std::string filename = std::get<std::string>(args[0]);
        char delimiter = ',';
        bool hasHeader = true;
        
        if (args.size() >= 2 && std::holds_alternative<std::string>(args[1])) {
            const auto& delim = std::get<std::string>(args[1]);
            if (!delim.empty()) delimiter = delim[0];
        }
        if (args.size() >= 3 && std::holds_alternative<bool>(args[2])) {
            hasHeader = std::get<bool>(args[2]);
        }
        
        // Read file content
        std::ifstream file(filename);
        if (!file.is_open()) {
            return Value(-1);
        }
        
        std::stringstream buffer;
        buffer << file.rdbuf();
        std::string content = buffer.str();
        file.close();
        
        DataFrame df = DataFrame::fromCSV(content, delimiter, hasHeader);
        return Value(static_cast<int>(registry.store(std::move(df))));
    });
    
    // data.io.writeCSV(dfId, filename, delimiter?, includeHeader?) -> bool
    reg.registerFunction("system.data.io.writeCSV", [&registry](const std::vector<Value>& args) -> Value {
        if (args.size() < 2) return Value(false);
        
        auto* df = registry.get(getDfId(args[0]));
        if (!df) return Value(false);
        
        std::string filename;
        if (std::holds_alternative<std::string>(args[1])) {
            filename = std::get<std::string>(args[1]);
        } else {
            return Value(false);
        }
        
        char delimiter = ',';
        bool includeHeader = true;
        
        if (args.size() >= 3 && std::holds_alternative<std::string>(args[2])) {
            const auto& delim = std::get<std::string>(args[2]);
            if (!delim.empty()) delimiter = delim[0];
        }
        if (args.size() >= 4 && std::holds_alternative<bool>(args[3])) {
            includeHeader = std::get<bool>(args[3]);
        }
        
        std::string csv = df->toCSV(delimiter, includeHeader);
        
        std::ofstream file(filename);
        if (!file.is_open()) {
            return Value(false);
        }
        
        file << csv;
        file.close();
        
        return Value(true);
    });
    
    // ========================================================================
    // JSON I/O
    // ========================================================================
    
    // data.df.toJSON(dfId, orient?) -> string
    // orient: "records" (default) or "columns"
    reg.registerFunction("system.data.df.toJSON", [&registry](const std::vector<Value>& args) -> Value {
        if (args.empty()) return Value(std::string("{}"));
        
        auto* df = registry.get(getDfId(args[0]));
        if (!df) return Value(std::string("{}"));
        
        bool orient_records = true;
        if (args.size() >= 2 && std::holds_alternative<std::string>(args[1])) {
            orient_records = std::get<std::string>(args[1]) != "columns";
        }
        
        return Value(df->toJSON(orient_records));
    });
    
    // data.io.writeJSON(dfId, filename, orient?) -> bool
    reg.registerFunction("system.data.io.writeJSON", [&registry](const std::vector<Value>& args) -> Value {
        if (args.size() < 2) return Value(false);
        
        auto* df = registry.get(getDfId(args[0]));
        if (!df) return Value(false);
        
        std::string filename;
        if (std::holds_alternative<std::string>(args[1])) {
            filename = std::get<std::string>(args[1]);
        } else {
            return Value(false);
        }
        
        bool orient_records = true;
        if (args.size() >= 3 && std::holds_alternative<std::string>(args[2])) {
            orient_records = std::get<std::string>(args[2]) != "columns";
        }
        
        std::string json = df->toJSON(orient_records);
        
        std::ofstream file(filename);
        if (!file.is_open()) {
            return Value(false);
        }
        
        file << json;
        file.close();
        
        return Value(true);
    });
    
    // ========================================================================
    // String conversion
    // ========================================================================
    
    // data.df.toCSV(dfId, delimiter?, includeHeader?) -> string
    reg.registerFunction("system.data.df.toCSV", [&registry](const std::vector<Value>& args) -> Value {
        if (args.empty()) return Value(std::string(""));
        
        auto* df = registry.get(getDfId(args[0]));
        if (!df) return Value(std::string(""));
        
        char delimiter = ',';
        bool includeHeader = true;
        
        if (args.size() >= 2 && std::holds_alternative<std::string>(args[1])) {
            const auto& delim = std::get<std::string>(args[1]);
            if (!delim.empty()) delimiter = delim[0];
        }
        if (args.size() >= 3 && std::holds_alternative<bool>(args[2])) {
            includeHeader = std::get<bool>(args[2]);
        }
        
        return Value(df->toCSV(delimiter, includeHeader));
    });
}
