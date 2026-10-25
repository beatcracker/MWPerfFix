#pragma once

#include "mwperffix/adaptive_reflections.hpp"
#include "mwperffix/reflection_blending.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string_view>

namespace mwperffix::notifications {

enum class Message : std::uint8_t {
    none,
    custom,
};

[[nodiscard]] constexpr std::string_view
adaptive_reason_label(
    const adaptive::StateChangeReason reason, const std::uint32_t previous_budget, const std::uint32_t new_budget
) noexcept {
    if (previous_budget == new_budget)
        return {};

    switch (reason) {
    case adaptive::StateChangeReason::full_refresh_complete:
        return "refresh complete";
    case adaptive::StateChangeReason::repeated_overload:
        return new_budget < previous_budget ? "load" : std::string_view{};
    case adaptive::StateChangeReason::sustained_headroom:
        return new_budget > previous_budget ? "affordable" : std::string_view{};
    case adaptive::StateChangeReason::invalid_sample_safety:
        return "invalid sample";
    case adaptive::StateChangeReason::device_reset_refresh:
        return "reset";
    case adaptive::StateChangeReason::observation_gap_reset:
        return "gap";
    case adaptive::StateChangeReason::configured_mode_reset:
        return "re-entry";
    }
    return {};
}

struct AdaptiveSnapshot {
    std::uint32_t previous_budget = 0U;
    std::uint32_t new_budget = 0U;
    adaptive::StateChangeReason reason = adaptive::StateChangeReason::full_refresh_complete;
};

struct AdaptiveText {
    std::array<char, 96> line1{};
};

template <std::size_t N, typename... Args>
[[nodiscard]] inline bool
checked_format(std::array<char, N> &output, const char *const format, Args... args) noexcept {
    static_assert(N > 1U);
    output.fill('\0');
    const int length = std::snprintf(output.data(), output.size(), format, args...);
    if (length < 0 || static_cast<std::size_t>(length) >= output.size()) {
        output.back() = '\0';
        return false;
    }
    return true;
}

[[nodiscard]] inline bool
format_adaptive_snapshot(const AdaptiveSnapshot &snapshot, AdaptiveText &output) noexcept {
    const std::string_view reason =
        adaptive_reason_label(snapshot.reason, snapshot.previous_budget, snapshot.new_budget);
    if (reason.empty())
        return false;

    return checked_format(
        output.line1, "Reflections: %s (%.*s)",
        snapshot.new_budget == 6U ? "Native full-update" : "Weighted single-face", static_cast<int>(reason.size()),
        reason.data()
    );
}

// Native transient notification of the effective blending setting.
// off preserves the currently displayed native face until direct capture.
template <std::size_t N>
[[nodiscard]] inline bool
format_blending_mode(const blending::Mode mode, const std::uint32_t steps, std::array<char, N> &output) noexcept {
    if (mode == blending::Mode::off)
        return checked_format(output, "%s", "Reflection blending: off");
    return checked_format(output, "Reflection blending: on (%u steps)", steps);
}

class State {
  public:
    explicit constexpr State(const bool enabled = false) noexcept : enabled_(enabled) {
    }

    [[nodiscard]] constexpr bool
    enabled() const noexcept {
        return enabled_;
    }

    void
    post_text(const std::string_view value) noexcept {
        if (!enabled_ || value.empty() || value.size() >= custom_text_.size())
            return;
        std::memcpy(custom_text_.data(), value.data(), value.size());
        custom_text_[value.size()] = '\0';
        pending_ = Message::custom;
    }

    [[nodiscard]] std::string_view
    message_text(const Message message) const noexcept {
        return message == Message::custom ? std::string_view{custom_text_.data()} : std::string_view{};
    }

    [[nodiscard]] constexpr Message
    pending() const noexcept {
        return pending_;
    }

    [[nodiscard]] constexpr Message
    take_pending() noexcept {
        const Message result = pending_;
        pending_ = Message::none;
        return result;
    }

    constexpr void
    shown(const std::uint64_t now, const std::uint64_t duration_ticks) noexcept {
        if (!enabled_)
            return;
        visible_ = true;
        expires_ = duration_ticks > std::numeric_limits<std::uint64_t>::max() - now
                       ? std::numeric_limits<std::uint64_t>::max()
                       : now + duration_ticks;
    }

    [[nodiscard]] constexpr bool
    visible() const noexcept {
        return visible_;
    }

    [[nodiscard]] constexpr bool
    expired(const std::uint64_t now) const noexcept {
        return visible_ && now >= expires_;
    }

    constexpr void
    hidden() noexcept {
        visible_ = false;
        expires_ = 0U;
    }

    constexpr void
    invalidate() noexcept {
        pending_ = Message::none;
        hidden();
    }

  private:
    bool enabled_ = false;
    bool visible_ = false;
    Message pending_ = Message::none;
    std::array<char, 64> custom_text_{};
    std::uint64_t expires_ = 0U;
};

class AdaptiveState {
  public:
    explicit constexpr AdaptiveState(const bool enabled = false) noexcept : enabled_(enabled) {
    }

    [[nodiscard]] constexpr bool
    enabled() const noexcept {
        return enabled_;
    }

    constexpr void
    post(const AdaptiveSnapshot &snapshot) noexcept {
        if (!enabled_)
            return;
        pending_snapshot_ = snapshot;
        pending_ = true;
    }

    [[nodiscard]] constexpr bool
    pending() const noexcept {
        return pending_;
    }

    [[nodiscard]] constexpr const AdaptiveSnapshot &
    pending_snapshot() const noexcept {
        return pending_snapshot_;
    }

    constexpr bool
    take_pending(AdaptiveSnapshot &snapshot) noexcept {
        if (!pending_)
            return false;
        snapshot = pending_snapshot_;
        pending_ = false;
        return true;
    }

    constexpr void
    shown(const std::uint64_t now, const std::uint64_t duration_ticks) noexcept {
        if (!enabled_)
            return;
        visible_ = true;
        expires_ = duration_ticks > std::numeric_limits<std::uint64_t>::max() - now
                       ? std::numeric_limits<std::uint64_t>::max()
                       : now + duration_ticks;
    }

    [[nodiscard]] constexpr bool
    visible() const noexcept {
        return visible_;
    }

    [[nodiscard]] constexpr bool
    expired(const std::uint64_t now) const noexcept {
        return visible_ && now >= expires_;
    }

    constexpr void
    hidden() noexcept {
        visible_ = false;
        expires_ = 0U;
    }

    constexpr void
    invalidate() noexcept {
        pending_ = false;
        pending_snapshot_ = {};
        hidden();
    }

  private:
    bool enabled_ = false;
    bool pending_ = false;
    bool visible_ = false;
    AdaptiveSnapshot pending_snapshot_{};
    std::uint64_t expires_ = 0U;
};

} // namespace mwperffix::notifications
