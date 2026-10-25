#include "mwperffix/diagnostics/capture.hpp"

#include <cstdint>
#include <cstdio>

int
main() {
    using namespace mwperffix;
    using namespace mwperffix::diagnostics;
    static_assert(sizeof(void *) == 4, "Diagnostics target must match the x86 game");
    Capture capture;
    if (!capture.initialize(true)) {
        std::fputs("TraceLogging registration failed\n", stderr);
        return 1;
    }
    if (!capture.enabled()) {
        std::fputs("No ETW listener. Run mise run smoke-etw from an elevated terminal.\n", stderr);
        return 2;
    }
    Metadata metadata{};
    metadata.build_version = "synthetic";
    metadata.build_variant = "diagnostic-test";
    metadata.executable_base = 0x400000U;
    metadata.executable_timestamp = 0x438E4C8CU;
    metadata.executable_size = 0x678E4EU;
    metadata.target_fps = 60U;
    metadata.fixes = {0x1FU, 0x1FU, 0U};
    capture.emit_metadata(metadata);

    // One adaptive frame observation and one transition.
    // The frame carries a valid six-face mode-3 sample and a work-admission transition.
    adaptive::FrameObservation frame{};
    frame.completion_qpc = 0x200000002ULL;
    frame.renderer_work_qpc = 0x400000004ULL;
    frame.observed_face_qpc = 0x100000001ULL;
    frame.residual_work_qpc = 0x300000003ULL;
    frame.active_frame_work_qpc = 0x600000006ULL;
    frame.active_non_reflection_qpc = 0x500000005ULL;
    frame.predicted_full_qpc = 0x500000005ULL;
    frame.frame_interval_qpc = 12000000000ULL;
    frame.cadence_healthy = true;
    frame.cadence_budget_qpc = 14000000000ULL;
    frame.decision_sequence = 7U;
    frame.game_frame_counter = 9002U;
    frame.configured_mode = 3U;
    frame.selected_budget = 6U;
    frame.policy_budget = 6U;
    frame.face_mask = 0x3FU;
    frame.requested_face_mask = 0x3FU;
    frame.face_begin_mask = 0x3FU;
    frame.completed_face_mask = 0x3FU;
    frame.face_begin_count = 6U;
    frame.face_end_count = 6U;
    frame.renderer_begin_qpc = 0x100000000ULL;
    frame.pre_present_qpc = 0x200000000ULL;
    frame.selection_valid = true;
    frame.eligible = true;
    frame.timing_valid = true;
    frame.present_succeeded = true;
    frame.full_refresh_pending = false;
    frame.face_qpc = {0x100000001ULL, 0U, 0U, 0U, 0U, 0U};
    frame.face_ema_qpc = frame.face_qpc;
    frame.residual_ema_qpc = 0x400000004ULL;
    frame.frame_interval_ema_qpc = frame.frame_interval_qpc;
    frame.costs_valid = true;
    frame.blend_qpc = {0x100000002ULL, 2U, 3U, 4U};
    frame.blend_mode = 1U;
    frame.blend_steps = 6U;
    frame.blend_timing_valid = true;
    capture.emit_frame(frame);

    adaptive::StateChange change{};
    change.sequence = 7U;
    change.decision_qpc = 0x300000003ULL;
    change.renderer_work_qpc = 0x400000004ULL;
    change.predicted_full_qpc = 0x500000005ULL;
    change.cadence_budget_qpc = 14000000000ULL;
    change.game_frame_counter = 9002U;
    change.previous_budget = 6U;
    change.new_budget = 1U;
    change.reason = adaptive::StateChangeReason::full_refresh_complete;
    change.eligible_frames = 12U;
    change.cheap_streak = 0U;
    change.expensive_streak = 0U;
    change.face_mask = 0x3FU;
    change.full_refresh_pending = false;
    capture.emit_state_change(change);

    std::puts("Synthetic scheduler metadata and events emitted.");
    return 0;
}
