/**
 * =============================================================================
 * Quartz JIT Stencils - x86_64 Copy-and-Patch Code Templates (FIXED VERSION)
 * =============================================================================
 *
 * These stencils use function arguments directly instead of absolute addresses.
 * 
 * JIT calling convention:
 *   - rdi: stack pointer (JITValue*) - points to current top of operand stack
 *   - rsi: locals pointer (JITValue*) - points to local variables array
 *   - rdx: runtime pointer (void*)
 *
 * The stencils receive these as arguments and operate on them directly.
 */

#include <stdint.h>
#include <stdbool.h>

// =============================================================================
// Value Layout (matches Quartz JITValue type - 16 bytes)
// =============================================================================

typedef struct {
    uint64_t bits;      // Raw bits (int64, double bits, or pointer)
    uint8_t  tag;       // 0=int, 1=double, 2=bool, 3=string_ptr, etc.
    uint8_t  _pad[7];   // Alignment padding to 16 bytes
} JITValue;

#define TAG_INT    0
#define TAG_DOUBLE 1
#define TAG_BOOL   2
#define TAG_STRING 3
#define TAG_ARRAY  4
#define TAG_DICT   5
#define TAG_NIL    6

#define JITVALUE_SIZE 16

// Hole markers for compile-time patching (only for constants, not pointers)
#define HOLE_IMM64 0xDEADBEEFCAFEBABEULL

volatile uint64_t HOLE_imm64 = HOLE_IMM64;

// =============================================================================
// Inline helpers
// =============================================================================

static inline int64_t jv_as_int(JITValue v) { return (int64_t)v.bits; }
static inline double jv_as_double(JITValue v) { return *(double*)&v.bits; }
static inline bool jv_as_bool(JITValue v) { return v.bits != 0; }

static inline JITValue jv_make_int(int64_t x) {
    JITValue v = { .bits = (uint64_t)x, .tag = TAG_INT };
    return v;
}

static inline JITValue jv_make_double(double x) {
    JITValue v;
    v.tag = TAG_DOUBLE;
    *(double*)&v.bits = x;
    return v;
}

static inline JITValue jv_make_bool(bool x) {
    JITValue v = { .bits = x ? 1 : 0, .tag = TAG_BOOL };
    return v;
}

// =============================================================================
// Stencil Declarations - Now using proper function arguments
// =============================================================================

#define STENCIL __attribute__((noinline, used))

// =============================================================================
// Stack Manipulation Stencils
// =============================================================================

/**
 * Push 32-bit integer constant onto stack
 * Arguments: stack (rdi), locals (rsi), runtime (rdx)
 * Holes: HOLE_IMM64 (the value to push)
 */
STENCIL
void stencil_push_int32(JITValue* stack, JITValue* locals, void* runtime) {
    (void)locals; (void)runtime;
    int32_t imm = (int32_t)HOLE_imm64;
    stack[0] = jv_make_int(imm);
}

/**
 * Push 64-bit integer constant onto stack
 */
STENCIL
void stencil_push_int64(JITValue* stack, JITValue* locals, void* runtime) {
    (void)locals; (void)runtime;
    int64_t imm = (int64_t)HOLE_imm64;
    stack[0] = jv_make_int(imm);
}

/**
 * Push 64-bit double constant onto stack
 */
STENCIL
void stencil_push_double(JITValue* stack, JITValue* locals, void* runtime) {
    (void)locals; (void)runtime;
    uint64_t bits = HOLE_imm64;
    JITValue v;
    v.bits = bits;
    v.tag = TAG_DOUBLE;
    stack[0] = v;
}

/**
 * Push boolean true
 */
STENCIL
void stencil_push_true(JITValue* stack, JITValue* locals, void* runtime) {
    (void)locals; (void)runtime;
    stack[0] = jv_make_bool(1);
}

/**
 * Push boolean false
 */
STENCIL
void stencil_push_false(JITValue* stack, JITValue* locals, void* runtime) {
    (void)locals; (void)runtime;
    stack[0] = jv_make_bool(0);
}

/**
 * Push integer 0 (optimized, common case)
 */
STENCIL
void stencil_push_int_0(JITValue* stack, JITValue* locals, void* runtime) {
    (void)locals; (void)runtime;
    stack[0] = jv_make_int(0);
}

/**
 * Push integer 1 (optimized, common case)
 */
STENCIL
void stencil_push_int_1(JITValue* stack, JITValue* locals, void* runtime) {
    (void)locals; (void)runtime;
    stack[0] = jv_make_int(1);
}

/**
 * Push integer -1 (optimized, common case)
 */
STENCIL
void stencil_push_int_neg1(JITValue* stack, JITValue* locals, void* runtime) {
    (void)locals; (void)runtime;
    stack[0] = jv_make_int(-1);
}

/**
 * Push nil
 */
STENCIL
void stencil_push_nil(JITValue* stack, JITValue* locals, void* runtime) {
    (void)locals; (void)runtime;
    JITValue v = { .bits = 0, .tag = TAG_NIL };
    stack[0] = v;
}

/**
 * Pop top of stack (no-op stencil)
 */
STENCIL
void stencil_pop(JITValue* stack, JITValue* locals, void* runtime) {
    (void)stack; (void)locals; (void)runtime;
    __asm__ volatile("nop");
}

/**
 * Pop 2 values from stack (no-op stencil)
 */
STENCIL
void stencil_pop2(JITValue* stack, JITValue* locals, void* runtime) {
    (void)stack; (void)locals; (void)runtime;
    __asm__ volatile("nop");
    __asm__ volatile("nop");
}

// =============================================================================
// Local Variable Stencils (Slot-Based)
// =============================================================================

/**
 * Load from slot 0 (most common local, optimized)
 * Loads locals[0] onto stack top
 */
STENCIL
void stencil_load_slot_0(JITValue* stack, JITValue* locals, void* runtime) {
    (void)runtime;
    stack[0] = locals[0];
}

/**
 * Load from slot 1
 */
STENCIL
void stencil_load_slot_1(JITValue* stack, JITValue* locals, void* runtime) {
    (void)runtime;
    stack[0] = locals[1];
}

/**
 * Load from slot 2
 */
STENCIL
void stencil_load_slot_2(JITValue* stack, JITValue* locals, void* runtime) {
    (void)runtime;
    stack[0] = locals[2];
}

/**
 * Load from slot 3
 */
STENCIL
void stencil_load_slot_3(JITValue* stack, JITValue* locals, void* runtime) {
    (void)runtime;
    stack[0] = locals[3];
}

/**
 * Store to slot 0
 * Stores stack top to locals[0]
 */
STENCIL
void stencil_store_slot_0(JITValue* stack, JITValue* locals, void* runtime) {
    (void)runtime;
    locals[0] = stack[0];
}

/**
 * Store to slot 1
 */
STENCIL
void stencil_store_slot_1(JITValue* stack, JITValue* locals, void* runtime) {
    (void)runtime;
    locals[1] = stack[0];
}

/**
 * Store to slot 2
 */
STENCIL
void stencil_store_slot_2(JITValue* stack, JITValue* locals, void* runtime) {
    (void)runtime;
    locals[2] = stack[0];
}

/**
 * Store to slot 3
 */
STENCIL
void stencil_store_slot_3(JITValue* stack, JITValue* locals, void* runtime) {
    (void)runtime;
    locals[3] = stack[0];
}

// =============================================================================
// Integer Arithmetic Stencils (Fast Path)
// =============================================================================

/**
 * Integer addition: stack[-1] = stack[-1] + stack[0]
 * Consumes top two, produces one (so caller adjusts stack ptr)
 */
STENCIL
void stencil_add_int(JITValue* stack, JITValue* locals, void* runtime) {
    (void)locals; (void)runtime;
    int64_t a = jv_as_int(stack[-1]);
    int64_t b = jv_as_int(stack[0]);
    stack[-1] = jv_make_int(a + b);
}

/**
 * Integer subtraction: stack[-1] = stack[-1] - stack[0]
 */
STENCIL
void stencil_sub_int(JITValue* stack, JITValue* locals, void* runtime) {
    (void)locals; (void)runtime;
    int64_t a = jv_as_int(stack[-1]);
    int64_t b = jv_as_int(stack[0]);
    stack[-1] = jv_make_int(a - b);
}

/**
 * Integer multiplication: stack[-1] = stack[-1] * stack[0]
 */
STENCIL
void stencil_mul_int(JITValue* stack, JITValue* locals, void* runtime) {
    (void)locals; (void)runtime;
    int64_t a = jv_as_int(stack[-1]);
    int64_t b = jv_as_int(stack[0]);
    stack[-1] = jv_make_int(a * b);
}

/**
 * Integer division: stack[-1] = stack[-1] / stack[0]
 */
STENCIL
void stencil_div_int(JITValue* stack, JITValue* locals, void* runtime) {
    (void)locals; (void)runtime;
    int64_t a = jv_as_int(stack[-1]);
    int64_t b = jv_as_int(stack[0]);
    // Note: division by zero should be handled elsewhere
    stack[-1] = jv_make_int(b != 0 ? a / b : 0);
}

/**
 * Integer modulo: stack[-1] = stack[-1] % stack[0]
 */
STENCIL
void stencil_mod_int(JITValue* stack, JITValue* locals, void* runtime) {
    (void)locals; (void)runtime;
    int64_t a = jv_as_int(stack[-1]);
    int64_t b = jv_as_int(stack[0]);
    stack[-1] = jv_make_int(b != 0 ? a % b : 0);
}

/**
 * Integer negation: stack[0] = -stack[0]
 */
STENCIL
void stencil_neg_int(JITValue* stack, JITValue* locals, void* runtime) {
    (void)locals; (void)runtime;
    int64_t a = jv_as_int(stack[0]);
    stack[0] = jv_make_int(-a);
}

// =============================================================================
// Comparison Stencils (Integer Fast Path)
// =============================================================================

/**
 * Equal: stack[-1] = (stack[-1] == stack[0])
 */
STENCIL
void stencil_eq_int(JITValue* stack, JITValue* locals, void* runtime) {
    (void)locals; (void)runtime;
    int64_t a = jv_as_int(stack[-1]);
    int64_t b = jv_as_int(stack[0]);
    stack[-1] = jv_make_bool(a == b);
}

/**
 * Not equal: stack[-1] = (stack[-1] != stack[0])
 */
STENCIL
void stencil_ne_int(JITValue* stack, JITValue* locals, void* runtime) {
    (void)locals; (void)runtime;
    int64_t a = jv_as_int(stack[-1]);
    int64_t b = jv_as_int(stack[0]);
    stack[-1] = jv_make_bool(a != b);
}

/**
 * Less than: stack[-1] = (stack[-1] < stack[0])
 */
STENCIL
void stencil_lt_int(JITValue* stack, JITValue* locals, void* runtime) {
    (void)locals; (void)runtime;
    int64_t a = jv_as_int(stack[-1]);
    int64_t b = jv_as_int(stack[0]);
    stack[-1] = jv_make_bool(a < b);
}

/**
 * Greater than: stack[-1] = (stack[-1] > stack[0])
 */
STENCIL
void stencil_gt_int(JITValue* stack, JITValue* locals, void* runtime) {
    (void)locals; (void)runtime;
    int64_t a = jv_as_int(stack[-1]);
    int64_t b = jv_as_int(stack[0]);
    stack[-1] = jv_make_bool(a > b);
}

/**
 * Less than or equal: stack[-1] = (stack[-1] <= stack[0])
 */
STENCIL
void stencil_le_int(JITValue* stack, JITValue* locals, void* runtime) {
    (void)locals; (void)runtime;
    int64_t a = jv_as_int(stack[-1]);
    int64_t b = jv_as_int(stack[0]);
    stack[-1] = jv_make_bool(a <= b);
}

/**
 * Greater than or equal: stack[-1] = (stack[-1] >= stack[0])
 */
STENCIL
void stencil_ge_int(JITValue* stack, JITValue* locals, void* runtime) {
    (void)locals; (void)runtime;
    int64_t a = jv_as_int(stack[-1]);
    int64_t b = jv_as_int(stack[0]);
    stack[-1] = jv_make_bool(a >= b);
}

/**
 * Logical NOT: stack[0] = !stack[0]
 */
STENCIL
void stencil_not(JITValue* stack, JITValue* locals, void* runtime) {
    (void)locals; (void)runtime;
    bool val = jv_as_bool(stack[0]);
    stack[0] = jv_make_bool(!val);
}

// =============================================================================
// Double Arithmetic Stencils
// =============================================================================

STENCIL
void stencil_add_double(JITValue* stack, JITValue* locals, void* runtime) {
    (void)locals; (void)runtime;
    double a = jv_as_double(stack[-1]);
    double b = jv_as_double(stack[0]);
    stack[-1] = jv_make_double(a + b);
}

STENCIL
void stencil_sub_double(JITValue* stack, JITValue* locals, void* runtime) {
    (void)locals; (void)runtime;
    double a = jv_as_double(stack[-1]);
    double b = jv_as_double(stack[0]);
    stack[-1] = jv_make_double(a - b);
}

STENCIL
void stencil_mul_double(JITValue* stack, JITValue* locals, void* runtime) {
    (void)locals; (void)runtime;
    double a = jv_as_double(stack[-1]);
    double b = jv_as_double(stack[0]);
    stack[-1] = jv_make_double(a * b);
}

STENCIL
void stencil_div_double(JITValue* stack, JITValue* locals, void* runtime) {
    (void)locals; (void)runtime;
    double a = jv_as_double(stack[-1]);
    double b = jv_as_double(stack[0]);
    stack[-1] = jv_make_double(a / b);
}

STENCIL
void stencil_neg_double(JITValue* stack, JITValue* locals, void* runtime) {
    (void)locals; (void)runtime;
    double a = jv_as_double(stack[0]);
    stack[0] = jv_make_double(-a);
}

// =============================================================================
// Return Stencils
// =============================================================================

/**
 * Return with value (result is in stack[0])
 */
STENCIL
void stencil_return_value(JITValue* stack, JITValue* locals, void* runtime) {
    (void)stack; (void)locals; (void)runtime;
    // The JIT prologue/epilogue handles the actual return
    // Stack[0] will be read by the caller after return
}

/**
 * Return void (no value)
 */
STENCIL
void stencil_return_void(JITValue* stack, JITValue* locals, void* runtime) {
    (void)stack; (void)locals; (void)runtime;
    // Nothing to do - just return
}

// =============================================================================
// Increment/Decrement Slot Stencils
// =============================================================================

STENCIL
void stencil_inc_slot_0(JITValue* stack, JITValue* locals, void* runtime) {
    (void)stack; (void)runtime;
    int64_t val = jv_as_int(locals[0]);
    locals[0] = jv_make_int(val + 1);
}

STENCIL
void stencil_inc_slot_1(JITValue* stack, JITValue* locals, void* runtime) {
    (void)stack; (void)runtime;
    int64_t val = jv_as_int(locals[1]);
    locals[1] = jv_make_int(val + 1);
}

STENCIL
void stencil_dec_slot_0(JITValue* stack, JITValue* locals, void* runtime) {
    (void)stack; (void)runtime;
    int64_t val = jv_as_int(locals[0]);
    locals[0] = jv_make_int(val - 1);
}

STENCIL
void stencil_dec_slot_1(JITValue* stack, JITValue* locals, void* runtime) {
    (void)stack; (void)runtime;
    int64_t val = jv_as_int(locals[1]);
    locals[1] = jv_make_int(val - 1);
}
