// ============================================================================
// system.fs Extension
// Cross-platform filesystem operations for Quartz
// Compatible with macOS, Linux, and Windows
// ============================================================================

#include "function_registry.h"
#include "runtime.h"

#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <cstdint>

// C++17 filesystem - cross-platform
#if defined(_MSC_VER) || defined(__MINGW32__)
    // Windows
    #include <filesystem>
    namespace fs = std::filesystem;
#elif defined(__APPLE__) || defined(__linux__)
    // macOS and Linux
    #include <filesystem>
    namespace fs = std::filesystem;
#else
    // Fallback for older systems
    #include <experimental/filesystem>
    namespace fs = std::experimental::filesystem;
#endif

// Platform-specific includes for some operations
#ifdef _WIN32
    #include <windows.h>
    #include <direct.h>
    #define PATH_SEP "\\"
#else
    #include <unistd.h>
    #include <sys/stat.h>
    #define PATH_SEP "/"
#endif

static inline Runtime* rt() {
    return global_runtime_ptr;
}

static inline bool isString(const Value& v) {
    return std::holds_alternative<std::string>(v);
}

static inline bool isBufferRef(const Value& v) {
    return std::holds_alternative<BufferRef>(v);
}

static inline bool isArrayRef(const Value& v) {
    return std::holds_alternative<ArrayRef>(v);
}

// Helper to convert path string to fs::path (handles UTF-8 properly)
static fs::path toFsPath(const std::string& pathStr) {
#ifdef _WIN32
    // On Windows, convert UTF-8 to wide string for proper Unicode support
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, pathStr.c_str(), -1, nullptr, 0);
    std::wstring wpath(size_needed - 1, 0);
    MultiByteToWideChar(CP_UTF8, 0, pathStr.c_str(), -1, &wpath[0], size_needed);
    return fs::path(wpath);
#else
    return fs::path(pathStr);
#endif
}

// Helper to convert fs::path to UTF-8 string
static std::string fromFsPath(const fs::path& p) {
#ifdef _WIN32
    std::wstring wpath = p.wstring();
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, wpath.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string result(size_needed - 1, 0);
    WideCharToMultiByte(CP_UTF8, 0, wpath.c_str(), -1, &result[0], size_needed, nullptr, nullptr);
    return result;
#else
    return p.string();
#endif
}

void register_fs_functions(FunctionRegistry& reg) {
    // ========================================================================
    // PATH QUERY FUNCTIONS
    // ========================================================================

    // ------------------------------------------------------------------------
    // system.fs.exists(path: string) -> bool
    // Check if a path exists (file or directory)
    // ------------------------------------------------------------------------
    reg.registerFunction("system.fs.exists", [](const std::vector<Value>& args) -> Value {
        if (args.empty() || !isString(args[0])) {
            throw LanguageException("TypeError", "system.fs.exists: argument must be string");
        }
        try {
            return Value(fs::exists(toFsPath(std::get<std::string>(args[0]))));
        } catch (const fs::filesystem_error&) {
            return Value(false);
        }
    });

    // ------------------------------------------------------------------------
    // system.fs.isFile(path: string) -> bool
    // Check if path is a regular file
    // ------------------------------------------------------------------------
    reg.registerFunction("system.fs.isFile", [](const std::vector<Value>& args) -> Value {
        if (args.empty() || !isString(args[0])) {
            throw LanguageException("TypeError", "system.fs.isFile: argument must be string");
        }
        try {
            return Value(fs::is_regular_file(toFsPath(std::get<std::string>(args[0]))));
        } catch (const fs::filesystem_error&) {
            return Value(false);
        }
    });

    // ------------------------------------------------------------------------
    // system.fs.isDir(path: string) -> bool
    // Check if path is a directory
    // ------------------------------------------------------------------------
    reg.registerFunction("system.fs.isDir", [](const std::vector<Value>& args) -> Value {
        if (args.empty() || !isString(args[0])) {
            throw LanguageException("TypeError", "system.fs.isDir: argument must be string");
        }
        try {
            return Value(fs::is_directory(toFsPath(std::get<std::string>(args[0]))));
        } catch (const fs::filesystem_error&) {
            return Value(false);
        }
    });

    // ------------------------------------------------------------------------
    // system.fs.size(path: string) -> int
    // Get file size in bytes. Returns -1 on error.
    // ------------------------------------------------------------------------
    reg.registerFunction("system.fs.size", [](const std::vector<Value>& args) -> Value {
        if (args.empty() || !isString(args[0])) {
            throw LanguageException("TypeError", "system.fs.size: argument must be string");
        }
        try {
            auto sz = fs::file_size(toFsPath(std::get<std::string>(args[0])));
            return Value(static_cast<int>(sz));
        } catch (const fs::filesystem_error&) {
            return Value(-1);
        }
    });

    // ========================================================================
    // PATH MANIPULATION FUNCTIONS
    // ========================================================================

    // ------------------------------------------------------------------------
    // system.fs.cwd() -> string
    // Get current working directory
    // ------------------------------------------------------------------------
    reg.registerFunction("system.fs.cwd", [](const std::vector<Value>& /*args*/) -> Value {
        try {
            return Value(fromFsPath(fs::current_path()));
        } catch (const fs::filesystem_error& e) {
            throw LanguageException("IOError", std::string("system.fs.cwd failed: ") + e.what());
        }
    });

    // ------------------------------------------------------------------------
    // system.fs.abs(path: string) -> string
    // Get absolute path
    // ------------------------------------------------------------------------
    reg.registerFunction("system.fs.abs", [](const std::vector<Value>& args) -> Value {
        if (args.empty() || !isString(args[0])) {
            throw LanguageException("TypeError", "system.fs.abs: argument must be string");
        }
        try {
            return Value(fromFsPath(fs::absolute(toFsPath(std::get<std::string>(args[0])))));
        } catch (const fs::filesystem_error& e) {
            throw LanguageException("IOError", std::string("system.fs.abs failed: ") + e.what());
        }
    });

    // ------------------------------------------------------------------------
    // system.fs.join(parts...: string) -> string
    // Join path components. Can take array or multiple string arguments.
    // ------------------------------------------------------------------------
    reg.registerFunction("system.fs.join", [](const std::vector<Value>& args) -> Value {
        if (args.empty()) {
            return Value(std::string(""));
        }

        fs::path result;
        
        // Check if first arg is an array
        if (isArrayRef(args[0])) {
            const ArrayRef& ref = std::get<ArrayRef>(args[0]);
            const auto* arr = rt()->getArray(ref);
            if (!arr) {
                throw LanguageException("RuntimeError", "system.fs.join: invalid array");
            }
            for (const auto& elem : *arr) {
                if (!isString(elem)) {
                    throw LanguageException("TypeError", "system.fs.join: all elements must be strings");
                }
                if (result.empty()) {
                    result = toFsPath(std::get<std::string>(elem));
                } else {
                    result /= toFsPath(std::get<std::string>(elem));
                }
            }
        } else {
            // Multiple string arguments
            for (const auto& arg : args) {
                if (!isString(arg)) {
                    throw LanguageException("TypeError", "system.fs.join: all arguments must be strings");
                }
                if (result.empty()) {
                    result = toFsPath(std::get<std::string>(arg));
                } else {
                    result /= toFsPath(std::get<std::string>(arg));
                }
            }
        }

        return Value(fromFsPath(result));
    });

    // ------------------------------------------------------------------------
    // system.fs.dirname(path: string) -> string
    // Get parent directory of path
    // ------------------------------------------------------------------------
    reg.registerFunction("system.fs.dirname", [](const std::vector<Value>& args) -> Value {
        if (args.empty() || !isString(args[0])) {
            throw LanguageException("TypeError", "system.fs.dirname: argument must be string");
        }
        fs::path p = toFsPath(std::get<std::string>(args[0]));
        return Value(fromFsPath(p.parent_path()));
    });

    // ------------------------------------------------------------------------
    // system.fs.basename(path: string) -> string
    // Get filename from path
    // ------------------------------------------------------------------------
    reg.registerFunction("system.fs.basename", [](const std::vector<Value>& args) -> Value {
        if (args.empty() || !isString(args[0])) {
            throw LanguageException("TypeError", "system.fs.basename: argument must be string");
        }
        fs::path p = toFsPath(std::get<std::string>(args[0]));
        return Value(fromFsPath(p.filename()));
    });

    // ------------------------------------------------------------------------
    // system.fs.extension(path: string) -> string
    // Get file extension (with dot)
    // ------------------------------------------------------------------------
    reg.registerFunction("system.fs.extension", [](const std::vector<Value>& args) -> Value {
        if (args.empty() || !isString(args[0])) {
            throw LanguageException("TypeError", "system.fs.extension: argument must be string");
        }
        fs::path p = toFsPath(std::get<std::string>(args[0]));
        return Value(fromFsPath(p.extension()));
    });

    // ========================================================================
    // FILE READ FUNCTIONS
    // ========================================================================

    // ------------------------------------------------------------------------
    // system.fs.read(path: string) -> string
    // Read entire file as string
    // ------------------------------------------------------------------------
    reg.registerFunction("system.fs.read", [](const std::vector<Value>& args) -> Value {
        if (args.empty() || !isString(args[0])) {
            throw LanguageException("TypeError", "system.fs.read: argument must be string");
        }
        const std::string& pathStr = std::get<std::string>(args[0]);
        
        std::ifstream file(toFsPath(pathStr), std::ios::in);
        if (!file.is_open()) {
            throw LanguageException("IOError", "system.fs.read: cannot open file: " + pathStr);
        }
        
        std::ostringstream oss;
        oss << file.rdbuf();
        return Value(oss.str());
    });

    // ------------------------------------------------------------------------
    // system.fs.readBytes(path: string) -> buffer
    // Read entire file as buffer
    // ------------------------------------------------------------------------
    reg.registerFunction("system.fs.readBytes", [](const std::vector<Value>& args) -> Value {
        if (!rt()) {
            throw LanguageException("RuntimeError", "system.fs.readBytes: no runtime");
        }
        if (args.empty() || !isString(args[0])) {
            throw LanguageException("TypeError", "system.fs.readBytes: argument must be string");
        }
        const std::string& pathStr = std::get<std::string>(args[0]);
        
        std::ifstream file(toFsPath(pathStr), std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            throw LanguageException("IOError", "system.fs.readBytes: cannot open file: " + pathStr);
        }
        
        auto size = file.tellg();
        file.seekg(0, std::ios::beg);
        
        // Create buffer and read directly into it
        Value bufVal = rt()->makeBuffer(static_cast<size_t>(size));
        const BufferRef& bufRef = std::get<BufferRef>(bufVal);
        auto* buf = rt()->getBuffer(bufRef);
        if (!buf) {
            throw LanguageException("RuntimeError", "system.fs.readBytes: failed to create buffer");
        }
        
        buf->resize(static_cast<size_t>(size));
        file.read(reinterpret_cast<char*>(buf->data()), size);
        
        return bufVal;
    });

    // ========================================================================
    // FILE WRITE FUNCTIONS
    // ========================================================================

    // ------------------------------------------------------------------------
    // system.fs.write(path: string, content: string) -> bool
    // Write string to file (creates or overwrites)
    // ------------------------------------------------------------------------
    reg.registerFunction("system.fs.write", [](const std::vector<Value>& args) -> Value {
        if (args.size() < 2 || !isString(args[0]) || !isString(args[1])) {
            throw LanguageException("TypeError", "system.fs.write: requires (path: string, content: string)");
        }
        const std::string& pathStr = std::get<std::string>(args[0]);
        const std::string& content = std::get<std::string>(args[1]);
        
        std::ofstream file(toFsPath(pathStr), std::ios::out | std::ios::trunc);
        if (!file.is_open()) {
            throw LanguageException("IOError", "system.fs.write: cannot open file: " + pathStr);
        }
        
        file << content;
        return Value(true);
    });

    // ------------------------------------------------------------------------
    // system.fs.writeBytes(path: string, buffer: buffer) -> bool
    // Write buffer to file (creates or overwrites)
    // ------------------------------------------------------------------------
    reg.registerFunction("system.fs.writeBytes", [](const std::vector<Value>& args) -> Value {
        if (!rt()) {
            throw LanguageException("RuntimeError", "system.fs.writeBytes: no runtime");
        }
        if (args.size() < 2 || !isString(args[0]) || !isBufferRef(args[1])) {
            throw LanguageException("TypeError", "system.fs.writeBytes: requires (path: string, buffer: buffer)");
        }
        const std::string& pathStr = std::get<std::string>(args[0]);
        const BufferRef& bufRef = std::get<BufferRef>(args[1]);
        
        const auto* buf = rt()->getBuffer(bufRef);
        if (!buf) {
            throw LanguageException("RuntimeError", "system.fs.writeBytes: invalid buffer");
        }
        
        std::ofstream file(toFsPath(pathStr), std::ios::binary | std::ios::out | std::ios::trunc);
        if (!file.is_open()) {
            throw LanguageException("IOError", "system.fs.writeBytes: cannot open file: " + pathStr);
        }
        
        file.write(reinterpret_cast<const char*>(buf->data()), buf->size());
        return Value(true);
    });

    // ------------------------------------------------------------------------
    // system.fs.append(path: string, content: string) -> bool
    // Append string to file
    // ------------------------------------------------------------------------
    reg.registerFunction("system.fs.append", [](const std::vector<Value>& args) -> Value {
        if (args.size() < 2 || !isString(args[0]) || !isString(args[1])) {
            throw LanguageException("TypeError", "system.fs.append: requires (path: string, content: string)");
        }
        const std::string& pathStr = std::get<std::string>(args[0]);
        const std::string& content = std::get<std::string>(args[1]);
        
        std::ofstream file(toFsPath(pathStr), std::ios::out | std::ios::app);
        if (!file.is_open()) {
            throw LanguageException("IOError", "system.fs.append: cannot open file: " + pathStr);
        }
        
        file << content;
        return Value(true);
    });

    // ------------------------------------------------------------------------
    // system.fs.appendBytes(path: string, buffer: buffer) -> bool
    // Append buffer to file
    // ------------------------------------------------------------------------
    reg.registerFunction("system.fs.appendBytes", [](const std::vector<Value>& args) -> Value {
        if (!rt()) {
            throw LanguageException("RuntimeError", "system.fs.appendBytes: no runtime");
        }
        if (args.size() < 2 || !isString(args[0]) || !isBufferRef(args[1])) {
            throw LanguageException("TypeError", "system.fs.appendBytes: requires (path: string, buffer: buffer)");
        }
        const std::string& pathStr = std::get<std::string>(args[0]);
        const BufferRef& bufRef = std::get<BufferRef>(args[1]);
        
        const auto* buf = rt()->getBuffer(bufRef);
        if (!buf) {
            throw LanguageException("RuntimeError", "system.fs.appendBytes: invalid buffer");
        }
        
        std::ofstream file(toFsPath(pathStr), std::ios::binary | std::ios::out | std::ios::app);
        if (!file.is_open()) {
            throw LanguageException("IOError", "system.fs.appendBytes: cannot open file: " + pathStr);
        }
        
        file.write(reinterpret_cast<const char*>(buf->data()), buf->size());
        return Value(true);
    });

    // ========================================================================
    // FILE/DIRECTORY MANAGEMENT FUNCTIONS
    // ========================================================================

    // ------------------------------------------------------------------------
    // system.fs.delete(path: string) -> bool
    // Delete a file
    // ------------------------------------------------------------------------
    reg.registerFunction("system.fs.delete", [](const std::vector<Value>& args) -> Value {
        if (args.empty() || !isString(args[0])) {
            throw LanguageException("TypeError", "system.fs.delete: argument must be string");
        }
        try {
            return Value(fs::remove(toFsPath(std::get<std::string>(args[0]))));
        } catch (const fs::filesystem_error& e) {
            throw LanguageException("IOError", std::string("system.fs.delete failed: ") + e.what());
        }
    });

    // ------------------------------------------------------------------------
    // system.fs.rename(from: string, to: string) -> bool
    // Rename or move a file/directory
    // ------------------------------------------------------------------------
    reg.registerFunction("system.fs.rename", [](const std::vector<Value>& args) -> Value {
        if (args.size() < 2 || !isString(args[0]) || !isString(args[1])) {
            throw LanguageException("TypeError", "system.fs.rename: requires (from: string, to: string)");
        }
        try {
            fs::rename(
                toFsPath(std::get<std::string>(args[0])),
                toFsPath(std::get<std::string>(args[1]))
            );
            return Value(true);
        } catch (const fs::filesystem_error& e) {
            throw LanguageException("IOError", std::string("system.fs.rename failed: ") + e.what());
        }
    });

    // ------------------------------------------------------------------------
    // system.fs.copy(from: string, to: string) -> bool
    // Copy a file
    // ------------------------------------------------------------------------
    reg.registerFunction("system.fs.copy", [](const std::vector<Value>& args) -> Value {
        if (args.size() < 2 || !isString(args[0]) || !isString(args[1])) {
            throw LanguageException("TypeError", "system.fs.copy: requires (from: string, to: string)");
        }
        try {
            fs::copy_file(
                toFsPath(std::get<std::string>(args[0])),
                toFsPath(std::get<std::string>(args[1])),
                fs::copy_options::overwrite_existing
            );
            return Value(true);
        } catch (const fs::filesystem_error& e) {
            throw LanguageException("IOError", std::string("system.fs.copy failed: ") + e.what());
        }
    });

    // ------------------------------------------------------------------------
    // system.fs.mkdir(path: string) -> bool
    // Create a directory (parent must exist)
    // ------------------------------------------------------------------------
    reg.registerFunction("system.fs.mkdir", [](const std::vector<Value>& args) -> Value {
        if (args.empty() || !isString(args[0])) {
            throw LanguageException("TypeError", "system.fs.mkdir: argument must be string");
        }
        try {
            return Value(fs::create_directory(toFsPath(std::get<std::string>(args[0]))));
        } catch (const fs::filesystem_error& e) {
            throw LanguageException("IOError", std::string("system.fs.mkdir failed: ") + e.what());
        }
    });

    // ------------------------------------------------------------------------
    // system.fs.mkdirs(path: string) -> bool
    // Create directory and all parent directories
    // ------------------------------------------------------------------------
    reg.registerFunction("system.fs.mkdirs", [](const std::vector<Value>& args) -> Value {
        if (args.empty() || !isString(args[0])) {
            throw LanguageException("TypeError", "system.fs.mkdirs: argument must be string");
        }
        try {
            return Value(fs::create_directories(toFsPath(std::get<std::string>(args[0]))));
        } catch (const fs::filesystem_error& e) {
            throw LanguageException("IOError", std::string("system.fs.mkdirs failed: ") + e.what());
        }
    });

    // ------------------------------------------------------------------------
    // system.fs.rmdir(path: string) -> bool
    // Remove an empty directory
    // ------------------------------------------------------------------------
    reg.registerFunction("system.fs.rmdir", [](const std::vector<Value>& args) -> Value {
        if (args.empty() || !isString(args[0])) {
            throw LanguageException("TypeError", "system.fs.rmdir: argument must be string");
        }
        try {
            return Value(fs::remove(toFsPath(std::get<std::string>(args[0]))));
        } catch (const fs::filesystem_error& e) {
            throw LanguageException("IOError", std::string("system.fs.rmdir failed: ") + e.what());
        }
    });

    // ------------------------------------------------------------------------
    // system.fs.rmdirs(path: string) -> bool
    // Remove directory and all contents recursively
    // Returns true if anything was removed
    // ------------------------------------------------------------------------
    reg.registerFunction("system.fs.rmdirs", [](const std::vector<Value>& args) -> Value {
        if (args.empty() || !isString(args[0])) {
            throw LanguageException("TypeError", "system.fs.rmdirs: argument must be string");
        }
        try {
            auto removed = fs::remove_all(toFsPath(std::get<std::string>(args[0])));
            return Value(removed > 0);
        } catch (const fs::filesystem_error& e) {
            throw LanguageException("IOError", std::string("system.fs.rmdirs failed: ") + e.what());
        }
    });

    // ------------------------------------------------------------------------
    // system.fs.list(path: string) -> array<string>
    // List directory contents
    // ------------------------------------------------------------------------
    reg.registerFunction("system.fs.list", [](const std::vector<Value>& args) -> Value {
        if (!rt()) {
            throw LanguageException("RuntimeError", "system.fs.list: no runtime");
        }
        if (args.empty() || !isString(args[0])) {
            throw LanguageException("TypeError", "system.fs.list: argument must be string");
        }
        
        std::vector<Value> entries;
        try {
            for (const auto& entry : fs::directory_iterator(toFsPath(std::get<std::string>(args[0])))) {
                entries.push_back(Value(fromFsPath(entry.path().filename())));
            }
        } catch (const fs::filesystem_error& e) {
            throw LanguageException("IOError", std::string("system.fs.list failed: ") + e.what());
        }
        
        return rt()->makeArray(std::move(entries));
    });

    // ========================================================================
    // ASYNC FILE OPERATIONS (using TaskRef)
    // ========================================================================

    // ------------------------------------------------------------------------
    // system.fs.readAsync(path: string) -> task<string>
    // Read file asynchronously, returns task
    // ------------------------------------------------------------------------
    reg.registerFunction("system.fs.readAsync", [](const std::vector<Value>& args) -> Value {
        if (!rt()) {
            throw LanguageException("RuntimeError", "system.fs.readAsync: no runtime");
        }
        if (args.empty() || !isString(args[0])) {
            throw LanguageException("TypeError", "system.fs.readAsync: argument must be string");
        }
        
        std::string pathStr = std::get<std::string>(args[0]);
        
        return rt()->submitTask([pathStr]() -> Value {
            std::ifstream file(toFsPath(pathStr), std::ios::in);
            if (!file.is_open()) {
                throw LanguageException("IOError", "system.fs.readAsync: cannot open file: " + pathStr);
            }
            
            std::ostringstream oss;
            oss << file.rdbuf();
            return Value(oss.str());
        });
    });

    // ------------------------------------------------------------------------
    // system.fs.readBytesAsync(path: string) -> task<buffer>
    // Read file as buffer asynchronously
    // Note: Buffer creation must happen in main thread after task completes
    // This returns a task that resolves to a string of bytes, caller can convert
    // ------------------------------------------------------------------------
    reg.registerFunction("system.fs.readBytesAsync", [](const std::vector<Value>& args) -> Value {
        if (!rt()) {
            throw LanguageException("RuntimeError", "system.fs.readBytesAsync: no runtime");
        }
        if (args.empty() || !isString(args[0])) {
            throw LanguageException("TypeError", "system.fs.readBytesAsync: argument must be string");
        }
        
        std::string pathStr = std::get<std::string>(args[0]);
        Runtime* runtime = rt();  // Capture runtime pointer
        
        return runtime->submitTask([pathStr, runtime]() -> Value {
            std::ifstream file(toFsPath(pathStr), std::ios::binary | std::ios::ate);
            if (!file.is_open()) {
                throw LanguageException("IOError", "system.fs.readBytesAsync: cannot open file: " + pathStr);
            }
            
            auto size = file.tellg();
            file.seekg(0, std::ios::beg);
            
            // Read into a temporary vector
            std::vector<uint8_t> data(static_cast<size_t>(size));
            file.read(reinterpret_cast<char*>(data.data()), size);
            
            // Create buffer in runtime (thread-safe operation)
            Value bufVal = runtime->makeBuffer(data.size());
            const BufferRef& bufRef = std::get<BufferRef>(bufVal);
            auto* buf = runtime->getBuffer(bufRef);
            if (buf) {
                *buf = std::move(data);
            }
            
            return bufVal;
        });
    });

    // ------------------------------------------------------------------------
    // system.fs.writeAsync(path: string, content: string) -> task<bool>
    // Write string to file asynchronously
    // ------------------------------------------------------------------------
    reg.registerFunction("system.fs.writeAsync", [](const std::vector<Value>& args) -> Value {
        if (!rt()) {
            throw LanguageException("RuntimeError", "system.fs.writeAsync: no runtime");
        }
        if (args.size() < 2 || !isString(args[0]) || !isString(args[1])) {
            throw LanguageException("TypeError", "system.fs.writeAsync: requires (path: string, content: string)");
        }
        
        std::string pathStr = std::get<std::string>(args[0]);
        std::string content = std::get<std::string>(args[1]);
        
        return rt()->submitTask([pathStr, content]() -> Value {
            std::ofstream file(toFsPath(pathStr), std::ios::out | std::ios::trunc);
            if (!file.is_open()) {
                throw LanguageException("IOError", "system.fs.writeAsync: cannot open file: " + pathStr);
            }
            
            file << content;
            return Value(true);
        });
    });

    // ------------------------------------------------------------------------
    // system.fs.writeBytesAsync(path: string, buffer: buffer) -> task<bool>
    // Write buffer to file asynchronously
    // ------------------------------------------------------------------------
    reg.registerFunction("system.fs.writeBytesAsync", [](const std::vector<Value>& args) -> Value {
        if (!rt()) {
            throw LanguageException("RuntimeError", "system.fs.writeBytesAsync: no runtime");
        }
        if (args.size() < 2 || !isString(args[0]) || !isBufferRef(args[1])) {
            throw LanguageException("TypeError", "system.fs.writeBytesAsync: requires (path: string, buffer: buffer)");
        }
        
        std::string pathStr = std::get<std::string>(args[0]);
        const BufferRef& bufRef = std::get<BufferRef>(args[1]);
        
        // Copy buffer data for async operation
        const auto* buf = rt()->getBuffer(bufRef);
        if (!buf) {
            throw LanguageException("RuntimeError", "system.fs.writeBytesAsync: invalid buffer");
        }
        std::vector<uint8_t> data = *buf;  // Copy the data
        
        return rt()->submitTask([pathStr, data]() -> Value {
            std::ofstream file(toFsPath(pathStr), std::ios::binary | std::ios::out | std::ios::trunc);
            if (!file.is_open()) {
                throw LanguageException("IOError", "system.fs.writeBytesAsync: cannot open file: " + pathStr);
            }
            
            file.write(reinterpret_cast<const char*>(data.data()), data.size());
            return Value(true);
        });
    });

    // ------------------------------------------------------------------------
    // system.fs.copyAsync(from: string, to: string) -> task<bool>
    // Copy file asynchronously
    // ------------------------------------------------------------------------
    reg.registerFunction("system.fs.copyAsync", [](const std::vector<Value>& args) -> Value {
        if (!rt()) {
            throw LanguageException("RuntimeError", "system.fs.copyAsync: no runtime");
        }
        if (args.size() < 2 || !isString(args[0]) || !isString(args[1])) {
            throw LanguageException("TypeError", "system.fs.copyAsync: requires (from: string, to: string)");
        }
        
        std::string fromPath = std::get<std::string>(args[0]);
        std::string toPath = std::get<std::string>(args[1]);
        
        return rt()->submitTask([fromPath, toPath]() -> Value {
            try {
                fs::copy_file(
                    toFsPath(fromPath),
                    toFsPath(toPath),
                    fs::copy_options::overwrite_existing
                );
                return Value(true);
            } catch (const fs::filesystem_error& e) {
                throw LanguageException("IOError", std::string("system.fs.copyAsync failed: ") + e.what());
            }
        });
    });

    // ------------------------------------------------------------------------
    // system.fs.deleteAsync(path: string) -> task<bool>
    // Delete file asynchronously
    // ------------------------------------------------------------------------
    reg.registerFunction("system.fs.deleteAsync", [](const std::vector<Value>& args) -> Value {
        if (!rt()) {
            throw LanguageException("RuntimeError", "system.fs.deleteAsync: no runtime");
        }
        if (args.empty() || !isString(args[0])) {
            throw LanguageException("TypeError", "system.fs.deleteAsync: argument must be string");
        }
        
        std::string pathStr = std::get<std::string>(args[0]);
        
        return rt()->submitTask([pathStr]() -> Value {
            try {
                return Value(fs::remove(toFsPath(pathStr)));
            } catch (const fs::filesystem_error& e) {
                throw LanguageException("IOError", std::string("system.fs.deleteAsync failed: ") + e.what());
            }
        });
    });

    // ========================================================================
    // UTILITY FUNCTIONS
    // ========================================================================

    // ------------------------------------------------------------------------
    // system.fs.isAbsolute(path: string) -> bool
    // Check if path is absolute
    // ------------------------------------------------------------------------
    reg.registerFunction("system.fs.isAbsolute", [](const std::vector<Value>& args) -> Value {
        if (args.empty() || !isString(args[0])) {
            throw LanguageException("TypeError", "system.fs.isAbsolute: argument must be string");
        }
        fs::path p = toFsPath(std::get<std::string>(args[0]));
        return Value(p.is_absolute());
    });

    // ------------------------------------------------------------------------
    // system.fs.isRelative(path: string) -> bool
    // Check if path is relative
    // ------------------------------------------------------------------------
    reg.registerFunction("system.fs.isRelative", [](const std::vector<Value>& args) -> Value {
        if (args.empty() || !isString(args[0])) {
            throw LanguageException("TypeError", "system.fs.isRelative: argument must be string");
        }
        fs::path p = toFsPath(std::get<std::string>(args[0]));
        return Value(p.is_relative());
    });

    // ------------------------------------------------------------------------
    // system.fs.normalize(path: string) -> string
    // Normalize path (resolve . and ..)
    // ------------------------------------------------------------------------
    reg.registerFunction("system.fs.normalize", [](const std::vector<Value>& args) -> Value {
        if (args.empty() || !isString(args[0])) {
            throw LanguageException("TypeError", "system.fs.normalize: argument must be string");
        }
        try {
            fs::path p = toFsPath(std::get<std::string>(args[0]));
            return Value(fromFsPath(p.lexically_normal()));
        } catch (const std::exception& e) {
            throw LanguageException("IOError", std::string("system.fs.normalize failed: ") + e.what());
        }
    });

    // ------------------------------------------------------------------------
    // system.fs.stem(path: string) -> string
    // Get filename without extension
    // ------------------------------------------------------------------------
    reg.registerFunction("system.fs.stem", [](const std::vector<Value>& args) -> Value {
        if (args.empty() || !isString(args[0])) {
            throw LanguageException("TypeError", "system.fs.stem: argument must be string");
        }
        fs::path p = toFsPath(std::get<std::string>(args[0]));
        return Value(fromFsPath(p.stem()));
    });

    // ------------------------------------------------------------------------
    // system.fs.tempDir() -> string
    // Get system temp directory
    // ------------------------------------------------------------------------
    reg.registerFunction("system.fs.tempDir", [](const std::vector<Value>& /*args*/) -> Value {
        try {
            return Value(fromFsPath(fs::temp_directory_path()));
        } catch (const fs::filesystem_error& e) {
            throw LanguageException("IOError", std::string("system.fs.tempDir failed: ") + e.what());
        }
    });
}

// Extension entry point
extern "C" __attribute__((visibility("default")))
void init_extension(FunctionRegistry& reg) {
    register_fs_functions(reg);
}
