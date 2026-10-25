#pragma once

#include <cstdint>

namespace mwperffix::diagnostics {

inline constexpr std::uint32_t kSchemaVersion = 23;
inline constexpr char kSchemaMarker[] = "MWPERFFIX_ETW_SCHEMA_23";

// Metadata is emitted before the first emitted frame while pending and again every this many emitted frame events.
// State-change events do not force metadata emission, so consumers accept matching metadata later in a trace.
inline constexpr std::uint32_t kMetadataFrameInterval = 60;

// Packed fix status.
// Bit i of requested_mask/applied_mask corresponds to fix i.
// outcomes packs two bits per fix in the same order.
struct FixStatusBits {
    std::uint32_t requested_mask = 0;
    std::uint32_t applied_mask = 0;
    std::uint32_t outcomes = 0;
};

} // namespace mwperffix::diagnostics
