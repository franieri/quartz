#pragma once

#include <cstdint>

// Quartz public version + bytecode format metadata.
//
// This header is intended to be a single source of truth for the on-disk
// .qzb container format, including magic values, versioning, and flags.

namespace qz {

// Quartz API version (for embedding in artifacts / compatibility checks).
inline constexpr uint32_t kQuartzApiVersion = 1;

// Human-facing Quartz version.
inline constexpr uint32_t kQuartzVersionMajor = 0;
inline constexpr uint32_t kQuartzVersionMinor = 1;
inline constexpr uint32_t kQuartzVersionPatch = 0;

// .qzb bytecode container format.
//
// Versioning policy:
// - Reader should accept older versions when possible.
// - Writer always emits the current version.
inline constexpr uint16_t kQzbFormatVersion = 5;

// Magic values.
// New files: QZB1
// Legacy (older builds): JLB1
inline constexpr uint32_t kQzbMagicQZB1 = 0x31425A51u;       // bytes: 'Q' 'Z' 'B' '1'
inline constexpr uint32_t kQzbMagicLegacyJLB1 = 0x4A4C4231u; // legacy value used by older builds

// Flags embedded in v3+ .qzb headers.
enum QzbFlags : uint32_t {
    kQzbFlagLittleEndian = 1u << 0,
    kQzbFlagCompressed = 1u << 1,
};

inline constexpr uint32_t kQzbDefaultFlags = kQzbFlagLittleEndian;

} // namespace qz
