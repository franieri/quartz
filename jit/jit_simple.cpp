/**
 * =============================================================================
 * Quartz Simple JIT Compiler - Direct Code Generation
 * =============================================================================
 *
 * A simpler JIT approach that generates x86-64 code directly without stencils.
 * This is more maintainable and avoids the complexity of copy-and-patch for
 * operations that depend on runtime values like stack pointers.
 *
 * Register Convention:
 *   - RBX: operand stack pointer (callee-saved, preserved across calls)
 *   - R12: locals pointer (callee-saved)
 *   - R13: runtime pointer (callee-saved)
 *   - RAX, RCX, RDX, R8-R11: scratch registers
 *
 * JITValue Layout (16 bytes):
 *   - bytes 0-7: value bits (int64, double bits, or pointer)
 *   - byte 8: type tag
 *   - bytes 9-15: padding
 */

#include "jit.h"
#include "bytecode.h"
#include "runtime.h"

#include <cstring>
#include <vector>

#ifdef QZ_JIT_DEBUG
#include <iostream>
#endif

#if defined(__x86_64__) || defined(_M_X64)

namespace qz::jit {

// =============================================================================
// x86-64 Code Emitter Helper
// =============================================================================

class X64Emitter {
public:
    std::vector<uint8_t> code;
    
    void emit(uint8_t b) { code.push_back(b); }
    void emit16(uint16_t v) { emit(v & 0xFF); emit((v >> 8) & 0xFF); }
    void emit32(uint32_t v) { 
        emit(v & 0xFF); emit((v >> 8) & 0xFF); 
        emit((v >> 16) & 0xFF); emit((v >> 24) & 0xFF); 
    }
    void emit64(uint64_t v) {
        emit32(v & 0xFFFFFFFF);
        emit32((v >> 32) & 0xFFFFFFFF);
    }
    
    size_t pos() const { return code.size(); }
    
    // REX prefix helpers
    void rexW() { emit(0x48); }  // 64-bit operand
    void rexWB() { emit(0x49); }  // 64-bit + extended rm
    void rexWR() { emit(0x4C); }  // 64-bit + extended reg
    void rexWRB() { emit(0x4D); }  // 64-bit + both extended
    
    // ==========================================================================
    // Prologue/Epilogue
    // ==========================================================================
    
    void emitPrologue() {
        // push rbp
        emit(0x55);
        // mov rbp, rsp
        rexW(); emit(0x89); emit(0xE5);
        // push rbx (will hold stack ptr)
        emit(0x53);
        // push r12 (will hold locals ptr)
        emit(0x41); emit(0x54);
        // push r13 (will hold runtime ptr)
        emit(0x41); emit(0x55);
        // push r14 (scratch if needed)
        emit(0x41); emit(0x56);
        
        // mov rbx, rdi  (stack ptr)
        rexW(); emit(0x89); emit(0xFB);
        // mov r12, rsi  (locals ptr)
        rexWB(); emit(0x89); emit(0xF4);
        // mov r13, rdx  (runtime ptr)
        rexWB(); emit(0x89); emit(0xD5);
    }
    
    void emitEpilogue() {
        // pop r14
        emit(0x41); emit(0x5E);
        // pop r13
        emit(0x41); emit(0x5D);
        // pop r12
        emit(0x41); emit(0x5C);
        // pop rbx
        emit(0x5B);
        // pop rbp
        emit(0x5D);
        // ret
        emit(0xC3);
    }
    
    // ==========================================================================
    // Stack Operations (using RBX as stack pointer)
    // ==========================================================================
    
    // Push int64 constant onto JIT stack
    // stack[0].bits = imm; stack[0].tag = 0; rbx += 16
    void emitPushInt(int64_t value) {
        // mov qword [rbx], value
        rexW(); emit(0xC7); emit(0x03);
        emit32(static_cast<uint32_t>(value));  // Only works for 32-bit signed immediates
        if (value > INT32_MAX || value < INT32_MIN) {
            // Need movabs rax, imm64; mov [rbx], rax
            code.resize(code.size() - 6);  // Remove previous instruction
            rexW(); emit(0xB8);  // movabs rax, imm64
            emit64(static_cast<uint64_t>(value));
            rexW(); emit(0x89); emit(0x03);  // mov [rbx], rax
        }
        // mov byte [rbx+8], 0  (TAG_INT)
        emit(0xC6); emit(0x43); emit(0x08); emit(0x00);
        // add rbx, 16
        rexW(); emit(0x83); emit(0xC3); emit(0x10);
    }
    
    // Push int32 constant (optimized)
    void emitPushInt32(int32_t value) {
        // mov dword [rbx], value
        emit(0xC7); emit(0x03); emit32(static_cast<uint32_t>(value));
        // mov dword [rbx+4], 0  (high bits for sign extension)
        if (value < 0) {
            emit(0xC7); emit(0x43); emit(0x04); emit32(0xFFFFFFFF);
        } else {
            emit(0xC7); emit(0x43); emit(0x04); emit32(0);
        }
        // mov byte [rbx+8], 0  (TAG_INT)
        emit(0xC6); emit(0x43); emit(0x08); emit(0x00);
        // add rbx, 16
        rexW(); emit(0x83); emit(0xC3); emit(0x10);
    }
    
    // Push double constant
    void emitPushDouble(double value) {
        uint64_t bits;
        std::memcpy(&bits, &value, 8);
        // movabs rax, bits
        rexW(); emit(0xB8); emit64(bits);
        // mov [rbx], rax
        rexW(); emit(0x89); emit(0x03);
        // mov byte [rbx+8], 1  (TAG_DOUBLE)
        emit(0xC6); emit(0x43); emit(0x08); emit(0x01);
        // add rbx, 16
        rexW(); emit(0x83); emit(0xC3); emit(0x10);
    }
    
    // Push bool constant
    void emitPushBool(bool value) {
        // mov qword [rbx], value
        rexW(); emit(0xC7); emit(0x03); emit32(value ? 1 : 0);
        // mov byte [rbx+8], 2  (TAG_BOOL)
        emit(0xC6); emit(0x43); emit(0x08); emit(0x02);
        // add rbx, 16
        rexW(); emit(0x83); emit(0xC3); emit(0x10);
    }
    
    // Pop (just adjust stack pointer)
    void emitPop() {
        // sub rbx, 16
        rexW(); emit(0x83); emit(0xEB); emit(0x10);
    }
    
    // ==========================================================================
    // Local Variable Operations (using R12 as locals pointer)
    // ==========================================================================
    
    // Load local slot onto stack: stack[0] = locals[slot]; rbx += 16
    void emitLoadSlot(uint16_t slot) {
        // Calculate offset: slot * 16
        int32_t offset = static_cast<int32_t>(slot) * 16;
        
        // mov rax, [r12 + offset]  (load bits)
        rexWR(); emit(0x8B); 
        if (offset == 0) {
            emit(0x04); emit(0x24);  // [r12]
        } else if (offset <= 127) {
            emit(0x44); emit(0x24); emit(static_cast<uint8_t>(offset));
        } else {
            emit(0x84); emit(0x24); emit32(offset);
        }
        // mov [rbx], rax
        rexW(); emit(0x89); emit(0x03);
        
        // mov al, [r12 + offset + 8]  (load tag)
        emit(0x41); emit(0x8A);
        if (offset + 8 <= 127) {
            emit(0x44); emit(0x24); emit(static_cast<uint8_t>(offset + 8));
        } else {
            emit(0x84); emit(0x24); emit32(offset + 8);
        }
        // mov [rbx+8], al
        emit(0x88); emit(0x43); emit(0x08);
        
        // add rbx, 16
        rexW(); emit(0x83); emit(0xC3); emit(0x10);
    }
    
    // Store stack top to local slot: locals[slot] = stack[-1]; rbx -= 16
    void emitStoreSlot(uint16_t slot) {
        int32_t offset = static_cast<int32_t>(slot) * 16;
        
        // sub rbx, 16 first
        rexW(); emit(0x83); emit(0xEB); emit(0x10);
        
        // mov rax, [rbx]  (load bits from stack top)
        rexW(); emit(0x8B); emit(0x03);
        // mov [r12 + offset], rax  (store to local)
        rexWR(); emit(0x89);
        if (offset == 0) {
            emit(0x04); emit(0x24);
        } else if (offset <= 127) {
            emit(0x44); emit(0x24); emit(static_cast<uint8_t>(offset));
        } else {
            emit(0x84); emit(0x24); emit32(offset);
        }
        
        // mov al, [rbx+8]  (load tag)
        emit(0x8A); emit(0x43); emit(0x08);
        // mov [r12 + offset + 8], al
        emit(0x41); emit(0x88);
        if (offset + 8 <= 127) {
            emit(0x44); emit(0x24); emit(static_cast<uint8_t>(offset + 8));
        } else {
            emit(0x84); emit(0x24); emit32(offset + 8);
        }
    }
    
    // ==========================================================================
    // Integer Arithmetic (operates on top 2 stack values)
    // ==========================================================================
    
    // Binary op: stack[-2] = stack[-2] OP stack[-1]; rbx -= 16
    void emitBinaryIntOp(bc::BinaryOp op) {
        // Load operands: rax = stack[-2].bits, rcx = stack[-1].bits
        // sub rbx, 16
        rexW(); emit(0x83); emit(0xEB); emit(0x10);
        // mov rcx, [rbx]  (right operand, was stack top)
        rexW(); emit(0x8B); emit(0x0B);
        // mov rax, [rbx-16]  (left operand)
        rexW(); emit(0x8B); emit(0x43); emit(0xF0);
        
        switch (op) {
            case bc::BinaryOp::ADD:
                // add rax, rcx
                rexW(); emit(0x01); emit(0xC8);
                break;
            case bc::BinaryOp::SUB:
                // sub rax, rcx
                rexW(); emit(0x29); emit(0xC8);
                break;
            case bc::BinaryOp::MUL:
                // imul rax, rcx
                rexW(); emit(0x0F); emit(0xAF); emit(0xC1);
                break;
            case bc::BinaryOp::DIV:
                // cqo; idiv rcx
                rexW(); emit(0x99);  // cqo
                rexW(); emit(0xF7); emit(0xF9);  // idiv rcx
                break;
            case bc::BinaryOp::MOD:
                // cqo; idiv rcx; mov rax, rdx
                rexW(); emit(0x99);  // cqo
                rexW(); emit(0xF7); emit(0xF9);  // idiv rcx
                rexW(); emit(0x89); emit(0xD0);  // mov rax, rdx
                break;
            default:
                break;
        }
        
        // Store result: stack[-2].bits = rax
        // mov [rbx-16], rax
        rexW(); emit(0x89); emit(0x43); emit(0xF0);
        // tag is already TAG_INT (0)
    }
    
    // Comparison op: stack[-2] = (stack[-2] CMP stack[-1]); rbx -= 16
    void emitCompareIntOp(bc::BinaryOp op) {
        // sub rbx, 16
        rexW(); emit(0x83); emit(0xEB); emit(0x10);
        // mov rcx, [rbx]  (right operand)
        rexW(); emit(0x8B); emit(0x0B);
        // mov rax, [rbx-16]  (left operand)
        rexW(); emit(0x8B); emit(0x43); emit(0xF0);
        // cmp rax, rcx
        rexW(); emit(0x39); emit(0xC8);
        
        // setCC al
        emit(0x0F);
        switch (op) {
            case bc::BinaryOp::EQ: emit(0x94); break;  // sete
            case bc::BinaryOp::NE: emit(0x95); break;  // setne
            case bc::BinaryOp::LT: emit(0x9C); break;  // setl
            case bc::BinaryOp::GT: emit(0x9F); break;  // setg
            case bc::BinaryOp::LE: emit(0x9E); break;  // setle
            case bc::BinaryOp::GE: emit(0x9D); break;  // setge
            default: emit(0x94); break;
        }
        emit(0xC0);  // al
        
        // movzx rax, al
        rexW(); emit(0x0F); emit(0xB6); emit(0xC0);
        // mov [rbx-16], rax
        rexW(); emit(0x89); emit(0x43); emit(0xF0);
        // mov byte [rbx-16+8], 2  (TAG_BOOL)
        emit(0xC6); emit(0x43); emit(0xF8); emit(0x02);
    }
    
    // Unary negation: stack[0] = -stack[0]
    void emitNegInt() {
        // mov rax, [rbx-16]
        rexW(); emit(0x8B); emit(0x43); emit(0xF0);
        // neg rax
        rexW(); emit(0xF7); emit(0xD8);
        // mov [rbx-16], rax
        rexW(); emit(0x89); emit(0x43); emit(0xF0);
    }
    
    // Logical NOT: stack[0] = !stack[0]
    void emitNot() {
        // mov rax, [rbx-16]
        rexW(); emit(0x8B); emit(0x43); emit(0xF0);
        // test rax, rax
        rexW(); emit(0x85); emit(0xC0);
        // sete al
        emit(0x0F); emit(0x94); emit(0xC0);
        // movzx rax, al
        rexW(); emit(0x0F); emit(0xB6); emit(0xC0);
        // mov [rbx-16], rax
        rexW(); emit(0x89); emit(0x43); emit(0xF0);
        // mov byte [rbx-16+8], 2  (TAG_BOOL)
        emit(0xC6); emit(0x43); emit(0xF8); emit(0x02);
    }
    
    // ==========================================================================
    // Control Flow
    // ==========================================================================
    
    // Returns offset of the 32-bit displacement to patch
    size_t emitJump() {
        // jmp rel32
        emit(0xE9);
        size_t patchOffset = pos();
        emit32(0);  // placeholder
        return patchOffset;
    }
    
    // Jump if stack top is false (0): rbx -= 16
    size_t emitJumpIfFalse() {
        // sub rbx, 16
        rexW(); emit(0x83); emit(0xEB); emit(0x10);
        // mov rax, [rbx]
        rexW(); emit(0x8B); emit(0x03);
        // test rax, rax
        rexW(); emit(0x85); emit(0xC0);
        // je rel32
        emit(0x0F); emit(0x84);
        size_t patchOffset = pos();
        emit32(0);
        return patchOffset;
    }
    
    // Jump if stack top is true (non-zero): rbx -= 16
    size_t emitJumpIfTrue() {
        // sub rbx, 16
        rexW(); emit(0x83); emit(0xEB); emit(0x10);
        // mov rax, [rbx]
        rexW(); emit(0x8B); emit(0x03);
        // test rax, rax
        rexW(); emit(0x85); emit(0xC0);
        // jne rel32
        emit(0x0F); emit(0x85);
        size_t patchOffset = pos();
        emit32(0);
        return patchOffset;
    }
    
    void patchJump(size_t patchOffset, size_t target) {
        int32_t rel = static_cast<int32_t>(target - (patchOffset + 4));
        std::memcpy(&code[patchOffset], &rel, 4);
    }
    
    // ==========================================================================
    // Slot Increment/Decrement (for loop optimization)
    // ==========================================================================
    
    void emitIncrementSlot(uint16_t slot) {
        int32_t offset = static_cast<int32_t>(slot) * 16;
        // inc qword [r12 + offset]
        rexWB(); emit(0xFF);
        if (offset == 0) {
            emit(0x04); emit(0x24);
        } else if (offset <= 127) {
            emit(0x44); emit(0x24); emit(static_cast<uint8_t>(offset));
        } else {
            emit(0x84); emit(0x24); emit32(offset);
        }
    }
    
    void emitDecrementSlot(uint16_t slot) {
        int32_t offset = static_cast<int32_t>(slot) * 16;
        // dec qword [r12 + offset]
        rexWB(); emit(0xFF);
        if (offset == 0) {
            emit(0x0C); emit(0x24);
        } else if (offset <= 127) {
            emit(0x4C); emit(0x24); emit(static_cast<uint8_t>(offset));
        } else {
            emit(0x8C); emit(0x24); emit32(offset);
        }
    }
};

// =============================================================================
// Simple JIT Compiler
// =============================================================================

class SimpleCompiler {
public:
    SimpleCompiler() = default;
    
    CompiledFunction* compile(const bc::Program& program, uint32_t functionIndex) {
        if (functionIndex >= program.functions.size()) {
            return nullptr;
        }
        
        const bc::Function& fn = program.functions[functionIndex];
        const auto& code = fn.code;
        
        X64Emitter emit;
        emit.emitPrologue();
        
        // Label management
        std::unordered_map<size_t, size_t> bcToNative;  // bytecode IP -> native offset
        std::vector<std::pair<size_t, size_t>> jumpPatches;  // (native patch offset, bytecode target)
        
        size_t ip = 0;
        while (ip < code.size()) {
            bcToNative[ip] = emit.pos();
            
            auto opcode = static_cast<bc::OpCode>(code[ip++]);
            
            switch (opcode) {
                case bc::OpCode::PUSH_INT32: {
                    int32_t val;
                    std::memcpy(&val, &code[ip], 4);
                    ip += 4;
                    emit.emitPushInt32(val);
                    break;
                }
                
                case bc::OpCode::PUSH_INT32_0:
                    emit.emitPushInt32(0);
                    break;
                    
                case bc::OpCode::PUSH_INT32_1:
                    emit.emitPushInt32(1);
                    break;
                    
                case bc::OpCode::PUSH_INT32_NEG1:
                    emit.emitPushInt32(-1);
                    break;
                
                case bc::OpCode::PUSH_DOUBLE64: {
                    double val;
                    std::memcpy(&val, &code[ip], 8);
                    ip += 8;
                    emit.emitPushDouble(val);
                    break;
                }
                
                case bc::OpCode::PUSH_BOOL: {
                    bool val = code[ip++] != 0;
                    emit.emitPushBool(val);
                    break;
                }
                
                case bc::OpCode::PUSH_TRUE:
                    emit.emitPushBool(true);
                    break;
                    
                case bc::OpCode::PUSH_FALSE:
                    emit.emitPushBool(false);
                    break;
                
                case bc::OpCode::POP:
                    emit.emitPop();
                    break;
                
                case bc::OpCode::LOAD_SLOT: {
                    uint16_t slot;
                    std::memcpy(&slot, &code[ip], 2);
                    ip += 2;
                    emit.emitLoadSlot(slot);
                    break;
                }
                
                case bc::OpCode::LOAD_SLOT_0:
                    emit.emitLoadSlot(0);
                    break;
                
                case bc::OpCode::STORE_SLOT: {
                    uint16_t slot;
                    std::memcpy(&slot, &code[ip], 2);
                    ip += 2;
                    emit.emitStoreSlot(slot);
                    break;
                }
                
                case bc::OpCode::STORE_SLOT_0:
                    emit.emitStoreSlot(0);
                    break;
                
                case bc::OpCode::BINARY_OP: {
                    auto op = static_cast<bc::BinaryOp>(code[ip++]);
                    switch (op) {
                        case bc::BinaryOp::ADD:
                        case bc::BinaryOp::SUB:
                        case bc::BinaryOp::MUL:
                        case bc::BinaryOp::DIV:
                        case bc::BinaryOp::MOD:
                            emit.emitBinaryIntOp(op);
                            break;
                        case bc::BinaryOp::EQ:
                        case bc::BinaryOp::NE:
                        case bc::BinaryOp::LT:
                        case bc::BinaryOp::GT:
                        case bc::BinaryOp::LE:
                        case bc::BinaryOp::GE:
                            emit.emitCompareIntOp(op);
                            break;
                        default:
                            return nullptr;  // Unsupported
                    }
                    break;
                }
                
                case bc::OpCode::UNARY_OP: {
                    auto op = static_cast<bc::UnaryOp>(code[ip++]);
                    switch (op) {
                        case bc::UnaryOp::NEG:
                            emit.emitNegInt();
                            break;
                        case bc::UnaryOp::NOT:
                            emit.emitNot();
                            break;
                        default:
                            return nullptr;
                    }
                    break;
                }
                
                case bc::OpCode::INCREMENT_SLOT: {
                    uint16_t slot;
                    std::memcpy(&slot, &code[ip], 2);
                    ip += 2;
                    emit.emitIncrementSlot(slot);
                    break;
                }
                
                case bc::OpCode::DECREMENT_SLOT: {
                    uint16_t slot;
                    std::memcpy(&slot, &code[ip], 2);
                    ip += 2;
                    emit.emitDecrementSlot(slot);
                    break;
                }
                
                case bc::OpCode::JUMP: {
                    int32_t offset;
                    std::memcpy(&offset, &code[ip], 4);
                    ip += 4;
                    size_t target = ip + offset;
                    size_t patchOff = emit.emitJump();
                    jumpPatches.push_back({patchOff, target});
                    break;
                }
                
                case bc::OpCode::JUMP_IF_FALSE: {
                    int32_t offset;
                    std::memcpy(&offset, &code[ip], 4);
                    ip += 4;
                    size_t target = ip + offset;
                    size_t patchOff = emit.emitJumpIfFalse();
                    jumpPatches.push_back({patchOff, target});
                    break;
                }
                
                case bc::OpCode::JUMP_IF_TRUE: {
                    int32_t offset;
                    std::memcpy(&offset, &code[ip], 4);
                    ip += 4;
                    size_t target = ip + offset;
                    size_t patchOff = emit.emitJumpIfTrue();
                    jumpPatches.push_back({patchOff, target});
                    break;
                }
                
                case bc::OpCode::RETURN_VALUE:
                case bc::OpCode::RETURN_VOID:
                    emit.emitEpilogue();
                    break;
                
                default:
                    // Unsupported opcode - bail out
                    return nullptr;
            }
        }
        
        // Ensure function ends with return
        emit.emitEpilogue();
        
        // Patch all jumps
        for (auto& [patchOff, bcTarget] : jumpPatches) {
            auto it = bcToNative.find(bcTarget);
            if (it != bcToNative.end()) {
                emit.patchJump(patchOff, it->second);
            }
        }
        
        // Allocate executable memory
        auto compiled = std::make_unique<CompiledFunction>();
        if (!compiled->code.allocate(emit.code.size())) {
            return nullptr;
        }
        
        std::memcpy(compiled->code.data(), emit.code.data(), emit.code.size());
        compiled->codeSize = emit.code.size();
        
        if (!compiled->code.makeExecutable()) {
            return nullptr;
        }
        
        compiled->functionIndex = functionIndex;
        compiled->isValid = true;
        
        return compiled.release();
    }
};

// Global simple compiler instance
static std::unique_ptr<SimpleCompiler> g_simpleCompiler;

SimpleCompiler& getSimpleCompiler() {
    if (!g_simpleCompiler) {
        g_simpleCompiler = std::make_unique<SimpleCompiler>();
    }
    return *g_simpleCompiler;
}

} // namespace qz::jit

#endif // x86_64
