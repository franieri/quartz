// ============================================================================
// system.async.task Extension
// Provides async task primitives: delay, sleep, ready, poll, get, status, error
// ============================================================================

#include "function_registry.h"
#include "runtime.h"

#include <thread>
#include <chrono>
#include <string>
#include <vector>

static inline Runtime* rt() {
    return global_runtime_ptr;
}

static inline bool isInt(const Value& v) {
    return std::holds_alternative<int>(v);
}

static inline bool isTaskRef(const Value& v) {
    return std::holds_alternative<TaskRef>(v);
}

static inline std::string valueToString(const Value& v) {
    return std::visit([](auto&& arg) -> std::string {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, std::string>) return arg;
        if constexpr (std::is_same_v<T, int>) return std::to_string(arg);
        if constexpr (std::is_same_v<T, double>) return std::to_string(arg);
        if constexpr (std::is_same_v<T, bool>) return arg ? "true" : "false";
        if constexpr (std::is_same_v<T, TaskRef>) return "<task:" + arg.id + ">";
        return std::string("");
    }, v);
}

void register_async_task_functions(FunctionRegistry& reg) {
    // ------------------------------------------------------------------------
    // system.async.task.delay(ms: int, value: any) -> task
    // Returns a task that resolves to `value` after `ms` milliseconds.
    // ------------------------------------------------------------------------
    reg.registerFunction("system.async.task.delay", [](const std::vector<Value>& args) -> Value {
        if (!rt() || args.size() < 2) {
            throw LanguageException("RuntimeError", "system.async.task.delay requires 2 arguments: (ms, value)");
        }
        if (!isInt(args[0])) {
            throw LanguageException("TypeError", "system.async.task.delay: first argument (ms) must be int");
        }
        int delayMs = std::get<int>(args[0]);
        if (delayMs < 0) delayMs = 0;
        return rt()->submitDelayedTask(delayMs, args[1]);
    });

    // ------------------------------------------------------------------------
    // system.async.task.sleep(ms: int) -> bool
    // Blocks the current thread for `ms` milliseconds. Returns true.
    // Useful to avoid busy-wait polling loops.
    // ------------------------------------------------------------------------
    reg.registerFunction("system.async.task.sleep", [](const std::vector<Value>& args) -> Value {
        if (args.empty() || !isInt(args[0])) {
            throw LanguageException("TypeError", "system.async.task.sleep: argument must be int");
        }
        int sleepMs = std::get<int>(args[0]);
        if (sleepMs > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs));
        }
        return Value(true);
    });

    // ------------------------------------------------------------------------
    // system.async.task.ready(t: task) -> bool
    // Returns true if the task has completed (fulfilled or rejected).
    // ------------------------------------------------------------------------
    reg.registerFunction("system.async.task.ready", [](const std::vector<Value>& args) -> Value {
        if (!rt() || args.empty()) {
            throw LanguageException("RuntimeError", "system.async.task.ready requires a task argument");
        }
        if (!isTaskRef(args[0])) {
            throw LanguageException("TypeError", "system.async.task.ready: argument must be a task");
        }
        const TaskRef& ref = std::get<TaskRef>(args[0]);
        return Value(rt()->taskReady(ref));
    });

    // ------------------------------------------------------------------------
    // system.async.task.poll(t: task) -> bool
    // Alias for ready(). Returns true if the task has completed.
    // ------------------------------------------------------------------------
    reg.registerFunction("system.async.task.poll", [](const std::vector<Value>& args) -> Value {
        if (!rt() || args.empty()) {
            throw LanguageException("RuntimeError", "system.async.task.poll requires a task argument");
        }
        if (!isTaskRef(args[0])) {
            throw LanguageException("TypeError", "system.async.task.poll: argument must be a task");
        }
        const TaskRef& ref = std::get<TaskRef>(args[0]);
        return Value(rt()->taskReady(ref));
    });

    // ------------------------------------------------------------------------
    // system.async.task.get(t: task) -> any
    // Blocks until the task completes and returns the result.
    // If the task was rejected, throws a RuntimeError.
    // ------------------------------------------------------------------------
    reg.registerFunction("system.async.task.get", [](const std::vector<Value>& args) -> Value {
        if (!rt() || args.empty()) {
            throw LanguageException("RuntimeError", "system.async.task.get requires a task argument");
        }
        if (!isTaskRef(args[0])) {
            throw LanguageException("TypeError", "system.async.task.get: argument must be a task");
        }
        const TaskRef& ref = std::get<TaskRef>(args[0]);
        return rt()->taskGet(ref);  // Blocks; throws on rejection
    });

    // ------------------------------------------------------------------------
    // system.async.task.status(t: task) -> string
    // Returns "pending", "ok", or "error" depending on the task state.
    // ------------------------------------------------------------------------
    reg.registerFunction("system.async.task.status", [](const std::vector<Value>& args) -> Value {
        if (!rt() || args.empty()) {
            throw LanguageException("RuntimeError", "system.async.task.status requires a task argument");
        }
        if (!isTaskRef(args[0])) {
            throw LanguageException("TypeError", "system.async.task.status: argument must be a task");
        }
        const TaskRef& ref = std::get<TaskRef>(args[0]);
        return Value(rt()->taskStatus(ref));
    });

    // ------------------------------------------------------------------------
    // system.async.task.error(t: task) -> string
    // Returns the error message if the task was rejected; empty string otherwise.
    // ------------------------------------------------------------------------
    reg.registerFunction("system.async.task.error", [](const std::vector<Value>& args) -> Value {
        if (!rt() || args.empty()) {
            throw LanguageException("RuntimeError", "system.async.task.error requires a task argument");
        }
        if (!isTaskRef(args[0])) {
            throw LanguageException("TypeError", "system.async.task.error: argument must be a task");
        }
        const TaskRef& ref = std::get<TaskRef>(args[0]);
        return Value(rt()->taskError(ref));
    });
}

// Extension entry point (macOS expects leading underscore with extern "C")
extern "C" __attribute__((visibility("default")))
void init_extension(FunctionRegistry& reg) {
    register_async_task_functions(reg);
}
