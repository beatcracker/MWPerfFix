#include "mwperffix/runtime_notifications.hpp"
#include "test_error_mode.hpp"

#include <array>
#include <cstdlib>
#include <string_view>

namespace {

int failures = 0;

#define CHECK(condition)                                                                                               \
    do {                                                                                                               \
        if (!(condition))                                                                                              \
            ++failures;                                                                                                \
    } while (false)

using mwperffix::adaptive::StateChangeReason;
using mwperffix::notifications::adaptive_reason_label;
using mwperffix::notifications::AdaptiveSnapshot;
using mwperffix::notifications::AdaptiveState;
using mwperffix::notifications::AdaptiveText;
namespace blending = mwperffix::blending;
using mwperffix::notifications::checked_format;
using mwperffix::notifications::format_adaptive_snapshot;
using mwperffix::notifications::format_blending_mode;
using mwperffix::notifications::Message;
using mwperffix::notifications::State;

void
test_messages() {
    std::array<char, 64> message{};
    CHECK(format_blending_mode(blending::Mode::on, 3U, message));
    CHECK(std::string_view(message.data()) == "Reflection blending: on (3 steps)");
    CHECK(format_blending_mode(blending::Mode::off, 6U, message));
    CHECK(std::string_view(message.data()) == "Reflection blending: off");
    std::array<char, 12> small{};
    CHECK(!format_blending_mode(blending::Mode::on, 6U, small));
    CHECK(small.back() == '\0');
}

void
test_transient_pending_and_lifetime() {
    State disabled;
    disabled.post_text("Reflection blending: on");
    CHECK(disabled.pending() == Message::none);

    State state(true);
    state.post_text("Reflection blending: off");
    state.post_text("Reflection blending: on");
    CHECK(state.pending() == Message::custom);
    CHECK(state.take_pending() == Message::custom);
    CHECK(state.pending() == Message::none);

    state.shown(100U, 50U);
    CHECK(state.visible());
    CHECK(!state.expired(149U));
    CHECK(state.expired(150U));

    state.shown(140U, 50U);
    CHECK(!state.expired(189U));
    CHECK(state.expired(190U));

    state.hidden();
    CHECK(!state.visible());
    CHECK(!state.expired(1000U));

    state.post_text("Reflection blending: on");
    state.shown(10U, 20U);
    state.invalidate();
    CHECK(state.pending() == Message::none);
    CHECK(!state.visible());
}

void
test_adaptive_reason_mapping() {
    CHECK(adaptive_reason_label(StateChangeReason::repeated_overload, 6U, 1U) == "load");
    CHECK(adaptive_reason_label(StateChangeReason::sustained_headroom, 1U, 6U) == "affordable");
    CHECK(adaptive_reason_label(StateChangeReason::device_reset_refresh, 1U, 6U) == "reset");
    CHECK(adaptive_reason_label(StateChangeReason::observation_gap_reset, 1U, 6U) == "gap");
    CHECK(adaptive_reason_label(StateChangeReason::repeated_overload, 1U, 6U).empty());
    CHECK(adaptive_reason_label(StateChangeReason::sustained_headroom, 6U, 1U).empty());
}

void
test_snapshot_formatting() {
    AdaptiveSnapshot snapshot{};
    snapshot.previous_budget = 6U;
    snapshot.new_budget = 1U;
    snapshot.reason = StateChangeReason::repeated_overload;
    AdaptiveText formatted{};
    CHECK(format_adaptive_snapshot(snapshot, formatted));
    CHECK(std::string_view(formatted.line1.data()) == "Reflections: Weighted single-face (load)");
    snapshot.previous_budget = 1U;
    snapshot.new_budget = 6U;
    snapshot.reason = StateChangeReason::sustained_headroom;
    CHECK(format_adaptive_snapshot(snapshot, formatted));
    CHECK(std::string_view(formatted.line1.data()) == "Reflections: Native full-update (affordable)");
    std::array<char, 4> bounded{};
    CHECK(!checked_format(bounded, "%s", "too long"));
    CHECK(bounded.back() == '\0');
}

void
test_adaptive_state_independence() {
    AdaptiveState disabled;
    AdaptiveSnapshot snapshot{};
    snapshot.previous_budget = 2U;
    snapshot.new_budget = 1U;
    snapshot.reason = StateChangeReason::repeated_overload;
    disabled.post(snapshot);
    CHECK(!disabled.pending());

    State transient(true);
    AdaptiveState adaptive(true);
    transient.post_text("Reflection blending: on");
    adaptive.post(snapshot);
    CHECK(transient.pending() == Message::custom);
    CHECK(adaptive.pending());

    AdaptiveSnapshot taken{};
    CHECK(adaptive.take_pending(taken));
    CHECK(taken.new_budget == 1U);
    CHECK(!adaptive.pending());
    CHECK(transient.pending() == Message::custom);

    transient.shown(100U, 20U);
    adaptive.shown(100U, 30U);
    CHECK(transient.expired(120U));
    CHECK(!adaptive.expired(120U));
    CHECK(adaptive.expired(130U));

    adaptive.post(snapshot);
    adaptive.invalidate();
    CHECK(!adaptive.pending());
    CHECK(!adaptive.visible());
    CHECK(transient.visible());
}

} // namespace

int
main() {
    mwperffix::tests::disable_windows_crash_dialogs();
    test_messages();
    test_transient_pending_and_lifetime();
    test_adaptive_reason_mapping();
    test_snapshot_formatting();
    test_adaptive_state_independence();
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
