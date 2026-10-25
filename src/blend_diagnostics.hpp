#pragma once

#ifdef MWPERFFIX_DIAGNOSTICS
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace mwperffix::adaptive {
// Values are the existing blend_qpc array positions in ETW schema 23.
enum class BlendPhase : std::size_t {
    begin_frame = 0,
    select_schedule = 1,
    begin_face = 2,
    end_face = 3
};

// Only the four non-nested blending entry points are measured.
// No GPU timing.
struct BlendTiming {
    std::array<std::uint64_t, 4> ticks{};
    bool valid = false;
    bool active = false;
    bool failed = false;

    void
    reset() noexcept {
        *this = {};
        valid = true;
    }
    void
    add(const BlendPhase phase, const std::uint64_t begin, const std::uint64_t end) noexcept {
        const auto index = static_cast<std::size_t>(phase);
        if (!valid)
            return;
        if (index >= ticks.size() || end < begin ||
            end - begin > std::numeric_limits<std::uint64_t>::max() - ticks[index]) {
            ticks = {};
            valid = false;
            return;
        }
        ticks[index] += end - begin;
    }
};

class BlendTimingScope {
  public:
    using Clock = std::uint64_t (*)() noexcept;
    BlendTimingScope(
        BlendTiming &timing, const BlendPhase phase, Clock clock, const bool &active, const bool &failed
    ) noexcept
        : timing_(timing), phase_(phase), clock_(clock), active_(active), failed_(failed), begin_(clock()) {
        if (phase_ != BlendPhase::begin_frame)
            timing_.active |= active_;
        timing_.failed |= failed_;
    }
    ~BlendTimingScope() {
        const auto end = clock_();
        timing_.add(phase_, begin_, end);
        timing_.active |= active_;
        timing_.failed |= failed_;
    }
    BlendTimingScope(const BlendTimingScope &) = delete;
    BlendTimingScope &operator=(const BlendTimingScope &) = delete;

  private:
    BlendTiming &timing_;
    BlendPhase phase_;
    Clock clock_;
    const bool &active_;
    const bool &failed_;
    std::uint64_t begin_;
};
} // namespace mwperffix::adaptive
#endif
