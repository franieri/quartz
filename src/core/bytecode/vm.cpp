#include <bytecode/vm.h>

#include "function_registry.h"
#include "logger.h"

#include <cstring>

// ============================================================================
// VM Configuration Constants - Tunable for performance
// ============================================================================
namespace vm_config {
    // Default stack reservation capacity to avoid frequent reallocations
    constexpr size_t kDefaultStackReserve = 256;
    
    // Reserve capacity for call arguments vector
    constexpr size_t kDefaultArgsReserve = 16;
    
    // Reserve capacity for locals vector (adjusted per function)
    constexpr size_t kDefaultLocalsReserve = 64;
    
    // Try stack reserve for exception handling frames
    constexpr size_t kDefaultTryStackReserve = 8;
}
// ============================================================================

static inline void appendValueRepr(std::string& out, const Value& val) {
    std::visit([&out](auto&& arg) {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, int>) {
            out += std::to_string(arg);
        } else if constexpr (std::is_same_v<T, double>) {
            out += std::to_string(arg);
        } else if constexpr (std::is_same_v<T, std::string>) {
            out += '"';
            out += arg;
            out += '"';
        } else if constexpr (std::is_same_v<T, bool>) {
            out += arg ? "true" : "false";
        }
    }, val);
}

static inline void appendValueToStringVM(std::string& out, const Value& val, bool quoteStrings) {
    std::visit([&out, quoteStrings](auto&& arg) {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, int>) {
            out += std::to_string(arg);
        } else if constexpr (std::is_same_v<T, double>) {
            out += std::to_string(arg);
        } else if constexpr (std::is_same_v<T, std::string>) {
            if (quoteStrings) {
                out += '"';
                out += arg;
                out += '"';
            } else {
                out += arg;
            }
        } else if constexpr (std::is_same_v<T, bool>) {
            out += arg ? "true" : "false";
        }
    }, val);
}

static inline bool isStringValue(const Value& v) { return std::holds_alternative<std::string>(v); }

// Branch prediction hints for hot paths
#if defined(__GNUC__) || defined(__clang__)
#define VM_LIKELY(x)   __builtin_expect(!!(x), 1)
#define VM_UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
#define VM_LIKELY(x)   (x)
#define VM_UNLIKELY(x) (x)
#endif

// Optimized byte reading - inline for hot path performance
uint8_t BytecodeVM::readU8(const std::vector<uint8_t>& code, size_t& ip, bool* ok) {
    if (VM_UNLIKELY(ip >= code.size())) { *ok = false; return 0; }
    return code[ip++];
}

uint16_t BytecodeVM::readU16(const std::vector<uint8_t>& code, size_t& ip, bool* ok) {
    if (VM_UNLIKELY(ip + 2 > code.size())) { *ok = false; return 0; }
    // Direct memory access for better optimization
    const uint8_t* ptr = code.data() + ip;
    uint16_t v = static_cast<uint16_t>(ptr[0]) | (static_cast<uint16_t>(ptr[1]) << 8);
    ip += 2;
    return v;
}

uint32_t BytecodeVM::readU32(const std::vector<uint8_t>& code, size_t& ip, bool* ok) {
    if (VM_UNLIKELY(ip + 4 > code.size())) { *ok = false; return 0; }
    // Direct memory access for better optimization
    const uint8_t* ptr = code.data() + ip;
    uint32_t v = static_cast<uint32_t>(ptr[0]) 
               | (static_cast<uint32_t>(ptr[1]) << 8) 
               | (static_cast<uint32_t>(ptr[2]) << 16) 
               | (static_cast<uint32_t>(ptr[3]) << 24);
    ip += 4;
    return v;
}

int32_t BytecodeVM::readI32(const std::vector<uint8_t>& code, size_t& ip, bool* ok) {
    return static_cast<int32_t>(readU32(code, ip, ok));
}

double BytecodeVM::readF64(const std::vector<uint8_t>& code, size_t& ip, bool* ok) {
    if (VM_UNLIKELY(ip + 8 > code.size())) { *ok = false; return 0.0; }
    // Use direct pointer access and unrolled byte reading
    const uint8_t* ptr = code.data() + ip;
    uint64_t bits = static_cast<uint64_t>(ptr[0])
                  | (static_cast<uint64_t>(ptr[1]) << 8)
                  | (static_cast<uint64_t>(ptr[2]) << 16)
                  | (static_cast<uint64_t>(ptr[3]) << 24)
                  | (static_cast<uint64_t>(ptr[4]) << 32)
                  | (static_cast<uint64_t>(ptr[5]) << 40)
                  | (static_cast<uint64_t>(ptr[6]) << 48)
                  | (static_cast<uint64_t>(ptr[7]) << 56);
    ip += 8;
    double d;
    std::memcpy(&d, &bits, sizeof(d));
    return d;
}

std::string BytecodeVM::str(uint32_t stringIndex) const {
    if (VM_UNLIKELY(!prog || stringIndex == bc::kInvalidIndex || stringIndex >= prog->strings.size())) return "";
    return prog->strings[stringIndex];
}

bool BytecodeVM::evalCondition(const Value& v) const {
    // Fast-path: direct type checks for common types to avoid std::visit overhead
    if (const bool* b = std::get_if<bool>(&v)) {
        return *b;
    }
    if (const int* i = std::get_if<int>(&v)) {
        return *i != 0;
    }
    if (const double* d = std::get_if<double>(&v)) {
        return *d != 0.0;
    }
    if (const std::string* s = std::get_if<std::string>(&v)) {
        return !s->empty();
    }
    // Fallback for less common types (TaskRef, BufferRef, etc.)
    return std::visit([](auto&& arg) -> bool {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, TaskRef>) return true;
        else if constexpr (std::is_same_v<T, BufferRef>) return true;
        else if constexpr (std::is_same_v<T, ArrayRef>) return true;
        else if constexpr (std::is_same_v<T, DictRef>) return true;
        else return false;
    }, v);
}

// Fast-path helper for int-int binary operations (most common case)
static inline Value applyBinaryIntInt(int l, int r, bc::BinaryOp op) {
    switch (op) {
    case bc::BinaryOp::ADD: return Value(l + r);
    case bc::BinaryOp::SUB: return Value(l - r);
    case bc::BinaryOp::MUL: return Value(l * r);
    case bc::BinaryOp::DIV:
        if (r == 0) throw LanguageException("ArithmeticError", "Division by zero");
        return Value(l / r);
    case bc::BinaryOp::EQ: return Value(l == r);
    case bc::BinaryOp::NE: return Value(l != r);
    case bc::BinaryOp::LT: return Value(l < r);
    case bc::BinaryOp::GT: return Value(l > r);
    case bc::BinaryOp::LE: return Value(l <= r);
    case bc::BinaryOp::GE: return Value(l >= r);
    }
    return Value{};
}

// Fast-path helper for double-double binary operations
static inline Value applyBinaryDoubleDouble(double l, double r, bc::BinaryOp op) {
    switch (op) {
    case bc::BinaryOp::ADD: return Value(l + r);
    case bc::BinaryOp::SUB: return Value(l - r);
    case bc::BinaryOp::MUL: return Value(l * r);
    case bc::BinaryOp::DIV:
        if (r == 0.0) throw LanguageException("ArithmeticError", "Division by zero");
        return Value(l / r);
    case bc::BinaryOp::EQ: return Value(l == r);
    case bc::BinaryOp::NE: return Value(l != r);
    case bc::BinaryOp::LT: return Value(l < r);
    case bc::BinaryOp::GT: return Value(l > r);
    case bc::BinaryOp::LE: return Value(l <= r);
    case bc::BinaryOp::GE: return Value(l >= r);
    }
    return Value{};
}

Value BytecodeVM::applyBinary(const Value& left, const Value& right, bc::BinaryOp op) const {
    // Fast-path: int-int operations (most common in loops, counters, etc.)
    if (const int* li = std::get_if<int>(&left)) {
        if (const int* ri = std::get_if<int>(&right)) {
            return applyBinaryIntInt(*li, *ri, op);
        }
        // int-double promotion
        if (const double* rd = std::get_if<double>(&right)) {
            return applyBinaryDoubleDouble(static_cast<double>(*li), *rd, op);
        }
    }
    
    // Fast-path: double-double operations
    if (const double* ld = std::get_if<double>(&left)) {
        if (const double* rd = std::get_if<double>(&right)) {
            return applyBinaryDoubleDouble(*ld, *rd, op);
        }
        // double-int promotion
        if (const int* ri = std::get_if<int>(&right)) {
            return applyBinaryDoubleDouble(*ld, static_cast<double>(*ri), op);
        }
    }
    
    // Fast-path: string concatenation
    if (op == bc::BinaryOp::ADD) {
        if (std::holds_alternative<std::string>(left) || std::holds_alternative<std::string>(right)) {
            return Value(to_string(left) + to_string(right));
        }
    }
    
    // Fallback to generic visitor for remaining cases
    return std::visit([&](auto&& l, auto&& r) -> Value {
        using L = std::decay_t<decltype(l)>;
        using R = std::decay_t<decltype(r)>;

        // string concat support for interpolated strings
        if constexpr (std::is_same_v<L, std::string> || std::is_same_v<R, std::string>) {
            if (op == bc::BinaryOp::ADD) {
                return Value(to_string(left) + to_string(right));
            }
            return Value{};
        }

        if constexpr (std::is_arithmetic_v<L> && std::is_arithmetic_v<R>) {
            switch (op) {
            case bc::BinaryOp::ADD: return Value(l + r);
            case bc::BinaryOp::SUB: return Value(l - r);
            case bc::BinaryOp::MUL: return Value(l * r);
            case bc::BinaryOp::DIV:
                // Check for division by zero
                if constexpr (std::is_integral_v<R>) {
                    if (r == 0) {
                        throw LanguageException("ArithmeticError", "Division by zero");
                    }
                } else if constexpr (std::is_floating_point_v<R>) {
                    if (r == 0.0) {
                        throw LanguageException("ArithmeticError", "Division by zero");
                    }
                }
                return Value(l / r);
            case bc::BinaryOp::EQ: return Value(l == r);
            case bc::BinaryOp::NE: return Value(l != r);
            case bc::BinaryOp::LT: return Value(l < r);
            case bc::BinaryOp::GT: return Value(l > r);
            case bc::BinaryOp::LE: return Value(l <= r);
            case bc::BinaryOp::GE: return Value(l >= r);
            }
        }
        return Value{};
    }, left, right);
}

Value BytecodeVM::applyUnary(const Value& operand, bc::UnaryOp op) const {
    // Fast-path for common unary operations
    if (op == bc::UnaryOp::NEG) {
        if (const int* i = std::get_if<int>(&operand)) return Value(-*i);
        if (const double* d = std::get_if<double>(&operand)) return Value(-*d);
        return operand;
    }
    // NOT
    return Value(!evalCondition(operand));
}

Value BytecodeVM::indexGet(const std::string& varName, const Value& indexValue) {
    // Mirror Runtime::evaluate(Index) limited semantics
    // Array
    auto aIt = runtime.varToArrayId.find(varName);
    if (aIt != runtime.varToArrayId.end() && std::holds_alternative<int>(indexValue)) {
        int idx = std::get<int>(indexValue);
        size_t id = aIt->second;
        if (id < runtime.arrayStorage.size()) {
            auto& vec = runtime.arrayStorage[id];
            if (idx < 0 || idx >= (int)vec.size()) {
                throw LanguageException("IndexError", "Array index out of bounds: " + std::to_string(idx) + " (size: " + std::to_string(vec.size()) + ")");
            }
            return vec[idx];
        }
    }

    auto dIt = runtime.varToDictId.find(varName);
    if (dIt != runtime.varToDictId.end() && std::holds_alternative<std::string>(indexValue)) {
        const std::string& key = std::get<std::string>(indexValue);
        size_t id = dIt->second;
        if (id < runtime.dictStorage.size()) {
            auto& dict = runtime.dictStorage[id];
            auto kIt = dict.find(key);
            if (kIt != dict.end()) return kIt->second;
            throw LanguageException("KeyError", "Dictionary key not found: '" + key + "'");
        }
    }

    return Value{};
}

bool BytecodeVM::execDefInterface(const std::vector<uint8_t>& code, size_t& ip, std::string* error) {
    bool ok = true;
    uint32_t nameIdx = readU32(code, ip, &ok);
    uint32_t extendsCount = readU32(code, ip, &ok);
    for (uint32_t i = 0; i < extendsCount; ++i) (void)readU32(code, ip, &ok);
    uint32_t genericCount = readU32(code, ip, &ok);
    for (uint32_t i = 0; i < genericCount; ++i) (void)readU32(code, ip, &ok);
    uint32_t methodCount = readU32(code, ip, &ok);
    for (uint32_t i = 0; i < methodCount; ++i) {
        (void)readU32(code, ip, &ok);
        (void)readU8(code, ip, &ok);
    }
    if (!ok) {
        if (error) *error = "Corrupt DEF_INTERFACE payload";
        return false;
    }

    // Keep current interpreter behavior: interfaces are registered but not enforced.
    InterfaceDef idef;
    idef.name = str(nameIdx);
    runtime.interfaceRegistry[idef.name] = idef;
    return true;
}

bool BytecodeVM::execDefClass(const std::vector<uint8_t>& code, size_t& ip, std::string* error) {
    bool ok = true;
    uint32_t nameIdx = readU32(code, ip, &ok);
    uint32_t parentIdx = readU32(code, ip, &ok);

    std::string className = str(nameIdx);
    std::string parentName = (parentIdx == bc::kInvalidIndex) ? "" : str(parentIdx);

    uint32_t ifaceCount = readU32(code, ip, &ok);
    for (uint32_t i = 0; i < ifaceCount; ++i) (void)readU32(code, ip, &ok);

    uint32_t genericCount = readU32(code, ip, &ok);
    for (uint32_t i = 0; i < genericCount; ++i) (void)readU32(code, ip, &ok);

    uint32_t fieldCount = readU32(code, ip, &ok);
    std::vector<std::string> fields;
    fields.reserve(fieldCount);
    for (uint32_t i = 0; i < fieldCount; ++i) fields.push_back(str(readU32(code, ip, &ok)));

    uint32_t staticFieldCount = readU32(code, ip, &ok);
    struct StaticInit { std::string name; bool hasInit=false; uint32_t exprFn=bc::kInvalidIndex; };
    std::vector<StaticInit> staticFields;
    for (uint32_t i = 0; i < staticFieldCount; ++i) {
        StaticInit si;
        si.name = str(readU32(code, ip, &ok));
        si.hasInit = readU8(code, ip, &ok) != 0;
        si.exprFn = readU32(code, ip, &ok);
        staticFields.push_back(si);
    }

    uint32_t methodCount = readU32(code, ip, &ok);
    std::vector<std::tuple<std::string,bool,std::vector<std::string>,uint32_t>> methods;
    methods.reserve(methodCount);
    for (uint32_t i = 0; i < methodCount; ++i) {
        std::string mname = str(readU32(code, ip, &ok));
        bool isStatic = readU8(code, ip, &ok) != 0;
        uint8_t pc = readU8(code, ip, &ok);
        std::vector<std::string> params;
        params.reserve(pc);
        for (uint8_t j = 0; j < pc; ++j) params.push_back(str(readU32(code, ip, &ok)));
        uint32_t fnIdx = readU32(code, ip, &ok);
        methods.push_back({mname, isStatic, std::move(params), fnIdx});
    }

    bool hasCtor = readU8(code, ip, &ok) != 0;
    uint32_t ctorFn = bc::kInvalidIndex;
    std::vector<std::string> ctorParams;
    std::vector<std::pair<std::string, uint32_t>> ctorFieldInits;
    if (hasCtor) {
        uint8_t pc = readU8(code, ip, &ok);
        ctorParams.reserve(pc);
        for (uint8_t i = 0; i < pc; ++i) ctorParams.push_back(str(readU32(code, ip, &ok)));
        ctorFn = readU32(code, ip, &ok);
        uint32_t initCount = readU32(code, ip, &ok);
        ctorFieldInits.reserve(initCount);
        for (uint32_t i = 0; i < initCount; ++i) {
            std::string fname = str(readU32(code, ip, &ok));
            uint32_t efn = readU32(code, ip, &ok);
            ctorFieldInits.push_back({fname, efn});
        }
    }

    if (!ok) {
        if (error) *error = "Corrupt DEF_CLASS payload";
        return false;
    }

    BCClass bcClass;
    bcClass.name = className;
    bcClass.parent = parentName;
    bcClass.fields = fields;
    bcClass.hasConstructor = hasCtor;
    bcClass.ctorFunctionIndex = ctorFn;
    bcClass.ctorParams = ctorParams;
    bcClass.ctorFieldInits = ctorFieldInits;
    bcClass.modulePath = currentLoadingModule;

    for (const auto& si : staticFields) {
        bcClass.staticFieldNames.push_back(si.name);
        if (si.hasInit && si.exprFn != bc::kInvalidIndex) {
            bcClass.staticFieldInitExprs.push_back({si.name, si.exprFn});
        }
    }

    for (auto& tup : methods) {
        const std::string& mname = std::get<0>(tup);
        bool isStatic = std::get<1>(tup);
        const std::vector<std::string>& params = std::get<2>(tup);
        uint32_t fnIdx = std::get<3>(tup);

        BCMethod m;
        m.functionIndex = fnIdx;
        m.paramNames = params;
        m.isStatic = isStatic;

        if (isStatic) bcClass.staticMethods[mname] = m;
        else bcClass.methods[mname] = m;
    }

    classes[className] = std::move(bcClass);
    if (!currentLoadingModule.empty()) classToModule[className] = currentLoadingModule;

    // Also mirror interpreter classRegistry enough for object creation / dot access naming.
    ClassDef def;
    def.name = className;
    def.parentClass = parentName;
    for (const auto& f : fields) def.fields.push_back({f, TypeAnnotation()});
    runtime.classRegistry[className] = def;

    // Initialize static fields
    for (const auto& sf : staticFields) {
        std::string key = className + "::" + sf.name;
        if (sf.hasInit && sf.exprFn != bc::kInvalidIndex) {
            Value v = runFunction(sf.exprFn, {}, nullptr, nullptr, error);
            runtime.staticFields[key] = v;
        } else {
            runtime.staticFields[key] = Value{};
        }
    }

    return true;
}

bool BytecodeVM::runModuleInit(const std::string& modulePath, std::string* error) {
    if (executedModules.find(modulePath) != executedModules.end()) return true;
    auto it = prog->modules.find(modulePath);
    if (it == prog->modules.end()) {
        if (error) *error = "Module not in bytecode program: " + modulePath;
        return false;
    }
    executedModules.insert(modulePath);
    runFunction(it->second, {}, nullptr, nullptr, error);
    return error == nullptr || error->empty();
}

bool BytecodeVM::execImportString(const std::string& importStr, std::string* error) {
    // Mirror Runtime::executeNode Import parsing, but for file modules execute embedded init.
    std::string alias;
    std::string modulePath;
    bool isWildcard = false;
    std::vector<std::string> specificItems;

    std::string s = importStr;
    size_t bracePos = s.find(":{");
    if (bracePos != std::string::npos) {
        std::string beforeBrace = s.substr(0, bracePos);
        size_t colonPos = beforeBrace.find(':');
        if (colonPos != std::string::npos) {
            alias = beforeBrace.substr(0, colonPos);
            modulePath = beforeBrace.substr(colonPos + 1);
        } else {
            modulePath = beforeBrace;
        }
        size_t endBrace = s.find('}', bracePos);
        if (endBrace != std::string::npos) {
            std::string itemsStr = s.substr(bracePos + 2, endBrace - bracePos - 2);
            size_t pos = 0;
            while (pos < itemsStr.length()) {
                size_t commaPos = itemsStr.find(',', pos);
                if (commaPos == std::string::npos) commaPos = itemsStr.length();
                specificItems.push_back(itemsStr.substr(pos, commaPos - pos));
                pos = commaPos + 1;
            }
        }
    } else {
        size_t colonPos = s.find(':');
        if (colonPos != std::string::npos) {
            alias = s.substr(0, colonPos);
            s = s.substr(colonPos + 1);
        }
        if (s.size() >= 2 && s.substr(s.size() - 2) == ".*") {
            modulePath = s.substr(0, s.size() - 2);
            isWildcard = true;
        } else {
            size_t lastDot = s.rfind('.');
            if (lastDot != std::string::npos) {
                modulePath = s.substr(0, lastDot);
                specificItems.push_back(s.substr(lastDot + 1));
            } else {
                modulePath = s;
                isWildcard = true;
            }
        }
    }

    if (FunctionRegistry::instance().hasNamespace(modulePath)) {
        if (!alias.empty()) {
            runtime.imports[alias] = modulePath;
        } else if (isWildcard) {
            runtime.imports[modulePath] = modulePath;
        } else {
            for (const auto& item : specificItems) runtime.imports[item] = modulePath;
        }
        return true;
    }

    if (!runModuleInit(modulePath, error)) return false;

    if (!alias.empty()) runtime.imports[alias] = modulePath;
    else runtime.imports[modulePath] = modulePath;

    return true;
}

Value BytecodeVM::callName(const std::string& name, const std::vector<Value>& args, std::string* error) {
    if (name == "__bc_import") {
        if (args.size() != 1 || !std::holds_alternative<std::string>(args[0])) {
            if (error) *error = "__bc_import expects 1 string arg";
            return Value{};
        }
        std::string importStr = std::get<std::string>(args[0]);
        if (!execImportString(importStr, error)) {
            return Value{};
        }
        return Value(true);
    }

    // Lambda var name mapping (mirror evaluate.cpp)
    auto lIt = runtime.varToLambdaId.find(name);
    if (lIt != runtime.varToLambdaId.end()) {
        auto lit = lambdas.find(lIt->second);
        if (lit != lambdas.end()) {
            return runFunction(lit->second.functionIndex, args, &lit->second.captures, nullptr, error);
        }
    }

    if (runtime.hasVariable(name)) {
        Value vv = runtime.getVariable(name);
        if (std::holds_alternative<std::string>(vv)) {
            std::string possible = std::get<std::string>(vv);
            if (possible.rfind("__lambda_", 0) == 0) {
                auto it = lambdas.find(possible);
                if (it != lambdas.end()) {
                    return runFunction(it->second.functionIndex, args, &it->second.captures, nullptr, error);
                }
            }
        }
    }

    // method call on object: prefix.member
    size_t dotPos = name.find('.');
    if (dotPos != std::string::npos) {
        std::string prefix = name.substr(0, dotPos);
        std::string member = name.substr(dotPos + 1);

        auto objIt = runtime.objects.find(prefix);
        if (objIt == runtime.objects.end()) {
            if (runtime.hasVariable(prefix)) {
                Value varVal = runtime.getVariable(prefix);
                if (std::holds_alternative<std::string>(varVal)) {
                    std::string objId = std::get<std::string>(varVal);
                    objIt = runtime.objects.find(objId);
                }
            }
        }

        if (objIt != runtime.objects.end()) {
            auto obj = objIt->second;
            std::string className = obj->getClassName();

            if (args.empty() && obj->hasField(member)) return obj->getField(member);

            auto cIt = classes.find(className);
            if (cIt != classes.end()) {
                auto mIt = cIt->second.methods.find(member);
                if (mIt != cIt->second.methods.end() && mIt->second.functionIndex != bc::kInvalidIndex) {
                    // Set this
                    std::string thisObj = objIt->first;
                    return runFunction(mIt->second.functionIndex, args, nullptr, &thisObj, error);
                }
            }

            // fallback field
            if (obj->hasField(member)) return obj->getField(member);
        }
    }

    // Built-in function registry
    if (name.find('.') == std::string::npos) {
        for (const auto& pair : runtime.imports) {
            std::string full = pair.second + "." + name;
            if (FunctionRegistry::instance().exists(full)) return FunctionRegistry::instance().call(full, args);
        }
        runtime.notifyError("call", "Unknown function: " + name, -1, -1, false);
        return Value{};
    }

    std::string resolved = runtime.resolveFunctionName(name);
    if (FunctionRegistry::instance().exists(resolved)) return FunctionRegistry::instance().call(resolved, args);

    runtime.notifyError("call", "Unknown function: " + resolved, -1, -1, false);
    return Value{};
}

Value BytecodeVM::newObject(const std::string& fullClassName, const std::vector<Value>& args, std::string* error) {
    std::string actualClassName = fullClassName;

    // qualified new: module.Class
    size_t dotPos = fullClassName.rfind('.');
    if (dotPos != std::string::npos) {
        std::string moduleRef = fullClassName.substr(0, dotPos);
        std::string className = fullClassName.substr(dotPos + 1);

        std::string modulePath;
        auto aliasIt = runtime.imports.find(moduleRef);
        if (aliasIt != runtime.imports.end()) modulePath = aliasIt->second;
        else modulePath = moduleRef;

        bool found = false;
        for (const auto& kv : classToModule) {
            if (kv.first == className && kv.second == modulePath) { found = true; break; }
        }
        if (!found) {
            Logger::instance().log(LogLevel::ERROR, "Class '" + className + "' not found in module '" + modulePath + "'");
            return Value(fullClassName + "_null");
        }
        actualClassName = className;
    }

    auto cIt = classes.find(actualClassName);
    if (cIt == classes.end()) {
        Logger::instance().log(LogLevel::ERROR, "Class '" + actualClassName + "' not found");
        return Value(fullClassName + "_null");
    }

    // Create instance
    auto instance = runtime.createObject(actualClassName);

    // Initialize fields (parent then own)
    if (!cIt->second.parent.empty()) {
        auto pIt = classes.find(cIt->second.parent);
        if (pIt != classes.end()) {
            for (const auto& f : pIt->second.fields) instance->setField(f, Value{});
        }
    }
    for (const auto& f : cIt->second.fields) instance->setField(f, Value{});

    static int objectCounter = 0;
    std::string objId = actualClassName + "_" + std::to_string(objectCounter++);
    runtime.objects[objId] = instance;

    // constructor
    if (cIt->second.hasConstructor && cIt->second.ctorFunctionIndex != bc::kInvalidIndex) {
        auto savedVars = runtime.variables;
        auto savedThis = runtime.currentThisObject;
        runtime.currentThisObject = objId;

        // bind params
        for (size_t i = 0; i < cIt->second.ctorParams.size() && i < args.size(); ++i) {
            runtime.variables[cIt->second.ctorParams[i]] = args[i];
        }

        // field initializers
        for (const auto& init : cIt->second.ctorFieldInits) {
            Value v = runFunction(init.second, {}, nullptr, &runtime.currentThisObject, error);
            instance->setField(init.first, v);
        }

        runFunction(cIt->second.ctorFunctionIndex, args, nullptr, &runtime.currentThisObject, error);

        runtime.variables = savedVars;
        runtime.currentThisObject = savedThis;
    }

    return Value(objId);
}

Value BytecodeVM::runFunction(uint32_t functionIndex, const std::vector<Value>& args,
                              const std::unordered_map<std::string, Value>* overrideVars,
                              const std::string* overrideThis,
                              std::string* error) {
    if (!prog || functionIndex >= prog->functions.size()) {
        if (error) *error = "Invalid function index";
        return Value{};
    }

    const bc::Function& fn = prog->functions[functionIndex];
    const std::vector<uint8_t>& code = fn.code;
    
    // Check if we have pre-decoded metadata available
    const bool useCachedMetadata = fn.hasCachedMetadata && !fn.instructionCache.empty();

    // Save/override variable scope for calls (mirrors interpreter behavior)
    auto savedVars = runtime.variables;
    auto savedThis = runtime.currentThisObject;

    if (overrideVars) runtime.variables = *overrideVars;
    if (overrideThis) runtime.currentThisObject = *overrideThis;

    // Bind parameters into runtime.variables (like interpreter)
    for (size_t i = 0; i < fn.paramNameStrings.size() && i < args.size(); ++i) {
        runtime.variables[str(fn.paramNameStrings[i])] = args[i];
    }

    // Slot locals (fast-path) - pre-reserve to avoid reallocations
    std::vector<Value> locals;
    const size_t localsSize = fn.localNameStrings.size();
    locals.reserve(std::max(localsSize, vm_config::kDefaultLocalsReserve));
    locals.resize(localsSize);
    // Initialize param slots if present
    for (size_t i = 0; i < fn.paramNameStrings.size() && i < args.size(); ++i) {
        if (i < locals.size()) locals[i] = args[i];
    }

    // Pre-reserve stack to avoid frequent reallocations in hot loops
    std::vector<Value> stack;
    stack.reserve(vm_config::kDefaultStackReserve);

    struct TryFrame {
        size_t catchIp = 0;
        size_t finallyIp = 0;
        bool hasFinally = false;
        std::string catchVarName;
        std::string catchType;
    };
    std::vector<TryFrame> tryStack;
    tryStack.reserve(vm_config::kDefaultTryStackReserve);

    bool pendingRethrow = false;
    LanguageException pendingExc("Exception", "");

    size_t ip = 0;

    // Optimized pop: use move semantics to avoid copies
    auto pop = [&]() -> Value {
        if (stack.empty()) {
            throw LanguageException("RuntimeError", "VM stack underflow");
        }
        Value v = std::move(stack.back());
        stack.pop_back();
        return v;
    };

    auto push = [&](Value v) { stack.push_back(std::move(v)); };

    auto restoreAndThrow = [&](const LanguageException& ex) -> void {
        runtime.variables = savedVars;
        runtime.currentThisObject = savedThis;
        throw ex;
    };

    auto bindCatchObject = [&](const std::string& varName, const LanguageException& ex) {
        auto excObj = std::make_shared<ObjectInstance>(ex.getType());
        excObj->setField("message", Value(ex.getMessage()));
        excObj->setField("type", Value(ex.getType()));
        for (const auto& kv : ex.getFields()) excObj->setField(kv.first, kv.second);
        runtime.objects[varName] = excObj;
    };

    auto handleException = [&](const LanguageException& ex) -> bool {
        // Walk try stack from innermost to outermost.
        while (!tryStack.empty()) {
            TryFrame& tf = tryStack.back();

            // Type match => jump to catch (leave TRY frame for catch's TRY_POP).
            if (tf.catchType == "Exception" || ex.getType() == tf.catchType) {
                bindCatchObject(tf.catchVarName, ex);
                ip = tf.catchIp;
                pendingRethrow = false;
                return true;
            }

            // Type mismatch: if finally exists, pop this TRY and jump to finally, then rethrow.
            if (tf.hasFinally) {
                pendingRethrow = true;
                pendingExc = ex;
                size_t finallyIp = tf.finallyIp;
                tryStack.pop_back();
                ip = finallyIp;
                return true;
            }

            // No finally, pop and continue searching outer TRY.
            tryStack.pop_back();
        }
        return false;
    };

    auto raise = [&](const LanguageException& ex) {
        if (!handleException(ex)) {
            restoreAndThrow(ex);
        }
    };

    while (ip < code.size()) {
        const size_t instructionStart = ip;
        bool ok = true;
        bc::OpCode op = (bc::OpCode)readU8(code, ip, &ok);
        if (!ok) {
            if (error) *error = "Bytecode decode error";
            runtime.variables = savedVars;
            runtime.currentThisObject = savedThis;
            return Value{};
        }
        
        // ====================================================================
        // Optimized Instruction Decoding Path
        // ====================================================================
        // If metadata cache is available, lookup pre-decoded instruction data
        // to avoid repeated readU32/readI32 calls. This significantly improves
        // performance for hot-path instructions in tight loops.
        //
        // The metadata cache is populated during bytecode loading (see
        // extractInstructionMetadata in bytecode.cpp) and contains pre-decoded
        // immediates and pre-computed jump targets.
        //
        // For instructions without cached metadata, we fall back to the
        // traditional runtime decoding path (calling readU32, readI32, etc).
        // ====================================================================
        const bc::InstructionMeta* meta = nullptr;
        if (useCachedMetadata) {
            // Linear lookup is acceptable for small functions (typical case)
            // For very large functions, binary search could be beneficial
            for (const auto& m : fn.instructionCache) {
                if (m.ip == instructionStart) {
                    meta = &m;
                    break;
                }
            }
        }

        try {
            switch (op) {
            case bc::OpCode::NOP:
                break;

            case bc::OpCode::PUSH_INT32: {
                int32_t v;
                if (VM_LIKELY(meta != nullptr)) {
                    // Use pre-decoded value (still need to advance IP manually)
                    v = static_cast<int32_t>(meta->imm0);
                    ip += 4; // Skip the 4-byte immediate
                } else {
                    v = readI32(code, ip, &ok);
                    if (!ok) throw std::runtime_error("Bytecode decode error");
                }
                push(Value((int)v));
                break;
            }

            case bc::OpCode::PUSH_DOUBLE64: {
                double v = readF64(code, ip, &ok);
                if (!ok) throw std::runtime_error("Bytecode decode error");
                push(Value(v));
                break;
            }

            case bc::OpCode::PUSH_BOOL: {
                uint8_t b;
                if (VM_LIKELY(meta != nullptr)) {
                    b = static_cast<uint8_t>(meta->imm0);
                    ip += 1; // Skip the 1-byte immediate
                } else {
                    b = readU8(code, ip, &ok);
                    if (!ok) throw std::runtime_error("Bytecode decode error");
                }
                push(Value(b != 0));
                break;
            }

            case bc::OpCode::PUSH_STRING: {
                uint32_t sidx;
                if (VM_LIKELY(meta != nullptr)) {
                    sidx = meta->imm0;
                    ip += 4; // Skip the 4-byte immediate
                } else {
                    sidx = readU32(code, ip, &ok);
                    if (!ok) throw std::runtime_error("Bytecode decode error");
                }
                push(Value(str(sidx)));
                break;
            }

            case bc::OpCode::POP:
                (void)pop();
                break;

            case bc::OpCode::LOAD_VAR: {
                uint32_t nidx;
                if (VM_LIKELY(meta != nullptr)) {
                    nidx = meta->imm0;
                    ip += 4; // Skip the 4-byte immediate
                } else {
                    nidx = readU32(code, ip, &ok);
                }
                std::string name = str(nidx);

                // Mirror Runtime::evaluate Identifier dot access
                size_t dotPos = name.find('.');
                if (dotPos != std::string::npos) {
                    std::string objName = name.substr(0, dotPos);
                    std::string fieldName = name.substr(dotPos + 1);

                    auto objIt = runtime.objects.find(objName);
                    if (objIt != runtime.objects.end()) {
                        push(objIt->second->getField(fieldName));
                        break;
                    }
                    if (runtime.hasVariable(objName)) {
                        Value vv = runtime.getVariable(objName);
                        if (std::holds_alternative<std::string>(vv)) {
                            std::string objId = std::get<std::string>(vv);
                            auto idIt = runtime.objects.find(objId);
                            if (idIt != runtime.objects.end()) {
                                push(idIt->second->getField(fieldName));
                                break;
                            }
                        }
                    }
                }

                push(runtime.getVariable(name));
                break;
            }

            case bc::OpCode::LOAD_SLOT: {
                uint16_t slot;
                if (VM_LIKELY(meta != nullptr)) {
                    slot = static_cast<uint16_t>(meta->imm0);
                    ip += 2; // Skip the 2-byte immediate
                } else {
                    slot = readU16(code, ip, &ok);
                    if (!ok) throw std::runtime_error("Bytecode decode error");
                }
                if (slot >= locals.size()) {
                    throw LanguageException("RuntimeError", "Local slot index out of bounds: " + std::to_string(slot));
                }
                push(locals[slot]);
                break;
            }

            case bc::OpCode::STORE_VAR: {
                uint32_t nidx;
                if (VM_LIKELY(meta != nullptr)) {
                    nidx = meta->imm0;
                    ip += 4; // Skip the 4-byte immediate
                } else {
                    nidx = readU32(code, ip, &ok);
                }
                std::string name = str(nidx);
                Value v = pop();
                runtime.setVariable(name, v);
                break;
            }

            case bc::OpCode::STORE_SLOT: {
                uint16_t slot;
                if (VM_LIKELY(meta != nullptr)) {
                    slot = static_cast<uint16_t>(meta->imm0);
                    ip += 2; // Skip the 2-byte immediate
                } else {
                    slot = readU16(code, ip, &ok);
                    if (!ok) throw std::runtime_error("Bytecode decode error");
                }
                Value v = pop();
                if (slot >= locals.size()) {
                    throw LanguageException("RuntimeError", "Local slot index out of bounds: " + std::to_string(slot));
                }
                locals[slot] = v;
                // Keep runtime.variables in sync for features that still consult it
                if (slot < fn.localNameStrings.size()) {
                    runtime.setVariable(str(fn.localNameStrings[slot]), v);
                }
                break;
            }

            case bc::OpCode::DECLARE_ARRAY: {
                uint32_t nidx = readU32(code, ip, &ok);
                uint16_t count = readU16(code, ip, &ok);
                if (!ok) throw std::runtime_error("Bytecode decode error");
                std::string varName = str(nidx);

                std::vector<Value> arrayVec;
                arrayVec.resize(count);
                for (int i = (int)count - 1; i >= 0; --i) arrayVec[(size_t)i] = pop();

                size_t arrayId = runtime.nextArrayId++;
                if (arrayId >= runtime.arrayStorage.size()) {
                    runtime.arrayStorage.resize(arrayId + 1);
                }
                runtime.varToArrayId[varName] = arrayId;
                runtime.arrayStorage[arrayId] = arrayVec;

                runtime.setVariable(varName, Value(ArrayRef{arrayId}));
                break;
            }

            case bc::OpCode::DECLARE_DICT: {
                uint32_t nidx = readU32(code, ip, &ok);
                uint16_t count = readU16(code, ip, &ok);
                std::string varName = str(nidx);

                std::vector<std::string> keys;
                keys.reserve(count);
                for (uint16_t i = 0; i < count; ++i) keys.push_back(str(readU32(code, ip, &ok)));
                if (!ok) throw std::runtime_error("Bytecode decode error");

                std::unordered_map<std::string, Value> dictMap;
                dictMap.reserve(count);
                std::vector<Value> values;
                values.resize(count);
                for (int i = (int)count - 1; i >= 0; --i) values[(size_t)i] = pop();
                for (size_t i = 0; i < count; ++i) dictMap[keys[i]] = values[i];

                size_t dictId = runtime.nextDictId++;
                if (dictId >= runtime.dictStorage.size()) {
                    runtime.dictStorage.resize(dictId + 1);
                }
                runtime.varToDictId[varName] = dictId;
                runtime.dictStorage[dictId] = dictMap;

                runtime.setVariable(varName, Value(DictRef{dictId}));
                break;
            }

            case bc::OpCode::MAKE_ARRAY_EXPR: {
                uint16_t count = readU16(code, ip, &ok);
                if (!ok) throw std::runtime_error("Bytecode decode error");

                std::vector<Value> arrayVec;
                arrayVec.resize(count);
                for (int i = (int)count - 1; i >= 0; --i) arrayVec[(size_t)i] = pop();

                size_t arrayId = runtime.nextArrayId++;
                if (arrayId >= runtime.arrayStorage.size()) {
                    runtime.arrayStorage.resize(arrayId + 1);
                }
                runtime.arrayStorage[arrayId] = std::move(arrayVec);
                push(Value(ArrayRef{arrayId}));
                break;
            }

            case bc::OpCode::MAKE_DICT_EXPR: {
                uint16_t count = readU16(code, ip, &ok);
                std::vector<std::string> keys;
                keys.reserve(count);
                for (uint16_t i = 0; i < count; ++i) keys.push_back(str(readU32(code, ip, &ok)));
                if (!ok) throw std::runtime_error("Bytecode decode error");

                std::vector<Value> values;
                values.resize(count);
                for (int i = (int)count - 1; i >= 0; --i) values[(size_t)i] = pop();

                std::unordered_map<std::string, Value> dictMap;
                dictMap.reserve(count);
                for (size_t i = 0; i < (size_t)count; ++i) {
                    dictMap[keys[i]] = values[i];
                }

                size_t dictId = runtime.nextDictId++;
                if (dictId >= runtime.dictStorage.size()) {
                    runtime.dictStorage.resize(dictId + 1);
                }
                runtime.dictStorage[dictId] = std::move(dictMap);
                push(Value(DictRef{dictId}));
                break;
            }

            case bc::OpCode::DECLARE_LAMBDA: {
                uint32_t nidx = readU32(code, ip, &ok);
                uint32_t fidx = readU32(code, ip, &ok);
                if (!ok) throw std::runtime_error("Bytecode decode error");

                std::string lambdaId = "__lambda_" + std::to_string(nextLambdaId++);
                BCLambda l;
                l.functionIndex = fidx;
                l.captures = runtime.variables;
                lambdas[lambdaId] = std::move(l);

                std::string varName = str(nidx);
                runtime.varToLambdaId[varName] = lambdaId;
                runtime.setVariable(varName, Value(lambdaId));
                break;
            }

            case bc::OpCode::MAKE_LAMBDA: {
                uint32_t fidx = readU32(code, ip, &ok);
                if (!ok) throw std::runtime_error("Bytecode decode error");
                std::string lambdaId = "__lambda_" + std::to_string(nextLambdaId++);
                BCLambda l;
                l.functionIndex = fidx;
                l.captures = runtime.variables;
                lambdas[lambdaId] = std::move(l);
                push(Value(lambdaId));
                break;
            }

            case bc::OpCode::BINARY_OP: {
                bc::BinaryOp bop = (bc::BinaryOp)readU8(code, ip, &ok);
                if (!ok) throw std::runtime_error("Bytecode decode error");
                Value right = pop();
                Value left = pop();
                push(applyBinary(left, right, bop));
                break;
            }

            case bc::OpCode::UNARY_OP: {
                bc::UnaryOp uop = (bc::UnaryOp)readU8(code, ip, &ok);
                if (!ok) throw std::runtime_error("Bytecode decode error");
                Value v = pop();
                push(applyUnary(v, uop));
                break;
            }

            case bc::OpCode::INDEX_GET: {
                uint32_t nidx = readU32(code, ip, &ok);
                if (!ok) throw std::runtime_error("Bytecode decode error");
                Value idxV = pop();
                push(indexGet(str(nidx), idxV));
                break;
            }

            case bc::OpCode::JUMP: {
                size_t target;
                if (VM_LIKELY(meta != nullptr)) {
                    // Pre-computed absolute target (validated at load time)
                    target = meta->imm0;
                    ip += 4; // Skip the 4-byte relative offset
                } else {
                    int32_t rel = readI32(code, ip, &ok);
                    if (!ok) throw std::runtime_error("Bytecode decode error");
                    int64_t newIp = (int64_t)ip + rel;
                    if (newIp < 0 || (size_t)newIp > code.size()) {
                        throw LanguageException("RuntimeError", "Invalid jump target: out of bounds");
                    }
                    target = (size_t)newIp;
                }
                ip = target;
                break;
            }

            case bc::OpCode::JUMP_IF_FALSE: {
                size_t target;
                if (VM_LIKELY(meta != nullptr)) {
                    target = meta->imm0;
                    ip += 4; // Skip the 4-byte relative offset
                } else {
                    int32_t rel = readI32(code, ip, &ok);
                    if (!ok) throw std::runtime_error("Bytecode decode error");
                    int64_t newIp = (int64_t)ip + rel;
                    if (newIp < 0 || (size_t)newIp > code.size()) {
                        throw LanguageException("RuntimeError", "Invalid jump target: out of bounds");
                    }
                    target = (size_t)newIp;
                }
                Value cond = pop();
                if (!evalCondition(cond)) {
                    ip = target;
                }
                break;
            }

            case bc::OpCode::JUMP_IF_TRUE: {
                size_t target;
                if (VM_LIKELY(meta != nullptr)) {
                    target = meta->imm0;
                    ip += 4; // Skip the 4-byte relative offset
                } else {
                    int32_t rel = readI32(code, ip, &ok);
                    if (!ok) throw std::runtime_error("Bytecode decode error");
                    int64_t newIp = (int64_t)ip + rel;
                    if (newIp < 0 || (size_t)newIp > code.size()) {
                        throw LanguageException("RuntimeError", "Invalid jump target: out of bounds");
                    }
                    target = (size_t)newIp;
                }
                Value cond = pop();
                if (evalCondition(cond)) {
                    ip = target;
                }
                break;
            }

            case bc::OpCode::CALL_NAME: {
                uint32_t nidx;
                uint8_t argc;
                if (VM_LIKELY(meta != nullptr)) {
                    nidx = meta->imm0;
                    argc = static_cast<uint8_t>(meta->imm1);
                    ip += 5; // Skip u32 + u8
                } else {
                    nidx = readU32(code, ip, &ok);
                    argc = readU8(code, ip, &ok);
                    if (!ok) throw std::runtime_error("Bytecode decode error");
                }

                std::vector<Value> callArgs;
                callArgs.reserve(vm_config::kDefaultArgsReserve);
                callArgs.resize(argc);
                for (int i = (int)argc - 1; i >= 0; --i) callArgs[(size_t)i] = std::move(pop());

                Value rv = callName(str(nidx), callArgs, error);
                push(std::move(rv));
                break;
            }

            case bc::OpCode::NEW_OBJECT: {
                uint32_t nidx;
                uint8_t argc;
                if (VM_LIKELY(meta != nullptr)) {
                    nidx = meta->imm0;
                    argc = static_cast<uint8_t>(meta->imm1);
                    ip += 5; // Skip u32 + u8
                } else {
                    nidx = readU32(code, ip, &ok);
                    argc = readU8(code, ip, &ok);
                    if (!ok) throw std::runtime_error("Bytecode decode error");
                }
                std::vector<Value> ctorArgs;
                ctorArgs.reserve(vm_config::kDefaultArgsReserve);
                ctorArgs.resize(argc);
                for (int i = (int)argc - 1; i >= 0; --i) ctorArgs[(size_t)i] = std::move(pop());

                Value obj = newObject(str(nidx), ctorArgs, error);
                push(std::move(obj));
                break;
            }

            case bc::OpCode::TRY_PUSH: {
                uint32_t catchIpAbs = readU32(code, ip, &ok);
                uint32_t finallyIpAbs = readU32(code, ip, &ok);
                bool hasFinally = readU8(code, ip, &ok) != 0;
                std::string catchVar = str(readU32(code, ip, &ok));
                std::string catchType = str(readU32(code, ip, &ok));
                if (!ok) throw std::runtime_error("Bytecode decode error");

                // Validate exception handler addresses
                if (catchIpAbs > code.size()) {
                    throw LanguageException("RuntimeError", "Invalid catch handler address: out of bounds");
                }
                if (hasFinally && finallyIpAbs > code.size()) {
                    throw LanguageException("RuntimeError", "Invalid finally handler address: out of bounds");
                }

                TryFrame tf;
                tf.catchIp = (size_t)catchIpAbs;
                tf.finallyIp = (size_t)finallyIpAbs;
                tf.hasFinally = hasFinally;
                tf.catchVarName = catchVar;
                tf.catchType = catchType;
                tryStack.push_back(std::move(tf));
                break;
            }

            case bc::OpCode::TRY_POP:
                if (!tryStack.empty()) tryStack.pop_back();
                break;

            case bc::OpCode::CATCH_CLEAR: {
                std::string varName = str(readU32(code, ip, &ok));
                if (!ok) throw std::runtime_error("Bytecode decode error");
                runtime.objects.erase(varName);
                break;
            }

            case bc::OpCode::THROW_VALUE: {
                Value v = pop();
                std::string msg = to_string(v);
                raise(LanguageException("Exception", msg));
                break;
            }

            case bc::OpCode::THROW_NEW: {
                uint32_t tIdx = readU32(code, ip, &ok);
                if (!ok) throw std::runtime_error("Bytecode decode error");
                Value v = pop();
                std::string msg = to_string(v);
                raise(LanguageException(str(tIdx), msg));
                break;
            }

            case bc::OpCode::FINALLY_END: {
                if (pendingRethrow) {
                    pendingRethrow = false;
                    raise(pendingExc);
                }
                break;
            }

            case bc::OpCode::DEF_CLASS: {
                if (!execDefClass(code, ip, error)) return Value{};
                break;
            }

            case bc::OpCode::DEF_INTERFACE: {
                if (!execDefInterface(code, ip, error)) return Value{};
                break;
            }

            case bc::OpCode::SET_CURRENT_MODULE: {
                uint32_t sidx = readU32(code, ip, &ok);
                if (!ok) throw std::runtime_error("Bytecode decode error");
                currentLoadingModule = str(sidx);
                break;
            }

            case bc::OpCode::CLEAR_CURRENT_MODULE:
                currentLoadingModule.clear();
                break;

            case bc::OpCode::RETURN_VALUE: {
                Value rv = pop();
                runtime.variables = savedVars;
                runtime.currentThisObject = savedThis;
                return rv;
            }

            case bc::OpCode::RETURN_VOID:
                runtime.variables = savedVars;
                runtime.currentThisObject = savedThis;
                return Value{};
            }
        } catch (const LanguageException& ex) {
            // Uncaught from a callee (lambda/method/constructor): try to handle here.
            raise(ex);
        }
    }

    runtime.variables = savedVars;
    runtime.currentThisObject = savedThis;
    return Value{};
}

bool BytecodeVM::run(const bc::Program& program, std::string* error) {
    prog = &program;

    runtime.executionMode = "bytecode";
    runtime.state = RuntimeState::EXECUTING;
    // Note: program-level hooks will register during execution; start is mainly for host-registered hooks.
    runtime.emitHook("start", {Value(runtime.executionMode)});

    // Allow extensions to invoke bytecode lambdas by id (e.g. system.collection.map).
    runtime.setExternalLambdaInvoker([this](const std::string& lambdaId, const std::vector<Value>& args) -> Value {
        auto it = lambdas.find(lambdaId);
        if (it == lambdas.end()) return Value{};
        std::string err;
        return runFunction(it->second.functionIndex, args, &it->second.captures, nullptr, &err);
    });

    // Initialize runtime state similarly to interpreter main
    // (extensions are already available via FunctionRegistry setup in main)

    try {
        (void)runFunction(program.entryFunction, {}, nullptr, nullptr, error);
    } catch (const std::exception& ex) {
        if (error) *error = std::string("VM fatal: ") + ex.what();
        runtime.notifyError("vm", error ? *error : std::string("VM fatal"), -1, -1, true);
        runtime.emitHook("end", {Value(runtime.executionMode), Value(std::string("error"))});
        runtime.setExternalLambdaInvoker(nullptr);
        return false;
    }

    if (error && !error->empty()) {
        runtime.notifyError("vm", *error, -1, -1, true);
        runtime.emitHook("end", {Value(runtime.executionMode), Value(std::string("error"))});
        runtime.setExternalLambdaInvoker(nullptr);
        return false;
    }

    runtime.state = RuntimeState::COMPLETED;
    runtime.emitHook("end", {Value(runtime.executionMode), Value(std::string("completed"))});

    runtime.setExternalLambdaInvoker(nullptr);
    return true;
}
