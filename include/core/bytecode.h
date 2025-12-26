#ifndef BYTECODE_H
#define BYTECODE_H

#include "types.h"

#include <cstdint>
#include <array>
#include <string>
#include <unordered_map>
#include <vector>

#include <quartz.h>

#include <deque>

// A compact stack-based bytecode format for Quartz.
// Design goals:
// - Single output file for whole program (incl. file-based modules)
// - Fast dispatch (byte-oriented opcodes, fixed-width immediates)
// - Mirrors current interpreter semantics (Runtime::executeNode/evaluate)

namespace bc {

static constexpr uint32_t kInvalidIndex = 0xFFFFFFFFu;
static constexpr uint32_t kMagic = qz::kQzbMagicQZB1;
static constexpr uint32_t kMagicLegacyJLB1 = qz::kQzbMagicLegacyJLB1;
static constexpr uint16_t kVersion = qz::kQzbFormatVersion;

enum class OpCode : uint8_t {
    NOP = 0,

    // Stack / constants
    PUSH_INT32,
    PUSH_DOUBLE64,
    PUSH_BOOL,
    PUSH_STRING,      // u32 stringIndex
    POP,

    // Variables
    LOAD_VAR,         // u32 nameStringIndex
    STORE_VAR,        // u32 nameStringIndex

    // Declarations with runtime side-effects
    DECLARE_ARRAY,    // u32 varName, u16 count  (values on stack)
    DECLARE_DICT,     // u32 varName, u16 count, then count*u32 keyStringIndex (values on stack)
    DECLARE_LAMBDA,   // u32 varName, u32 functionIndex

    // Expressions
    BINARY_OP,        // u8 op
    UNARY_OP,         // u8 op
    INDEX_GET,        // u32 varName (index on stack)

    // Control flow
    JUMP,             // i32 rel
    JUMP_IF_FALSE,    // i32 rel (pops condition)
    JUMP_IF_TRUE,     // i32 rel (pops condition)

    // Calls / objects
    CALL_NAME,        // u32 nameStringIndex, u8 argc (args on stack)
    NEW_OBJECT,       // u32 classNameStringIndex, u8 argc

    // Lambdas
    MAKE_LAMBDA,      // u32 functionIndex

    // Exceptions
    TRY_PUSH,         // u32 catchIp, u32 finallyIp, u8 hasFinally, u32 catchVarName, u32 catchTypeName
    TRY_POP,
    CATCH_CLEAR,      // u32 catchVarName
    THROW_VALUE,      // (message value on stack)
    THROW_NEW,        // u32 typeName (message value on stack)
    FINALLY_END,      // checks pending rethrow

    // Definitions
    DEF_CLASS,        // packed payload (see compiler/vm)
    DEF_INTERFACE,    // packed payload

    // Module context
    SET_CURRENT_MODULE, // u32 modulePathStringIndex
    CLEAR_CURRENT_MODULE,

    // Function return
    RETURN_VALUE,     // (value on stack)
    RETURN_VOID,

    // Locals (slot-based fast path)
    // NOTE: appended to preserve opcode numeric values in v1 bytecode.
    LOAD_SLOT,        // u16 slotIndex
    STORE_SLOT,       // u16 slotIndex

    // Expression literals (array/dict) that produce a value without binding to a variable
    MAKE_ARRAY_EXPR,  // u16 count (values on stack)
    MAKE_DICT_EXPR,   // u16 count, then count*u32 keyStringIndex (values on stack)
};

enum class BinaryOp : uint8_t {
    ADD = 0,
    SUB,
    MUL,
    DIV,
    EQ,
    NE,
    LT,
    GT,
    LE,
    GE,
};

enum class UnaryOp : uint8_t {
    NEG = 0,
    NOT,
};

struct Function {
    uint32_t nameString = kInvalidIndex; // optional
    std::vector<uint32_t> paramNameStrings; // for binding
    // Slot locals (includes params first). Slots are indexed by position.
    std::vector<uint32_t> localNameStrings;
    std::vector<uint8_t> code;
};

struct Program {
    std::vector<std::string> strings;
    // deque keeps references stable while compiler pushes more functions
    std::deque<Function> functions;
    uint32_t entryFunction = 0;

    // modulePath -> function index (module init)
    std::unordered_map<std::string, uint32_t> modules;

    // Provenance (written into .qzb v4+ metadata section)
    struct SourceFileMeta {
        std::string path;
        uint64_t sizeBytes = 0;
        std::array<uint8_t, 32> sha256{};
    };
    std::vector<SourceFileMeta> sources;
    std::string compilerOptions;
};

bool writeProgramToFile(const Program& program, const std::string& filePath, std::string* error);
bool readProgramFromFile(const std::string& filePath, Program* outProgram, std::string* error);

} // namespace bc

#endif // BYTECODE_H
