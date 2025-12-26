#include "bytecode.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <sstream>

#include <quartz.h>

namespace bc {

static constexpr uint32_t kQzbMaxMetaSize = 16u * 1024u * 1024u;
static constexpr uint32_t kQzbMaxPayloadSize = 256u * 1024u * 1024u;
static constexpr uint32_t kQzbMaxSectionCount = 32u;
static constexpr uint32_t kQzbMaxStringCount = 200000u;
static constexpr uint32_t kQzbMaxSingleStringBytes = 1u * 1024u * 1024u;
static constexpr uint32_t kQzbMaxTotalStringBytes = 64u * 1024u * 1024u;
static constexpr uint32_t kQzbMaxFunctionCount = 50000u;
static constexpr uint32_t kQzbMaxParamsPerFunction = 2048u;
static constexpr uint32_t kQzbMaxLocalsPerFunction = 65535u;
static constexpr uint32_t kQzbMaxCodeBytesPerFunction = 16u * 1024u * 1024u;
static constexpr uint64_t kQzbMaxTotalCodeBytes = 128ull * 1024ull * 1024ull;
static constexpr uint32_t kQzbMaxModuleCount = 200000u;
static constexpr uint32_t kQzbMaxMetadataSourceCount = 100000u;
static constexpr uint32_t kQzbMaxMetadataPathBytes = 4096u;
static constexpr uint32_t kQzbMaxCompilerOptionsBytes = 1u * 1024u * 1024u;

static constexpr uint32_t fourcc(char a, char b, char c, char d) {
    return (uint32_t)(uint8_t)a | ((uint32_t)(uint8_t)b << 8) | ((uint32_t)(uint8_t)c << 16) | ((uint32_t)(uint8_t)d << 24);
}

static constexpr uint32_t kQzbSection_META = fourcc('M', 'E', 'T', 'A');
static constexpr uint32_t kQzbSection_PAYL = fourcc('P', 'A', 'Y', 'L');

static void writeU8(std::vector<uint8_t>& out, uint8_t v) { out.push_back(v); }
static void writeU16(std::vector<uint8_t>& out, uint16_t v) {
    out.push_back((uint8_t)(v & 0xFF));
    out.push_back((uint8_t)((v >> 8) & 0xFF));
}
static void writeU32(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back((uint8_t)(v & 0xFF));
    out.push_back((uint8_t)((v >> 8) & 0xFF));
    out.push_back((uint8_t)((v >> 16) & 0xFF));
    out.push_back((uint8_t)((v >> 24) & 0xFF));
}

static void writeU64(std::vector<uint8_t>& out, uint64_t v) {
    for (int i = 0; i < 8; ++i) out.push_back((uint8_t)((v >> (i * 8)) & 0xFF));
}

static void writeBytes(std::vector<uint8_t>& out, const void* data, size_t n) {
    const uint8_t* p = reinterpret_cast<const uint8_t*>(data);
    out.insert(out.end(), p, p + n);
}

static void patchU32(std::vector<uint8_t>& out, size_t offset, uint32_t v) {
    out[offset + 0] = (uint8_t)(v & 0xFF);
    out[offset + 1] = (uint8_t)((v >> 8) & 0xFF);
    out[offset + 2] = (uint8_t)((v >> 16) & 0xFF);
    out[offset + 3] = (uint8_t)((v >> 24) & 0xFF);
}
static void writeI32(std::vector<uint8_t>& out, int32_t v) { writeU32(out, (uint32_t)v); }
static void writeF64(std::vector<uint8_t>& out, double v) {
    uint64_t bits;
    std::memcpy(&bits, &v, sizeof(bits));
    for (int i = 0; i < 8; ++i) out.push_back((uint8_t)((bits >> (i * 8)) & 0xFF));
}

static bool readExact(std::istream& in, void* buf, size_t n) {
    in.read(reinterpret_cast<char*>(buf), (std::streamsize)n);
    return (size_t)in.gcount() == n;
}
static bool readU32(std::istream& in, uint32_t* out) {
    uint8_t b[4];
    if (!readExact(in, b, 4)) return false;
    *out = (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
    return true;
}

static bool readU64(std::istream& in, uint64_t* out) {
    uint8_t b[8];
    if (!readExact(in, b, 8)) return false;
    *out = 0;
    for (int i = 0; i < 8; ++i) *out |= (uint64_t)b[i] << (i * 8);
    return true;
}
static bool readU16(std::istream& in, uint16_t* out) {
    uint8_t b[2];
    if (!readExact(in, b, 2)) return false;
    *out = (uint16_t)b[0] | ((uint16_t)b[1] << 8);
    return true;
}
static bool readU8(std::istream& in, uint8_t* out) {
    uint8_t b;
    if (!readExact(in, &b, 1)) return false;
    *out = b;
    return true;
}

static uint32_t crc32(const uint8_t* data, size_t len) {
    // Standard CRC-32 (IEEE 802.3 polynomial 0xEDB88320)
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; ++i) {
        crc ^= (uint32_t)data[i];
        for (int k = 0; k < 8; ++k) {
            uint32_t mask = (uint32_t)-(int)(crc & 1u);
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return ~crc;
}

static std::vector<uint8_t> buildMetadataBlob(const Program& program) {
    // Metadata v1:
    // u32 metaVersion
    // u32 sourceCount
    //  repeat sourceCount times:
    //    u32 pathLen, pathBytes
    //    u64 sizeBytes
    //    u8[32] sha256
    // u32 compilerOptionsLen, compilerOptionsBytes
    std::vector<uint8_t> meta;
    meta.reserve(256);

    const uint32_t metaVersion = 1;
    writeU32(meta, metaVersion);

    std::vector<Program::SourceFileMeta> sources = program.sources;
    std::sort(sources.begin(), sources.end(), [](const auto& a, const auto& b) { return a.path < b.path; });
    // de-dup by path
    sources.erase(std::unique(sources.begin(), sources.end(), [](const auto& a, const auto& b) { return a.path == b.path; }),
                  sources.end());

    writeU32(meta, (uint32_t)sources.size());
    for (const auto& s : sources) {
        writeU32(meta, (uint32_t)s.path.size());
        writeBytes(meta, s.path.data(), s.path.size());
        writeU64(meta, s.sizeBytes);
        writeBytes(meta, s.sha256.data(), s.sha256.size());
    }

    writeU32(meta, (uint32_t)program.compilerOptions.size());
    writeBytes(meta, program.compilerOptions.data(), program.compilerOptions.size());

    return meta;
}

static bool parseMetadataBlob(const std::vector<uint8_t>& meta, Program* p, std::string* error) {
    if (!p) return true;
    std::string metaStr(reinterpret_cast<const char*>(meta.data()), meta.size());
    std::istringstream in(metaStr);

    auto fail = [&](const std::string& msg) {
        if (error) *error = msg;
        return false;
    };

    uint32_t metaVersion = 0;
    if (!readU32(in, &metaVersion)) {
        return fail("Corrupt bytecode file (metadata)");
    }
    if (metaVersion != 1) {
        return fail("Unsupported metadata version");
    }

    uint32_t sourceCount = 0;
    if (!readU32(in, &sourceCount)) {
        return fail("Corrupt bytecode file (metadata sources)");
    }
    if (sourceCount > kQzbMaxMetadataSourceCount) {
        return fail("Bytecode metadata too large (too many sources)");
    }
    p->sources.clear();
    p->sources.reserve(sourceCount);
    for (uint32_t i = 0; i < sourceCount; ++i) {
        uint32_t pathLen = 0;
        if (!readU32(in, &pathLen)) return fail("Corrupt bytecode file (metadata path length)");
        if (pathLen > kQzbMaxMetadataPathBytes) {
            return fail("Bytecode metadata too large (path length)");
        }
        std::string path;
        path.resize(pathLen);
        if (!readExact(in, path.data(), pathLen)) {
            return fail("Corrupt bytecode file (metadata path)");
        }
        uint64_t sizeBytes = 0;
        if (!readU64(in, &sizeBytes)) {
            return fail("Corrupt bytecode file (metadata size)");
        }
        Program::SourceFileMeta sf;
        sf.path = std::move(path);
        sf.sizeBytes = sizeBytes;
        if (!readExact(in, sf.sha256.data(), sf.sha256.size())) {
            return fail("Corrupt bytecode file (metadata sha256)");
        }
        p->sources.push_back(std::move(sf));
    }

    uint32_t optLen = 0;
    if (!readU32(in, &optLen)) {
        return fail("Corrupt bytecode file (metadata compiler options)");
    }
    if (optLen > kQzbMaxCompilerOptionsBytes) {
        return fail("Bytecode metadata too large (compiler options)");
    }
    std::string opts;
    opts.resize(optLen);
    if (!readExact(in, opts.data(), optLen)) {
        return fail("Corrupt bytecode file (metadata compiler options data)");
    }
    p->compilerOptions = std::move(opts);
    return true;
}

bool writeProgramToFile(const Program& program, const std::string& filePath, std::string* error) {
    std::vector<uint8_t> payload;
    payload.reserve(1024);

    // Strings
    writeU32(payload, (uint32_t)program.strings.size());
    for (const auto& s : program.strings) {
        writeU32(payload, (uint32_t)s.size());
        payload.insert(payload.end(), s.begin(), s.end());
    }

    // Functions
    writeU32(payload, (uint32_t)program.functions.size());
    writeU32(payload, program.entryFunction);

    for (const auto& fn : program.functions) {
        writeU32(payload, fn.nameString);

        writeU32(payload, (uint32_t)fn.paramNameStrings.size());
        for (uint32_t p : fn.paramNameStrings) writeU32(payload, p);

        // locals (v2+)
        writeU32(payload, (uint32_t)fn.localNameStrings.size());
        for (uint32_t l : fn.localNameStrings) writeU32(payload, l);

        writeU32(payload, (uint32_t)fn.code.size());
        payload.insert(payload.end(), fn.code.begin(), fn.code.end());
    }

    // Modules table
    writeU32(payload, (uint32_t)program.modules.size());
    for (const auto& kv : program.modules) {
        // store modulePath as string index for canonicalization
        // If missing in pool, write raw string (0xFFFFFFFF) + bytes.
        // Compiler always interns, so keep simple.
        auto it = std::find(program.strings.begin(), program.strings.end(), kv.first);
        if (it == program.strings.end()) {
            if (error) *error = "Module path not interned: " + kv.first;
            return false;
        }
        uint32_t sidx = (uint32_t)std::distance(program.strings.begin(), it);
        writeU32(payload, sidx);
        writeU32(payload, kv.second);
    }

    const std::vector<uint8_t> meta = buildMetadataBlob(program);
    const uint32_t metaCrc = crc32(meta.data(), meta.size());
    const uint32_t payloadCrc = crc32(payload.data(), payload.size());

    std::vector<uint8_t> blob;
    blob.reserve(64 + meta.size() + payload.size());

    // v5+: sectioned container for forward compatibility.
    if (kVersion >= 5) {
        writeU32(blob, kMagic);
        writeU16(blob, kVersion);
        writeU16(blob, 0);

        writeU32(blob, qz::kQuartzApiVersion);
        writeU32(blob, qz::kQzbDefaultFlags);

        writeU32(blob, 2); // sectionCount

        // Section descriptors: type, size, crc32
        writeU32(blob, kQzbSection_META);
        writeU32(blob, (uint32_t)meta.size());
        writeU32(blob, metaCrc);

        writeU32(blob, kQzbSection_PAYL);
        writeU32(blob, (uint32_t)payload.size());
        writeU32(blob, payloadCrc);

        // Section bytes
        writeBytes(blob, meta.data(), meta.size());
        writeBytes(blob, payload.data(), payload.size());
    } else {
        // v4 layout (legacy): fixed header with metaSize/metaCrc/payloadSize/payloadCrc.
        writeU32(blob, kMagic);
        writeU16(blob, kVersion);
        writeU16(blob, 0);
        writeU32(blob, qz::kQuartzApiVersion);
        writeU32(blob, qz::kQzbDefaultFlags);
        writeU32(blob, (uint32_t)meta.size());
        writeU32(blob, metaCrc);
        writeU32(blob, (uint32_t)payload.size());
        writeU32(blob, payloadCrc);
        writeBytes(blob, meta.data(), meta.size());
        writeBytes(blob, payload.data(), payload.size());
    }

    std::ofstream out(filePath, std::ios::binary);
    if (!out.is_open()) {
        if (error) *error = "Could not open output bytecode file: " + filePath;
        return false;
    }

    out.write(reinterpret_cast<const char*>(blob.data()), (std::streamsize)blob.size());
    if (!out.good()) {
        if (error) *error = "Failed writing bytecode to file: " + filePath;
        return false;
    }
    return true;
}

bool readProgramFromFile(const std::string& filePath, Program* outProgram, std::string* error) {
    std::ifstream in(filePath, std::ios::binary);
    if (!in.is_open()) {
        if (error) *error = "Could not open bytecode file: " + filePath;
        return false;
    }

    auto fail = [&](const std::string& msg) {
        if (error) *error = msg;
        return false;
    };

    auto readBytesSkip = [&](size_t n) {
        static constexpr size_t kChunk = 64 * 1024;
        std::vector<char> buf;
        buf.resize(std::min(n, kChunk));
        size_t remaining = n;
        while (remaining > 0) {
            size_t take = std::min(remaining, buf.size());
            if (!readExact(in, buf.data(), take)) return false;
            remaining -= take;
        }
        return true;
    };

    uint32_t magic = 0;
    uint16_t ver = 0;
    uint16_t reserved = 0;

    if (!readU32(in, &magic) || (magic != kMagic && magic != kMagicLegacyJLB1)) {
        return fail("Invalid bytecode file (bad magic)");
    }
    if (!readU16(in, &ver)) {
        return fail("Corrupt bytecode file (header)");
    }
    if (ver < 1 || ver > kVersion) {
        return fail("Unsupported bytecode version");
    }
    if (!readU16(in, &reserved)) {
        return fail("Corrupt bytecode file (header)");
    }

    auto parsePayloadStream = [&](std::istream& payloadIn, Program* p, uint16_t payloadVer) {
        uint32_t stringCount = 0;
        if (!readU32(payloadIn, &stringCount)) return fail("Corrupt bytecode file (strings)");
        if (stringCount > kQzbMaxStringCount) return fail("Bytecode too large (string table)");
        p->strings.clear();
        p->strings.reserve(stringCount);
        uint64_t totalStringBytes = 0;
        for (uint32_t i = 0; i < stringCount; ++i) {
            uint32_t len = 0;
            if (!readU32(payloadIn, &len)) return fail("Corrupt bytecode file (string length)");
            if (len > kQzbMaxSingleStringBytes) return fail("Bytecode too large (single string)");
            totalStringBytes += len;
            if (totalStringBytes > kQzbMaxTotalStringBytes) return fail("Bytecode too large (string bytes)");
            std::string s;
            s.resize(len);
            if (!readExact(payloadIn, s.data(), len)) return fail("Corrupt bytecode file (string data)");
            p->strings.push_back(std::move(s));
        }

        uint32_t fnCount = 0;
        if (!readU32(payloadIn, &fnCount)) return fail("Corrupt bytecode file (functions)");
        if (fnCount > kQzbMaxFunctionCount) return fail("Bytecode too large (function table)");

        if (!readU32(payloadIn, &p->entryFunction)) return fail("Corrupt bytecode file (entry)");

        p->functions.clear();
        p->functions.resize(fnCount);

        uint64_t totalCodeBytes = 0;
        for (uint32_t i = 0; i < fnCount; ++i) {
            Function fn;
            if (!readU32(payloadIn, &fn.nameString)) return fail("Corrupt bytecode file (function name)");

            uint32_t paramCount = 0;
            if (!readU32(payloadIn, &paramCount)) return fail("Corrupt bytecode file (function params)");
            if (paramCount > kQzbMaxParamsPerFunction) return fail("Bytecode too large (param count)");
            fn.paramNameStrings.resize(paramCount);
            for (uint32_t j = 0; j < paramCount; ++j) {
                if (!readU32(payloadIn, &fn.paramNameStrings[j])) return fail("Corrupt bytecode file (function params)");
            }

            if (payloadVer >= 2) {
                uint32_t localCount = 0;
                if (!readU32(payloadIn, &localCount)) return fail("Corrupt bytecode file (function locals)");
                if (localCount > kQzbMaxLocalsPerFunction) return fail("Bytecode too large (local count)");
                fn.localNameStrings.resize(localCount);
                for (uint32_t j = 0; j < localCount; ++j) {
                    if (!readU32(payloadIn, &fn.localNameStrings[j])) return fail("Corrupt bytecode file (function locals)");
                }
            } else {
                fn.localNameStrings = fn.paramNameStrings;
            }

            uint32_t codeSize = 0;
            if (!readU32(payloadIn, &codeSize)) return fail("Corrupt bytecode file (function code size)");
            if (codeSize > kQzbMaxCodeBytesPerFunction) return fail("Bytecode too large (function code)");
            totalCodeBytes += codeSize;
            if (totalCodeBytes > kQzbMaxTotalCodeBytes) return fail("Bytecode too large (total code)");
            fn.code.resize(codeSize);
            if (!readExact(payloadIn, fn.code.data(), codeSize)) return fail("Corrupt bytecode file (function code)");

            p->functions[i] = std::move(fn);
        }

        uint32_t moduleCount = 0;
        if (!readU32(payloadIn, &moduleCount)) return fail("Corrupt bytecode file (modules)");
        if (moduleCount > kQzbMaxModuleCount) return fail("Bytecode too large (module table)");
        p->modules.clear();
        for (uint32_t i = 0; i < moduleCount; ++i) {
            uint32_t modStrIdx = 0;
            uint32_t fnIdx = 0;
            if (!readU32(payloadIn, &modStrIdx) || !readU32(payloadIn, &fnIdx)) return fail("Corrupt bytecode file (module entry)");
            if (modStrIdx >= p->strings.size() || fnIdx >= p->functions.size()) return fail("Corrupt bytecode file (module indices)");
            p->modules[p->strings[modStrIdx]] = fnIdx;
        }

        if (fnCount > 0 && p->entryFunction >= fnCount) return fail("Corrupt bytecode file (entry out of range)");
        return true;
    };

    // v5+: sectioned container.
    if (ver >= 5) {
        uint32_t quartzApiVersion = 0;
        uint32_t flags = 0;
        uint32_t sectionCount = 0;
        if (!readU32(in, &quartzApiVersion) || !readU32(in, &flags) || !readU32(in, &sectionCount)) {
            return fail("Corrupt bytecode file (header)");
        }
        if (sectionCount == 0 || sectionCount > kQzbMaxSectionCount) {
            return fail("Corrupt bytecode file (section count)");
        }
        if ((flags & qz::kQzbFlagCompressed) != 0) {
            return fail("Unsupported bytecode compression");
        }

        struct SectionDesc {
            uint32_t type = 0;
            uint32_t size = 0;
            uint32_t crc = 0;
        };
        std::vector<SectionDesc> sections;
        sections.resize(sectionCount);
        for (uint32_t i = 0; i < sectionCount; ++i) {
            if (!readU32(in, &sections[i].type) || !readU32(in, &sections[i].size) || !readU32(in, &sections[i].crc)) {
                return fail("Corrupt bytecode file (section headers)");
            }
            if (sections[i].size > kQzbMaxPayloadSize) {
                return fail("Bytecode too large (section)");
            }
        }

        std::vector<uint8_t> meta;
        std::vector<uint8_t> payload;

        for (uint32_t i = 0; i < sectionCount; ++i) {
            const auto& s = sections[i];
            if (s.type == kQzbSection_META) {
                if (s.size > kQzbMaxMetaSize) return fail("Bytecode too large (metadata)");
                meta.resize(s.size);
                if (!readExact(in, meta.data(), meta.size())) return fail("Corrupt bytecode file (metadata truncated)");
                const uint32_t actual = crc32(meta.data(), meta.size());
                if (actual != s.crc) return fail("Corrupt bytecode file (metadata CRC mismatch)");
            } else if (s.type == kQzbSection_PAYL) {
                if (s.size > kQzbMaxPayloadSize) return fail("Bytecode too large (payload)");
                payload.resize(s.size);
                if (!readExact(in, payload.data(), payload.size())) return fail("Corrupt bytecode file (payload truncated)");
                const uint32_t actual = crc32(payload.data(), payload.size());
                if (actual != s.crc) return fail("Corrupt bytecode file (payload CRC mismatch)");
            } else {
                if (!readBytesSkip(s.size)) return fail("Corrupt bytecode file (section truncated)");
            }
        }

        if (payload.empty()) return fail("Corrupt bytecode file (missing payload section)");

        Program p;
        if (!meta.empty()) {
            if (!parseMetadataBlob(meta, &p, error)) return false;
        }

        std::string payloadStr;
        payloadStr.assign(reinterpret_cast<const char*>(payload.data()), payload.size());
        std::istringstream payloadIn(payloadStr);
        if (!parsePayloadStream(payloadIn, &p, 2)) return false;

        *outProgram = std::move(p);
        (void)quartzApiVersion;
        return true;
    }

    // v4: fixed header with metadata+payload sizes and CRCs.
    if (ver == 4) {
        uint32_t quartzApiVersion = 0;
        uint32_t flags = 0;
        uint32_t metaSize = 0;
        uint32_t metaCrc = 0;
        uint32_t payloadSize = 0;
        uint32_t payloadCrc = 0;
        if (!readU32(in, &quartzApiVersion) || !readU32(in, &flags) || !readU32(in, &metaSize) || !readU32(in, &metaCrc) ||
            !readU32(in, &payloadSize) || !readU32(in, &payloadCrc)) {
            return fail("Corrupt bytecode file (header)");
        }

        if ((flags & qz::kQzbFlagCompressed) != 0) {
            return fail("Unsupported bytecode compression");
        }

        if (metaSize > kQzbMaxMetaSize) return fail("Bytecode too large (metadata)");
        if (payloadSize > kQzbMaxPayloadSize) return fail("Bytecode too large (payload)");

        std::vector<uint8_t> meta;
        meta.resize(metaSize);
        if (!readExact(in, meta.data(), meta.size())) {
            return fail("Corrupt bytecode file (metadata truncated)");
        }
        const uint32_t actualMetaCrc = crc32(meta.data(), meta.size());
        if (actualMetaCrc != metaCrc) {
            return fail("Corrupt bytecode file (metadata CRC mismatch)");
        }

        std::vector<uint8_t> payload;
        payload.resize(payloadSize);
        if (!readExact(in, payload.data(), payload.size())) {
            return fail("Corrupt bytecode file (payload truncated)");
        }
        const uint32_t actualPayloadCrc = crc32(payload.data(), payload.size());
        if (actualPayloadCrc != payloadCrc) {
            return fail("Corrupt bytecode file (payload CRC mismatch)");
        }

        std::string payloadStr;
        payloadStr.assign(reinterpret_cast<const char*>(payload.data()), payload.size());
        std::istringstream payloadIn(payloadStr);

        Program p;
        if (!parseMetadataBlob(meta, &p, error)) return false;
        if (!parsePayloadStream(payloadIn, &p, 2)) return false;

        *outProgram = std::move(p);
        (void)quartzApiVersion;
        return true;
    }

    // v3: validate payload CRC and size, then parse from payload bytes (no metadata section).
    if (ver == 3) {
        uint32_t quartzApiVersion = 0;
        uint32_t flags = 0;
        uint32_t payloadSize = 0;
        uint32_t payloadCrc = 0;
        if (!readU32(in, &quartzApiVersion) || !readU32(in, &flags) || !readU32(in, &payloadSize) || !readU32(in, &payloadCrc)) {
            return fail("Corrupt bytecode file (header)");
        }

        if ((flags & qz::kQzbFlagCompressed) != 0) {
            return fail("Unsupported bytecode compression");
        }

        if (payloadSize > kQzbMaxPayloadSize) return fail("Bytecode too large (payload)");

        std::vector<uint8_t> payload;
        payload.resize(payloadSize);
        if (!readExact(in, payload.data(), payload.size())) {
            return fail("Corrupt bytecode file (payload truncated)");
        }
        const uint32_t actualPayloadCrc = crc32(payload.data(), payload.size());
        if (actualPayloadCrc != payloadCrc) {
            return fail("Corrupt bytecode file (payload CRC mismatch)");
        }

        std::string payloadStr;
        payloadStr.assign(reinterpret_cast<const char*>(payload.data()), payload.size());
        std::istringstream payloadIn(payloadStr);

        Program p;
        if (!parsePayloadStream(payloadIn, &p, 2)) return false;

        *outProgram = std::move(p);
        (void)quartzApiVersion;
        return true;
    }

    Program p;
    if (!parsePayloadStream(in, &p, ver)) return false;
    *outProgram = std::move(p);
    return true;
}

} // namespace bc
