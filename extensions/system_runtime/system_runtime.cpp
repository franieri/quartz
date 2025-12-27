#include "function_registry.h"
#include "runtime.h"
#include "logger.h"

#include <cstdlib>
#include <unordered_map>
#include <filesystem>
#include <thread>

#if defined(__APPLE__)
    #include <sys/types.h>
    #include <sys/sysctl.h>
    #include <unistd.h>
#elif defined(__linux__)
    #include <unistd.h>
    #include <sys/sysinfo.h>
#elif defined(_WIN32)
    #include <windows.h>
#endif

#include <vector>
#include <string>

static inline Runtime* rt() {
    return global_runtime_ptr;
}

static inline bool isString(const Value& v) {
    return std::holds_alternative<std::string>(v);
}

static inline std::string valueToString(const Value& v) {
    return std::visit([](auto&& arg) -> std::string {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, std::string>) return arg;
        if constexpr (std::is_same_v<T, int>) return std::to_string(arg);
        if constexpr (std::is_same_v<T, double>) return std::to_string(arg);
        if constexpr (std::is_same_v<T, bool>) return arg ? "true" : "false";
        return std::string("");
    }, v);
}

static inline std::string logLevelToString(LogLevel l) {
    switch (l) {
        case LogLevel::DEBUG: return "DEBUG";
        case LogLevel::INFO: return "INFO";
        case LogLevel::NOTICE: return "NOTICE";
        case LogLevel::WARNING: return "WARNING";
        case LogLevel::ERROR: return "ERROR";
        default: return "INFO";
    }
}

static inline bool parseLogLevel(const std::string& s, LogLevel* out) {
    if (!out) return false;
    if (s == "DEBUG" || s == "debug") { *out = LogLevel::DEBUG; return true; }
    if (s == "INFO" || s == "info") { *out = LogLevel::INFO; return true; }
    if (s == "NOTICE" || s == "notice") { *out = LogLevel::NOTICE; return true; }
    if (s == "WARNING" || s == "warning" || s == "WARN" || s == "warn") { *out = LogLevel::WARNING; return true; }
    if (s == "ERROR" || s == "error") { *out = LogLevel::ERROR; return true; }
    return false;
}

static inline std::string osName() {
#if defined(__APPLE__)
    return "macos";
#elif defined(__linux__)
    return "linux";
#elif defined(_WIN32)
    return "windows";
#else
    return "unknown";
#endif
}

static inline std::string archName() {
#if defined(__aarch64__) || defined(__arm64__) || defined(_M_ARM64)
    return "arm64";
#elif defined(__x86_64__) || defined(_M_X64)
    return "x86_64";
#elif defined(__i386__) || defined(_M_IX86)
    return "x86";
#else
    return "unknown";
#endif
}

static inline double totalMemoryBytes() {
#if defined(__APPLE__)
    uint64_t mem = 0;
    size_t size = sizeof(mem);
    if (sysctlbyname("hw.memsize", &mem, &size, nullptr, 0) == 0) {
        return (double)mem;
    }
    return 0.0;
#elif defined(__linux__)
    struct sysinfo info;
    if (sysinfo(&info) == 0) {
        // totalram is in units of mem_unit
        return (double)info.totalram * (double)info.mem_unit;
    }
    return 0.0;
#elif defined(_WIN32)
    MEMORYSTATUSEX statex;
    statex.dwLength = sizeof(statex);
    if (GlobalMemoryStatusEx(&statex)) {
        return (double)statex.ullTotalPhys;
    }
    return 0.0;
#else
    return 0.0;
#endif
}

static inline int pageSizeBytes() {
#if defined(_WIN32)
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return (int)si.dwPageSize;
#else
    long ps = ::sysconf(_SC_PAGESIZE);
    if (ps <= 0) ps = 4096;
    return (int)ps;
#endif
}

static inline int processId() {
#if defined(_WIN32)
    return (int)GetCurrentProcessId();
#else
    return (int)::getpid();
#endif
}

void register_runtime_functions(FunctionRegistry& reg) {
    // --------------------------------------------------------------------
    // Environment variables
    // --------------------------------------------------------------------

    // system.runtime.env(key: string) -> string
    reg.registerFunction("system.runtime.env", [](const std::vector<Value>& args) -> Value {
        if (args.empty() || !isString(args[0])) return Value(std::string(""));
        const char* v = std::getenv(std::get<std::string>(args[0]).c_str());
        return Value(std::string(v ? v : ""));
    });

    // system.runtime.envOrDefault(key: string, default: any) -> string
    reg.registerFunction("system.runtime.envOrDefault", [](const std::vector<Value>& args) -> Value {
        if (args.empty() || !isString(args[0])) return Value(std::string(""));
        const char* v = std::getenv(std::get<std::string>(args[0]).c_str());
        if (v) return Value(std::string(v));
        if (args.size() >= 2) return Value(valueToString(args[1]));
        return Value(std::string(""));
    });

    // system.runtime.hasEnv(key: string) -> bool
    reg.registerFunction("system.runtime.hasEnv", [](const std::vector<Value>& args) -> Value {
        if (args.empty() || !isString(args[0])) return Value(false);
        const char* v = std::getenv(std::get<std::string>(args[0]).c_str());
        return Value(v != nullptr);
    });

    // system.runtime.path() -> string
    reg.registerFunction("system.runtime.path", [](const std::vector<Value>& args) -> Value {
        (void)args;
        const char* v = std::getenv("PATH");
        return Value(std::string(v ? v : ""));
    });

    // system.runtime.home() -> string
    reg.registerFunction("system.runtime.home", [](const std::vector<Value>& args) -> Value {
        (void)args;
        const char* v = std::getenv("HOME");
        return Value(std::string(v ? v : ""));
    });

    // system.runtime.envAll() -> {string:string}
    reg.registerFunction("system.runtime.envAll", [](const std::vector<Value>& args) -> Value {
        (void)args;
        if (!rt()) return Value{};

        std::unordered_map<std::string, Value> out;

#if defined(_WIN32)
        // Windows env enumeration is more involved; keep this minimal.
        // Developers can use env(key) for specific lookups.
        (void)out;
        return rt()->makeDict({});
#else
        extern char** environ;
        for (char** p = environ; p && *p; ++p) {
            std::string kv(*p);
            auto eq = kv.find('=');
            if (eq == std::string::npos) continue;
            std::string k = kv.substr(0, eq);
            std::string v = kv.substr(eq + 1);
            out.emplace(std::move(k), Value(std::move(v)));
        }
        return rt()->makeDict(std::move(out));
#endif
    });

    // --------------------------------------------------------------------
    // System/runtime parameters
    // --------------------------------------------------------------------

    // system.runtime.os() -> string
    reg.registerFunction("system.runtime.os", [](const std::vector<Value>& args) -> Value {
        (void)args;
        return Value(osName());
    });

    // system.runtime.arch() -> string
    reg.registerFunction("system.runtime.arch", [](const std::vector<Value>& args) -> Value {
        (void)args;
        return Value(archName());
    });

    // system.runtime.cpuCount() -> int
    reg.registerFunction("system.runtime.cpuCount", [](const std::vector<Value>& args) -> Value {
        (void)args;
        unsigned int n = std::thread::hardware_concurrency();
        if (n == 0) n = 1;
        return Value((int)n);
    });

    // system.runtime.pageSizeBytes() -> int
    reg.registerFunction("system.runtime.pageSizeBytes", [](const std::vector<Value>& args) -> Value {
        (void)args;
        return Value(pageSizeBytes());
    });

    // system.runtime.memTotalBytes() -> double
    reg.registerFunction("system.runtime.memTotalBytes", [](const std::vector<Value>& args) -> Value {
        (void)args;
        return Value(totalMemoryBytes());
    });

    // system.runtime.pid() -> int
    reg.registerFunction("system.runtime.pid", [](const std::vector<Value>& args) -> Value {
        (void)args;
        return Value(processId());
    });

    // system.runtime.cwd() -> string
    reg.registerFunction("system.runtime.cwd", [](const std::vector<Value>& args) -> Value {
        (void)args;
        return Value(std::filesystem::current_path().string());
    });

    // system.runtime.getLogLevel() -> string
    reg.registerFunction("system.runtime.getLogLevel", [](const std::vector<Value>& args) -> Value {
        (void)args;
        return Value(logLevelToString(Logger::instance().getMinLevel()));
    });

    // system.runtime.setLogLevel(level: string) -> bool
    reg.registerFunction("system.runtime.setLogLevel", [](const std::vector<Value>& args) -> Value {
        if (args.empty() || !isString(args[0])) return Value(false);
        LogLevel lvl;
        if (!parseLogLevel(std::get<std::string>(args[0]), &lvl)) return Value(false);
        Logger::instance().setMinLevel(lvl);
        return Value(true);
    });

    // system.runtime.getSourceDir() -> string
    reg.registerFunction("system.runtime.getSourceDir", [](const std::vector<Value>& args) -> Value {
        (void)args;
        if (!rt()) return Value(std::string(""));
        return Value(rt()->getSourceDirectory());
    });

    // system.runtime.setSourceDir(dir: string) -> bool
    reg.registerFunction("system.runtime.setSourceDir", [](const std::vector<Value>& args) -> Value {
        if (!rt() || args.empty() || !isString(args[0])) return Value(false);
        rt()->setSourceDirectory(std::get<std::string>(args[0]));
        return Value(true);
    });
}

extern "C" __attribute__((visibility("default"))) void init_extension(FunctionRegistry& reg) {
    register_runtime_functions(reg);
}
