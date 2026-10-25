#pragma once

#include "mwperffix/adaptive_reflections.hpp"
#include "mwperffix/diagnostics/core.hpp"
#include "mwperffix/patch.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace mwperffix::diagnostics {

class Capture;

struct FixStatus {
    bool requested;
    Outcome outcome;
};

// The existing ETW schema has no "not attempted" outcome.
// Preserve its legacy mismatch placeholder here; requested remains a separate feature-selection flag.
[[nodiscard]] constexpr FixStatus
installation_status(const bool requested, const std::optional<Outcome> outcome) noexcept {
    return {requested, outcome.value_or(Outcome::mismatch)};
}

using LogFunction = void (*)(void *, std::string_view) noexcept;

struct InitializeInput {
    Memory &memory;
    std::uintptr_t executable_base;
    const wchar_t *ini_path;
    adaptive::Config adaptive_config;
    std::array<FixStatus, 5> fixes;
    LogFunction log;
    void *log_context;
};

// Registers the scheduler diagnostic provider and publishes the capture.
void initialize(const InitializeInput &input) noexcept;

// Diagnostic-only bridge between the adaptive controller and the installed capture.
// The observer is safe to install before the capture exists.
// When no ETW listener is enabled it does nothing.
[[nodiscard]] adaptive::Observer adaptive_observer() noexcept;

// Publishes the capture to the process-lifetime observer bridge.
// The capture must outlive the process.
void publish_capture(Capture *capture) noexcept;

} // namespace mwperffix::diagnostics
