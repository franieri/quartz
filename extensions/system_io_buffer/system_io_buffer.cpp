// ============================================================================
// system.io.buffer Extension
// Provides byte buffer primitives for binary I/O operations
// ============================================================================

#include "function_registry.h"
#include "runtime.h"

#include <string>
#include <vector>
#include <sstream>
#include <iomanip>

static inline Runtime* rt() {
    return global_runtime_ptr;
}

static inline bool isInt(const Value& v) {
    return std::holds_alternative<int>(v);
}

static inline bool isString(const Value& v) {
    return std::holds_alternative<std::string>(v);
}

static inline bool isBufferRef(const Value& v) {
    return std::holds_alternative<BufferRef>(v);
}

// Hex encoding helpers
static std::string toHex(const std::vector<uint8_t>& data) {
    std::ostringstream oss;
    for (uint8_t byte : data) {
        oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte);
    }
    return oss.str();
}

static std::vector<uint8_t> fromHex(const std::string& hex) {
    std::vector<uint8_t> data;
    if (hex.length() % 2 != 0) return data;
    
    for (size_t i = 0; i < hex.length(); i += 2) {
        std::string byteStr = hex.substr(i, 2);
        try {
            uint8_t byte = static_cast<uint8_t>(std::stoi(byteStr, nullptr, 16));
            data.push_back(byte);
        } catch (...) {
            return std::vector<uint8_t>();  // Invalid hex
        }
    }
    return data;
}

// Base64 encoding helpers
static const char* base64Chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static std::string toBase64(const std::vector<uint8_t>& data) {
    std::string result;
    int val = 0, valb = -6;
    for (uint8_t c : data) {
        val = (val << 8) + c;
        valb += 8;
        while (valb >= 0) {
            result.push_back(base64Chars[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }
    if (valb > -6) {
        result.push_back(base64Chars[((val << 8) >> (valb + 8)) & 0x3F]);
    }
    while (result.size() % 4) {
        result.push_back('=');
    }
    return result;
}

static std::vector<uint8_t> fromBase64(const std::string& encoded) {
    std::vector<uint8_t> result;
    int val = 0, valb = -8;
    for (char c : encoded) {
        if (c == '=') break;
        const char* p = strchr(base64Chars, c);
        if (!p) continue;
        val = (val << 6) + static_cast<int>(p - base64Chars);
        valb += 6;
        if (valb >= 0) {
            result.push_back(static_cast<uint8_t>((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return result;
}

void register_io_buffer_functions(FunctionRegistry& reg) {
    // ------------------------------------------------------------------------
    // system.io.buffer.create(capacity?: int) -> buffer
    // Creates a new empty buffer with optional initial capacity.
    // ------------------------------------------------------------------------
    reg.registerFunction("system.io.buffer.create", [](const std::vector<Value>& args) -> Value {
        if (!rt()) {
            throw LanguageException("RuntimeError", "Runtime not available");
        }
        size_t capacity = 0;
        if (!args.empty() && isInt(args[0])) {
            int cap = std::get<int>(args[0]);
            if (cap > 0) capacity = static_cast<size_t>(cap);
        }
        return rt()->makeBuffer(capacity);
    });

    // ------------------------------------------------------------------------
    // system.io.buffer.fromString(s: string) -> buffer
    // Creates a buffer from a string (copies bytes).
    // ------------------------------------------------------------------------
    reg.registerFunction("system.io.buffer.fromString", [](const std::vector<Value>& args) -> Value {
        if (!rt() || args.empty()) {
            throw LanguageException("RuntimeError", "system.io.buffer.fromString requires a string argument");
        }
        if (!isString(args[0])) {
            throw LanguageException("TypeError", "system.io.buffer.fromString: argument must be a string");
        }
        return rt()->makeBufferFromString(std::get<std::string>(args[0]));
    });

    // ------------------------------------------------------------------------
    // system.io.buffer.toString(b: buffer) -> string
    // Converts buffer contents to a string.
    // ------------------------------------------------------------------------
    reg.registerFunction("system.io.buffer.toString", [](const std::vector<Value>& args) -> Value {
        if (!rt() || args.empty()) {
            throw LanguageException("RuntimeError", "system.io.buffer.toString requires a buffer argument");
        }
        if (!isBufferRef(args[0])) {
            throw LanguageException("TypeError", "system.io.buffer.toString: argument must be a buffer");
        }
        return Value(rt()->bufferToString(std::get<BufferRef>(args[0])));
    });

    // ------------------------------------------------------------------------
    // system.io.buffer.size(b: buffer) -> int
    // Returns the number of bytes in the buffer.
    // ------------------------------------------------------------------------
    reg.registerFunction("system.io.buffer.size", [](const std::vector<Value>& args) -> Value {
        if (!rt() || args.empty()) {
            throw LanguageException("RuntimeError", "system.io.buffer.size requires a buffer argument");
        }
        if (!isBufferRef(args[0])) {
            throw LanguageException("TypeError", "system.io.buffer.size: argument must be a buffer");
        }
        return Value(static_cast<int>(rt()->bufferSize(std::get<BufferRef>(args[0]))));
    });

    // ------------------------------------------------------------------------
    // system.io.buffer.capacity(b: buffer) -> int
    // Returns the capacity of the buffer.
    // ------------------------------------------------------------------------
    reg.registerFunction("system.io.buffer.capacity", [](const std::vector<Value>& args) -> Value {
        if (!rt() || args.empty()) {
            throw LanguageException("RuntimeError", "system.io.buffer.capacity requires a buffer argument");
        }
        if (!isBufferRef(args[0])) {
            throw LanguageException("TypeError", "system.io.buffer.capacity: argument must be a buffer");
        }
        return Value(static_cast<int>(rt()->bufferCapacity(std::get<BufferRef>(args[0]))));
    });

    // ------------------------------------------------------------------------
    // system.io.buffer.append(b: buffer, data: string|buffer) -> bool
    // Appends data to the buffer.
    // ------------------------------------------------------------------------
    reg.registerFunction("system.io.buffer.append", [](const std::vector<Value>& args) -> Value {
        if (!rt() || args.size() < 2) {
            throw LanguageException("RuntimeError", "system.io.buffer.append requires (buffer, data) arguments");
        }
        if (!isBufferRef(args[0])) {
            throw LanguageException("TypeError", "system.io.buffer.append: first argument must be a buffer");
        }
        const BufferRef& ref = std::get<BufferRef>(args[0]);
        
        if (isString(args[1])) {
            return Value(rt()->bufferAppendString(ref, std::get<std::string>(args[1])));
        } else if (isBufferRef(args[1])) {
            return Value(rt()->bufferAppendBuffer(ref, std::get<BufferRef>(args[1])));
        } else {
            throw LanguageException("TypeError", "system.io.buffer.append: data must be string or buffer");
        }
    });

    // ------------------------------------------------------------------------
    // system.io.buffer.slice(b: buffer, start: int, end?: int) -> buffer
    // Returns a new buffer with a slice of the original.
    // ------------------------------------------------------------------------
    reg.registerFunction("system.io.buffer.slice", [](const std::vector<Value>& args) -> Value {
        if (!rt() || args.size() < 2) {
            throw LanguageException("RuntimeError", "system.io.buffer.slice requires (buffer, start, end?) arguments");
        }
        if (!isBufferRef(args[0])) {
            throw LanguageException("TypeError", "system.io.buffer.slice: first argument must be a buffer");
        }
        if (!isInt(args[1])) {
            throw LanguageException("TypeError", "system.io.buffer.slice: start must be an int");
        }
        
        const BufferRef& ref = std::get<BufferRef>(args[0]);
        int start = std::get<int>(args[1]);
        if (start < 0) start = 0;
        
        size_t end = rt()->bufferSize(ref);
        if (args.size() > 2 && isInt(args[2])) {
            int e = std::get<int>(args[2]);
            if (e >= 0) end = static_cast<size_t>(e);
        }
        
        return rt()->bufferSlice(ref, static_cast<size_t>(start), end);
    });

    // ------------------------------------------------------------------------
    // system.io.buffer.clear(b: buffer) -> bool
    // Clears the buffer contents.
    // ------------------------------------------------------------------------
    reg.registerFunction("system.io.buffer.clear", [](const std::vector<Value>& args) -> Value {
        if (!rt() || args.empty()) {
            throw LanguageException("RuntimeError", "system.io.buffer.clear requires a buffer argument");
        }
        if (!isBufferRef(args[0])) {
            throw LanguageException("TypeError", "system.io.buffer.clear: argument must be a buffer");
        }
        return Value(rt()->bufferClear(std::get<BufferRef>(args[0])));
    });

    // ------------------------------------------------------------------------
    // system.io.buffer.copy(b: buffer) -> buffer
    // Returns a copy of the buffer.
    // ------------------------------------------------------------------------
    reg.registerFunction("system.io.buffer.copy", [](const std::vector<Value>& args) -> Value {
        if (!rt() || args.empty()) {
            throw LanguageException("RuntimeError", "system.io.buffer.copy requires a buffer argument");
        }
        if (!isBufferRef(args[0])) {
            throw LanguageException("TypeError", "system.io.buffer.copy: argument must be a buffer");
        }
        return rt()->bufferCopy(std::get<BufferRef>(args[0]));
    });

    // ------------------------------------------------------------------------
    // system.io.buffer.getByte(b: buffer, index: int) -> int
    // Returns the byte at the given index, or -1 if out of bounds.
    // ------------------------------------------------------------------------
    reg.registerFunction("system.io.buffer.getByte", [](const std::vector<Value>& args) -> Value {
        if (!rt() || args.size() < 2) {
            throw LanguageException("RuntimeError", "system.io.buffer.getByte requires (buffer, index) arguments");
        }
        if (!isBufferRef(args[0])) {
            throw LanguageException("TypeError", "system.io.buffer.getByte: first argument must be a buffer");
        }
        if (!isInt(args[1])) {
            throw LanguageException("TypeError", "system.io.buffer.getByte: index must be an int");
        }
        int index = std::get<int>(args[1]);
        if (index < 0) return Value(-1);
        return Value(rt()->bufferGetByte(std::get<BufferRef>(args[0]), static_cast<size_t>(index)));
    });

    // ------------------------------------------------------------------------
    // system.io.buffer.setByte(b: buffer, index: int, value: int) -> bool
    // Sets the byte at the given index.
    // ------------------------------------------------------------------------
    reg.registerFunction("system.io.buffer.setByte", [](const std::vector<Value>& args) -> Value {
        if (!rt() || args.size() < 3) {
            throw LanguageException("RuntimeError", "system.io.buffer.setByte requires (buffer, index, value) arguments");
        }
        if (!isBufferRef(args[0])) {
            throw LanguageException("TypeError", "system.io.buffer.setByte: first argument must be a buffer");
        }
        if (!isInt(args[1])) {
            throw LanguageException("TypeError", "system.io.buffer.setByte: index must be an int");
        }
        if (!isInt(args[2])) {
            throw LanguageException("TypeError", "system.io.buffer.setByte: value must be an int");
        }
        int index = std::get<int>(args[1]);
        int value = std::get<int>(args[2]);
        if (index < 0) return Value(false);
        return Value(rt()->bufferSetByte(std::get<BufferRef>(args[0]), 
                                         static_cast<size_t>(index), 
                                         static_cast<uint8_t>(value & 0xFF)));
    });

    // ------------------------------------------------------------------------
    // system.io.buffer.toHex(b: buffer) -> string
    // Converts buffer to hexadecimal string.
    // ------------------------------------------------------------------------
    reg.registerFunction("system.io.buffer.toHex", [](const std::vector<Value>& args) -> Value {
        if (!rt() || args.empty()) {
            throw LanguageException("RuntimeError", "system.io.buffer.toHex requires a buffer argument");
        }
        if (!isBufferRef(args[0])) {
            throw LanguageException("TypeError", "system.io.buffer.toHex: argument must be a buffer");
        }
        const auto* buf = rt()->getBuffer(std::get<BufferRef>(args[0]));
        if (!buf) return Value(std::string(""));
        return Value(toHex(*buf));
    });

    // ------------------------------------------------------------------------
    // system.io.buffer.fromHex(s: string) -> buffer
    // Creates buffer from hexadecimal string.
    // ------------------------------------------------------------------------
    reg.registerFunction("system.io.buffer.fromHex", [](const std::vector<Value>& args) -> Value {
        if (!rt() || args.empty()) {
            throw LanguageException("RuntimeError", "system.io.buffer.fromHex requires a string argument");
        }
        if (!isString(args[0])) {
            throw LanguageException("TypeError", "system.io.buffer.fromHex: argument must be a string");
        }
        std::vector<uint8_t> data = fromHex(std::get<std::string>(args[0]));
        Value buf = rt()->makeBuffer(data.size());
        auto* storage = rt()->getBuffer(std::get<BufferRef>(buf));
        if (storage) *storage = std::move(data);
        return buf;
    });

    // ------------------------------------------------------------------------
    // system.io.buffer.toBase64(b: buffer) -> string
    // Converts buffer to Base64 string.
    // ------------------------------------------------------------------------
    reg.registerFunction("system.io.buffer.toBase64", [](const std::vector<Value>& args) -> Value {
        if (!rt() || args.empty()) {
            throw LanguageException("RuntimeError", "system.io.buffer.toBase64 requires a buffer argument");
        }
        if (!isBufferRef(args[0])) {
            throw LanguageException("TypeError", "system.io.buffer.toBase64: argument must be a buffer");
        }
        const auto* buf = rt()->getBuffer(std::get<BufferRef>(args[0]));
        if (!buf) return Value(std::string(""));
        return Value(toBase64(*buf));
    });

    // ------------------------------------------------------------------------
    // system.io.buffer.fromBase64(s: string) -> buffer
    // Creates buffer from Base64 string.
    // ------------------------------------------------------------------------
    reg.registerFunction("system.io.buffer.fromBase64", [](const std::vector<Value>& args) -> Value {
        if (!rt() || args.empty()) {
            throw LanguageException("RuntimeError", "system.io.buffer.fromBase64 requires a string argument");
        }
        if (!isString(args[0])) {
            throw LanguageException("TypeError", "system.io.buffer.fromBase64: argument must be a string");
        }
        std::vector<uint8_t> data = fromBase64(std::get<std::string>(args[0]));
        Value buf = rt()->makeBuffer(data.size());
        auto* storage = rt()->getBuffer(std::get<BufferRef>(buf));
        if (storage) *storage = std::move(data);
        return buf;
    });

    // ------------------------------------------------------------------------
    // system.io.buffer.equals(a: buffer, b: buffer) -> bool
    // Compares two buffers for equality.
    // ------------------------------------------------------------------------
    reg.registerFunction("system.io.buffer.equals", [](const std::vector<Value>& args) -> Value {
        if (!rt() || args.size() < 2) {
            throw LanguageException("RuntimeError", "system.io.buffer.equals requires two buffer arguments");
        }
        if (!isBufferRef(args[0]) || !isBufferRef(args[1])) {
            throw LanguageException("TypeError", "system.io.buffer.equals: arguments must be buffers");
        }
        const auto* a = rt()->getBuffer(std::get<BufferRef>(args[0]));
        const auto* b = rt()->getBuffer(std::get<BufferRef>(args[1]));
        if (!a || !b) return Value(false);
        return Value(*a == *b);
    });
}

// Extension entry point
extern "C" __attribute__((visibility("default")))
void init_extension(FunctionRegistry& reg) {
    register_io_buffer_functions(reg);
}
