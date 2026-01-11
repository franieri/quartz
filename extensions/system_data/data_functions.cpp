#include "function_registry.h"
#include "runtime.h"
#include "dataframe.h"
#include <sstream>
#include <cmath>

using namespace qz_data;

static inline Runtime* rt() {
    return global_runtime_ptr;
}

// Helper to get DataFrame ID from Value (stored as int)
static inline size_t getDfId(const Value& v) {
    if (std::holds_alternative<int>(v)) {
        return static_cast<size_t>(std::get<int>(v));
    }
    return 0;
}

// Helper to get Series ID from Value
static inline size_t getSeriesId(const Value& v) {
    if (std::holds_alternative<int>(v)) {
        return static_cast<size_t>(std::get<int>(v));
    }
    return 0;
}

// Helper to extract double array from Quartz array
static std::vector<double> extractDoubleArray(const Value& v) {
    std::vector<double> result;
    if (!rt() || !std::holds_alternative<ArrayRef>(v)) return result;
    
    auto* arr = rt()->getArray(std::get<ArrayRef>(v));
    if (!arr) return result;
    
    result.reserve(arr->size());
    for (const auto& elem : *arr) {
        if (std::holds_alternative<double>(elem)) {
            result.push_back(std::get<double>(elem));
        } else if (std::holds_alternative<int>(elem)) {
            result.push_back(static_cast<double>(std::get<int>(elem)));
        } else {
            result.push_back(std::nan(""));
        }
    }
    return result;
}

// Helper to extract string array from Quartz array
static std::vector<std::string> extractStringArray(const Value& v) {
    std::vector<std::string> result;
    if (!rt() || !std::holds_alternative<ArrayRef>(v)) return result;
    
    auto* arr = rt()->getArray(std::get<ArrayRef>(v));
    if (!arr) return result;
    
    result.reserve(arr->size());
    for (const auto& elem : *arr) {
        if (std::holds_alternative<std::string>(elem)) {
            result.push_back(std::get<std::string>(elem));
        } else {
            result.push_back("");
        }
    }
    return result;
}

// Helper to convert vector<double> to Quartz array
static Value makeDoubleArray(const std::vector<double>& vec) {
    if (!rt()) return Value{};
    std::vector<Value> arr;
    arr.reserve(vec.size());
    for (double v : vec) {
        arr.push_back(Value(v));
    }
    return rt()->makeArray(std::move(arr));
}

// Helper to convert vector<string> to Quartz array
static Value makeStringArray(const std::vector<std::string>& vec) {
    if (!rt()) return Value{};
    std::vector<Value> arr;
    arr.reserve(vec.size());
    for (const auto& v : vec) {
        arr.push_back(Value(v));
    }
    return rt()->makeArray(std::move(arr));
}

void register_data_functions(FunctionRegistry& reg) {
    auto& registry = DataFrameRegistry::instance();
    
    // ========================================================================
    // DataFrame Creation
    // ========================================================================
    
    // data.df.create() -> dfId
    // Creates an empty DataFrame
    reg.registerFunction("system.data.df.create", [&registry](const std::vector<Value>& args) -> Value {
        DataFrame df;
        size_t id = registry.store(std::move(df));
        return Value(static_cast<int>(id));
    });
    
    // data.df.fromCSV(csvString, delimiter?, hasHeader?) -> dfId
    reg.registerFunction("system.data.df.fromCSV", [&registry](const std::vector<Value>& args) -> Value {
        if (args.empty() || !std::holds_alternative<std::string>(args[0])) {
            return Value(-1);
        }
        
        std::string csv = std::get<std::string>(args[0]);
        char delimiter = ',';
        bool hasHeader = true;
        
        if (args.size() >= 2 && std::holds_alternative<std::string>(args[1])) {
            const auto& delim = std::get<std::string>(args[1]);
            if (!delim.empty()) delimiter = delim[0];
        }
        if (args.size() >= 3 && std::holds_alternative<bool>(args[2])) {
            hasHeader = std::get<bool>(args[2]);
        }
        
        DataFrame df = DataFrame::fromCSV(csv, delimiter, hasHeader);
        size_t id = registry.store(std::move(df));
        return Value(static_cast<int>(id));
    });
    
    // data.df.fromArrays(columnNames[], dataArrays[]) -> dfId
    // Create DataFrame from parallel arrays
    reg.registerFunction("system.data.df.fromArrays", [&registry](const std::vector<Value>& args) -> Value {
        if (args.size() < 2 || !rt()) return Value(-1);
        
        auto names = extractStringArray(args[0]);
        if (names.empty()) return Value(-1);
        
        // args[1] should be array of arrays
        if (!std::holds_alternative<ArrayRef>(args[1])) return Value(-1);
        auto* outerArr = rt()->getArray(std::get<ArrayRef>(args[1]));
        if (!outerArr || outerArr->size() != names.size()) return Value(-1);
        
        DataFrame df;
        for (size_t i = 0; i < names.size(); ++i) {
            auto data = extractDoubleArray((*outerArr)[i]);
            df.addColumn(names[i], Series(names[i], data));
        }
        
        size_t id = registry.store(std::move(df));
        return Value(static_cast<int>(id));
    });
    
    // data.df.destroy(dfId) -> bool
    reg.registerFunction("system.data.df.destroy", [&registry](const std::vector<Value>& args) -> Value {
        if (args.empty()) return Value(false);
        size_t id = getDfId(args[0]);
        registry.remove(id);
        return Value(true);
    });
    
    // ========================================================================
    // DataFrame Info
    // ========================================================================
    
    // data.df.numRows(dfId) -> int
    reg.registerFunction("system.data.df.numRows", [&registry](const std::vector<Value>& args) -> Value {
        if (args.empty()) return Value(0);
        auto* df = registry.get(getDfId(args[0]));
        return df ? Value(static_cast<int>(df->numRows())) : Value(0);
    });
    
    // data.df.numCols(dfId) -> int
    reg.registerFunction("system.data.df.numCols", [&registry](const std::vector<Value>& args) -> Value {
        if (args.empty()) return Value(0);
        auto* df = registry.get(getDfId(args[0]));
        return df ? Value(static_cast<int>(df->numCols())) : Value(0);
    });
    
    // data.df.columns(dfId) -> string[]
    reg.registerFunction("system.data.df.columns", [&registry](const std::vector<Value>& args) -> Value {
        if (args.empty() || !rt()) return Value{};
        auto* df = registry.get(getDfId(args[0]));
        if (!df) return rt()->makeArray({});
        
        return makeStringArray(df->columnNames());
    });
    
    // data.df.repr(dfId, maxRows?) -> string
    reg.registerFunction("system.data.df.repr", [&registry](const std::vector<Value>& args) -> Value {
        if (args.empty()) return Value(std::string(""));
        auto* df = registry.get(getDfId(args[0]));
        if (!df) return Value(std::string("DataFrame not found"));
        
        size_t maxRows = 10;
        if (args.size() >= 2 && std::holds_alternative<int>(args[1])) {
            maxRows = std::get<int>(args[1]);
        }
        
        return Value(df->repr(maxRows));
    });
    
    // data.df.describe(dfId) -> dfId (new df with stats)
    reg.registerFunction("system.data.df.describe", [&registry](const std::vector<Value>& args) -> Value {
        if (args.empty()) return Value(-1);
        auto* df = registry.get(getDfId(args[0]));
        if (!df) return Value(-1);
        
        DataFrame desc = df->describe();
        return Value(static_cast<int>(registry.store(std::move(desc))));
    });
    
    // data.df.memory(dfId) -> int (bytes)
    reg.registerFunction("system.data.df.memory", [&registry](const std::vector<Value>& args) -> Value {
        if (args.empty()) return Value(0);
        auto* df = registry.get(getDfId(args[0]));
        return df ? Value(static_cast<int>(df->memoryUsage())) : Value(0);
    });
    
    // ========================================================================
    // Column Operations
    // ========================================================================
    
    // data.df.addColumn(dfId, name, dataArray) -> bool
    reg.registerFunction("system.data.df.addColumn", [&registry](const std::vector<Value>& args) -> Value {
        if (args.size() < 3) return Value(false);
        
        auto* df = registry.get(getDfId(args[0]));
        if (!df) return Value(false);
        
        std::string name;
        if (std::holds_alternative<std::string>(args[1])) {
            name = std::get<std::string>(args[1]);
        } else {
            return Value(false);
        }
        
        auto data = extractDoubleArray(args[2]);
        try {
            df->addColumn(name, Series(name, data));
            return Value(true);
        } catch (...) {
            return Value(false);
        }
    });
    
    // data.df.removeColumn(dfId, name) -> bool
    reg.registerFunction("system.data.df.removeColumn", [&registry](const std::vector<Value>& args) -> Value {
        if (args.size() < 2) return Value(false);
        
        auto* df = registry.get(getDfId(args[0]));
        if (!df) return Value(false);
        
        std::string name;
        if (std::holds_alternative<std::string>(args[1])) {
            name = std::get<std::string>(args[1]);
        } else {
            return Value(false);
        }
        
        df->removeColumn(name);
        return Value(true);
    });
    
    // data.df.renameColumn(dfId, oldName, newName) -> bool
    reg.registerFunction("system.data.df.renameColumn", [&registry](const std::vector<Value>& args) -> Value {
        if (args.size() < 3) return Value(false);
        
        auto* df = registry.get(getDfId(args[0]));
        if (!df) return Value(false);
        
        std::string oldName, newName;
        if (std::holds_alternative<std::string>(args[1]) && std::holds_alternative<std::string>(args[2])) {
            oldName = std::get<std::string>(args[1]);
            newName = std::get<std::string>(args[2]);
        } else {
            return Value(false);
        }
        
        df->renameColumn(oldName, newName);
        return Value(true);
    });
    
    // data.df.select(dfId, columnNames[]) -> dfId
    reg.registerFunction("system.data.df.select", [&registry](const std::vector<Value>& args) -> Value {
        if (args.size() < 2) return Value(-1);
        
        auto* df = registry.get(getDfId(args[0]));
        if (!df) return Value(-1);
        
        auto names = extractStringArray(args[1]);
        DataFrame result = df->select(names);
        return Value(static_cast<int>(registry.store(std::move(result))));
    });
    
    // data.df.drop(dfId, columnNames[]) -> dfId
    reg.registerFunction("system.data.df.drop", [&registry](const std::vector<Value>& args) -> Value {
        if (args.size() < 2) return Value(-1);
        
        auto* df = registry.get(getDfId(args[0]));
        if (!df) return Value(-1);
        
        auto names = extractStringArray(args[1]);
        DataFrame result = df->drop(names);
        return Value(static_cast<int>(registry.store(std::move(result))));
    });
    
    // data.df.getColumn(dfId, name) -> array
    reg.registerFunction("system.data.df.getColumn", [&registry](const std::vector<Value>& args) -> Value {
        if (args.size() < 2 || !rt()) return Value{};
        
        auto* df = registry.get(getDfId(args[0]));
        if (!df) return rt()->makeArray({});
        
        std::string name;
        if (std::holds_alternative<std::string>(args[1])) {
            name = std::get<std::string>(args[1]);
        } else {
            return rt()->makeArray({});
        }
        
        if (!df->hasColumn(name)) return rt()->makeArray({});
        
        const Series& col = (*df)[name];
        return makeDoubleArray(col.toVector());
    });
    
    // ========================================================================
    // Row Operations
    // ========================================================================
    
    // data.df.head(dfId, n?) -> dfId
    reg.registerFunction("system.data.df.head", [&registry](const std::vector<Value>& args) -> Value {
        if (args.empty()) return Value(-1);
        
        auto* df = registry.get(getDfId(args[0]));
        if (!df) return Value(-1);
        
        size_t n = 5;
        if (args.size() >= 2 && std::holds_alternative<int>(args[1])) {
            n = std::get<int>(args[1]);
        }
        
        DataFrame result = df->head(n);
        return Value(static_cast<int>(registry.store(std::move(result))));
    });
    
    // data.df.tail(dfId, n?) -> dfId
    reg.registerFunction("system.data.df.tail", [&registry](const std::vector<Value>& args) -> Value {
        if (args.empty()) return Value(-1);
        
        auto* df = registry.get(getDfId(args[0]));
        if (!df) return Value(-1);
        
        size_t n = 5;
        if (args.size() >= 2 && std::holds_alternative<int>(args[1])) {
            n = std::get<int>(args[1]);
        }
        
        DataFrame result = df->tail(n);
        return Value(static_cast<int>(registry.store(std::move(result))));
    });
    
    // data.df.slice(dfId, start, end) -> dfId
    reg.registerFunction("system.data.df.slice", [&registry](const std::vector<Value>& args) -> Value {
        if (args.size() < 3) return Value(-1);
        
        auto* df = registry.get(getDfId(args[0]));
        if (!df) return Value(-1);
        
        size_t start = 0, end = 0;
        if (std::holds_alternative<int>(args[1])) start = std::get<int>(args[1]);
        if (std::holds_alternative<int>(args[2])) end = std::get<int>(args[2]);
        
        DataFrame result = df->slice(start, end);
        return Value(static_cast<int>(registry.store(std::move(result))));
    });
    
    // data.df.query(dfId, expr) -> dfId
    // Simple query: "column > value"
    reg.registerFunction("system.data.df.query", [&registry](const std::vector<Value>& args) -> Value {
        if (args.size() < 2) return Value(-1);
        
        auto* df = registry.get(getDfId(args[0]));
        if (!df) return Value(-1);
        
        std::string expr;
        if (std::holds_alternative<std::string>(args[1])) {
            expr = std::get<std::string>(args[1]);
        } else {
            return Value(-1);
        }
        
        DataFrame result = df->query(expr);
        return Value(static_cast<int>(registry.store(std::move(result))));
    });
    
    // ========================================================================
    // Sorting
    // ========================================================================
    
    // data.df.sortBy(dfId, column, ascending?) -> dfId
    reg.registerFunction("system.data.df.sortBy", [&registry](const std::vector<Value>& args) -> Value {
        if (args.size() < 2) return Value(-1);
        
        auto* df = registry.get(getDfId(args[0]));
        if (!df) return Value(-1);
        
        std::string column;
        if (std::holds_alternative<std::string>(args[1])) {
            column = std::get<std::string>(args[1]);
        } else {
            return Value(-1);
        }
        
        bool ascending = true;
        if (args.size() >= 3 && std::holds_alternative<bool>(args[2])) {
            ascending = std::get<bool>(args[2]);
        }
        
        DataFrame result = df->sortBy(column, ascending);
        return Value(static_cast<int>(registry.store(std::move(result))));
    });
    
    // ========================================================================
    // Aggregation
    // ========================================================================
    
    // data.df.sum(dfId) -> dict
    reg.registerFunction("system.data.df.sum", [&registry](const std::vector<Value>& args) -> Value {
        if (args.empty() || !rt()) return Value{};
        
        auto* df = registry.get(getDfId(args[0]));
        if (!df) return rt()->makeDict({});
        
        auto result = df->sum();
        std::unordered_map<std::string, Value> dict;
        for (const auto& [k, v] : result) {
            dict[k] = Value(v);
        }
        return rt()->makeDict(std::move(dict));
    });
    
    // data.df.mean(dfId) -> dict
    reg.registerFunction("system.data.df.mean", [&registry](const std::vector<Value>& args) -> Value {
        if (args.empty() || !rt()) return Value{};
        
        auto* df = registry.get(getDfId(args[0]));
        if (!df) return rt()->makeDict({});
        
        auto result = df->mean();
        std::unordered_map<std::string, Value> dict;
        for (const auto& [k, v] : result) {
            dict[k] = Value(v);
        }
        return rt()->makeDict(std::move(dict));
    });
    
    // data.df.min(dfId) -> dict
    reg.registerFunction("system.data.df.min", [&registry](const std::vector<Value>& args) -> Value {
        if (args.empty() || !rt()) return Value{};
        
        auto* df = registry.get(getDfId(args[0]));
        if (!df) return rt()->makeDict({});
        
        auto result = df->min();
        std::unordered_map<std::string, Value> dict;
        for (const auto& [k, v] : result) {
            dict[k] = Value(v);
        }
        return rt()->makeDict(std::move(dict));
    });
    
    // data.df.max(dfId) -> dict
    reg.registerFunction("system.data.df.max", [&registry](const std::vector<Value>& args) -> Value {
        if (args.empty() || !rt()) return Value{};
        
        auto* df = registry.get(getDfId(args[0]));
        if (!df) return rt()->makeDict({});
        
        auto result = df->max();
        std::unordered_map<std::string, Value> dict;
        for (const auto& [k, v] : result) {
            dict[k] = Value(v);
        }
        return rt()->makeDict(std::move(dict));
    });
    
    // data.df.std(dfId) -> dict
    reg.registerFunction("system.data.df.std", [&registry](const std::vector<Value>& args) -> Value {
        if (args.empty() || !rt()) return Value{};
        
        auto* df = registry.get(getDfId(args[0]));
        if (!df) return rt()->makeDict({});
        
        auto result = df->std();
        std::unordered_map<std::string, Value> dict;
        for (const auto& [k, v] : result) {
            dict[k] = Value(v);
        }
        return rt()->makeDict(std::move(dict));
    });
    
    // ========================================================================
    // GroupBy Operations
    // ========================================================================
    
    // data.df.groupby.sum(dfId, groupColumn) -> dfId
    reg.registerFunction("system.data.df.groupby.sum", [&registry](const std::vector<Value>& args) -> Value {
        if (args.size() < 2) return Value(-1);
        
        auto* df = registry.get(getDfId(args[0]));
        if (!df) return Value(-1);
        
        std::string column;
        if (std::holds_alternative<std::string>(args[1])) {
            column = std::get<std::string>(args[1]);
        } else {
            return Value(-1);
        }
        
        DataFrame result = df->groupby(column).sum();
        return Value(static_cast<int>(registry.store(std::move(result))));
    });
    
    // data.df.groupby.mean(dfId, groupColumn) -> dfId
    reg.registerFunction("system.data.df.groupby.mean", [&registry](const std::vector<Value>& args) -> Value {
        if (args.size() < 2) return Value(-1);
        
        auto* df = registry.get(getDfId(args[0]));
        if (!df) return Value(-1);
        
        std::string column;
        if (std::holds_alternative<std::string>(args[1])) {
            column = std::get<std::string>(args[1]);
        } else {
            return Value(-1);
        }
        
        DataFrame result = df->groupby(column).mean();
        return Value(static_cast<int>(registry.store(std::move(result))));
    });
    
    // data.df.groupby.count(dfId, groupColumn) -> dfId
    reg.registerFunction("system.data.df.groupby.count", [&registry](const std::vector<Value>& args) -> Value {
        if (args.size() < 2) return Value(-1);
        
        auto* df = registry.get(getDfId(args[0]));
        if (!df) return Value(-1);
        
        std::string column;
        if (std::holds_alternative<std::string>(args[1])) {
            column = std::get<std::string>(args[1]);
        } else {
            return Value(-1);
        }
        
        DataFrame result = df->groupby(column).count();
        return Value(static_cast<int>(registry.store(std::move(result))));
    });
    
    // ========================================================================
    // Join Operations
    // ========================================================================
    
    // data.df.merge(dfId1, dfId2, on, how?) -> dfId
    reg.registerFunction("system.data.df.merge", [&registry](const std::vector<Value>& args) -> Value {
        if (args.size() < 3) return Value(-1);
        
        auto* df1 = registry.get(getDfId(args[0]));
        auto* df2 = registry.get(getDfId(args[1]));
        if (!df1 || !df2) return Value(-1);
        
        std::string on;
        if (std::holds_alternative<std::string>(args[2])) {
            on = std::get<std::string>(args[2]);
        } else {
            return Value(-1);
        }
        
        std::string how = "inner";
        if (args.size() >= 4 && std::holds_alternative<std::string>(args[3])) {
            how = std::get<std::string>(args[3]);
        }
        
        DataFrame result = df1->merge(*df2, on, on, how);
        return Value(static_cast<int>(registry.store(std::move(result))));
    });
    
    // data.df.concat(dfId1, dfId2, axis?) -> dfId
    reg.registerFunction("system.data.df.concat", [&registry](const std::vector<Value>& args) -> Value {
        if (args.size() < 2) return Value(-1);
        
        auto* df1 = registry.get(getDfId(args[0]));
        auto* df2 = registry.get(getDfId(args[1]));
        if (!df1 || !df2) return Value(-1);
        
        bool axis0 = true;  // rows
        if (args.size() >= 3 && std::holds_alternative<int>(args[2])) {
            axis0 = std::get<int>(args[2]) == 0;
        }
        
        DataFrame result = df1->concat(*df2, axis0);
        return Value(static_cast<int>(registry.store(std::move(result))));
    });
    
    // ========================================================================
    // Missing Data
    // ========================================================================
    
    // data.df.dropna(dfId, any?) -> dfId
    reg.registerFunction("system.data.df.dropna", [&registry](const std::vector<Value>& args) -> Value {
        if (args.empty()) return Value(-1);
        
        auto* df = registry.get(getDfId(args[0]));
        if (!df) return Value(-1);
        
        bool any = true;
        if (args.size() >= 2 && std::holds_alternative<bool>(args[1])) {
            any = std::get<bool>(args[1]);
        }
        
        DataFrame result = df->dropna(any);
        return Value(static_cast<int>(registry.store(std::move(result))));
    });
    
    // data.df.fillna(dfId, value) -> dfId
    reg.registerFunction("system.data.df.fillna", [&registry](const std::vector<Value>& args) -> Value {
        if (args.size() < 2) return Value(-1);
        
        auto* df = registry.get(getDfId(args[0]));
        if (!df) return Value(-1);
        
        double value = 0.0;
        if (std::holds_alternative<double>(args[1])) {
            value = std::get<double>(args[1]);
        } else if (std::holds_alternative<int>(args[1])) {
            value = std::get<int>(args[1]);
        }
        
        DataFrame result = df->fillna(value);
        return Value(static_cast<int>(registry.store(std::move(result))));
    });
    
    // ========================================================================
    // Correlation & Covariance
    // ========================================================================
    
    // data.df.corr(dfId) -> dfId
    reg.registerFunction("system.data.df.corr", [&registry](const std::vector<Value>& args) -> Value {
        if (args.empty()) return Value(-1);
        
        auto* df = registry.get(getDfId(args[0]));
        if (!df) return Value(-1);
        
        DataFrame result = df->corr();
        return Value(static_cast<int>(registry.store(std::move(result))));
    });
    
    // data.df.cov(dfId) -> dfId
    reg.registerFunction("system.data.df.cov", [&registry](const std::vector<Value>& args) -> Value {
        if (args.empty()) return Value(-1);
        
        auto* df = registry.get(getDfId(args[0]));
        if (!df) return Value(-1);
        
        DataFrame result = df->cov();
        return Value(static_cast<int>(registry.store(std::move(result))));
    });
    
    // ========================================================================
    // Transformations
    // ========================================================================
    
    // data.df.transpose(dfId) -> dfId
    reg.registerFunction("system.data.df.transpose", [&registry](const std::vector<Value>& args) -> Value {
        if (args.empty()) return Value(-1);
        
        auto* df = registry.get(getDfId(args[0]));
        if (!df) return Value(-1);
        
        DataFrame result = df->transpose();
        return Value(static_cast<int>(registry.store(std::move(result))));
    });
    
    // data.df.copy(dfId) -> dfId
    reg.registerFunction("system.data.df.copy", [&registry](const std::vector<Value>& args) -> Value {
        if (args.empty()) return Value(-1);
        
        auto* df = registry.get(getDfId(args[0]));
        if (!df) return Value(-1);
        
        DataFrame result = df->copy();
        return Value(static_cast<int>(registry.store(std::move(result))));
    });
    
    // ========================================================================
    // Series Operations
    // ========================================================================
    
    // data.series.create(name, dataArray) -> seriesId
    reg.registerFunction("system.data.series.create", [&registry](const std::vector<Value>& args) -> Value {
        if (args.size() < 2) return Value(-1);
        
        std::string name;
        if (std::holds_alternative<std::string>(args[0])) {
            name = std::get<std::string>(args[0]);
        } else {
            return Value(-1);
        }
        
        auto data = extractDoubleArray(args[1]);
        Series s(name, data);
        return Value(static_cast<int>(registry.storeSeries(std::move(s))));
    });
    
    // data.series.range(name, start, end, step?) -> seriesId
    reg.registerFunction("system.data.series.range", [&registry](const std::vector<Value>& args) -> Value {
        if (args.size() < 3) return Value(-1);
        
        std::string name;
        if (std::holds_alternative<std::string>(args[0])) {
            name = std::get<std::string>(args[0]);
        } else {
            return Value(-1);
        }
        
        int64_t start = 0, end = 0, step = 1;
        if (std::holds_alternative<int>(args[1])) start = std::get<int>(args[1]);
        if (std::holds_alternative<int>(args[2])) end = std::get<int>(args[2]);
        if (args.size() >= 4 && std::holds_alternative<int>(args[3])) {
            step = std::get<int>(args[3]);
        }
        
        Series s = Series::range(name, start, end, step);
        return Value(static_cast<int>(registry.storeSeries(std::move(s))));
    });
    
    // data.series.zeros(name, count) -> seriesId
    reg.registerFunction("system.data.series.zeros", [&registry](const std::vector<Value>& args) -> Value {
        if (args.size() < 2) return Value(-1);
        
        std::string name;
        if (std::holds_alternative<std::string>(args[0])) {
            name = std::get<std::string>(args[0]);
        } else {
            return Value(-1);
        }
        
        size_t count = 0;
        if (std::holds_alternative<int>(args[1])) {
            count = std::get<int>(args[1]);
        }
        
        Series s = Series::zeros(name, count);
        return Value(static_cast<int>(registry.storeSeries(std::move(s))));
    });
    
    // data.series.ones(name, count) -> seriesId
    reg.registerFunction("system.data.series.ones", [&registry](const std::vector<Value>& args) -> Value {
        if (args.size() < 2) return Value(-1);
        
        std::string name;
        if (std::holds_alternative<std::string>(args[0])) {
            name = std::get<std::string>(args[0]);
        } else {
            return Value(-1);
        }
        
        size_t count = 0;
        if (std::holds_alternative<int>(args[1])) {
            count = std::get<int>(args[1]);
        }
        
        Series s = Series::ones(name, count);
        return Value(static_cast<int>(registry.storeSeries(std::move(s))));
    });
    
    // data.series.sum(seriesId) -> double
    reg.registerFunction("system.data.series.sum", [&registry](const std::vector<Value>& args) -> Value {
        if (args.empty()) return Value(0.0);
        auto* s = registry.getSeries(getSeriesId(args[0]));
        return s ? Value(s->sum()) : Value(0.0);
    });
    
    // data.series.mean(seriesId) -> double
    reg.registerFunction("system.data.series.mean", [&registry](const std::vector<Value>& args) -> Value {
        if (args.empty()) return Value(0.0);
        auto* s = registry.getSeries(getSeriesId(args[0]));
        return s ? Value(s->mean()) : Value(0.0);
    });
    
    // data.series.min(seriesId) -> double
    reg.registerFunction("system.data.series.min", [&registry](const std::vector<Value>& args) -> Value {
        if (args.empty()) return Value(0.0);
        auto* s = registry.getSeries(getSeriesId(args[0]));
        return s ? Value(s->min()) : Value(0.0);
    });
    
    // data.series.max(seriesId) -> double
    reg.registerFunction("system.data.series.max", [&registry](const std::vector<Value>& args) -> Value {
        if (args.empty()) return Value(0.0);
        auto* s = registry.getSeries(getSeriesId(args[0]));
        return s ? Value(s->max()) : Value(0.0);
    });
    
    // data.series.std(seriesId) -> double
    reg.registerFunction("system.data.series.std", [&registry](const std::vector<Value>& args) -> Value {
        if (args.empty()) return Value(0.0);
        auto* s = registry.getSeries(getSeriesId(args[0]));
        return s ? Value(s->std()) : Value(0.0);
    });
    
    // data.series.median(seriesId) -> double
    reg.registerFunction("system.data.series.median", [&registry](const std::vector<Value>& args) -> Value {
        if (args.empty()) return Value(0.0);
        auto* s = registry.getSeries(getSeriesId(args[0]));
        return s ? Value(s->median()) : Value(0.0);
    });
    
    // data.series.quantile(seriesId, q) -> double
    reg.registerFunction("system.data.series.quantile", [&registry](const std::vector<Value>& args) -> Value {
        if (args.size() < 2) return Value(0.0);
        auto* s = registry.getSeries(getSeriesId(args[0]));
        if (!s) return Value(0.0);
        
        double q = 0.5;
        if (std::holds_alternative<double>(args[1])) {
            q = std::get<double>(args[1]);
        }
        
        return Value(s->quantile(q));
    });
    
    // data.series.normalize(seriesId) -> seriesId
    reg.registerFunction("system.data.series.normalize", [&registry](const std::vector<Value>& args) -> Value {
        if (args.empty()) return Value(-1);
        auto* s = registry.getSeries(getSeriesId(args[0]));
        if (!s) return Value(-1);
        
        Series result = s->normalize();
        return Value(static_cast<int>(registry.storeSeries(std::move(result))));
    });
    
    // data.series.toArray(seriesId) -> array
    reg.registerFunction("system.data.series.toArray", [&registry](const std::vector<Value>& args) -> Value {
        if (args.empty() || !rt()) return Value{};
        auto* s = registry.getSeries(getSeriesId(args[0]));
        if (!s) return rt()->makeArray({});
        
        return makeDoubleArray(s->toVector());
    });
    
    // data.series.repr(seriesId) -> string
    reg.registerFunction("system.data.series.repr", [&registry](const std::vector<Value>& args) -> Value {
        if (args.empty()) return Value(std::string(""));
        auto* s = registry.getSeries(getSeriesId(args[0]));
        return s ? Value(s->repr()) : Value(std::string("Series not found"));
    });
    
    // data.series.destroy(seriesId) -> bool
    reg.registerFunction("system.data.series.destroy", [&registry](const std::vector<Value>& args) -> Value {
        if (args.empty()) return Value(false);
        registry.removeSeries(getSeriesId(args[0]));
        return Value(true);
    });
    
    // ========================================================================
    // Column-Level Series Operations via DataFrame
    // ========================================================================
    
    // data.df.col.sum(dfId, column) -> double
    reg.registerFunction("system.data.df.col.sum", [&registry](const std::vector<Value>& args) -> Value {
        if (args.size() < 2) return Value(0.0);
        
        auto* df = registry.get(getDfId(args[0]));
        if (!df) return Value(0.0);
        
        std::string col;
        if (std::holds_alternative<std::string>(args[1])) {
            col = std::get<std::string>(args[1]);
        } else {
            return Value(0.0);
        }
        
        if (!df->hasColumn(col)) return Value(0.0);
        return Value((*df)[col].sum());
    });
    
    // data.df.col.mean(dfId, column) -> double
    reg.registerFunction("system.data.df.col.mean", [&registry](const std::vector<Value>& args) -> Value {
        if (args.size() < 2) return Value(0.0);
        
        auto* df = registry.get(getDfId(args[0]));
        if (!df) return Value(0.0);
        
        std::string col;
        if (std::holds_alternative<std::string>(args[1])) {
            col = std::get<std::string>(args[1]);
        } else {
            return Value(0.0);
        }
        
        if (!df->hasColumn(col)) return Value(0.0);
        return Value((*df)[col].mean());
    });
    
    // data.df.col.std(dfId, column) -> double
    reg.registerFunction("system.data.df.col.std", [&registry](const std::vector<Value>& args) -> Value {
        if (args.size() < 2) return Value(0.0);
        
        auto* df = registry.get(getDfId(args[0]));
        if (!df) return Value(0.0);
        
        std::string col;
        if (std::holds_alternative<std::string>(args[1])) {
            col = std::get<std::string>(args[1]);
        } else {
            return Value(0.0);
        }
        
        if (!df->hasColumn(col)) return Value(0.0);
        return Value((*df)[col].std());
    });
    
    // data.df.col.min(dfId, column) -> double
    reg.registerFunction("system.data.df.col.min", [&registry](const std::vector<Value>& args) -> Value {
        if (args.size() < 2) return Value(0.0);
        
        auto* df = registry.get(getDfId(args[0]));
        if (!df) return Value(0.0);
        
        std::string col;
        if (std::holds_alternative<std::string>(args[1])) {
            col = std::get<std::string>(args[1]);
        } else {
            return Value(0.0);
        }
        
        if (!df->hasColumn(col)) return Value(0.0);
        return Value((*df)[col].min());
    });
    
    // data.df.col.max(dfId, column) -> double
    reg.registerFunction("system.data.df.col.max", [&registry](const std::vector<Value>& args) -> Value {
        if (args.size() < 2) return Value(0.0);
        
        auto* df = registry.get(getDfId(args[0]));
        if (!df) return Value(0.0);
        
        std::string col;
        if (std::holds_alternative<std::string>(args[1])) {
            col = std::get<std::string>(args[1]);
        } else {
            return Value(0.0);
        }
        
        if (!df->hasColumn(col)) return Value(0.0);
        return Value((*df)[col].max());
    });
}
