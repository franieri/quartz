#include <bytecode/compiler.h>

#include "lexer.h"
#include "parser.h"
#include "syntax.h"
#include "logger.h"
#include "function_registry.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <filesystem>

static inline bool tryGetLocalSlot(const std::vector<LocalContext>& localsStack, const std::string& name, uint16_t* outSlot) {
    if (localsStack.empty()) return false;
    auto it = localsStack.back().slotByName.find(name);
    if (it == localsStack.back().slotByName.end()) return false;
    if (outSlot) *outSlot = it->second;
    return true;
}

static inline uint16_t ensureLocalSlot(std::vector<LocalContext>& localsStack, bc::Function& fn, uint32_t nameStringIndex,
                                      const std::string& name) {
    uint16_t slot = 0;
    if (tryGetLocalSlot(localsStack, name, &slot)) return slot;
    if (localsStack.empty()) return 0xFFFFu;
    if (fn.localNameStrings.size() >= 0xFFFFu) return 0xFFFFu;
    slot = (uint16_t)fn.localNameStrings.size();
    fn.localNameStrings.push_back(nameStringIndex);
    localsStack.back().slotByName[name] = slot;
    return slot;
}

struct LoopContext {
    size_t loopStartIp = 0;
    std::vector<size_t> breakJumpOperandOffsets;
    std::vector<size_t> continueJumpOperandOffsets;
};

static bc::BinaryOp mapBinaryOp(const std::string& op) {
    if (op == "+") return bc::BinaryOp::ADD;
    if (op == "-") return bc::BinaryOp::SUB;
    if (op == "*") return bc::BinaryOp::MUL;
    if (op == "/") return bc::BinaryOp::DIV;
    if (op == "==") return bc::BinaryOp::EQ;
    if (op == "!=") return bc::BinaryOp::NE;
    if (op == "<") return bc::BinaryOp::LT;
    if (op == ">") return bc::BinaryOp::GT;
    if (op == "<=") return bc::BinaryOp::LE;
    if (op == ">=") return bc::BinaryOp::GE;
    return bc::BinaryOp::ADD;
}

static bc::UnaryOp mapUnaryOp(const std::string& op) {
    if (op == "-") return bc::UnaryOp::NEG;
    return bc::UnaryOp::NOT;
}

uint32_t BytecodeCompiler::internString(const std::string& s) {
    auto it = stringToIndex.find(s);
    if (it != stringToIndex.end()) return it->second;
    uint32_t idx = (uint32_t)stringToIndex.size();
    stringToIndex[s] = idx;
    return idx;
}

void BytecodeCompiler::emitOp(bc::Function& fn, bc::OpCode op) { fn.code.push_back((uint8_t)op); }
void BytecodeCompiler::emitU8(bc::Function& fn, uint8_t v) { fn.code.push_back(v); }
void BytecodeCompiler::emitU16(bc::Function& fn, uint16_t v) {
    fn.code.push_back((uint8_t)(v & 0xFF));
    fn.code.push_back((uint8_t)((v >> 8) & 0xFF));
}
void BytecodeCompiler::emitU32(bc::Function& fn, uint32_t v) {
    fn.code.push_back((uint8_t)(v & 0xFF));
    fn.code.push_back((uint8_t)((v >> 8) & 0xFF));
    fn.code.push_back((uint8_t)((v >> 16) & 0xFF));
    fn.code.push_back((uint8_t)((v >> 24) & 0xFF));
}
void BytecodeCompiler::emitI32(bc::Function& fn, int32_t v) { emitU32(fn, (uint32_t)v); }
void BytecodeCompiler::emitF64(bc::Function& fn, double v) {
    uint64_t bits;
    std::memcpy(&bits, &v, sizeof(bits));
    for (int i = 0; i < 8; ++i) fn.code.push_back((uint8_t)((bits >> (i * 8)) & 0xFF));
}

size_t BytecodeCompiler::emitJumpPlaceholder(bc::Function& fn, bc::OpCode op) {
    emitOp(fn, op);
    // placeholder i32
    size_t operandOffset = fn.code.size();
    emitI32(fn, 0);
    return operandOffset;
}

void BytecodeCompiler::patchRelJump(bc::Function& fn, size_t jumpOperandOffset, size_t targetIp) {
    // jumpOperandOffset points at 4-byte i32 operand.
    int32_t rel = (int32_t)targetIp - (int32_t)(jumpOperandOffset + 4);
    fn.code[jumpOperandOffset + 0] = (uint8_t)(rel & 0xFF);
    fn.code[jumpOperandOffset + 1] = (uint8_t)((rel >> 8) & 0xFF);
    fn.code[jumpOperandOffset + 2] = (uint8_t)((rel >> 16) & 0xFF);
    fn.code[jumpOperandOffset + 3] = (uint8_t)((rel >> 24) & 0xFF);
}

uint32_t BytecodeCompiler::addFunction(bc::Program& program, const std::string& name, const std::vector<std::string>& params) {
    bc::Function fn;
    fn.nameString = name.empty() ? bc::kInvalidIndex : internString(name);
    for (const auto& p : params) fn.paramNameStrings.push_back(internString(p));
    // params are also the initial locals slots
    fn.localNameStrings = fn.paramNameStrings;
    program.functions.push_back(std::move(fn));
    return (uint32_t)program.functions.size() - 1;
}

static bool readWholeFile(const std::filesystem::path& path, std::string* out, std::string* error) {
    std::ifstream f(path);
    if (!f.is_open()) {
        if (error) *error = "Could not open file: " + path.string();
        return false;
    }
    *out = std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return true;
}

static uint32_t rotr32(uint32_t x, uint32_t n) { return (x >> n) | (x << (32 - n)); }

static std::array<uint8_t, 32> sha256Bytes(const uint8_t* data, size_t len) {
    // Minimal SHA-256 implementation (FIPS 180-4).
    static const uint32_t K[64] = {
        0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
        0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
        0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
        0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
        0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
        0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
        0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
        0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
    };

    uint32_t H[8] = {
        0x6a09e667u,
        0xbb67ae85u,
        0x3c6ef372u,
        0xa54ff53au,
        0x510e527fu,
        0x9b05688cu,
        0x1f83d9abu,
        0x5be0cd19u,
    };

    // Pad message into blocks.
    const uint64_t bitLen = (uint64_t)len * 8u;
    size_t paddedLen = len + 1;
    while ((paddedLen % 64) != 56) paddedLen++;
    paddedLen += 8;

    std::vector<uint8_t> msg;
    msg.resize(paddedLen, 0);
    if (len) std::memcpy(msg.data(), data, len);
    msg[len] = 0x80;
    // big-endian length
    for (int i = 0; i < 8; ++i) msg[paddedLen - 1 - i] = (uint8_t)((bitLen >> (i * 8)) & 0xFF);

    uint32_t W[64];

    for (size_t offset = 0; offset < msg.size(); offset += 64) {
        for (int i = 0; i < 16; ++i) {
            const uint8_t* b = msg.data() + offset + (i * 4);
            W[i] = ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | (uint32_t)b[3];
        }
        for (int i = 16; i < 64; ++i) {
            uint32_t s0 = rotr32(W[i - 15], 7) ^ rotr32(W[i - 15], 18) ^ (W[i - 15] >> 3);
            uint32_t s1 = rotr32(W[i - 2], 17) ^ rotr32(W[i - 2], 19) ^ (W[i - 2] >> 10);
            W[i] = W[i - 16] + s0 + W[i - 7] + s1;
        }

        uint32_t a = H[0], b = H[1], c = H[2], d = H[3], e = H[4], f = H[5], g = H[6], h = H[7];

        for (int i = 0; i < 64; ++i) {
            uint32_t S1 = rotr32(e, 6) ^ rotr32(e, 11) ^ rotr32(e, 25);
            uint32_t ch = (e & f) ^ ((~e) & g);
            uint32_t temp1 = h + S1 + ch + K[i] + W[i];
            uint32_t S0 = rotr32(a, 2) ^ rotr32(a, 13) ^ rotr32(a, 22);
            uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            uint32_t temp2 = S0 + maj;
            h = g;
            g = f;
            f = e;
            e = d + temp1;
            d = c;
            c = b;
            b = a;
            a = temp1 + temp2;
        }

        H[0] += a;
        H[1] += b;
        H[2] += c;
        H[3] += d;
        H[4] += e;
        H[5] += f;
        H[6] += g;
        H[7] += h;
    }

    std::array<uint8_t, 32> out{};
    for (int i = 0; i < 8; ++i) {
        out[i * 4 + 0] = (uint8_t)((H[i] >> 24) & 0xFF);
        out[i * 4 + 1] = (uint8_t)((H[i] >> 16) & 0xFF);
        out[i * 4 + 2] = (uint8_t)((H[i] >> 8) & 0xFF);
        out[i * 4 + 3] = (uint8_t)(H[i] & 0xFF);
    }
    return out;
}

static std::string normalizeProvenancePath(const std::filesystem::path& absPath, const std::filesystem::path& baseDir) {
    std::error_code ec;
    std::filesystem::path rel = std::filesystem::relative(absPath, baseDir, ec);
    if (!ec) {
        return rel.lexically_normal().generic_string();
    }
    return absPath.lexically_normal().generic_string();
}

void BytecodeCompiler::recordSourceMeta(bc::Program& program, const std::filesystem::path& filePath, const std::string& source) {
    std::filesystem::path abs = filePath;
    std::error_code ec;
    abs = std::filesystem::absolute(abs, ec);
    std::string key = normalizeProvenancePath(abs, provenanceBaseDirectory);

    bc::Program::SourceFileMeta meta;
    meta.path = key;
    meta.sizeBytes = (uint64_t)source.size();
    meta.sha256 = sha256Bytes(reinterpret_cast<const uint8_t*>(source.data()), source.size());

    sourceMetaByPath[key] = meta;
    (void)program;
}

bool BytecodeCompiler::ensureModuleCompiled(const std::string& modulePath, bc::Program& program, std::string* error) {
    if (moduleToFunction.find(modulePath) != moduleToFunction.end()) return true;
    if (moduleCompiling.find(modulePath) != moduleCompiling.end()) {
        // circular import; allow (module init will be guarded at runtime)
        return true;
    }

    // Skip extensions
    if (FunctionRegistry::instance().hasNamespace(modulePath)) {
        return true;
    }

    moduleCompiling.insert(modulePath);

    // directory: sourceDirectory / modulePath with '.' -> '/'
    std::string dirPath = modulePath;
    std::replace(dirPath.begin(), dirPath.end(), '.', '/');
    std::filesystem::path fullPath = sourceDirectory / dirPath;
    if (!std::filesystem::exists(fullPath) || !std::filesystem::is_directory(fullPath)) {
        if (error) *error = "Module directory not found: " + fullPath.string();
        moduleCompiling.erase(modulePath);
        return false;
    }

    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::directory_iterator(fullPath)) {
        if (entry.path().extension() == ".qz") files.push_back(entry.path());
    }
    std::sort(files.begin(), files.end());

    // Create module init function
    uint32_t modFnIndex = addFunction(program, "<module:" + modulePath + ">", {});
    moduleToFunction[modulePath] = modFnIndex;

    bc::Function& modFn = program.functions[modFnIndex];

    // Set module context
    emitOp(modFn, bc::OpCode::SET_CURRENT_MODULE);
    emitU32(modFn, internString(modulePath));

    // Module init has its own locals context
    localsStack.push_back(LocalContext{});
    struct LocalsScopePop {
        BytecodeCompiler* self;
        ~LocalsScopePop() { self->localsStack.pop_back(); }
    } localsScopePop{this};

    for (const auto& file : files) {
        std::string source;
        if (!readWholeFile(file, &source, error)) {
            moduleCompiling.erase(modulePath);
            return false;
        }

        recordSourceMeta(program, file, source);

        SyntaxConfig config;
        Lexer lexer(source, config);
        std::vector<Token> tokens = lexer.tokenize();
        Parser parser(tokens, source);

        AST ast;
        try {
            ast = parser.parse();
        } catch (const std::exception& ex) {
            if (error) *error = std::string("Parse error in module file ") + file.string() + ": " + ex.what();
            moduleCompiling.erase(modulePath);
            return false;
        }

        std::vector<LoopContext> loopStack;
        for (const auto& node : ast.nodes) {
            compileStatement(node, program, modFn, loopStack, error);
        }
    }

    emitOp(modFn, bc::OpCode::CLEAR_CURRENT_MODULE);
    emitOp(modFn, bc::OpCode::RETURN_VOID);

    // record in program table
    program.modules[modulePath] = modFnIndex;

    moduleCompiling.erase(modulePath);
    return true;
}

bool BytecodeCompiler::compileFile(const std::string& entrySourceFile, bc::Program* out, std::string* error) {
    stringToIndex.clear();
    moduleToFunction.clear();
    moduleCompiling.clear();
    localsStack.clear();
    sourceMetaByPath.clear();

    bc::Program program;

    // Determine source directory (for module resolution)
    sourceDirectory = std::filesystem::path(entrySourceFile).parent_path();
    provenanceBaseDirectory = sourceDirectory.parent_path();

    // Read entry
    std::string source;
    if (!readWholeFile(entrySourceFile, &source, error)) return false;

    recordSourceMeta(program, entrySourceFile, source);

    SyntaxConfig config;
    Lexer lexer(source, config);
    std::vector<Token> tokens = lexer.tokenize();

    Parser parser(tokens, source);
    AST ast;
    try {
        ast = parser.parse();
    } catch (const std::exception& ex) {
        if (error) *error = std::string("Parse error in entry file: ") + ex.what();
        return false;
    }

    // Create entry function
    uint32_t entryFn = addFunction(program, "<entry>", {});
    program.entryFunction = entryFn;

    bc::Function& fn = program.functions[entryFn];

    // Entry has its own locals context
    localsStack.push_back(LocalContext{});

    std::vector<LoopContext> loopStack;
    for (const auto& node : ast.nodes) {
        compileStatement(node, program, fn, loopStack, error);
    }

    localsStack.pop_back();

    emitOp(fn, bc::OpCode::RETURN_VOID);

    // Materialize string pool deterministically (by insertion order in unordered_map is not stable)
    // Use a vector sized by max index and fill.
    std::vector<std::string> pool(stringToIndex.size());
    for (const auto& kv : stringToIndex) {
        pool[kv.second] = kv.first;
    }
    program.strings = std::move(pool);

    // Modules table already uses paths as keys, but must be present in strings.
    for (const auto& kv : moduleToFunction) {
        program.modules[kv.first] = kv.second;
    }

    // Provenance: stable list of all compiled source files.
    program.sources.clear();
    program.sources.reserve(sourceMetaByPath.size());
    for (const auto& kv : sourceMetaByPath) program.sources.push_back(kv.second);
    std::sort(program.sources.begin(), program.sources.end(), [](const auto& a, const auto& b) { return a.path < b.path; });

    // Human-readable compiler/options string.
    // (This intentionally avoids machine-specific paths; use program.sources for exact inputs.)
    program.compilerOptions = "quartz-bytecode-compiler;source_ext=.qz;container=.qzb";

    *out = std::move(program);
    return true;
}

void BytecodeCompiler::compileStatement(const ASTNodePtr& node, bc::Program& program, bc::Function& fn,
                                       std::vector<LoopContext>& loopStack, std::string* error) {
    if (!node) return;

    switch (node->type) {
    case NodeType::NoOp:
        return;

    case NodeType::Block:
        for (const auto& child : node->children) {
            compileStatement(child, program, fn, loopStack, error);
        }
        return;

    case NodeType::Import: {
        // Ensure file module is compiled if applicable.
        std::string importStr = std::get<std::string>(node->value);

        // Parse module path similarly to runtime, but only to pre-compile file modules.
        std::string tmp = importStr;
        size_t colonPos = tmp.find(':');
        if (colonPos != std::string::npos) tmp = tmp.substr(colonPos + 1);
        size_t bracePos = tmp.find(":{");
        if (bracePos != std::string::npos) tmp = tmp.substr(0, bracePos);
        bool isWildcard = (tmp.size() >= 2 && tmp.substr(tmp.size() - 2) == ".*");
        std::string modulePath;
        if (isWildcard) {
            modulePath = tmp.substr(0, tmp.size() - 2);
        } else {
            size_t lastDot = tmp.rfind('.');
            if (lastDot != std::string::npos) modulePath = tmp.substr(0, lastDot);
            else modulePath = tmp;
        }

        if (!FunctionRegistry::instance().hasNamespace(modulePath)) {
            // file module; precompile
            if (!ensureModuleCompiled(modulePath, program, error)) return;
        }

        // Emit runtime import instruction
        emitOp(fn, bc::OpCode::PUSH_STRING);
        emitU32(fn, internString(importStr));
        emitOp(fn, bc::OpCode::CALL_NAME);
        emitU32(fn, internString("__bc_import"));
        emitU8(fn, 1);
        emitOp(fn, bc::OpCode::POP);
        return;
    }

    case NodeType::VarDecl: {
        std::string varName;
        if (std::holds_alternative<std::string>(node->value)) varName = std::get<std::string>(node->value);
        else varName = node->name;

        uint32_t nameIdx = internString(varName);
        uint16_t slot = ensureLocalSlot(localsStack, fn, nameIdx, varName);

        if (node->children.empty()) return;
        const ASTNodePtr& rhs = node->children[0];

        if (rhs->type == NodeType::Array) {
            // push values
            for (const auto& elem : rhs->children) compileExpression(elem, program, fn, loopStack, error);
            emitOp(fn, bc::OpCode::DECLARE_ARRAY);
            emitU32(fn, nameIdx);
            emitU16(fn, (uint16_t)rhs->children.size());
            if (slot != 0xFFFFu) {
                emitOp(fn, bc::OpCode::LOAD_VAR);
                emitU32(fn, nameIdx);
                emitOp(fn, bc::OpCode::STORE_SLOT);
                emitU16(fn, slot);
            }
            return;
        }
        if (rhs->type == NodeType::Dict) {
            std::vector<uint32_t> keys;
            keys.reserve(rhs->children.size());
            for (const auto& pair : rhs->children) {
                keys.push_back(internString(pair->name));
                if (!pair->children.empty()) compileExpression(pair->children[0], program, fn, loopStack, error);
                else {
                    emitOp(fn, bc::OpCode::PUSH_BOOL);
                    emitU8(fn, 0);
                }
            }
            emitOp(fn, bc::OpCode::DECLARE_DICT);
            emitU32(fn, nameIdx);
            emitU16(fn, (uint16_t)keys.size());
            for (uint32_t k : keys) emitU32(fn, k);
            if (slot != 0xFFFFu) {
                emitOp(fn, bc::OpCode::LOAD_VAR);
                emitU32(fn, nameIdx);
                emitOp(fn, bc::OpCode::STORE_SLOT);
                emitU16(fn, slot);
            }
            return;
        }
        if (rhs->type == NodeType::Lambda) {
            // compile lambda to function
            std::vector<std::string> params;
            for (size_t i = 0; i + 1 < rhs->children.size(); ++i) {
                const auto& p = rhs->children[i];
                if (p->type == NodeType::Parameter) params.push_back(std::get<std::string>(p->value));
            }
            uint32_t lambdaFnIdx = addFunction(program, "<lambda>", params);

            // compile lambda body
            bc::Function& lambdaFn = program.functions[lambdaFnIdx];
            std::vector<LoopContext> lambdaLoop;

            // lambda has its own locals context; params are locals slots 0..N-1
            localsStack.push_back(LocalContext{});
            struct LocalsScopePop2 {
                BytecodeCompiler* self;
                ~LocalsScopePop2() { self->localsStack.pop_back(); }
            } localsScopePop2{this};
            for (size_t i = 0; i < params.size(); ++i) {
                if (i < 0xFFFFu) localsStack.back().slotByName[params[i]] = (uint16_t)i;
            }

            ASTNodePtr body = rhs->children.back();
            if (rhs->isMutable) {
                compileExpression(body, program, lambdaFn, lambdaLoop, error);
                emitOp(lambdaFn, bc::OpCode::RETURN_VALUE);
            } else {
                compileStatement(body, program, lambdaFn, lambdaLoop, error);
                emitOp(lambdaFn, bc::OpCode::RETURN_VOID);
            }

            emitOp(fn, bc::OpCode::DECLARE_LAMBDA);
            emitU32(fn, nameIdx);
            emitU32(fn, lambdaFnIdx);
            if (slot != 0xFFFFu) {
                emitOp(fn, bc::OpCode::LOAD_VAR);
                emitU32(fn, nameIdx);
                emitOp(fn, bc::OpCode::STORE_SLOT);
                emitU16(fn, slot);
            }
            return;
        }

        compileExpression(rhs, program, fn, loopStack, error);
        if (slot != 0xFFFFu) {
            emitOp(fn, bc::OpCode::STORE_SLOT);
            emitU16(fn, slot);
        } else {
            emitOp(fn, bc::OpCode::STORE_VAR);
            emitU32(fn, nameIdx);
        }
        return;
    }

    case NodeType::Assign: {
        std::string varName = std::get<std::string>(node->children[0]->value);
        compileExpression(node->children[1], program, fn, loopStack, error);
        uint16_t slot = 0;
        if (tryGetLocalSlot(localsStack, varName, &slot)) {
            emitOp(fn, bc::OpCode::STORE_SLOT);
            emitU16(fn, slot);
        } else {
            emitOp(fn, bc::OpCode::STORE_VAR);
            emitU32(fn, internString(varName));
        }
        return;
    }

    case NodeType::Declare: {
        std::string varName = std::get<std::string>(node->children[0]->value);
        compileExpression(node->children[1], program, fn, loopStack, error);
        // Declare introduces a local slot
        uint32_t nameIdx = internString(varName);
        uint16_t slot = ensureLocalSlot(localsStack, fn, nameIdx, varName);
        if (slot != 0xFFFFu) {
            emitOp(fn, bc::OpCode::STORE_SLOT);
            emitU16(fn, slot);
        } else {
            emitOp(fn, bc::OpCode::STORE_VAR);
            emitU32(fn, nameIdx);
        }
        return;
    }

    case NodeType::If: {
        compileExpression(node->children[0], program, fn, loopStack, error);
        size_t jFalse = emitJumpPlaceholder(fn, bc::OpCode::JUMP_IF_FALSE);
        compileStatement(node->children[1], program, fn, loopStack, error);
        if (node->children.size() > 2) {
            size_t jEnd = emitJumpPlaceholder(fn, bc::OpCode::JUMP);
            patchRelJump(fn, jFalse, fn.code.size());
            compileStatement(node->children[2], program, fn, loopStack, error);
            patchRelJump(fn, jEnd, fn.code.size());
        } else {
            patchRelJump(fn, jFalse, fn.code.size());
        }
        return;
    }

    case NodeType::While: {
        LoopContext ctx;
        ctx.loopStartIp = fn.code.size();
        loopStack.push_back(ctx);

        compileExpression(node->children[0], program, fn, loopStack, error);
        size_t jExit = emitJumpPlaceholder(fn, bc::OpCode::JUMP_IF_FALSE);

        compileStatement(node->children[1], program, fn, loopStack, error);
        // jump to start
        emitOp(fn, bc::OpCode::JUMP);
        emitI32(fn, (int32_t)loopStack.back().loopStartIp - (int32_t)(fn.code.size() + 4));

        // patch exit
        patchRelJump(fn, jExit, fn.code.size());

        // patch breaks/continues
        auto finished = loopStack.back();
        loopStack.pop_back();
        for (size_t off : finished.breakJumpOperandOffsets) patchRelJump(fn, off, fn.code.size());
        for (size_t off : finished.continueJumpOperandOffsets) patchRelJump(fn, off, finished.loopStartIp);
        return;
    }

    case NodeType::DoWhile: {
        LoopContext ctx;
        ctx.loopStartIp = fn.code.size();
        loopStack.push_back(ctx);

        compileStatement(node->children[0], program, fn, loopStack, error);
        compileExpression(node->children[1], program, fn, loopStack, error);
        size_t jTrue = emitJumpPlaceholder(fn, bc::OpCode::JUMP_IF_TRUE);
        patchRelJump(fn, jTrue, loopStack.back().loopStartIp);

        auto finished = loopStack.back();
        loopStack.pop_back();
        for (size_t off : finished.breakJumpOperandOffsets) patchRelJump(fn, off, fn.code.size());
        for (size_t off : finished.continueJumpOperandOffsets) patchRelJump(fn, off, finished.loopStartIp);
        return;
    }

    case NodeType::Break: {
        if (loopStack.empty()) return;
        size_t off = emitJumpPlaceholder(fn, bc::OpCode::JUMP);
        loopStack.back().breakJumpOperandOffsets.push_back(off);
        return;
    }

    case NodeType::Continue: {
        if (loopStack.empty()) return;
        size_t off = emitJumpPlaceholder(fn, bc::OpCode::JUMP);
        loopStack.back().continueJumpOperandOffsets.push_back(off);
        return;
    }

    case NodeType::Return: {
        if (!node->children.empty()) {
            compileExpression(node->children[0], program, fn, loopStack, error);
            emitOp(fn, bc::OpCode::RETURN_VALUE);
        } else {
            emitOp(fn, bc::OpCode::RETURN_VOID);
        }
        return;
    }

    case NodeType::Try: {
        // children: tryBody, catchVar(Identifier w/ annotationType.name), catchBody, finallyBody(optional)
        const ASTNodePtr& tryBody = node->children[0];
        const ASTNodePtr& catchVar = node->children[1];
        const ASTNodePtr& catchBody = node->children[2];
        const ASTNodePtr finallyBody = (node->children.size() > 3) ? node->children[3] : nullptr;

        std::string catchVarName = std::get<std::string>(catchVar->value);
        std::string catchType = catchVar->annotationType.name;

        emitOp(fn, bc::OpCode::TRY_PUSH);
        size_t catchIpOff = fn.code.size();
        emitU32(fn, 0);
        size_t finallyIpOff = fn.code.size();
        emitU32(fn, 0);
        emitU8(fn, finallyBody ? 1 : 0);
        emitU32(fn, internString(catchVarName));
        emitU32(fn, internString(catchType));

        // try block
        compileStatement(tryBody, program, fn, loopStack, error);

        emitOp(fn, bc::OpCode::TRY_POP);

        // jump to finally/after
        size_t jToFinally = emitJumpPlaceholder(fn, bc::OpCode::JUMP);

        // catch label
        size_t catchIp = fn.code.size();
        // Patch TRY_PUSH catch IP (absolute)
        fn.code[catchIpOff + 0] = (uint8_t)(catchIp & 0xFF);
        fn.code[catchIpOff + 1] = (uint8_t)((catchIp >> 8) & 0xFF);
        fn.code[catchIpOff + 2] = (uint8_t)((catchIp >> 16) & 0xFF);
        fn.code[catchIpOff + 3] = (uint8_t)((catchIp >> 24) & 0xFF);
        // In the exceptional path, we jump directly here with TRY frame still present.
        // Pop it immediately so the catch body doesn't accidentally pop an outer TRY.
        emitOp(fn, bc::OpCode::TRY_POP);
        compileStatement(catchBody, program, fn, loopStack, error);
        // Clear catch variable object (matches interpreter behavior)
        emitOp(fn, bc::OpCode::CATCH_CLEAR);
        emitU32(fn, internString(catchVarName));
        emitOp(fn, bc::OpCode::JUMP);
        size_t jToFinally2Off = fn.code.size();
        emitI32(fn, 0);

        // finally label
        size_t finallyIp = fn.code.size();
        patchRelJump(fn, jToFinally, finallyIp);
        patchRelJump(fn, jToFinally2Off, finallyIp);
        // Patch TRY_PUSH finally IP (absolute)
        fn.code[finallyIpOff + 0] = (uint8_t)(finallyIp & 0xFF);
        fn.code[finallyIpOff + 1] = (uint8_t)((finallyIp >> 8) & 0xFF);
        fn.code[finallyIpOff + 2] = (uint8_t)((finallyIp >> 16) & 0xFF);
        fn.code[finallyIpOff + 3] = (uint8_t)((finallyIp >> 24) & 0xFF);

        if (finallyBody) {
            compileStatement(finallyBody, program, fn, loopStack, error);
            emitOp(fn, bc::OpCode::FINALLY_END);
        }
        return;
    }

    case NodeType::Throw: {
        if (node->children.empty()) return;
        const ASTNodePtr& expr = node->children[0];
        if (expr->type == NodeType::New) {
            std::string typeName = std::get<std::string>(expr->value);
            // message is first arg if present
            if (!expr->children.empty()) {
                compileExpression(expr->children[0], program, fn, loopStack, error);
            } else {
                emitOp(fn, bc::OpCode::PUSH_STRING);
                emitU32(fn, internString(""));
            }
            emitOp(fn, bc::OpCode::THROW_NEW);
            emitU32(fn, internString(typeName));
        } else {
            compileExpression(expr, program, fn, loopStack, error);
            emitOp(fn, bc::OpCode::THROW_VALUE);
        }
        return;
    }

    case NodeType::ClassDef: {
        // Packed payload matches vm execDefClass
        emitOp(fn, bc::OpCode::DEF_CLASS);

        emitU32(fn, internString(node->name));
        emitU32(fn, node->annotationType.name.empty() ? bc::kInvalidIndex : internString(node->annotationType.name));

        // interfaces from annotationType.element_type (comma-separated)
        std::vector<std::string> interfaces;
        if (!node->annotationType.element_type.empty()) {
            std::string s = node->annotationType.element_type;
            size_t start = 0;
            while (true) {
                size_t end = s.find(',', start);
                if (end == std::string::npos) {
                    interfaces.push_back(s.substr(start));
                    break;
                }
                interfaces.push_back(s.substr(start, end - start));
                start = end + 1;
            }
        }
        emitU32(fn, (uint32_t)interfaces.size());
        for (const auto& i : interfaces) emitU32(fn, internString(i));

        // generics (GenericType children)
        std::vector<std::string> generics;
        for (const auto& c : node->children) {
            if (c->type == NodeType::GenericType) {
                for (const auto& tp : c->children) {
                    if (tp->type == NodeType::TypeParam) generics.push_back(std::get<std::string>(tp->value));
                }
            }
        }
        emitU32(fn, (uint32_t)generics.size());
        for (const auto& g : generics) emitU32(fn, internString(g));

        // fields
        std::vector<std::string> fields;
        for (const auto& c : node->children) {
            if (c->type == NodeType::Field) fields.push_back(std::get<std::string>(c->value));
        }
        emitU32(fn, (uint32_t)fields.size());
        for (const auto& f : fields) emitU32(fn, internString(f));

        // static fields (names + optional init expr function)
        struct SFInit { std::string name; uint32_t exprFn = bc::kInvalidIndex; bool hasInit=false; };
        std::vector<SFInit> staticFields;
        for (const auto& c : node->children) {
            if (c->type != NodeType::StaticField) continue;
            SFInit sf;
            sf.name = std::get<std::string>(c->value);
            if (!c->children.empty()) {
                // compile init expression to a function
                uint32_t initFn = addFunction(program, "<static_init>", {});
                bc::Function& initF = program.functions[initFn];
                std::vector<LoopContext> ls;
                localsStack.push_back(LocalContext{});
                struct LocalsScopePopStaticInit {
                    BytecodeCompiler* self;
                    ~LocalsScopePopStaticInit() { self->localsStack.pop_back(); }
                } localsScopePopStaticInit{this};
                compileExpression(c->children[0], program, initF, ls, error);
                emitOp(initF, bc::OpCode::RETURN_VALUE);
                sf.exprFn = initFn;
                sf.hasInit = true;
            }
            staticFields.push_back(sf);
        }
        emitU32(fn, (uint32_t)staticFields.size());
        for (const auto& sf : staticFields) {
            emitU32(fn, internString(sf.name));
            emitU8(fn, sf.hasInit ? 1 : 0);
            emitU32(fn, sf.hasInit ? sf.exprFn : bc::kInvalidIndex);
        }

        // methods + static methods
        struct Meth { std::string name; bool isStatic=false; std::vector<std::string> params; uint32_t fnIdx=bc::kInvalidIndex; };
        std::vector<Meth> methods;

        for (const auto& c : node->children) {
            if (c->type != NodeType::Method && c->type != NodeType::StaticMethod) continue;
            Meth m;
            m.isStatic = (c->type == NodeType::StaticMethod);
            m.name = std::get<std::string>(c->value);

            ASTNodePtr body = nullptr;
            for (const auto& p : c->children) {
                if (p->type == NodeType::Parameter) m.params.push_back(std::get<std::string>(p->value));
                else if (p->type == NodeType::Block) body = p;
            }

            if (body) {
                m.fnIdx = addFunction(program, "<method:" + node->name + "." + m.name + ">", m.params);
                bc::Function& mf = program.functions[m.fnIdx];
                std::vector<LoopContext> ls;
                localsStack.push_back(LocalContext{});
                struct LocalsScopePopMethod {
                    BytecodeCompiler* self;
                    ~LocalsScopePopMethod() { self->localsStack.pop_back(); }
                } localsScopePopMethod{this};
                for (size_t i = 0; i < m.params.size(); ++i) {
                    if (i < 0xFFFFu) localsStack.back().slotByName[m.params[i]] = (uint16_t)i;
                }
                compileStatement(body, program, mf, ls, error);
                emitOp(mf, bc::OpCode::RETURN_VOID);
            }

            methods.push_back(std::move(m));
        }

        emitU32(fn, (uint32_t)methods.size());
        for (const auto& m : methods) {
            emitU32(fn, internString(m.name));
            emitU8(fn, m.isStatic ? 1 : 0);
            emitU8(fn, (uint8_t)m.params.size());
            for (const auto& p : m.params) emitU32(fn, internString(p));
            emitU32(fn, m.fnIdx);
        }

        // constructor
        bool hasCtor = false;
        std::vector<std::string> ctorParams;
        ASTNodePtr ctorBody = nullptr;
        std::vector<std::pair<std::string, uint32_t>> ctorFieldInits;

        for (const auto& c : node->children) {
            if (c->type != NodeType::Constructor) continue;
            hasCtor = true;
            for (const auto& p : c->children) {
                if (p->type == NodeType::Parameter) ctorParams.push_back(std::get<std::string>(p->value));
                else if (p->type == NodeType::Block) {
                    if (p->name == "initializers") {
                        for (const auto& init : p->children) {
                            if (init->type == NodeType::Assign) {
                                // init->name is field name, init->children[0] is expr
                                uint32_t initFn = addFunction(program, "<field_init>", {});
                                bc::Function& ifn = program.functions[initFn];
                                std::vector<LoopContext> ls;
                                localsStack.push_back(LocalContext{});
                                struct LocalsScopePopFieldInit {
                                    BytecodeCompiler* self;
                                    ~LocalsScopePopFieldInit() { self->localsStack.pop_back(); }
                                } localsScopePopFieldInit{this};
                                compileExpression(init->children[0], program, ifn, ls, error);
                                emitOp(ifn, bc::OpCode::RETURN_VALUE);
                                ctorFieldInits.push_back({init->name, initFn});
                            }
                        }
                    } else {
                        ctorBody = p;
                    }
                }
            }
        }

        emitU8(fn, hasCtor ? 1 : 0);
        uint32_t ctorFnIdx = bc::kInvalidIndex;
        if (hasCtor) {
            if (ctorBody) {
                ctorFnIdx = addFunction(program, "<ctor:" + node->name + ">", ctorParams);
                bc::Function& cf = program.functions[ctorFnIdx];
                std::vector<LoopContext> ls;
                localsStack.push_back(LocalContext{});
                struct LocalsScopePopCtor {
                    BytecodeCompiler* self;
                    ~LocalsScopePopCtor() { self->localsStack.pop_back(); }
                } localsScopePopCtor{this};
                for (size_t i = 0; i < ctorParams.size(); ++i) {
                    if (i < 0xFFFFu) localsStack.back().slotByName[ctorParams[i]] = (uint16_t)i;
                }
                compileStatement(ctorBody, program, cf, ls, error);
                emitOp(cf, bc::OpCode::RETURN_VOID);
            }
            emitU8(fn, (uint8_t)ctorParams.size());
            for (const auto& p : ctorParams) emitU32(fn, internString(p));
            emitU32(fn, ctorFnIdx);

            emitU32(fn, (uint32_t)ctorFieldInits.size());
            for (const auto& fi : ctorFieldInits) {
                emitU32(fn, internString(fi.first));
                emitU32(fn, fi.second);
            }
        }
        return;
    }

    case NodeType::InterfaceDef: {
        emitOp(fn, bc::OpCode::DEF_INTERFACE);
        emitU32(fn, internString(node->name));
        // extends (annotationType.name)
        std::vector<std::string> extends;
        if (!node->annotationType.name.empty()) {
            std::string s = node->annotationType.name;
            size_t start = 0;
            while (true) {
                size_t end = s.find(',', start);
                if (end == std::string::npos) { extends.push_back(s.substr(start)); break; }
                extends.push_back(s.substr(start, end - start));
                start = end + 1;
            }
        }
        emitU32(fn, (uint32_t)extends.size());
        for (const auto& e : extends) emitU32(fn, internString(e));

        // generics
        std::vector<std::string> generics;
        for (const auto& c : node->children) {
            if (c->type == NodeType::GenericType) {
                for (const auto& tp : c->children) {
                    if (tp->type == NodeType::TypeParam) generics.push_back(std::get<std::string>(tp->value));
                }
            }
        }
        emitU32(fn, (uint32_t)generics.size());
        for (const auto& g : generics) emitU32(fn, internString(g));

        // method signatures
        uint32_t methodCount = 0;
        for (const auto& c : node->children) if (c->type == NodeType::Method) methodCount++;
        emitU32(fn, methodCount);
        for (const auto& c : node->children) {
            if (c->type != NodeType::Method) continue;
            std::string mname = std::get<std::string>(c->value);
            emitU32(fn, internString(mname));
            uint8_t pc = 0;
            for (const auto& p : c->children) if (p->type == NodeType::Parameter) pc++;
            emitU8(fn, pc);
        }
        return;
    }

    case NodeType::Call:
    case NodeType::Binary:
    case NodeType::Unary:
    case NodeType::New:
    case NodeType::Array:
    case NodeType::Dict:
    case NodeType::Index:
    case NodeType::InterpolatedString:
    case NodeType::Identifier:
    case NodeType::Literal: {
        compileExpression(node, program, fn, loopStack, error);
        emitOp(fn, bc::OpCode::POP);
        return;
    }

    default:
        // Unhandled statements: ignore to match current interpreter tolerance
        return;
    }
}

void BytecodeCompiler::compileExpression(const ASTNodePtr& node, bc::Program& program, bc::Function& fn,
                                        std::vector<LoopContext>& loopStack, std::string* error) {
    if (!node) {
        emitOp(fn, bc::OpCode::PUSH_BOOL);
        emitU8(fn, 0);
        return;
    }

    switch (node->type) {
    case NodeType::Literal: {
        if (std::holds_alternative<int>(node->value)) {
            emitOp(fn, bc::OpCode::PUSH_INT32);
            emitI32(fn, (int32_t)std::get<int>(node->value));
        } else if (std::holds_alternative<double>(node->value)) {
            emitOp(fn, bc::OpCode::PUSH_DOUBLE64);
            emitF64(fn, std::get<double>(node->value));
        } else if (std::holds_alternative<bool>(node->value)) {
            emitOp(fn, bc::OpCode::PUSH_BOOL);
            emitU8(fn, std::get<bool>(node->value) ? 1 : 0);
        } else if (std::holds_alternative<std::string>(node->value)) {
            emitOp(fn, bc::OpCode::PUSH_STRING);
            emitU32(fn, internString(std::get<std::string>(node->value)));
        } else {
            emitOp(fn, bc::OpCode::PUSH_BOOL);
            emitU8(fn, 0);
        }
        return;
    }

    case NodeType::Identifier: {
        std::string name = std::get<std::string>(node->value);
        uint16_t slot = 0;
        if (tryGetLocalSlot(localsStack, name, &slot)) {
            emitOp(fn, bc::OpCode::LOAD_SLOT);
            emitU16(fn, slot);
        } else {
            emitOp(fn, bc::OpCode::LOAD_VAR);
            emitU32(fn, internString(name));
        }
        return;
    }

    case NodeType::InterpolatedString: {
        // Build by repeated concat using ADD on strings (interpreter uses concatenation)
        emitOp(fn, bc::OpCode::PUSH_STRING);
        emitU32(fn, internString(""));
        for (const auto& child : node->children) {
            if (child->type == NodeType::StringPart) {
                emitOp(fn, bc::OpCode::PUSH_STRING);
                emitU32(fn, internString(std::get<std::string>(child->value)));
            } else if (child->type == NodeType::InterpExpr && !child->children.empty()) {
                compileExpression(child->children[0], program, fn, loopStack, error);
            }
            emitOp(fn, bc::OpCode::BINARY_OP);
            emitU8(fn, (uint8_t)bc::BinaryOp::ADD);
        }
        return;
    }

    case NodeType::Binary: {
        compileExpression(node->children[0], program, fn, loopStack, error);
        compileExpression(node->children[1], program, fn, loopStack, error);
        emitOp(fn, bc::OpCode::BINARY_OP);
        emitU8(fn, (uint8_t)mapBinaryOp(std::get<std::string>(node->value)));
        return;
    }

    case NodeType::Unary: {
        compileExpression(node->children[0], program, fn, loopStack, error);
        emitOp(fn, bc::OpCode::UNARY_OP);
        emitU8(fn, (uint8_t)mapUnaryOp(std::get<std::string>(node->value)));
        return;
    }

    case NodeType::Call: {
        std::string name = std::get<std::string>(node->value);
        for (const auto& a : node->children) compileExpression(a, program, fn, loopStack, error);
        emitOp(fn, bc::OpCode::CALL_NAME);
        emitU32(fn, internString(name));
        emitU8(fn, (uint8_t)node->children.size());
        return;
    }

    case NodeType::New: {
        std::string name = std::get<std::string>(node->value);
        for (const auto& a : node->children) compileExpression(a, program, fn, loopStack, error);
        emitOp(fn, bc::OpCode::NEW_OBJECT);
        emitU32(fn, internString(name));
        emitU8(fn, (uint8_t)node->children.size());
        return;
    }

    case NodeType::Index: {
        // only supports identifier container like interpreter
        const auto& container = node->children[0];
        const auto& idx = node->children[1];
        compileExpression(idx, program, fn, loopStack, error);
        if (container->type == NodeType::Identifier) {
            std::string varName = std::get<std::string>(container->value);
            emitOp(fn, bc::OpCode::INDEX_GET);
            emitU32(fn, internString(varName));
        } else {
            emitOp(fn, bc::OpCode::POP);
            emitOp(fn, bc::OpCode::PUSH_BOOL);
            emitU8(fn, 0);
        }
        return;
    }

    case NodeType::Array: {
        for (const auto& elem : node->children) {
            compileExpression(elem, program, fn, loopStack, error);
        }
        emitOp(fn, bc::OpCode::MAKE_ARRAY_EXPR);
        emitU16(fn, (uint16_t)node->children.size());
        return;
    }

    case NodeType::Dict: {
        std::vector<uint32_t> keys;
        keys.reserve(node->children.size());
        for (const auto& pair : node->children) {
            keys.push_back(internString(pair->name));
            if (!pair->children.empty()) {
                compileExpression(pair->children[0], program, fn, loopStack, error);
            } else {
                emitOp(fn, bc::OpCode::PUSH_BOOL);
                emitU8(fn, 0);
            }
        }

        emitOp(fn, bc::OpCode::MAKE_DICT_EXPR);
        emitU16(fn, (uint16_t)keys.size());
        for (uint32_t k : keys) emitU32(fn, k);
        return;
    }

    case NodeType::Lambda:
    {
        // Compile lambda expression to a bytecode function and push a lambda value.
        std::vector<std::string> params;
        for (size_t i = 0; i + 1 < node->children.size(); ++i) {
            const auto& p = node->children[i];
            if (p->type == NodeType::Parameter) params.push_back(std::get<std::string>(p->value));
        }

        uint32_t lambdaFnIdx = addFunction(program, "<lambda>", params);
        bc::Function& lambdaFn = program.functions[lambdaFnIdx];
        std::vector<LoopContext> lambdaLoop;

        // Lambda has its own locals context; params are locals slots 0..N-1
        localsStack.push_back(LocalContext{});
        struct LocalsScopePopLambda {
            BytecodeCompiler* self;
            ~LocalsScopePopLambda() { self->localsStack.pop_back(); }
        } localsScopePopLambda{this};

        for (size_t i = 0; i < params.size(); ++i) {
            if (i < 0xFFFFu) localsStack.back().slotByName[params[i]] = (uint16_t)i;
        }

        ASTNodePtr body = node->children.back();
        if (node->isMutable) {
            compileExpression(body, program, lambdaFn, lambdaLoop, error);
            emitOp(lambdaFn, bc::OpCode::RETURN_VALUE);
        } else {
            compileStatement(body, program, lambdaFn, lambdaLoop, error);
            emitOp(lambdaFn, bc::OpCode::RETURN_VOID);
        }

        emitOp(fn, bc::OpCode::MAKE_LAMBDA);
        emitU32(fn, lambdaFnIdx);
        return;
    }

    default:
        emitOp(fn, bc::OpCode::PUSH_BOOL);
        emitU8(fn, 0);
        return;
    }
}
