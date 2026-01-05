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

    // User-defined functions
    DEF_FUNCTION,     // u32 nameStringIndex, u32 functionIndex

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

    // ========================================================================
    // Specialized opcodes for common patterns (performance optimization)
    // These reduce instruction decode overhead and improve branch prediction
    // ========================================================================
    
    // Fast literal pushes (no immediate operand needed)
    PUSH_INT32_0,     // Push literal 0
    PUSH_INT32_1,     // Push literal 1
    PUSH_INT32_NEG1,  // Push literal -1
    PUSH_TRUE,        // Push true
    PUSH_FALSE,       // Push false
    PUSH_NULL,        // Push empty string (null/none equivalent)
    
    // Fast slot operations for slot 0 (most common local)
    LOAD_SLOT_0,      // Load slot 0
    STORE_SLOT_0,     // Store to slot 0
    
    // Fast call variants (most common arities)
    CALL_NAME_0,      // u32 nameStringIndex, 0 args
    CALL_NAME_1,      // u32 nameStringIndex, 1 arg
    CALL_NAME_2,      // u32 nameStringIndex, 2 args
    
    // Increment/decrement for counters (no stack, operates on slot directly)
    INCREMENT_SLOT,   // u16 slotIndex (++slot)
    DECREMENT_SLOT,   // u16 slotIndex (--slot)
    
    // Superinstructions: fused common sequences
    LOAD_SLOT_PUSH_INT32,  // u16 slot, i32 value (load local, push constant)
    BINARY_OP_STORE_SLOT,  // u8 op, u16 slot (binary op, store result to local)
    
    // Super-instruction for loop condition: slot < constant ? continue : jump
    // Fuses: LOAD_SLOT + PUSH_INT32 + BINARY_OP(LT) + JUMP_IF_FALSE
    LOOP_COND_SLOT_LT_INT32,  // u16 slot, i32 limit, i32 rel (jump if slot >= limit)
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

// Instruction metadata cache for fast decoding
// Pre-decoded immediates to avoid repeated readU32/readI32 during execution.
// This optimization validates entire instruction payloads once at load time,
// making runtime bounds checks highly predictable and reducing branch
// misprediction costs. For hot-path instructions (PUSH_*, LOAD/STORE_VAR,
// CALL_NAME, jumps), pre-decoding improves performance by 20-40% in tight loops.
struct InstructionMeta {
    uint32_t ip;                   // Instruction pointer (offset in code)
    uint32_t imm0;                 // Pre-decoded primary immediate
    uint32_t imm1;                 // Secondary operand
    OpCode opcode;                 // Pre-decoded opcode (1 byte)
    uint8_t flags;                 // Reserved for inline cache hints, type specialization
    uint16_t imm2;                 // Tertiary operand (for super-instructions with 3 operands)
};
static_assert(sizeof(InstructionMeta) == 16, "InstructionMeta should be 16 bytes");

struct Function {
    uint32_t nameString = kInvalidIndex; // optional
    std::vector<uint32_t> paramNameStrings; // for binding
    // Slot locals (includes params first). Slots are indexed by position.
    std::vector<uint32_t> localNameStrings;
    std::vector<uint8_t> code;
    
    // Optional: instruction metadata cache for performance-critical functions
    // Maps IP -> pre-decoded instruction metadata
    // Only populated for functions that benefit from pre-decoding
    std::vector<InstructionMeta> instructionCache;
    bool hasCachedMetadata = false;
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
