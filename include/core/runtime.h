#ifndef RUNTIME_H
#define RUNTIME_H

#include "types.h"
#include "function_registry.h"
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <map>
#include <stdexcept>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <thread>
#include <queue>
#include <vector>
#include "qz_export.h"

// Forward declaration
class ObjectInstance;
using ObjectInstancePtr = std::shared_ptr<ObjectInstance>;

// ============================================================================
// Runtime Exceptions
// ============================================================================

class RuntimeException : public std::runtime_error {
public:
    RuntimeException(const std::string& message, int line = -1, int column = -1)
        : std::runtime_error(formatMessage(message, line, column))
        , line(line), column(column) {}
    
    int getLine() const { return line; }
    int getColumn() const { return column; }
    
private:
    int line;
    int column;
    
    static std::string formatMessage(const std::string& msg, int line, int col) {
        if (line >= 0) {
            return msg + " [line " + std::to_string(line) + ", col " + std::to_string(col) + "]";
        }
        return msg;
    }
};

class UndefinedVariableException : public RuntimeException {
public:
    UndefinedVariableException(const std::string& varName, int line = -1, int col = -1)
        : RuntimeException("Undefined variable: '" + varName + "'", line, col) {}
};

class UndefinedClassException : public RuntimeException {
public:
    UndefinedClassException(const std::string& className, int line = -1, int col = -1)
        : RuntimeException("Undefined class: '" + className + "'", line, col) {}
};

class UndefinedMethodException : public RuntimeException {
public:
    UndefinedMethodException(const std::string& methodName, const std::string& className, int line = -1, int col = -1)
        : RuntimeException("Method '" + methodName + "' not found in class '" + className + "'", line, col) {}
};

class UndefinedFunctionException : public RuntimeException {
public:
    UndefinedFunctionException(const std::string& funcName, int line = -1, int col = -1)
        : RuntimeException("Undefined function: '" + funcName + "'", line, col) {}
};

class ImmutableVariableException : public RuntimeException {
public:
    ImmutableVariableException(const std::string& varName, int line = -1, int col = -1)
        : RuntimeException("Cannot reassign immutable variable: '" + varName + "'", line, col) {}
};

class ModuleNotFoundException : public RuntimeException {
public:
    ModuleNotFoundException(const std::string& moduleName, int line = -1, int col = -1)
        : RuntimeException("Module not found: '" + moduleName + "'", line, col) {}
};

class TypeError : public RuntimeException {
public:
    TypeError(const std::string& message, int line = -1, int col = -1)
        : RuntimeException("Type error: " + message, line, col) {}
};

// ============================================================================
// Language-Level Exception (for throw/catch in user code)
// ============================================================================

class LanguageException : public std::exception {
public:
    LanguageException(const std::string& type, const std::string& message, int line = -1, int col = -1)
        : exceptionType(type), message(message), line(line), column(col) {}
    
    const char* what() const noexcept override { return message.c_str(); }
    std::string getType() const { return exceptionType; }
    std::string getMessage() const { return message; }
    int getLine() const { return line; }
    int getColumn() const { return column; }
    
    // For exception object fields
    void setField(const std::string& name, const Value& val) { fields[name] = val; }
    Value getField(const std::string& name) const { 
        auto it = fields.find(name);
        return it != fields.end() ? it->second : Value{std::string("")};
    }
    bool hasField(const std::string& name) const { return fields.find(name) != fields.end(); }
    const std::unordered_map<std::string, Value>& getFields() const { return fields; }

private:
    std::string exceptionType;
    std::string message;
    int line;
    int column;
    std::unordered_map<std::string, Value> fields;
};

// ============================================================================
// Object Instance
// ============================================================================

// Runtime representation of a class instance
class ObjectInstance {
public:
    ObjectInstance(const std::string& className) : className(className) {}
    
    std::string getClassName() const { return className; }
    
    void setField(const std::string& name, const Value& value) {
        fields[name] = value;
    }
    
    Value getField(const std::string& name) {
        if (fields.find(name) != fields.end()) {
            return fields[name];
        }
        return Value{};
    }
    
    bool hasField(const std::string& name) const {
        return fields.find(name) != fields.end();
    }
    
private:
    std::string className;
    std::unordered_map<std::string, Value> fields;
};

enum class RuntimeState { IDLE, EXECUTING, HALTED, COMPLETED };

class Runtime {
    friend class BytecodeVM;
public:
    Runtime() : state(RuntimeState::IDLE), threadPoolShutdown(false) {}
    ~Runtime();  // Destructor to clean up thread pool

    // Initialize standard library + extensions (should be called before execution)
    void initialize();
    
    // Execution control
    void execute(const AST& ast);
    void halt();
    bool isHalted() const { return state == RuntimeState::HALTED; }
    RuntimeState getState() const { return state; }

    // ------------------------------------------------------------------------
    // Runtime lifecycle hooks + global error callbacks
    // ------------------------------------------------------------------------
    // Hooks are registered with an event name and a lambda value.
    // Supported events (convention): "start", "end", "halt", "error".
    // - start(mode)
    // - end(mode, status)
    // - halt(mode, reason)
    // - error(mode, context, message, line, col)
    bool addHook(const std::string& event, const Value& callback);
    bool clearHooks(const std::string& event);
    void clearAllHooks();
    size_t hookCount(const std::string& event) const;

    // Report an error to hooks + logger. If halt==true, runtime moves to HALTED.
    void notifyError(const std::string& context, const std::string& message,
                     int line = -1, int column = -1, bool halt = false);
    
    // Source file management (for module resolution)
    void setSourcePath(const std::string& path);
    void setSourceDirectory(const std::string& dir);
    std::string getSourceDirectory() const { return sourceDirectory; }
    
    // Variable management
    void setVariable(const std::string& name, const Value& value);
    Value getVariable(const std::string& name);
    bool hasVariable(const std::string& name) const;
    void clearVariables();
    
    // Object management
    ObjectInstancePtr createObject(const std::string& className);
    ObjectInstancePtr getObject(const std::string& name);
    bool isObjectVariable(const std::string& name) const;
    
    // Namespace/import management
    void registerImport(const std::string& ns);
    std::string resolveFunctionName(const std::string& name);
    bool hasImport(const std::string& alias) const;
    
    // Diagnostics
    size_t getVariableCount() const;
    std::vector<std::string> listVariables() const;

    // ------------------------------------------------------------------------
    // Container helpers (arrays/dicts)
    // ------------------------------------------------------------------------
    std::vector<Value>* getArray(const ArrayRef& ref);
    const std::vector<Value>* getArray(const ArrayRef& ref) const;
    std::unordered_map<std::string, Value>* getDict(const DictRef& ref);
    const std::unordered_map<std::string, Value>* getDict(const DictRef& ref) const;

    Value makeArray(std::vector<Value> elements);
    Value makeDict(std::unordered_map<std::string, Value> entries);

    // ------------------------------------------------------------------------
    // Task helpers (async background work)
    // ------------------------------------------------------------------------
    enum class TaskState { Pending, Fulfilled, Rejected };
    
    // Submit work that will run on a background thread. Returns a TaskRef immediately.
    // The work function should NOT call back into the interpreter/VM.
    Value submitTask(std::function<Value()> work);
    
    // Create a delayed task that resolves to `value` after `delayMs` milliseconds.
    Value submitDelayedTask(int delayMs, const Value& value);
    
    // Check if a task has completed (fulfilled or rejected).
    bool taskReady(const TaskRef& ref) const;
    
    // Get the result of a completed task. Blocks until done.
    // Throws LanguageException if the task was rejected.
    Value taskGet(const TaskRef& ref);
    
    // Get task status as a string: "pending", "ok", or "error"
    std::string taskStatus(const TaskRef& ref) const;
    
    // Get error message if task was rejected; empty string otherwise.
    std::string taskError(const TaskRef& ref) const;

    // ------------------------------------------------------------------------
    // Buffer helpers (binary data for I/O operations)
    // ------------------------------------------------------------------------
    
    // Create a new empty buffer with optional initial capacity
    Value makeBuffer(size_t initialCapacity = 0);
    
    // Create a buffer from a string (copies bytes)
    Value makeBufferFromString(const std::string& str);
    
    // Get raw pointer to buffer data (for extensions)
    std::vector<uint8_t>* getBuffer(const BufferRef& ref);
    const std::vector<uint8_t>* getBuffer(const BufferRef& ref) const;
    
    // Get buffer size
    size_t bufferSize(const BufferRef& ref) const;
    
    // Get buffer capacity
    size_t bufferCapacity(const BufferRef& ref) const;
    
    // Convert buffer to string
    std::string bufferToString(const BufferRef& ref) const;
    
    // Append data to buffer (from string or another buffer)
    bool bufferAppendString(const BufferRef& ref, const std::string& data);
    bool bufferAppendBuffer(const BufferRef& ref, const BufferRef& other);
    
    // Slice buffer (returns new buffer with copy of data)
    Value bufferSlice(const BufferRef& ref, size_t start, size_t end);
    
    // Clear buffer contents
    bool bufferClear(const BufferRef& ref);
    
    // Copy buffer (returns new buffer)
    Value bufferCopy(const BufferRef& ref);
    
    // Byte-level access
    int bufferGetByte(const BufferRef& ref, size_t index) const;
    bool bufferSetByte(const BufferRef& ref, size_t index, uint8_t value);

    // Formatting (used by to_string(Value) and I/O)
    std::string formatValue(const Value& v, bool quoteStrings = false) const;
    std::string formatArrayById(const std::string& arrayId, bool quoteStrings = false) const;
    std::string formatDictById(const std::string& dictId, bool quoteStrings = false) const;

    // Lambda invocation helper for extensions
    Value invokeLambdaValue(const Value& lambdaVal, const std::vector<Value>& args);

    // Set by the bytecode VM during execution so extensions can invoke bytecode lambdas.
    void setExternalLambdaInvoker(std::function<Value(const std::string& lambdaId, const std::vector<Value>& args)> invoker);

private:
    std::unordered_map<std::string, Value> variables;
    std::unordered_map<std::string, ObjectInstancePtr> objects;  // Store object instances
    std::unordered_map<std::string, std::vector<Value>> arrayStorage;  // Store actual arrays
    std::unordered_map<std::string, std::unordered_map<std::string, Value>> dictStorage;  // Store actual dicts
    std::unordered_map<std::string, std::vector<uint8_t>> bufferStorage;  // Store byte buffers
    mutable std::mutex bufferStorageMtx;  // Protects bufferStorage map and nextBufferId
    std::unordered_map<std::string, std::string> varToArrayId;  // Map variable name to array ID
    std::unordered_map<std::string, std::string> varToDictId;   // Map variable name to dict ID
    RuntimeState state;
    std::string executionMode = "interp"; // "interp" or "bytecode" (best-effort)
    std::unordered_map<std::string, std::string> imports;  // alias -> full namespace
    bool shouldBreak = false;     // For break statement
    bool shouldContinue = false;  // For continue statement
    size_t nextArrayId = 0;  // Counter for unique array IDs
    size_t nextDictId = 0;   // Counter for unique dict IDs
    size_t nextLambdaId = 0; // Counter for unique lambda IDs
    std::atomic<size_t> nextBufferId{0}; // Counter for unique buffer IDs (atomic for thread safety)
    
    // Lambda/Closure storage
    struct StoredLambda {
        ASTNodePtr node;  // The lambda AST node
        std::unordered_map<std::string, Value> captures;  // Captured variables
    };
    std::unordered_map<std::string, StoredLambda> lambdaStorage;  // Store lambda closures
    std::unordered_map<std::string, std::string> varToLambdaId;  // Map variable name to lambda ID

    // Task/async storage
    struct StoredTask {
        TaskState state = TaskState::Pending;
        Value result;
        std::string error;
        mutable std::mutex mtx;
        mutable std::condition_variable cv;
    };
    std::unordered_map<std::string, std::shared_ptr<StoredTask>> taskStorage;
    mutable std::mutex taskStorageMtx;  // Protects taskStorage map itself
    std::atomic<size_t> nextTaskId{0};

    // Thread pool for async task execution
    struct WorkItem {
        std::shared_ptr<StoredTask> task;
        std::function<Value()> work;
    };
    std::vector<std::thread> threadPoolWorkers;
    std::queue<WorkItem> workQueue;
    std::mutex workQueueMtx;
    std::condition_variable workQueueCv;
    std::atomic<bool> threadPoolShutdown;
    static constexpr size_t kThreadPoolSize = 4;  // Configurable pool size
    
    void initThreadPool();
    void threadPoolWorkerLoop();

    // If set, used to invoke lambdas by id from outside the interpreter (e.g. bytecode VM).
    std::function<Value(const std::string&, const std::vector<Value>&)> externalLambdaInvoker;
    
    // Class registry
    std::map<std::string, ClassDef> classRegistry;  // Registered class definitions
    std::map<std::string, InterfaceDef> interfaceRegistry;  // Registered interface definitions
    std::unordered_map<std::string, std::string> classToModule;  // className -> modulePath
    std::string currentThisObject;  // Current 'this' context
    std::string currentLoadingModule;  // Module currently being loaded (for class association)
    std::string sourceDirectory;    // Directory of the main source file
    std::unordered_set<std::string> loadedModules;  // Track already loaded modules
    
    // Static field storage: className::fieldName -> value
    std::unordered_map<std::string, Value> staticFields;

    // Hook storage: event -> ordered lambda IDs
    std::unordered_map<std::string, std::vector<std::string>> hooks;
    bool inErrorCallback = false;
    
    // Internal methods
    void initStandardLibrary();
    void loadExtensions();
    void registerGlobalExceptionClass();  // Register built-in Exception class
    void executeNode(const ASTNodePtr& node);
    Value evaluate(const ASTNodePtr& node);
    Value applyBinaryOp(const Value& left, const Value& right, const std::string& op);
    Value executeMethodBody(const ASTNodePtr& body);  // Execute method body and return value
    Value invokeLambda(const std::string& lambdaId, const std::vector<Value>& args);  // Call a lambda
    
    // Module loading
    bool loadModule(const std::string& modulePath);  // Load a file-based module
    bool isExtensionLoaded(const std::string& ns) const;  // Check if namespace is from extension
    
    // Helper for error reporting
    std::string getNodeTypeString(const ASTNodePtr& node) const;
    void reportError(const std::string& context, const std::string& message);

    // Hook emission helper
    void emitHook(const std::string& event, const std::vector<Value>& args);
};

// Exported pointer to the most recently-initialized Runtime instance.
// This is used by stdlib/extension helpers that need container storage.
extern QZ_CORE_API Runtime* global_runtime_ptr;

// Quartz naming convention (Qz prefix)
using QzRuntime = Runtime;

#endif // RUNTIME_H