#ifndef QZ_BYTECODE_COMPILER_H
#define QZ_BYTECODE_COMPILER_H

#include "bytecode.h"

#include <filesystem>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>

struct LoopContext;

struct LocalContext {
    std::unordered_map<std::string, uint16_t> slotByName;
};

class BytecodeCompiler {
public:
    // Compiles a whole program starting at entrySourceFile.
    // Includes any file-based modules reachable via import statements into a single Program.
    bool compileFile(const std::string& entrySourceFile, bc::Program* out, std::string* error);

private:
    std::filesystem::path sourceDirectory;
    std::filesystem::path provenanceBaseDirectory;

    // Interning
    uint32_t internString(const std::string& s);

    // Module compilation
    bool ensureModuleCompiled(const std::string& modulePath, bc::Program& program, std::string* error);

    // Function compilation
    uint32_t addFunction(bc::Program& program, const std::string& name, const std::vector<std::string>& params);

    // AST compilation
    void compileStatement(const ASTNodePtr& node, bc::Program& program, bc::Function& fn,
                          std::vector<LoopContext>& loopStack, std::string* error);
    void compileExpression(const ASTNodePtr& node, bc::Program& program, bc::Function& fn,
                           std::vector<LoopContext>& loopStack, std::string* error);

    // Helpers
    void emitOp(bc::Function& fn, bc::OpCode op);
    void emitU8(bc::Function& fn, uint8_t v);
    void emitU16(bc::Function& fn, uint16_t v);
    void emitU32(bc::Function& fn, uint32_t v);
    void emitI32(bc::Function& fn, int32_t v);
    void emitF64(bc::Function& fn, double v);

    void recordSourceMeta(bc::Program& program, const std::filesystem::path& filePath, const std::string& source);

    size_t emitJumpPlaceholder(bc::Function& fn, bc::OpCode op);
    void patchRelJump(bc::Function& fn, size_t jumpOperandOffset, size_t targetIp);

    // Internal state per compilation
    std::unordered_map<std::string, uint32_t> stringToIndex;
    std::unordered_map<std::string, uint32_t> moduleToFunction;
    std::unordered_set<std::string> moduleCompiling;

    // Provenance per compilation (filled into bc::Program for .qzb v4+)
    std::unordered_map<std::string, bc::Program::SourceFileMeta> sourceMetaByPath;

    // Slot locals compilation context (per function)
    std::vector<LocalContext> localsStack;
};

#endif // QZ_BYTECODE_COMPILER_H
