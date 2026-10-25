#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace mwperffix::hotkeys {

inline constexpr std::size_t kCount = 5U;

struct Config {
    std::uint32_t toggle_adaptive = 0U;
    std::uint32_t target_down = 0U;
    std::uint32_t target_up = 0U;
    std::uint32_t toggle_blending = 0U;
    std::uint32_t cycle_steps = 0U;

    [[nodiscard]] constexpr std::array<std::uint32_t, kCount>
    keys() const noexcept {
        return {toggle_adaptive, target_down, target_up, toggle_blending, cycle_steps};
    }
};

struct Actions {
    bool toggle_adaptive = false;
    bool target_down = false;
    bool target_up = false;
    bool toggle_blending = false;
    bool cycle_steps = false;
};

[[nodiscard]] constexpr bool
ascii_space(const wchar_t value) noexcept {
    return value == L' ' || value == L'\t' || value == L'\r' || value == L'\n';
}

[[nodiscard]] constexpr std::wstring_view
trim(std::wstring_view value) noexcept {
    while (!value.empty() && ascii_space(value.front()))
        value.remove_prefix(1U);
    while (!value.empty() && ascii_space(value.back()))
        value.remove_suffix(1U);
    return value;
}

// Single raw Win32 virtual key, decimal or 0x-prefixed hex.
// Zero is unbound.
[[nodiscard]] constexpr std::uint32_t
parse_virtual_key(std::wstring_view value) noexcept {
    value = trim(value);
    if (value.empty())
        return 0U;
    std::uint32_t base = 10U;
    if (value.size() > 2U && value[0] == L'0' && (value[1] == L'x' || value[1] == L'X')) {
        base = 16U;
        value.remove_prefix(2U);
    }
    if (value.empty())
        return 0U;
    std::uint32_t result = 0U;
    for (const wchar_t character : value) {
        std::uint32_t digit = 0U;
        if (character >= L'0' && character <= L'9')
            digit = static_cast<std::uint32_t>(character - L'0');
        else if (base == 16U && character >= L'a' && character <= L'f')
            digit = 10U + static_cast<std::uint32_t>(character - L'a');
        else if (base == 16U && character >= L'A' && character <= L'F')
            digit = 10U + static_cast<std::uint32_t>(character - L'A');
        else
            return 0U;
        if (digit >= base)
            return 0U;
        result = result * base + digit;
        if (result > 0xfeU)
            return 0U;
    }
    // Mouse buttons, modifiers, reserved/undefined keys are not tuning actions.
    if (result < 0x08U || (result >= 0x10U && result <= 0x12U) || result == 0x5BU || result == 0x5CU || result == 0xffU)
        return 0U;
    return result;
}

// Both sides of a collision become unbound.
// Returns the conflicting VKs so startup can report the ambiguity exactly once per distinct key.
[[nodiscard]] constexpr std::array<std::uint32_t, kCount>
collisions(const Config config) noexcept {
    const auto keys = config.keys();
    std::array<std::uint32_t, kCount> result{};
    for (std::size_t i = 0; i < kCount; ++i)
        for (std::size_t j = i + 1U; j < kCount; ++j)
            if (keys[i] != 0U && keys[i] == keys[j])
                result[i] = result[j] = keys[i];
    return result;
}

[[nodiscard]] constexpr Config
remove_collisions(Config config) noexcept {
    const auto duplicate = collisions(config);
    auto keys = config.keys();
    for (std::size_t i = 0; i < kCount; ++i)
        if (duplicate[i] != 0U)
            keys[i] = 0U;
    return {keys[0], keys[1], keys[2], keys[3], keys[4]};
}

class State {
  public:
    explicit State(const Config config = {}) noexcept : keys_(config.keys()) {
    }

    [[nodiscard]] bool
    configured() const noexcept {
        for (const auto key : keys_)
            if (key != 0U)
                return true;
        return false;
    }

    [[nodiscard]] Config
    config() const noexcept {
        return {keys_[0], keys_[1], keys_[2], keys_[3], keys_[4]};
    }

    // Foreground regain establishes a fresh baseline for every bound key.
    [[nodiscard]] Actions
    sample(const bool foreground, const std::array<bool, kCount> &down) noexcept {
        if (!configured())
            return {};
        if (!foreground) {
            foreground_ = false;
            return {};
        }
        if (!foreground_) {
            foreground_ = true;
            previous_ = down;
            return {};
        }
        std::array<bool, kCount> edge{};
        for (std::size_t i = 0; i < kCount; ++i)
            edge[i] = keys_[i] != 0U && down[i] && !previous_[i];
        previous_ = down;
        return {edge[0], edge[1], edge[2], edge[3], edge[4]};
    }

  private:
    std::array<std::uint32_t, kCount> keys_{};
    std::array<bool, kCount> previous_{};
    bool foreground_ = false;
};

} // namespace mwperffix::hotkeys
