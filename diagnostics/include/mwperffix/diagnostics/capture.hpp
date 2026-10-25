#pragma once

#include "mwperffix/adaptive_reflections.hpp"
#include "mwperffix/diagnostics/core.hpp"

#include <cstdint>

namespace mwperffix::diagnostics {

inline constexpr char kProviderName[] = "MWPerfFix.Diagnostics";
inline constexpr char kProviderGuid[] = "9e8a7dcb-4fa5-45e2-a126-4d962b7ea3f1";
inline constexpr std::uint8_t kProviderLevel = 4;
inline constexpr std::uint64_t kProviderKeyword = 1;

struct Metadata {
    const char *build_version = "";
    const char *build_variant = "";
    std::uint64_t executable_base = 0;
    std::uint32_t executable_timestamp = 0;
    std::uint32_t executable_size = 0;
    std::uint32_t target_fps = adaptive::kDefaultTargetFps;
    FixStatusBits fixes{};
};

// Scheduler-only ETW capture, schema 23.
class Capture {
  public:
    Capture() noexcept = default;
    Capture(const Capture &) = delete;
    Capture &operator=(const Capture &) = delete;
    ~Capture();

    [[nodiscard]] bool initialize(bool synthetic) noexcept;
    [[nodiscard]] bool enabled() const noexcept;
    // TraceLoggingWrite does not expose delivery status.
    // Validate ETW loss when consuming the trace.
    void emit_metadata(const Metadata &metadata) noexcept;
    // Stores metadata without emitting it.
    // The next frame event emits it before that frame.
    // A state-change event may still precede metadata.
    void
    set_metadata(const Metadata &metadata) noexcept {
        metadata_ = metadata;
    }
    void emit_frame(const adaptive::FrameObservation &observation) noexcept;
    void emit_state_change(const adaptive::StateChange &change) noexcept;

    [[nodiscard]] std::uint64_t
    process_creation_id() const noexcept {
        return process_creation_id_;
    }
    [[nodiscard]] std::uint64_t
    qpc_frequency() const noexcept {
        return qpc_frequency_;
    }

  private:
    bool registered_ = false;
    bool synthetic_ = false;
    std::uint64_t process_creation_id_ = 0;
    std::uint64_t qpc_frequency_ = 0;
    std::uint64_t frame_sequence_ = 0;
    std::uint32_t frames_since_metadata_ = 0;
    bool metadata_due_ = true;
    // Last metadata value, emitted before the first pending frame and re-emitted periodically
    // so frame-bearing captures regain identity context.
    // The string pointers reference static build-info storage and stay valid for the process lifetime.
    Metadata metadata_{};
};

} // namespace mwperffix::diagnostics
