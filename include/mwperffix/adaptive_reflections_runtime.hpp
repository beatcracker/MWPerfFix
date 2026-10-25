#pragma once

#include "mwperffix/adaptive_reflections.hpp"
#include "mwperffix/patch.hpp"
#include "mwperffix/reflection_blending.hpp"
#include "mwperffix/runtime_hotkeys.hpp"
#include "mwperffix/runtime_notifications.hpp"

#include <cstdint>
#include <string_view>

namespace mwperffix::adaptive {

using LogFunction = void (*)(void *, std::string_view) noexcept;

enum class Schedule : std::uint8_t {
    native_single,
    native_other,
    native_full_update,
    weighted_single_face
};

// Classifies blending eligibility; native_other includes disabled and multi-face modes.
[[nodiscard]] constexpr Schedule
classify_native_schedule(const std::uint32_t setting) noexcept {
    return setting == 1U ? Schedule::native_single : Schedule::native_other;
}

struct ReflectionConfig {
    Config scheduler{};
    blending::Config blending{};
    bool adaptive_enabled = true;
    hotkeys::Config hotkeys{};
    bool notifications_enabled = false;
};

[[nodiscard]] Outcome install(
    Memory &memory, std::uintptr_t executable_base, const ReflectionConfig &config, LogFunction log, void *log_context,
    Observer observer = {}
) noexcept;

} // namespace mwperffix::adaptive
