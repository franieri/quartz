#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <cstring>
#include <filesystem>
#include "lexer.h"
#include "parser.h"
#include "runtime.h"
#include "syntax.h"
#include "bytecode.h"
#include <bytecode/compiler.h>
#include <bytecode/vm.h>

namespace fs = std::filesystem;

static bool pathExists(const fs::path& p) {
    std::error_code ec;
    return fs::exists(p, ec);
}

static fs::path resolveBytecodePath(const std::string& userPath, std::vector<fs::path>* tried) {
    fs::path p(userPath);
    auto addCandidate = [&](const fs::path& c) {
        if (tried) tried->push_back(c);
        if (pathExists(c)) return c;
        return fs::path();
    };

    // 1) As given
    if (auto r = addCandidate(p); !r.empty()) return r;

    // 2) Add .qzb if missing
    if (p.extension().empty()) {
        if (auto r = addCandidate(fs::path(userPath + ".qzb")); !r.empty()) return r;
    }

    // 3) Common typo: sample/ -> samples/
    if (!p.empty()) {
        auto it = p.begin();
        if (it != p.end() && it->string() == "sample") {
            fs::path fixed("samples");
            ++it;
            for (; it != p.end(); ++it) fixed /= *it;
            if (auto r = addCandidate(fixed); !r.empty()) return r;
            if (fixed.extension().empty()) {
                if (auto r = addCandidate(fs::path(fixed.string() + ".qzb")); !r.empty()) return r;
            }
        }
    }

    // 4) If relative, also try under ./samples/
    if (!p.is_absolute()) {
        fs::path underSamples = fs::path("samples") / p;
        if (auto r = addCandidate(underSamples); !r.empty()) return r;
        if (underSamples.extension().empty()) {
            if (auto r = addCandidate(fs::path(underSamples.string() + ".qzb")); !r.empty()) return r;
        }
    }

    return p;
}

static void printUsage(const char* exe) {
    std::cerr << "Usage:\n";
    std::cerr << "  " << exe << " <source_file>\n";
    std::cerr << "  " << exe << " --interp <source_file>\n";
    std::cerr << "  " << exe << " --compile <source_file> -o <output.qzb>\n";
    std::cerr << "  " << exe << " --compile-run <source_file> -o <output.qzb>\n";
    std::cerr << "  " << exe << " --run-bc <bytecode.qzb>\n";
    std::cerr << "  " << exe << " --dump-qzb-meta <bytecode.qzb>\n";
}

static std::string toHex(const uint8_t* data, size_t n) {
    static const char* kHex = "0123456789abcdef";
    std::string out;
    out.reserve(n * 2);
    for (size_t i = 0; i < n; ++i) {
        out.push_back(kHex[(data[i] >> 4) & 0xF]);
        out.push_back(kHex[(data[i] >> 0) & 0xF]);
    }
    return out;
}

static void registerBuiltinIO() {
    // Initialize the function registry
    FunctionRegistry::instance();

    // Register minimal builtins for printing (kept for backwards compatibility)
    FunctionRegistry& reg = FunctionRegistry::instance();
    auto println = [](const std::vector<Value>& args) -> Value {
        for (size_t i = 0; i < args.size(); ++i) {
            if (i > 0) std::cout << " ";
            std::cout << ::to_string(args[i]);
        }
        std::cout << std::endl;
        return Value();
    };
    reg.registerFunction("system.io.println", println);
    reg.registerFunction("system.io.out.println", println);
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printUsage(argv[0]);
        return 1;
    }

    enum class Mode { INTERP, COMPILE, COMPILE_RUN, RUN_BC, DUMP_QZB_META };
    Mode mode = Mode::INTERP;
    std::string inputPath;
    std::string outputPath = "program.qzb";

    // Simple argument parsing
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--help" || a == "-h") {
            printUsage(argv[0]);
            return 0;
        } else if (a == "--interp") {
            mode = Mode::INTERP;
        } else if (a == "--compile") {
            mode = Mode::COMPILE;
        } else if (a == "--compile-run") {
            mode = Mode::COMPILE_RUN;
        } else if (a == "--run-bc") {
            mode = Mode::RUN_BC;
        } else if (a == "--dump-qzb-meta") {
            mode = Mode::DUMP_QZB_META;
        } else if (a == "-o" || a == "--out") {
            if (i + 1 >= argc) {
                std::cerr << "Missing output path after " << a << std::endl;
                return 1;
            }
            outputPath = argv[++i];
        } else if (!a.empty() && a[0] == '-') {
            std::cerr << "Unknown option: " << a << std::endl;
            printUsage(argv[0]);
            return 1;
        } else {
            inputPath = a;
        }
    }

    if (inputPath.empty()) {
        printUsage(argv[0]);
        return 1;
    }

    registerBuiltinIO();

    // Ensure extensions/stdlib namespaces are registered before compiling bytecode.
    // (BytecodeCompiler needs FunctionRegistry::hasNamespace() to distinguish file modules vs extensions.)
    Runtime initRuntime;
    initRuntime.initialize();

    try {
        if (mode == Mode::DUMP_QZB_META) {
            bc::Program program;
            std::string err;

            std::vector<fs::path> tried;
            fs::path resolved = resolveBytecodePath(inputPath, &tried);
            if (!bc::readProgramFromFile(resolved.string(), &program, &err)) {
                std::cerr << "Bytecode read error: " << err << std::endl;
                if (!tried.empty()) {
                    std::cerr << "Tried paths:" << std::endl;
                    for (const auto& t : tried) std::cerr << "  " << t.string() << std::endl;
                }
                return 1;
            }

            std::cout << "QZB metadata for: " << resolved.string() << std::endl;
            std::cout << "Sources: " << program.sources.size() << std::endl;
            for (const auto& s : program.sources) {
                std::cout << "- " << s.path << std::endl;
                std::cout << "  size: " << s.sizeBytes << std::endl;
                std::cout << "  sha256: " << toHex(s.sha256.data(), s.sha256.size()) << std::endl;
            }
            if (!program.compilerOptions.empty()) {
                std::cout << "Compiler: " << program.compilerOptions << std::endl;
            }
            std::cout << "Strings: " << program.strings.size() << std::endl;
            std::cout << "Functions: " << program.functions.size() << std::endl;
            std::cout << "Modules: " << program.modules.size() << std::endl;
            return 0;
        }

        if (mode == Mode::RUN_BC) {
            bc::Program program;
            std::string err;
            std::vector<fs::path> tried;
            fs::path resolved = resolveBytecodePath(inputPath, &tried);
            if (!bc::readProgramFromFile(resolved.string(), &program, &err)) {
                std::cerr << "Bytecode read error: " << err << std::endl;
                if (!tried.empty()) {
                    std::cerr << "Tried paths:" << std::endl;
                    for (const auto& t : tried) std::cerr << "  " << t.string() << std::endl;
                }
                return 1;
            }

            Runtime runtime;
            runtime.initialize();

            BytecodeVM vm(runtime);
#ifdef QZ_JIT_ENABLED
            // JIT is enabled at build time - log it
            if (vm.isJITEnabled()) {
                std::cerr << "[JIT] Enabled with threshold=" << vm.jitThreshold() << std::endl;
            }
#endif
            if (!vm.run(program, &err)) {
                std::cerr << "VM error: " << err << std::endl;
                return 1;
            }
            return 0;
        }

        // Otherwise, input is a source file.
        std::ifstream sourceFile(inputPath);
        if (!sourceFile.is_open()) {
            std::cerr << "Error: Could not open source file " << inputPath << std::endl;
            return 1;
        }
        std::string sourceCode((std::istreambuf_iterator<char>(sourceFile)), std::istreambuf_iterator<char>());
        sourceFile.close();

        if (mode == Mode::INTERP) {
            SyntaxConfig config;
            Lexer lexer(sourceCode, config);
            std::vector<Token> tokens = lexer.tokenize();

            Parser parser(tokens, sourceCode);
            Runtime runtime;
            runtime.initialize();
            runtime.setSourcePath(inputPath);

            AST ast = parser.parse();
            if (parser.getState() == ParserState::ERROR) {
                return 1;
            }
            runtime.execute(ast);
            if (parser.getState() == ParserState::ERROR || runtime.getState() == RuntimeState::HALTED) {
                return 1;
            }
            return 0;
        }

        // Compile to bytecode
        BytecodeCompiler compiler;
        bc::Program program;
        std::string err;
        if (!compiler.compileFile(inputPath, &program, &err)) {
            std::cerr << "Compile error: " << err << std::endl;
            return 1;
        }
        if (!bc::writeProgramToFile(program, outputPath, &err)) {
            std::cerr << "Bytecode write error: " << err << std::endl;
            return 1;
        }

        if (mode == Mode::COMPILE) {
            return 0;
        }

        // Compile + Run
        Runtime runtime;
        runtime.initialize();
        runtime.setSourcePath(inputPath);
        BytecodeVM vm(runtime);
#ifdef QZ_JIT_ENABLED
        // JIT is enabled at build time - log it
        if (vm.isJITEnabled()) {
            std::cerr << "[JIT] Enabled with threshold=" << vm.jitThreshold() << std::endl;
        }
#endif
        if (!vm.run(program, &err)) {
            std::cerr << "VM error: " << err << std::endl;
            return 1;
        }
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "Fatal: " << ex.what() << std::endl;
        return 1;
    }
}