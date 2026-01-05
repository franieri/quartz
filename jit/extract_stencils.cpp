/**
 * =============================================================================
 * Quartz JIT Stencil Extractor (C++ Version)
 * =============================================================================
 * 
 * Extracts machine code from compiled stencils and generates C++ headers.
 * Replaces extract_stencils.py for a fully self-contained toolchain.
 * 
 * Usage: ./extract_stencils [architecture]
 *        e.g., ./extract_stencils x86_64
 * 
 * Build: g++ -O2 -std=c++17 extract_stencils.cpp -o extract_stencils
 */

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <array>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <filesystem>
#include <regex>

namespace fs = std::filesystem;

// =============================================================================
// Configuration
// =============================================================================

// Hole markers (must match stencils.c)
struct HoleMarker {
    const char* name;
    uint64_t value;
};

static const HoleMarker HOLE_MARKERS[] = {
    { "HOLE_IMM64",       0xDEADBEEFCAFEBABEULL },
    { "HOLE_STACK_PTR",   0xAAAAAAAAAAAAAAAAULL },
    { "HOLE_JUMP_TARGET", 0xBBBBBBBBBBBBBBBBULL },
    { "HOLE_SLOT_IDX",    0xCCCCCCCCCCCCCCCCULL },
    { "HOLE_IMM64_2",     0xDDDDDDDDDDDDDDDDULL },
    { "HOLE_FUNC_PTR",    0xEEEEEEEEEEEEEEEEULL },
};
static constexpr size_t NUM_HOLE_MARKERS = sizeof(HOLE_MARKERS) / sizeof(HOLE_MARKERS[0]);

// Stencil names to extract (auto-discovered from object file)
// This list is used as a fallback and for ordering
static const char* STENCIL_NAMES[] = {
    // Push operations
    "stencil_push_int32",
    "stencil_push_int64",
    "stencil_push_double",
    "stencil_push_true",
    "stencil_push_false",
    "stencil_push_int_0",
    "stencil_push_int_1",
    "stencil_push_int_neg1",
    "stencil_push_nil",
    "stencil_pop",
    "stencil_pop2",
    
    // Slot operations
    "stencil_load_slot",
    "stencil_store_slot",
    "stencil_load_slot_0",
    "stencil_load_slot_1",
    "stencil_load_slot_2",
    "stencil_load_slot_3",
    "stencil_store_slot_0",
    "stencil_store_slot_1",
    "stencil_store_slot_2",
    "stencil_store_slot_3",
    "stencil_inc_slot_0",
    "stencil_inc_slot_1",
    "stencil_dec_slot_0",
    
    // Integer arithmetic
    "stencil_add_int",
    "stencil_sub_int",
    "stencil_mul_int",
    "stencil_div_int",
    "stencil_mod_int",
    "stencil_neg_int",
    "stencil_inc_slot",
    "stencil_dec_slot",
    "stencil_add_int_imm",
    "stencil_sub_int_imm",
    "stencil_mul_int_imm",
    
    // Double arithmetic
    "stencil_add_double",
    "stencil_sub_double",
    "stencil_mul_double",
    "stencil_div_double",
    "stencil_neg_double",
    
    // Integer comparisons
    "stencil_eq_int",
    "stencil_ne_int",
    "stencil_lt_int",
    "stencil_gt_int",
    "stencil_le_int",
    "stencil_ge_int",
    "stencil_lt_int_imm",
    "stencil_le_int_imm",
    "stencil_gt_int_imm",
    "stencil_eq_int_imm",
    "stencil_is_zero",
    "stencil_is_nonzero",
    
    // Double comparisons
    "stencil_eq_double",
    "stencil_lt_double",
    "stencil_le_double",
    "stencil_gt_double",
    "stencil_ge_double",
    
    // Logical operations
    "stencil_not",
    "stencil_and",
    "stencil_or",
    
    // Bitwise operations
    "stencil_bitand",
    "stencil_bitor",
    "stencil_bitxor",
    "stencil_bitnot",
    "stencil_shl",
    "stencil_shr",
    "stencil_ushr",
    
    // Control flow
    "stencil_jump",
    "stencil_jump_if_false",
    "stencil_jump_if_true",
    "stencil_jump_if_zero",
    "stencil_jump_if_nonzero",
    "stencil_loop_cond_lt_int",
    "stencil_loop_cond_le_int",
    "stencil_inc_and_loop",
    "stencil_dec_and_loop",
    "stencil_compare_and_jump_lt",
    "stencil_compare_and_jump_eq",
    
    // Fused/super instructions
    "stencil_load_slot_push_int",
    "stencil_add_store_slot",
    "stencil_load_cmp_lt_jump",
    "stencil_add_imm_to_slot",
    "stencil_sub_imm_from_slot",
    "stencil_load_two_add",
    "stencil_store_and_pop",
    
    // Stack manipulation
    "stencil_dup",
    "stencil_dup2",
    "stencil_swap",
    "stencil_rot3",
    "stencil_over",
    
    // Type conversions
    "stencil_int_to_double",
    "stencil_double_to_int",
    "stencil_bool_to_int",
    
    // Misc
    "stencil_call_runtime",
    "stencil_return",
    "stencil_nop",
    "stencil_nop4",
    "stencil_check_bounds",
};
static constexpr size_t NUM_STENCILS = sizeof(STENCIL_NAMES) / sizeof(STENCIL_NAMES[0]);

// =============================================================================
// Data Structures
// =============================================================================

struct HoleInfo {
    uint16_t offset;
    std::string type;
};

struct StencilData {
    std::string name;
    std::vector<uint8_t> code;
    std::vector<HoleInfo> holes;
};

// =============================================================================
// Utility Functions
// =============================================================================

static std::string exec(const std::string& cmd) {
    std::array<char, 4096> buffer;
    std::string result;
    
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        fprintf(stderr, "ERROR: Failed to run command: %s\n", cmd.c_str());
        return "";
    }
    
    while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
        result += buffer.data();
    }
    
    pclose(pipe);
    return result;
}

static int hexCharToInt(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static std::vector<uint8_t> extractFunctionBytes(const std::string& objfile, const std::string& funcname) {
    std::vector<uint8_t> bytes;
    
    // Use objdump to disassemble
    std::string cmd = "objdump -d -j .text \"" + objfile + "\" 2>/dev/null";
    std::string output = exec(cmd);
    
    if (output.empty()) {
        return bytes;
    }
    
    // Find the function
    std::string marker = "<" + funcname + ">:";
    size_t funcStart = output.find(marker);
    if (funcStart == std::string::npos) {
        return bytes;
    }
    
    // Move past the function header line
    size_t lineEnd = output.find('\n', funcStart);
    if (lineEnd == std::string::npos) return bytes;
    
    size_t pos = lineEnd + 1;
    
    // Parse each line until we hit the next function or end
    while (pos < output.size()) {
        // Skip empty lines
        while (pos < output.size() && (output[pos] == '\n' || output[pos] == '\r')) {
            pos++;
        }
        if (pos >= output.size()) break;
        
        // Check if this is a new function (starts with address and <name>)
        size_t lineStart = pos;
        size_t nextNewline = output.find('\n', pos);
        if (nextNewline == std::string::npos) nextNewline = output.size();
        
        std::string line = output.substr(lineStart, nextNewline - lineStart);
        
        // New function marker: contains '<' and '>:' without leading whitespace
        if (!line.empty() && line[0] != ' ' && line[0] != '\t' && 
            line.find('<') != std::string::npos && line.find(">:") != std::string::npos) {
            break;
        }
        
        // Parse hex bytes from objdump output
        // Format: "   addr:\txx xx xx xx \tinstruction"
        size_t colonPos = line.find(':');
        if (colonPos != std::string::npos && colonPos + 1 < line.size()) {
            size_t hexStart = colonPos + 1;
            
            // Skip whitespace after colon
            while (hexStart < line.size() && (line[hexStart] == ' ' || line[hexStart] == '\t')) {
                hexStart++;
            }
            
            // Find the tab that separates hex from instruction
            size_t hexEnd = line.find('\t', hexStart);
            if (hexEnd == std::string::npos) hexEnd = line.size();
            
            // Parse hex bytes
            size_t i = hexStart;
            while (i + 1 < hexEnd) {
                // Skip spaces
                while (i < hexEnd && line[i] == ' ') i++;
                if (i + 1 >= hexEnd) break;
                
                int hi = hexCharToInt(line[i]);
                int lo = hexCharToInt(line[i + 1]);
                
                if (hi >= 0 && lo >= 0) {
                    bytes.push_back(static_cast<uint8_t>((hi << 4) | lo));
                    i += 2;
                } else {
                    break;
                }
            }
        }
        
        pos = nextNewline + 1;
    }
    
    return bytes;
}

static std::vector<HoleInfo> findHoles(const std::vector<uint8_t>& code) {
    std::vector<HoleInfo> holes;
    
    for (size_t m = 0; m < NUM_HOLE_MARKERS; m++) {
        uint64_t marker = HOLE_MARKERS[m].value;
        
        // Convert marker to little-endian bytes
        uint8_t markerBytes[8];
        for (int i = 0; i < 8; i++) {
            markerBytes[i] = static_cast<uint8_t>((marker >> (i * 8)) & 0xFF);
        }
        
        // Search for marker in code
        for (size_t i = 0; i + 8 <= code.size(); i++) {
            bool match = true;
            for (int j = 0; j < 8; j++) {
                if (code[i + j] != markerBytes[j]) {
                    match = false;
                    break;
                }
            }
            if (match) {
                HoleInfo hole;
                hole.offset = static_cast<uint16_t>(i);
                hole.type = HOLE_MARKERS[m].name;
                holes.push_back(hole);
            }
        }
    }
    
    // Sort by offset
    std::sort(holes.begin(), holes.end(), 
              [](const HoleInfo& a, const HoleInfo& b) { return a.offset < b.offset; });
    
    return holes;
}

// =============================================================================
// Header Generation
// =============================================================================

static void generateHeader(const std::vector<StencilData>& stencils, 
                          const std::string& arch,
                          const std::string& outputPath) {
    std::ofstream out(outputPath);
    if (!out) {
        fprintf(stderr, "ERROR: Cannot open output file: %s\n", outputPath.c_str());
        exit(1);
    }
    
    std::string archUpper = arch;
    std::transform(archUpper.begin(), archUpper.end(), archUpper.begin(), ::toupper);
    
    out << "// AUTO-GENERATED by extract_stencils - DO NOT EDIT\n";
    out << "// Generated for architecture: " << arch << "\n";
    out << "// Stencil count: " << stencils.size() << "\n\n";
    
    out << "#ifndef QZ_JIT_STENCILS_" << archUpper << "_H\n";
    out << "#define QZ_JIT_STENCILS_" << archUpper << "_H\n\n";
    
    out << "#include <cstdint>\n";
    out << "#include <cstddef>\n\n";
    
    out << "namespace qz::jit {\n\n";
    
    // Hole type enum
    out << "// Hole types for patching\n";
    out << "enum class HoleType : uint8_t {\n";
    out << "    NONE = 0,\n";
    for (size_t i = 0; i < NUM_HOLE_MARKERS; i++) {
        out << "    " << HOLE_MARKERS[i].name << ",\n";
    }
    out << "};\n\n";
    
    // Hole info struct
    out << "// Information about a patchable hole\n";
    out << "struct HoleInfo {\n";
    out << "    uint16_t offset;    // Byte offset in code\n";
    out << "    HoleType type;      // Type of hole\n";
    out << "    uint8_t size;       // Size in bytes (always 8 for now)\n";
    out << "};\n\n";
    
    // Stencil struct
    out << "// A compiled stencil template\n";
    out << "struct Stencil {\n";
    out << "    const uint8_t* code;     // Machine code bytes\n";
    out << "    uint16_t codeSize;       // Size of code in bytes\n";
    out << "    const HoleInfo* holes;   // Array of holes to patch\n";
    out << "    uint8_t numHoles;        // Number of holes\n";
    out << "};\n\n";
    
    // Generate data for each stencil
    for (const auto& st : stencils) {
        out << "// " << st.name << " (" << st.code.size() << " bytes, " 
            << st.holes.size() << " holes)\n";
        out << "alignas(16) static const uint8_t " << st.name << "_code[] = {\n";
        
        for (size_t i = 0; i < st.code.size(); i += 16) {
            out << "    ";
            for (size_t j = i; j < std::min(i + 16, st.code.size()); j++) {
                char buf[8];
                snprintf(buf, sizeof(buf), "0x%02x", st.code[j]);
                out << buf;
                if (j + 1 < st.code.size()) out << ", ";
            }
            out << "\n";
        }
        out << "};\n";
        
        // Holes
        if (!st.holes.empty()) {
            out << "static const HoleInfo " << st.name << "_holes[] = {\n";
            for (const auto& hole : st.holes) {
                out << "    { " << hole.offset << ", HoleType::" << hole.type << ", 8 },\n";
            }
            out << "};\n";
        } else {
            out << "static const HoleInfo* " << st.name << "_holes = nullptr;\n";
        }
        out << "\n";
    }
    
    // Stencil type enum
    out << "// Stencil type identifiers\n";
    out << "enum class StencilType : uint8_t {\n";
    for (const auto& st : stencils) {
        std::string enumName = st.name;
        // Remove "stencil_" prefix and uppercase
        if (enumName.substr(0, 8) == "stencil_") {
            enumName = enumName.substr(8);
        }
        std::transform(enumName.begin(), enumName.end(), enumName.begin(), ::toupper);
        out << "    " << enumName << ",\n";
    }
    out << "    COUNT  // Number of stencils\n";
    out << "};\n\n";
    
    // Stencil table
    out << "// Master stencil table\n";
    out << "inline const Stencil STENCILS[] = {\n";
    for (const auto& st : stencils) {
        out << "    { " << st.name << "_code, " << st.code.size() << ", ";
        if (!st.holes.empty()) {
            out << st.name << "_holes, " << st.holes.size();
        } else {
            out << "nullptr, 0";
        }
        out << " },\n";
    }
    out << "};\n\n";
    
    // Helper functions
    out << "// Get stencil by type\n";
    out << "inline const Stencil& getStencil(StencilType type) {\n";
    out << "    return STENCILS[static_cast<size_t>(type)];\n";
    out << "}\n\n";
    
    out << "// Get stencil name (for debugging)\n";
    out << "inline const char* getStencilName(StencilType type) {\n";
    out << "    static const char* names[] = {\n";
    for (const auto& st : stencils) {
        out << "        \"" << st.name << "\",\n";
    }
    out << "    };\n";
    out << "    return names[static_cast<size_t>(type)];\n";
    out << "}\n\n";
    
    out << "} // namespace qz::jit\n\n";
    out << "#endif // QZ_JIT_STENCILS_" << archUpper << "_H\n";
    
    out.close();
}

// =============================================================================
// Main
// =============================================================================

int main(int argc, char* argv[]) {
    // Determine architecture
    std::string arch = "x86_64";
    if (argc > 1) {
        arch = argv[1];
    }
    
    // Determine paths
    fs::path exePath = fs::canonical(fs::path(argv[0])).parent_path();
    fs::path jitDir = exePath;
    
    // Handle case where we're run from build directory
    if (jitDir.filename() == "build") {
        jitDir = jitDir.parent_path() / "jit";
    } else if (fs::exists(jitDir / "stencils")) {
        // Already in jit directory
    } else {
        jitDir = jitDir / "jit";
    }
    
    fs::path stencilDir = jitDir / "stencils";
    
    // Select architecture-specific source file
    fs::path stencilSrc;
    if (arch == "aarch64") {
        stencilSrc = stencilDir / "stencils_aarch64.c";
    } else {
        stencilSrc = stencilDir / "stencils.c";
    }
    
    fs::path stencilObj = stencilDir / ("stencils_" + arch + ".o");
    fs::path outputHeader = jitDir / ("stencils_" + arch + ".h");
    
    printf("=== Quartz JIT Stencil Extractor (C++) ===\n");
    printf("Architecture: %s\n", arch.c_str());
    printf("Source: %s\n", stencilSrc.c_str());
    printf("Output: %s\n\n", outputHeader.c_str());
    
    // Check source exists
    if (!fs::exists(stencilSrc)) {
        fprintf(stderr, "ERROR: Stencil source not found: %s\n", stencilSrc.c_str());
        return 1;
    }
    
    // Build compiler command with appropriate flags and compiler
    std::string compiler = "gcc";
    std::string cflags = "-O2 -fno-stack-protector -fno-pie -fno-pic "
                        "-fno-asynchronous-unwind-tables -fomit-frame-pointer";
    
    if (arch == "x86_64") {
        cflags += " -mno-red-zone";
    } else if (arch == "aarch64") {
        // Check for macOS (Apple Silicon)
#ifdef __APPLE__
        compiler = "clang";
#endif
    }
    
    // Add hole marker definitions
    for (size_t i = 0; i < NUM_HOLE_MARKERS; i++) {
        char buf[64];
        snprintf(buf, sizeof(buf), " -D%s=0x%llxULL", 
                 HOLE_MARKERS[i].name, 
                 (unsigned long long)HOLE_MARKERS[i].value);
        cflags += buf;
    }
    
    // Compile stencils
    printf("Compiling stencils...\n");
    std::string compileCmd = compiler + " " + cflags + " -c \"" + stencilSrc.string() + 
                            "\" -o \"" + stencilObj.string() + "\" 2>&1";
    
    printf("  Command: %s\n", compileCmd.c_str());
    std::string compileOutput = exec(compileCmd);
    
    if (!fs::exists(stencilObj)) {
        fprintf(stderr, "ERROR: Compilation failed:\n%s\n", compileOutput.c_str());
        return 1;
    }
    printf("  Compilation successful\n\n");
    
    // Extract stencils
    printf("Extracting stencils...\n");
    std::vector<StencilData> stencils;
    size_t totalBytes = 0;
    size_t totalHoles = 0;
    size_t extracted = 0;
    
    for (size_t i = 0; i < NUM_STENCILS; i++) {
        StencilData st;
        st.name = STENCIL_NAMES[i];
        st.code = extractFunctionBytes(stencilObj.string(), st.name);
        
        if (st.code.empty()) {
            // Not all stencils are present in all architectures - that's OK
            continue;
        }
        
        extracted++;
        st.holes = findHoles(st.code);
        
        printf("  %s: %zu bytes, %zu holes\n", 
               st.name.c_str(), st.code.size(), st.holes.size());
        
        totalBytes += st.code.size();
        totalHoles += st.holes.size();
        stencils.push_back(std::move(st));
    }
    
    printf("\nExtracted %zu stencils (%zu found in source)\n", stencils.size(), extracted);
    
    if (stencils.empty()) {
        fprintf(stderr, "ERROR: No stencils extracted!\n");
        return 1;
    }
    
    // Generate header
    printf("\nGenerating header...\n");
    generateHeader(stencils, arch, outputHeader.string());
    printf("  Written to: %s\n", outputHeader.c_str());
    
    // Summary
    printf("\n=== Summary ===\n");
    printf("Total stencils: %zu\n", stencils.size());
    printf("Total code bytes: %zu\n", totalBytes);
    printf("Total holes: %zu\n", totalHoles);
    printf("Average stencil size: %.1f bytes\n", 
           static_cast<double>(totalBytes) / stencils.size());
    printf("\nDone!\n");
    
    return 0;
}
