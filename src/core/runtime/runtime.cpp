// ============================================================================
// Runtime - Core Implementation
// Handles core runtime initialization, module loading, and variable management
// ============================================================================

#include "runtime.h"
#include "types.h"
#include "parser.h"
#include "lexer.h"
#include "syntax.h"
#include "logger.h"
#include "function_registry.h"

#include <fstream>
#include <filesystem>
#include <algorithm>
#include <dlfcn.h>
#include <cctype>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#elif defined(__linux__)
#include <unistd.h>
#endif

namespace fs = std::filesystem;

Runtime* global_runtime_ptr __attribute__((visibility("default"))) = nullptr;

static fs::path getExecutableDir() {
#if defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string buf(size, '\0');
    if (_NSGetExecutablePath(buf.data(), &size) == 0) {
        std::error_code ec;
        fs::path p = fs::weakly_canonical(fs::path(buf.c_str()), ec);
        if (!ec) return p.parent_path();
        return fs::path(buf.c_str()).parent_path();
    }
    return fs::current_path();
#elif defined(__linux__)
    std::string buf(4096, '\0');
    ssize_t n = ::readlink("/proc/self/exe", buf.data(), buf.size() - 1);
    if (n > 0) {
        buf[(size_t)n] = '\0';
        std::error_code ec;
        fs::path p = fs::weakly_canonical(fs::path(buf.c_str()), ec);
        if (!ec) return p.parent_path();
        return fs::path(buf.c_str()).parent_path();
    }
    return fs::current_path();
#else
    return fs::current_path();
#endif
}

static fs::path findExtensionsDir() {
    // Preferred: next to the executable (./build/quartz -> ./build/extensions)
    {
        fs::path candidate = getExecutableDir() / "extensions";
        if (fs::exists(candidate)) return candidate;
    }

    // Fallback: cwd-relative repo layout
    {
        fs::path candidate = fs::path("build") / "extensions";
        if (fs::exists(candidate)) return candidate;
    }

    return {};
}

// ============================================================================
// Core Runtime Methods
// ============================================================================

void Runtime::setSourcePath(const std::string& path) {
    sourceDirectory = fs::path(path).parent_path().string();
}

void Runtime::setSourceDirectory(const std::string& dir) {
    sourceDirectory = dir;
}

void Runtime::execute(const AST& ast) {
    executionMode = "interp";
    state = RuntimeState::EXECUTING;
    emitHook("start", {Value(executionMode)});

    for (const auto& node : ast.nodes) {
        if (state == RuntimeState::HALTED) break;
        executeNode(node);
    }

    if (state == RuntimeState::HALTED) {
        emitHook("halt", {Value(executionMode), Value(std::string("halted"))});
        emitHook("end", {Value(executionMode), Value(std::string("halted"))});
        return;
    }

    state = RuntimeState::COMPLETED;
    emitHook("end", {Value(executionMode), Value(std::string("completed"))});
}

void Runtime::initialize() {
    global_runtime_ptr = this;
    initStandardLibrary();
}

std::vector<Value>* Runtime::getArray(const ArrayRef& ref) {
    auto it = arrayStorage.find(ref.id);
    if (it == arrayStorage.end()) return nullptr;
    return &it->second;
}

const std::vector<Value>* Runtime::getArray(const ArrayRef& ref) const {
    auto it = arrayStorage.find(ref.id);
    if (it == arrayStorage.end()) return nullptr;
    return &it->second;
}

std::unordered_map<std::string, Value>* Runtime::getDict(const DictRef& ref) {
    auto it = dictStorage.find(ref.id);
    if (it == dictStorage.end()) return nullptr;
    return &it->second;
}

const std::unordered_map<std::string, Value>* Runtime::getDict(const DictRef& ref) const {
    auto it = dictStorage.find(ref.id);
    if (it == dictStorage.end()) return nullptr;
    return &it->second;
}

Value Runtime::makeArray(std::vector<Value> elements) {
    std::string arrayId = "__array_" + std::to_string(nextArrayId++);
    arrayStorage[arrayId] = std::move(elements);
    return Value(ArrayRef{arrayId});
}

Value Runtime::makeDict(std::unordered_map<std::string, Value> entries) {
    std::string dictId = "__dict_" + std::to_string(nextDictId++);
    dictStorage[dictId] = std::move(entries);
    return Value(DictRef{dictId});
}

static inline void appendFormatted(std::string& out, const Runtime* rt, const Value& val, bool quoteStrings) {
    std::visit([&](auto&& arg) {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, int>) {
            out += std::to_string(arg);
        } else if constexpr (std::is_same_v<T, double>) {
            out += std::to_string(arg);
        } else if constexpr (std::is_same_v<T, bool>) {
            out += arg ? "true" : "false";
        } else if constexpr (std::is_same_v<T, std::string>) {
            if (quoteStrings) {
                out += '"';
                out += arg;
                out += '"';
            } else {
                out += arg;
            }
        } else if constexpr (std::is_same_v<T, ArrayRef>) {
            if (rt) out += rt->formatArrayById(arg.id, quoteStrings);
            else out += "<array>";
        } else if constexpr (std::is_same_v<T, DictRef>) {
            if (rt) out += rt->formatDictById(arg.id, quoteStrings);
            else out += "<dict>";
        }
    }, val);
}

std::string Runtime::formatArrayById(const std::string& arrayId, bool quoteStrings) const {
    auto it = arrayStorage.find(arrayId);
    if (it == arrayStorage.end()) return "[]";
    const auto& vec = it->second;
    std::string out;
    out.reserve(2 + vec.size() * 8);
    out += "[";
    for (size_t i = 0; i < vec.size(); ++i) {
        if (i) out += ", ";
        appendFormatted(out, this, vec[i], true /*quote strings inside containers*/);
    }
    out += "]";
    return out;
}

std::string Runtime::formatDictById(const std::string& dictId, bool quoteStrings) const {
    auto it = dictStorage.find(dictId);
    if (it == dictStorage.end()) return "{}";
    const auto& dict = it->second;
    std::string out;
    out.reserve(2 + dict.size() * 16);
    out += "{";
    bool first = true;
    for (const auto& kv : dict) {
        if (!first) out += ", ";
        first = false;
        out += '"';
        out += kv.first;
        out += "\": ";
        appendFormatted(out, this, kv.second, true /*quote strings inside containers*/);
    }
    out += "}";
    return out;
}

std::string Runtime::formatValue(const Value& v, bool quoteStrings) const {
    std::string out;
    out.reserve(32);
    appendFormatted(out, this, v, quoteStrings);
    return out;
}

Value Runtime::invokeLambdaValue(const Value& lambdaVal, const std::vector<Value>& args) {
    if (!std::holds_alternative<std::string>(lambdaVal)) return Value{};
    const std::string& id = std::get<std::string>(lambdaVal);
    if (id.rfind("__lambda_", 0) != 0) return Value{};

    if (externalLambdaInvoker) {
        return externalLambdaInvoker(id, args);
    }

    return invokeLambda(id, args);
}

void Runtime::setExternalLambdaInvoker(
    std::function<Value(const std::string& lambdaId, const std::vector<Value>& args)> invoker) {
    externalLambdaInvoker = std::move(invoker);
}

void Runtime::setVariable(const std::string& name, const Value& val) {
    variables[name] = val;
}

void Runtime::registerImport(const std::string& ns) {
    imports[ns] = ns;
}

std::string Runtime::resolveFunctionName(const std::string& name) {
    size_t dotPos = name.find('.');
    if (dotPos == std::string::npos) {
        return name;
    }
    std::string prefix = name.substr(0, dotPos);
    std::string rest = name.substr(dotPos + 1);
    auto it = imports.find(prefix);
    if (it != imports.end()) {
        return it->second + "." + rest;
    }
    return name;
}

// ============================================================================
// Standard Library and Extensions
// ============================================================================

void Runtime::initStandardLibrary() {
    loadExtensions();
    registerGlobalExceptionClass();
}

void Runtime::registerGlobalExceptionClass() {
    // Register base Exception class - available globally without import
    ClassDef exceptionClass;
    exceptionClass.name = "Exception";
    exceptionClass.fields.push_back({"message", TypeAnnotation("string")});
    exceptionClass.fields.push_back({"type", TypeAnnotation("string")});
    
    // Add getMessage method
    MethodDef getMessage;
    getMessage.name = "getMessage";
    getMessage.visibility = "public";
    getMessage.returnType = TypeAnnotation("string");
    getMessage.body = nullptr;  // Built-in implementation
    exceptionClass.methods.push_back(getMessage);
    
    // Add getType method
    MethodDef getType;
    getType.name = "getType";
    getType.visibility = "public";
    getType.returnType = TypeAnnotation("string");
    getType.body = nullptr;  // Built-in implementation
    exceptionClass.methods.push_back(getType);
    
    classRegistry["Exception"] = exceptionClass;
    
    // Register derived exception types
    ClassDef runtimeError = exceptionClass;
    runtimeError.name = "RuntimeError";
    runtimeError.parentClass = "Exception";
    classRegistry["RuntimeError"] = runtimeError;
    
    ClassDef valueError = exceptionClass;
    valueError.name = "ValueError";
    valueError.parentClass = "Exception";
    classRegistry["ValueError"] = valueError;
    
    ClassDef typeError = exceptionClass;
    typeError.name = "TypeError";
    typeError.parentClass = "Exception";
    classRegistry["TypeError"] = typeError;
    
    ClassDef indexError = exceptionClass;
    indexError.name = "IndexError";
    indexError.parentClass = "Exception";
    classRegistry["IndexError"] = indexError;
    
    ClassDef nullError = exceptionClass;
    nullError.name = "NullError";
    classRegistry["NullError"] = nullError;
}

void Runtime::loadExtensions() {
    // Look for extensions in the build directory
    fs::path base_ext_dir = findExtensionsDir();
    if (base_ext_dir.empty() || !fs::exists(base_ext_dir)) {
        return;
    }
    
    try {
        // Iterate through each extension subdirectory
        for (auto& ext_dir : fs::directory_iterator(base_ext_dir)) {
            if (!fs::is_directory(ext_dir)) continue;
            
            // Look for shared library files in each extension directory
            // .dylib on macOS, .so on Linux
            for (auto& p : fs::recursive_directory_iterator(ext_dir)) {
                std::string ext = p.path().extension().string();
                if (ext == ".dylib" || ext == ".so" || ext == ".dll") {
                    void* handle = dlopen(p.path().c_str(), RTLD_LAZY | RTLD_GLOBAL);
                    if (handle) {
                        // Try with underscore first (macOS convention)
                        auto init = (void (*)(FunctionRegistry&)) dlsym(handle, "_init_extension");
                        if (!init) {
                            // Try without underscore
                            init = (void (*)(FunctionRegistry&)) dlsym(handle, "init_extension");
                        }
                        if (init) {
                            init(FunctionRegistry::instance());
                        }
                    }
                }
            }
        }
    } catch (const std::exception& e) {
        Logger::instance().log(LogLevel::DEBUG, std::string("Exception in loadExtensions: ") + e.what());
    }
}

bool Runtime::isExtensionLoaded(const std::string& ns) const {
    return FunctionRegistry::instance().hasNamespace(ns);
}

// ============================================================================
// Module Loading
// ============================================================================

bool Runtime::loadModule(const std::string& modulePath) {
    // Convert module path (e.g., "mymodule.utils") to directory path (e.g., "mymodule/utils")
    std::string dirPath = modulePath;
    std::replace(dirPath.begin(), dirPath.end(), '.', '/');
    
    // Build full path relative to source file directory
    fs::path fullPath = fs::path(sourceDirectory) / dirPath;
    
    // Check if directory exists
    if (!fs::exists(fullPath) || !fs::is_directory(fullPath)) {
        return false;
    }
    
    // Track loaded modules to prevent circular imports
    if (loadedModules.find(modulePath) != loadedModules.end()) {
        return true;  // Already loaded
    }
    loadedModules.insert(modulePath);
    
    // Set current loading module for class association
    std::string previousModule = currentLoadingModule;
    currentLoadingModule = modulePath;
    
    Logger::instance().log(LogLevel::DEBUG, "Loading module from: " + fullPath.string());
    
    // Load all .qz files in the directory
    std::vector<fs::path> files;
    for (const auto& entry : fs::directory_iterator(fullPath)) {
        if (entry.path().extension() == ".qz") {
            files.push_back(entry.path());
        }
    }
    
    // Sort files for deterministic loading order
    std::sort(files.begin(), files.end());
    
    // Parse and execute each file
    for (const auto& file : files) {
        std::ifstream sourceFile(file);
        if (!sourceFile.is_open()) {
            Logger::instance().log(LogLevel::ERROR, "Could not open module file: " + file.string());
            continue;
        }
        
        std::string sourceCode((std::istreambuf_iterator<char>(sourceFile)), std::istreambuf_iterator<char>());
        sourceFile.close();
        
        Logger::instance().log(LogLevel::DEBUG, "Parsing module file: " + file.filename().string());
        
        // Tokenize and parse
        SyntaxConfig config;
        Lexer lexer(sourceCode, config);
        std::vector<Token> tokens = lexer.tokenize();
        
        Parser parser(tokens, sourceCode);
        
        try {
            AST ast = parser.parse();
            
            // Execute the module's AST (this will register classes, functions, etc.)
            for (const auto& node : ast.nodes) {
                if (state == RuntimeState::HALTED) break;
                executeNode(node);
            }
        } catch (const std::exception& ex) {
            Logger::instance().log(LogLevel::ERROR, "Error loading module file " + file.string() + ": " + ex.what());
            currentLoadingModule = previousModule;
            return false;
        }
    }
    
    // Restore previous module context
    currentLoadingModule = previousModule;
    
    return true;
}

// ============================================================================
// Runtime Control
// ============================================================================

void Runtime::halt() {
    if (state == RuntimeState::EXECUTING) {
        state = RuntimeState::HALTED;
        emitHook("halt", {Value(executionMode), Value(std::string("halt()"))});
    }
}

// ============================================================================
// Hooks + Global Error Callbacks
// ============================================================================

static inline std::string normalizeEvent(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

static inline bool isLambdaIdValue(const Value& v) {
    if (!std::holds_alternative<std::string>(v)) return false;
    const std::string& s = std::get<std::string>(v);
    return s.rfind("__lambda_", 0) == 0;
}

bool Runtime::addHook(const std::string& event, const Value& callback) {
    if (!isLambdaIdValue(callback)) return false;
    std::string ev = normalizeEvent(event);
    hooks[ev].push_back(std::get<std::string>(callback));
    return true;
}

bool Runtime::clearHooks(const std::string& event) {
    std::string ev = normalizeEvent(event);
    auto it = hooks.find(ev);
    if (it == hooks.end()) return false;
    hooks.erase(it);
    return true;
}

void Runtime::clearAllHooks() {
    hooks.clear();
}

size_t Runtime::hookCount(const std::string& event) const {
    std::string ev = normalizeEvent(event);
    auto it = hooks.find(ev);
    if (it == hooks.end()) return 0;
    return it->second.size();
}

void Runtime::emitHook(const std::string& event, const std::vector<Value>& args) {
    std::string ev = normalizeEvent(event);
    auto it = hooks.find(ev);
    if (it == hooks.end()) return;
    for (const auto& lambdaId : it->second) {
        try {
            (void)invokeLambdaValue(Value(lambdaId), args);
        } catch (const std::exception& e) {
            // Avoid hook failures crashing the runtime; log and continue.
            Logger::instance().log(LogLevel::ERROR, std::string("Hook '") + ev + "' failed: " + e.what());
        } catch (...) {
            Logger::instance().log(LogLevel::ERROR, std::string("Hook '") + ev + "' failed");
        }
    }
}

void Runtime::notifyError(const std::string& context, const std::string& message,
                          int line, int column, bool haltNow) {
    // Log the error (keeps existing behavior where errors are visible by default).
    std::string msg = context + ": " + message;
    if (line >= 0) {
        msg += " [line " + std::to_string(line) + ", col " + std::to_string(column) + "]";
    }
    Logger::instance().log(LogLevel::ERROR, msg);

    // Invoke error callbacks (guard against recursive errors).
    if (!inErrorCallback) {
        inErrorCallback = true;
        emitHook("error", {Value(executionMode), Value(context), Value(message), Value(line), Value(column)});
        inErrorCallback = false;
    }

    if (haltNow) {
        state = RuntimeState::HALTED;
    }
}

// ============================================================================
// Object Management
// ============================================================================

ObjectInstancePtr Runtime::createObject(const std::string& className) {
    auto instance = std::make_shared<ObjectInstance>(className);
    return instance;
}

ObjectInstancePtr Runtime::getObject(const std::string& name) {
    auto it = objects.find(name);
    if (it != objects.end()) {
        return it->second;
    }
    return nullptr;
}

bool Runtime::isObjectVariable(const std::string& name) const {
    return objects.find(name) != objects.end();
}

// ============================================================================
// Variable Management
// ============================================================================

bool Runtime::hasVariable(const std::string& name) const {
    return variables.find(name) != variables.end();
}

void Runtime::clearVariables() {
    variables.clear();
    objects.clear();
}

size_t Runtime::getVariableCount() const {
    return variables.size() + objects.size();
}

std::vector<std::string> Runtime::listVariables() const {
    std::vector<std::string> result;
    for (const auto& pair : variables) {
        result.push_back(pair.first);
    }
    for (const auto& pair : objects) {
        result.push_back(pair.first + " (object)");
    }
    return result;
}

Value Runtime::getVariable(const std::string& name) {
    auto it = variables.find(name);
    if (it != variables.end()) {
        return it->second;
    }
    return Value{}; // Default
}
