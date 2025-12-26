#include "function_registry.h"
#include "runtime.h"

#include <algorithm>

static inline bool isTruthy(const Value& v) {
    return std::visit([](auto&& arg) -> bool {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, bool>) return arg;
        if constexpr (std::is_same_v<T, int>) return arg != 0;
        if constexpr (std::is_same_v<T, double>) return arg != 0.0;
        if constexpr (std::is_same_v<T, std::string>) return !arg.empty();
        return false;
    }, v);
}

static inline Runtime* rt() {
    return global_runtime_ptr;
}

static inline Value makeEmptyArray() {
    if (!rt()) return Value{};
    return rt()->makeArray({});
}

static inline Value makeEmptyDict() {
    if (!rt()) return Value{};
    return rt()->makeDict({});
}

void register_collection_functions(FunctionRegistry& reg) {
    // len(x): array/dict length
    reg.registerFunction("system.collection.len", [](const std::vector<Value>& args) -> Value {
        if (!rt() || args.empty()) return Value(0);
        if (std::holds_alternative<ArrayRef>(args[0])) {
            auto* vec = rt()->getArray(std::get<ArrayRef>(args[0]));
            return Value(vec ? (int)vec->size() : 0);
        }
        if (std::holds_alternative<DictRef>(args[0])) {
            auto* dict = rt()->getDict(std::get<DictRef>(args[0]));
            return Value(dict ? (int)dict->size() : 0);
        }
        return Value(0);
    });

    // push(arr, value) -> arr (mutates)
    reg.registerFunction("system.collection.push", [](const std::vector<Value>& args) -> Value {
        if (!rt() || args.size() < 2) return Value{};
        if (!std::holds_alternative<ArrayRef>(args[0])) return Value{};
        ArrayRef aref = std::get<ArrayRef>(args[0]);
        auto* vec = rt()->getArray(aref);
        if (!vec) return Value{};
        vec->push_back(args[1]);
        return Value(aref);
    });

    // pop(arr) -> lastValue
    reg.registerFunction("system.collection.pop", [](const std::vector<Value>& args) -> Value {
        if (!rt() || args.empty()) return Value{};
        if (!std::holds_alternative<ArrayRef>(args[0])) return Value{};
        auto* vec = rt()->getArray(std::get<ArrayRef>(args[0]));
        if (!vec || vec->empty()) return Value{};
        Value v = vec->back();
        vec->pop_back();
        return v;
    });

    // slice(arr, start, end?) -> newArr
    reg.registerFunction("system.collection.slice", [](const std::vector<Value>& args) -> Value {
        if (!rt() || args.size() < 2) return makeEmptyArray();
        if (!std::holds_alternative<ArrayRef>(args[0])) return makeEmptyArray();
        auto* vec = rt()->getArray(std::get<ArrayRef>(args[0]));
        if (!vec) return makeEmptyArray();

        int start = std::holds_alternative<int>(args[1]) ? std::get<int>(args[1]) : 0;
        int end = (int)vec->size();
        if (args.size() >= 3 && std::holds_alternative<int>(args[2])) end = std::get<int>(args[2]);

        if (start < 0) start = 0;
        if (end < start) end = start;
        if (end > (int)vec->size()) end = (int)vec->size();

        std::vector<Value> out;
        out.reserve((size_t)(end - start));
        for (int i = start; i < end; ++i) out.push_back((*vec)[(size_t)i]);
        return rt()->makeArray(std::move(out));
    });

    // concat(a, b) -> newArr
    reg.registerFunction("system.collection.concat", [](const std::vector<Value>& args) -> Value {
        if (!rt() || args.size() < 2) return makeEmptyArray();
        if (!std::holds_alternative<ArrayRef>(args[0]) || !std::holds_alternative<ArrayRef>(args[1])) return makeEmptyArray();
        auto* a = rt()->getArray(std::get<ArrayRef>(args[0]));
        auto* b = rt()->getArray(std::get<ArrayRef>(args[1]));
        if (!a || !b) return makeEmptyArray();
        std::vector<Value> out;
        out.reserve(a->size() + b->size());
        out.insert(out.end(), a->begin(), a->end());
        out.insert(out.end(), b->begin(), b->end());
        return rt()->makeArray(std::move(out));
    });

    // join(arr, sep?) -> string
    reg.registerFunction("system.collection.join", [](const std::vector<Value>& args) -> Value {
        if (!rt() || args.empty()) return Value(std::string(""));
        if (!std::holds_alternative<ArrayRef>(args[0])) return Value(std::string(""));
        const std::string sep = (args.size() >= 2 && std::holds_alternative<std::string>(args[1])) ? std::get<std::string>(args[1]) : std::string("");
        auto* vec = rt()->getArray(std::get<ArrayRef>(args[0]));
        if (!vec) return Value(std::string(""));

        std::string out;
        for (size_t i = 0; i < vec->size(); ++i) {
            if (i) out += sep;
            out += rt()->formatValue((*vec)[i], false);
        }
        return Value(out);
    });

    // map(arr, lambda) -> newArr
    reg.registerFunction("system.collection.map", [](const std::vector<Value>& args) -> Value {
        if (!rt() || args.size() < 2) return makeEmptyArray();
        if (!std::holds_alternative<ArrayRef>(args[0])) return makeEmptyArray();
        auto* vec = rt()->getArray(std::get<ArrayRef>(args[0]));
        if (!vec) return makeEmptyArray();

        std::vector<Value> out;
        out.reserve(vec->size());
        for (const auto& v : *vec) {
            out.push_back(rt()->invokeLambdaValue(args[1], {v}));
        }
        return rt()->makeArray(std::move(out));
    });

    // filter(arr, predLambda) -> newArr
    reg.registerFunction("system.collection.filter", [](const std::vector<Value>& args) -> Value {
        if (!rt() || args.size() < 2) return makeEmptyArray();
        if (!std::holds_alternative<ArrayRef>(args[0])) return makeEmptyArray();
        auto* vec = rt()->getArray(std::get<ArrayRef>(args[0]));
        if (!vec) return makeEmptyArray();

        std::vector<Value> out;
        for (const auto& v : *vec) {
            Value keep = rt()->invokeLambdaValue(args[1], {v});
            if (isTruthy(keep)) out.push_back(v);
        }
        return rt()->makeArray(std::move(out));
    });

    // reduce(arr, fn, initial?) -> value
    reg.registerFunction("system.collection.reduce", [](const std::vector<Value>& args) -> Value {
        if (!rt() || args.size() < 2) return Value{};
        if (!std::holds_alternative<ArrayRef>(args[0])) return Value{};
        auto* vec = rt()->getArray(std::get<ArrayRef>(args[0]));
        if (!vec || vec->empty()) return Value{};

        size_t idx = 0;
        Value acc;
        if (args.size() >= 3) {
            acc = args[2];
        } else {
            acc = (*vec)[0];
            idx = 1;
        }

        for (; idx < vec->size(); ++idx) {
            acc = rt()->invokeLambdaValue(args[1], {acc, (*vec)[idx]});
        }
        return acc;
    });

    // sort(arr, comparatorLambda?) -> arr (mutates)
    reg.registerFunction("system.collection.sort", [](const std::vector<Value>& args) -> Value {
        if (!rt() || args.empty()) return Value{};
        if (!std::holds_alternative<ArrayRef>(args[0])) return Value{};
        ArrayRef aref = std::get<ArrayRef>(args[0]);
        auto* vec = rt()->getArray(aref);
        if (!vec) return Value{};

        if (args.size() >= 2) {
            // comparator: (a,b) -> bool
            std::stable_sort(vec->begin(), vec->end(), [&](const Value& a, const Value& b) {
                Value r = rt()->invokeLambdaValue(args[1], {a, b});
                return isTruthy(r);
            });
        } else {
            // default ordering by formatted string
            std::stable_sort(vec->begin(), vec->end(), [&](const Value& a, const Value& b) {
                return rt()->formatValue(a, false) < rt()->formatValue(b, false);
            });
        }
        return Value(aref);
    });

    // keys(dict) -> array
    reg.registerFunction("system.collection.keys", [](const std::vector<Value>& args) -> Value {
        if (!rt() || args.empty() || !std::holds_alternative<DictRef>(args[0])) return makeEmptyArray();
        auto* dict = rt()->getDict(std::get<DictRef>(args[0]));
        if (!dict) return makeEmptyArray();
        std::vector<Value> out;
        out.reserve(dict->size());
        for (const auto& kv : *dict) out.push_back(Value(kv.first));
        return rt()->makeArray(std::move(out));
    });

    // values(dict) -> array
    reg.registerFunction("system.collection.values", [](const std::vector<Value>& args) -> Value {
        if (!rt() || args.empty() || !std::holds_alternative<DictRef>(args[0])) return makeEmptyArray();
        auto* dict = rt()->getDict(std::get<DictRef>(args[0]));
        if (!dict) return makeEmptyArray();
        std::vector<Value> out;
        out.reserve(dict->size());
        for (const auto& kv : *dict) out.push_back(kv.second);
        return rt()->makeArray(std::move(out));
    });

    // contains(dict, key) -> bool
    reg.registerFunction("system.collection.contains", [](const std::vector<Value>& args) -> Value {
        if (!rt() || args.size() < 2) return Value(false);
        if (!std::holds_alternative<DictRef>(args[0]) || !std::holds_alternative<std::string>(args[1])) return Value(false);
        auto* dict = rt()->getDict(std::get<DictRef>(args[0]));
        if (!dict) return Value(false);
        return Value(dict->find(std::get<std::string>(args[1])) != dict->end());
    });

    // getOrDefault(dict, key, default) -> value
    reg.registerFunction("system.collection.getOrDefault", [](const std::vector<Value>& args) -> Value {
        if (!rt() || args.size() < 3) return Value{};
        if (!std::holds_alternative<DictRef>(args[0]) || !std::holds_alternative<std::string>(args[1])) return args[2];
        auto* dict = rt()->getDict(std::get<DictRef>(args[0]));
        if (!dict) return args[2];
        auto it = dict->find(std::get<std::string>(args[1]));
        return it != dict->end() ? it->second : args[2];
    });

    // remove(dict, key) -> bool (mutates)
    reg.registerFunction("system.collection.remove", [](const std::vector<Value>& args) -> Value {
        if (!rt() || args.size() < 2) return Value(false);
        if (!std::holds_alternative<DictRef>(args[0]) || !std::holds_alternative<std::string>(args[1])) return Value(false);
        auto* dict = rt()->getDict(std::get<DictRef>(args[0]));
        if (!dict) return Value(false);
        size_t n = dict->erase(std::get<std::string>(args[1]));
        return Value(n != 0);
    });

    // merge(a, b) -> newDict
    reg.registerFunction("system.collection.merge", [](const std::vector<Value>& args) -> Value {
        if (!rt() || args.size() < 2) return makeEmptyDict();
        if (!std::holds_alternative<DictRef>(args[0]) || !std::holds_alternative<DictRef>(args[1])) return makeEmptyDict();
        auto* a = rt()->getDict(std::get<DictRef>(args[0]));
        auto* b = rt()->getDict(std::get<DictRef>(args[1]));
        if (!a || !b) return makeEmptyDict();
        std::unordered_map<std::string, Value> out = *a;
        for (const auto& kv : *b) out[kv.first] = kv.second;
        return rt()->makeDict(std::move(out));
    });

    // entries(dict) -> array of {"key": string, "value": any}
    reg.registerFunction("system.collection.entries", [](const std::vector<Value>& args) -> Value {
        if (!rt() || args.empty() || !std::holds_alternative<DictRef>(args[0])) return makeEmptyArray();
        auto* dict = rt()->getDict(std::get<DictRef>(args[0]));
        if (!dict) return makeEmptyArray();

        std::vector<Value> out;
        out.reserve(dict->size());
        for (const auto& kv : *dict) {
            std::unordered_map<std::string, Value> entry;
            entry["key"] = Value(kv.first);
            entry["value"] = kv.second;
            out.push_back(rt()->makeDict(std::move(entry)));
        }
        return rt()->makeArray(std::move(out));
    });
}
