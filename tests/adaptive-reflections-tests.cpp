#include "mwperffix/adaptive_reflections.hpp"
#include "test_error_mode.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <vector>

namespace {
using namespace mwperffix::adaptive;
[[noreturn]] void
fail(const char *value, int line) {
    std::fprintf(stderr, "line %d: %s\n", line, value);
    std::exit(EXIT_FAILURE);
}
#define CHECK(x) ((x) ? static_cast<void>(0) : fail(#x, __LINE__))

struct Recorder {
    std::vector<StateChange> changes;
#ifdef MWPERFFIX_DIAGNOSTICS
    std::vector<FrameObservation> frames;
    static void
    frame(void *ctx, const FrameObservation &v) noexcept {
        static_cast<Recorder *>(ctx)->frames.push_back(v);
    }
#endif
    static void
    transition(void *ctx, const StateChange &v) noexcept {
        static_cast<Recorder *>(ctx)->changes.push_back(v);
    }
    Observer
    observer() {
#ifdef MWPERFFIX_DIAGNOSTICS
        return {&transition, &frame, this};
#else
        return {&transition, nullptr, this};
#endif
    }
};

struct Harness {
    explicit Harness(std::uint32_t fps = 60U, Observer observer = {})
        : controller(720000U, Config{fps}, observer), period(720000U / normalize_config(Config{fps}).target_fps) {
    }

    Controller controller;
    RendererClock clock{720000U};
    std::uint64_t period;
    std::uint32_t frame = 0;
    std::uint64_t time = 720000U;
    std::uint64_t last_present = 0U;
    std::uint64_t overhead = 100U;
    std::uint64_t outside_wait = 0U;
    std::array<std::uint64_t, 6> costs{400U, 500U, 600U, 700U, 800U, 900U};

    Selection
    step(
        std::uint32_t ui = 3U, bool eligible = true, bool ok = true, std::uint64_t cadence = 0U,
        bool missing_face = false, bool reversed_post = false
    ) {
        ++frame;
        if (last_present != 0U)
            time = last_present + outside_wait;
        controller.begin_renderer(time, frame, clock.observe(time));
        const Selection s = controller.select(ui, eligible, time + 1U);
        std::uint64_t cursor = time + 10U;
        for (std::size_t f = 0; f < 6U; ++f) {
            if (!s.custom || (s.face_mask & (1U << f)) == 0U || missing_face)
                continue;
            controller.begin_face(f, cursor);
            cursor += costs[f];
            controller.end_face(f, cursor);
            ++cursor;
        }
        const std::uint64_t pre = std::max(
            time + overhead +
                [&] {
                    std::uint64_t v = 0U;
                    for (std::size_t f = 0; f < 6U; ++f)
                        if ((s.face_mask & (1U << f)) != 0U && !missing_face)
                            v += costs[f];
                    return v;
                }(),
            cursor
        );
        controller.before_present(pre);
        const std::uint64_t desired = cadence == 0U ? period : cadence;
        last_present = std::max(pre + 10U, time + desired);
        controller.after_present(reversed_post ? pre - 1U : last_present, ok);
        return s;
    }
    void
    downshift() {
        overhead = period;
        for (int n = 0; n < 5 && controller.effective_budget() != 1U; ++n)
            CHECK(step().face_mask == kAllFaceMask);
        CHECK(controller.effective_budget() == 1U);
    }
};

void
test_completed_faces_with_bad_timing_preserve_policy() {
    Recorder recorder;
    Harness h(60U, recorder.observer());
    h.downshift();
    const auto transitions = recorder.changes.size();
    const auto start = h.last_present;
    h.controller.begin_renderer(start, ++h.frame, h.clock.observe(start));
    const auto selected = h.controller.select(3U, true, start + 1U);
    CHECK(selected.budget == 1U);
    const auto face = static_cast<std::size_t>(std::countr_zero(selected.face_mask));
    h.controller.begin_face(face, start + 10U);
    h.controller.end_face(face, start + 110U);
    // Complete physical callbacks but inconsistent renderer-duration evidence.
    h.controller.before_present(start + 150U);
    h.last_present = start + h.period;
    h.controller.after_present(start + 149U, true);
    CHECK(!h.controller.full_refresh_pending());
    CHECK(h.controller.effective_budget() == 1U);
    CHECK(recorder.changes.size() == transitions);
#ifdef MWPERFFIX_DIAGNOSTICS
    CHECK(!recorder.frames.back().timing_valid);
    CHECK(recorder.frames.back().first_invalid_reason == InvalidReason::post_before_pre);
    CHECK(recorder.frames.back().recovery_streak == 0U);
#endif
    CHECK(h.step().budget == 1U);
    h.controller.invalidate_contents(h.last_present, h.frame);
    CHECK(h.controller.full_refresh_pending());
    CHECK(h.step().face_mask == kAllFaceMask);
}

void
test_face_sequence_and_wrap() {
    // Costs keep predicted Native full-update above the target, preventing upshift.
    Harness h;
    h.costs.fill(3000U);
    h.downshift();
    constexpr std::array<unsigned, 15> sequence{4U, 0U, 1U, 4U, 2U, 3U, 4U, 5U, 0U, 4U, 1U, 2U, 4U, 3U, 5U};
    std::array<int, 6> counts{};
    std::array<int, 6> last{};
    last.fill(-1);
    for (int i = 0; i < 30; ++i) {
        const Selection s = h.step();
        CHECK(s.custom);
        CHECK(s.budget == 1U);
        CHECK(s.face_mask == (1U << sequence[static_cast<std::size_t>(i) % 15U]));
        CHECK(std::popcount(s.face_mask) == 1);
        const unsigned face = sequence[static_cast<std::size_t>(i) % 15U];
        if (last[face] >= 0) {
            const int delta = i - last[face];
            CHECK(delta == (face == 4U ? 3 : 7) || (face != 4U && delta == 8));
        }
        last[face] = i;
        ++counts[face];
    }
    CHECK(counts[4] == 10);
    for (unsigned i = 0; i < 6U; ++i)
        if (i != 4U)
            CHECK(counts[i] == 4);
}

void
test_ui_and_invalid() {
    for (unsigned ui = 1U; ui <= 3U; ++ui) {
        Harness h;
        CHECK(h.step(ui).face_mask == kAllFaceMask);
        CHECK(h.step(ui).face_mask == kAllFaceMask);
    }
    Harness h;
    CHECK(h.step(0U).face_mask == 0U);
    CHECK(!h.step(4U).custom);
    CHECK(h.step().face_mask == kAllFaceMask);
    CHECK(h.step().face_mask == kAllFaceMask);
    CHECK(!h.step(3U, false).custom);
    CHECK(h.step().face_mask == kAllFaceMask);
    h.downshift(); // newly seeded full after native bypass
    CHECK(h.step().budget == 1U);
    CHECK(h.step(3U, true, true, 0U, true).budget == 1U);
    CHECK(h.controller.full_refresh_pending());
    CHECK(h.step().face_mask == kAllFaceMask);
    CHECK(h.step(0U).face_mask == 0U);
    CHECK(h.step().face_mask == kAllFaceMask);
}

void
test_two_pressured_frames_and_invalid_spike() {
    Recorder r;
    Harness h(60U, r.observer());
    CHECK(h.step().budget == 6U);
    h.overhead = 8300U;
    CHECK(h.step().budget == 6U);
    CHECK(h.controller.effective_budget() == 6U);
    h.overhead = 100U;
    CHECK(h.step().budget == 6U);
    CHECK(h.controller.effective_budget() == 6U);
    h.overhead = 8300U;
    h.step();
    h.step();
    CHECK(h.controller.effective_budget() == 1U);
    CHECK(!r.changes.empty());
    CHECK(r.changes.back().previous_budget == 6U);
    CHECK(r.changes.back().new_budget == 1U);
    CHECK(r.changes.back().reason == StateChangeReason::repeated_overload);
    CHECK(h.step().face_mask == (1U << 4U));
    CHECK(h.step(3U, true, true, 0U, true).face_mask != 0U);
    CHECK(h.controller.full_refresh_pending());
}

void
test_upshift_four_valid_and_failure_cooldown() {
    Recorder r;
    Harness h(60U, r.observer());
    h.downshift();
    h.overhead = 100U;
    std::uint32_t healthy_count = 0U;
    std::uint64_t recovery = 0U;
    // A prior bad EMA may require several frames to become healthy.
    for (int i = 0; i < 100; ++i) {
        const Selection s = h.step();
        if (s.budget == 1U && h.controller.effective_budget() == 6U) {
            recovery = h.last_present;
            break;
        }
        ++healthy_count;
    }
    CHECK(recovery != 0U);
    CHECK(healthy_count < 60U); // Recovery must not require two seconds of healthy frames.
    CHECK(r.changes.back().previous_budget == 1U);
    CHECK(r.changes.back().new_budget == 6U);

    // Immediate two-frame failure sets a 500ms retry deadline.
    h.overhead = 15000U;
    CHECK(h.step().budget == 6U);
    CHECK(h.step().budget == 6U);
    CHECK(h.controller.effective_budget() == 1U);
    h.overhead = 100U;
    bool early_upgrade = false;
    for (int i = 0; i < 20; ++i) {
        h.step();
        early_upgrade |= h.controller.effective_budget() == 6U;
    }
    CHECK(!early_upgrade);
    bool late_upgrade = false;
    for (int i = 0; i < 60; ++i) {
        h.step();
        late_upgrade |= h.controller.effective_budget() == 6U;
        if (late_upgrade)
            break;
    }
    CHECK(late_upgrade);
}

void
test_recovery_95_percent_margin() {
    // A full cost near 92.5% of the frame budget leaves the required 5% headroom.
    Recorder recovered;
    Harness affordable(60U, recovered.observer());
    affordable.downshift();
    affordable.overhead = affordable.period * 60U / 100U;
    for (int i = 0; i < 150 && affordable.controller.effective_budget() == 1U; ++i)
        affordable.step();
    CHECK(affordable.controller.effective_budget() == 6U);
    const auto &transition = recovered.changes.back();
    CHECK(transition.reason == StateChangeReason::sustained_headroom);
    CHECK(transition.predicted_full_qpc > (affordable.period / 10U) * 9U);
    CHECK(transition.predicted_full_qpc <= (affordable.period / 20U) * 19U);

    // A full update at 97.5% of the budget still fails the 95% recovery threshold,
    // even though weighted frames are cheap and cadence is healthy.
    Harness too_expensive;
    too_expensive.downshift();
    too_expensive.overhead = too_expensive.period * 65U / 100U;
    for (int i = 0; i < 150; ++i)
        CHECK(too_expensive.step().budget == 1U);
    CHECK(too_expensive.controller.effective_budget() == 1U);
}

void
test_multiple_targets_and_caps() {
    for (const unsigned fps : {30U, 40U, 60U, 100U, 144U}) {
        Harness healthy(fps);
        healthy.costs.fill(100U);
        for (int i = 0; i < 20; ++i)
            CHECK(healthy.step().budget == 6U);
        Harness capped(fps);
        capped.costs.fill(3000U);
        capped.downshift();
        for (int i = 0; i < 12; ++i) {
            capped.outside_wait = capped.period / 2U; // external pacing, not CPU work
            CHECK(capped.step().budget == 1U);
        }
    }
    // 58fps cadence is within the 5% tolerance for a 60fps target.
    Harness slight;
    slight.costs.fill(200U);
    for (int i = 0; i < 20; ++i)
        CHECK(slight.step(3U, true, true, 720000U / 58U).budget == 6U);

    Harness mismatched;
    mismatched.costs.fill(400U);
    for (int i = 0; i < 30; ++i)
        mismatched.step(3U, true, true, 720000U / 30U);
    CHECK(mismatched.controller.effective_budget() == 1U);
}

void
test_renderer_timing_and_lifecycle() {
    RendererClock clock(1000U);
    const auto first = clock.observe(100U);
    CHECK(first.interval_qpc == 0U && !first.gap && !first.repeated && !first.reversed);
    CHECK(clock.observe(350U).interval_qpc == 250U);
    CHECK(!clock.observe(600U).gap);
    CHECK(clock.observe(851U).gap);
    CHECK(clock.observe(851U).repeated);
    const auto reversed = clock.observe(850U);
    CHECK(reversed.reversed && !reversed.gap && reversed.interval_qpc == 0U);
    CHECK(clock.observe(851U).interval_qpc == 1U);

    // Test policy consequences with learned contents, not an already-pending refresh.
    enum Scenario {
        equal_time,
        reversed_time,
        exact_gap,
        long_gap,
        repeated_frame,
        reentered,
        ordinary_frame
    };
    for (const auto scenario :
        {equal_time, reversed_time, exact_gap, long_gap, repeated_frame, reentered, ordinary_frame}) {
        Recorder recorder;
        Harness h(60U, recorder.observer());
        h.downshift();
        const std::uint64_t next = scenario == equal_time      ? h.time
                                   : scenario == reversed_time ? h.time - 1U
                                   : scenario == exact_gap     ? h.time + 180000U
                                   : scenario == long_gap      ? h.time + 180001U
                                                               : h.time + h.period;
        const auto frame = scenario == repeated_frame ? h.frame : ++h.frame;
        h.controller.begin_renderer(next, frame, h.clock.observe(next));
        if (scenario == reentered)
            h.controller.begin_renderer(next + 1U, ++h.frame, h.clock.observe(next + 1U));
        const bool refresh =
            scenario == reversed_time || scenario == long_gap || scenario == repeated_frame || scenario == reentered;
        CHECK(h.controller.full_refresh_pending() == refresh);
        CHECK(h.controller.select(3U, true, next + 2U).budget == (refresh ? 6U : 1U));
        if (refresh)
            CHECK(
                recorder.changes.back().reason == (scenario == long_gap ? StateChangeReason::observation_gap_reset
                                                                        : StateChangeReason::invalid_sample_safety)
            );
    }
}

void
test_reset_and_callback_fallback() {
    Harness h;
    h.downshift();
    CHECK(h.step().budget == 1U);
    CHECK(!h.step(0U).custom);
    CHECK(h.step().face_mask == kAllFaceMask);
    h.downshift();
    CHECK(h.step().budget == 1U);
    h.controller.invalidate_contents(h.last_present, h.frame);
    CHECK(h.step().face_mask == kAllFaceMask);
    h.downshift();
    CHECK(h.step().budget == 1U);
    CHECK(h.step(3U, true, false).face_mask != 0U);
    CHECK(h.controller.full_refresh_pending());
    CHECK(h.step().face_mask == kAllFaceMask);

    h.downshift();
    CHECK(h.step().budget == 1U);
    h.outside_wait = 720000U; // observation gap
    CHECK(h.step().face_mask == kAllFaceMask);
}

#ifdef MWPERFFIX_DIAGNOSTICS
void
test_diagnostics_report_real_masks_and_invalid_samples() {
    Recorder recorder;
    Harness h(60U, recorder.observer());
    h.downshift();
    h.step();
    CHECK(recorder.frames.back().timing_valid);
    CHECK(recorder.frames.back().selected_budget == 1U);
    CHECK(recorder.frames.back().requested_face_mask == (1U << 4U));
    CHECK(recorder.frames.back().face_begin_mask == (1U << 4U));
    CHECK(recorder.frames.back().completed_face_mask == (1U << 4U));
    h.step(3U, true, true, 0U, true);
    CHECK(!recorder.frames.back().timing_valid);
    CHECK(recorder.frames.back().first_invalid_reason == InvalidReason::face_mask_mismatch);
}
#endif

#ifdef MWPERFFIX_DIAGNOSTICS
void
test_diagnostic_cost_snapshots() {
    Recorder recorder;
    Harness h(60U, recorder.observer());
    h.step();
    CHECK(recorder.frames.back().face_qpc == h.costs);
    CHECK(recorder.frames.back().face_ema_qpc == h.costs);
    h.downshift();
    for (unsigned n = 0; n < 15U; ++n) {
        const auto selected = h.step();
        const auto &sample = recorder.frames.back();
        std::uint64_t raw_sum = 0U;
        std::uint64_t estimate = sample.residual_ema_qpc;
        for (std::size_t face = 0; face < kFaceCount; ++face) {
            CHECK(sample.face_qpc[face] == ((selected.face_mask & (1U << face)) != 0U ? h.costs[face] : 0U));
            raw_sum += sample.face_qpc[face];
            estimate += sample.face_ema_qpc[face];
        }
        CHECK(sample.costs_valid);
        CHECK(raw_sum == sample.observed_face_qpc);
        CHECK(estimate == sample.predicted_full_qpc);
        CHECK(sample.frame_interval_ema_qpc != 0U);
    }
    const auto check_zero = [&] {
        const auto &sample = recorder.frames.back();
        CHECK(!sample.timing_valid);
        CHECK(!sample.costs_valid);
        CHECK(sample.residual_ema_qpc == 0U);
        CHECK(sample.frame_interval_ema_qpc == 0U);
        for (std::size_t face = 0; face < kFaceCount; ++face) {
            CHECK(sample.face_qpc[face] == 0U);
            CHECK(sample.face_ema_qpc[face] == 0U);
        }
    };
    h.step(3U, true, true, 0U, true);
    check_zero();
    h.step(3U, false);
    check_zero();
    h.step(3U, true, false);
    check_zero();

    // Observation must not change decisions, including resets and recovery.
    for (const unsigned fps : {30U, 60U, 144U}) {
        Harness observed(fps, recorder.observer());
        Harness plain(fps);
        for (unsigned frame = 0U; frame < 300U; ++frame) {
            observed.overhead = plain.overhead = frame % 80U < 20U ? plain.period : 100U;
            const bool eligible = frame % 71U != 0U;
            const auto a = observed.step(3U, eligible);
            const auto b = plain.step(3U, eligible);
            CHECK(a.face_mask == b.face_mask);
            CHECK(observed.controller.effective_budget() == plain.controller.effective_budget());
        }
    }
}
#endif

void
test_live_target_and_native_bypass() {
    Recorder recorder;
    Harness h(60U, recorder.observer());
    h.downshift();
    CHECK(!h.controller.full_refresh_pending());
    CHECK(h.step().face_mask == (1U << 4U));
    CHECK(h.step().face_mask == (1U << 0U));
    const auto transitions = recorder.changes.size();
    CHECK(h.controller.set_target_fps(75U));
    CHECK(h.controller.target_fps() == 75U);
    CHECK(!h.controller.full_refresh_pending());
    CHECK(h.controller.effective_budget() == 1U);
    CHECK(recorder.changes.size() == transitions);
    CHECK(h.step().face_mask == (1U << 1U)); // retained sequence position
#ifdef MWPERFFIX_DIAGNOSTICS
    CHECK(recorder.frames.back().cadence_budget_qpc == 720000U / 75U);
    CHECK(recorder.frames.back().frame_interval_ema_qpc != 0U);
    CHECK(recorder.frames.back().residual_ema_qpc != 0U);
#endif
    CHECK(!h.controller.set_target_fps(75U));
    CHECK(h.controller.set_target_fps(30U));
    CHECK(h.controller.target_fps() == 30U);
    CHECK(h.controller.set_target_fps(1000U));
    CHECK(h.controller.target_fps() == 1000U);
    CHECK(!h.controller.set_target_fps(1000U));

    h.controller.suspend();
    CHECK(h.controller.full_refresh_pending());
    CHECK(h.controller.effective_budget() == 6U);
    CHECK(recorder.changes.size() == transitions);
    CHECK(h.step().face_mask == kAllFaceMask);
    CHECK(!h.controller.full_refresh_pending());
}

} // namespace

int
main() {
    test_completed_faces_with_bad_timing_preserve_policy();
    test_face_sequence_and_wrap();
    test_ui_and_invalid();
    test_two_pressured_frames_and_invalid_spike();
    test_upshift_four_valid_and_failure_cooldown();
    test_recovery_95_percent_margin();
    test_multiple_targets_and_caps();
    test_live_target_and_native_bypass();
    test_renderer_timing_and_lifecycle();
    test_reset_and_callback_fallback();
#ifdef MWPERFFIX_DIAGNOSTICS
    test_diagnostics_report_real_masks_and_invalid_samples();
    test_diagnostic_cost_snapshots();
#endif
    return EXIT_SUCCESS;
}
