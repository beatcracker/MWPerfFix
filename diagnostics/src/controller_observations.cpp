#include "mwperffix/adaptive_reflections.hpp"

#include <cstddef>
#include <cstdint>

namespace mwperffix::adaptive {

void
Controller::note_invalid(
    const InvalidReason reason, const InvalidSite site, const std::uint64_t ticks, const std::size_t face
) noexcept {
    if (first_invalid_reason_ != InvalidReason::none)
        return;
    first_invalid_reason_ = reason;
    first_invalid_site_ = site;
    first_invalid_qpc_ = ticks;
    first_invalid_face_ = face < kFaceCount ? static_cast<std::uint32_t>(face) : static_cast<std::uint32_t>(kFaceCount);
}

void
Controller::set_dispatch_rejections(const std::uint32_t foreign, const std::uint32_t unknown) noexcept {
    ignored_foreign_face_callbacks_ = foreign;
    unknown_face_indices_ = unknown;
}

void
Controller::emit_frame_observation(
    const std::uint64_t completion_qpc, const bool present_succeeded, const bool timing_valid,
    const std::uint64_t renderer_work_qpc, const std::uint64_t observed_face_qpc, const std::uint64_t residual_work_qpc,
    const std::uint64_t predicted_full_qpc, const std::uint32_t face_mask, const PolicyEvidence &evidence
) noexcept {
    if (observer_.frame_complete == nullptr)
        return;
    FrameObservation observation{};
    if (timing_valid) {
        observation.face_qpc = sample_faces_;
        for (std::size_t face = 0; face < kFaceCount; ++face)
            observation.face_ema_qpc[face] = faces_[face].ema;
        observation.residual_ema_qpc = active_non_reflection_ema_;
        observation.frame_interval_ema_qpc = frame_interval_ema_;
        observation.costs_valid = costs_valid();
    }
    observation.recovery_streak = good_streak_;
    observation.retry_after_qpc = retry_after_qpc_;
    observation.completion_qpc = completion_qpc;
    observation.renderer_work_qpc = timing_valid ? renderer_work_qpc : 0U;
    observation.observed_face_qpc = timing_valid ? observed_face_qpc : 0U;
    observation.residual_work_qpc = timing_valid ? residual_work_qpc : 0U;
    observation.active_frame_work_qpc = timing_valid ? active_frame_work_qpc_ : 0U;
    observation.active_non_reflection_qpc =
        timing_valid && active_frame_work_qpc_ >= observed_face_qpc ? active_frame_work_qpc_ - observed_face_qpc : 0U;
    observation.predicted_full_qpc = timing_valid ? predicted_full_qpc : 0U;
    observation.frame_interval_qpc = timing_valid ? renderer_interval_qpc_ : 0U;
    observation.cadence_budget_qpc = timing_valid ? evidence.cadence_budget_qpc : 0U;
    observation.decision_sequence = decision_sequence_;
    observation.game_frame_counter = active_frame_;
    // Zero on a bypassed frame, otherwise the actual selected count.
    observation.policy_budget = sample_eligible_ ? selected_budget() : 0U;
    observation.face_mask = timing_valid ? (face_mask & kAllFaceMask) : 0U;
    observation.first_invalid_reason = sample_eligible_ ? first_invalid_reason_ : InvalidReason::none;
    observation.first_invalid_site = sample_eligible_ ? first_invalid_site_ : InvalidSite::none;
    observation.first_invalid_face = sample_eligible_ ? first_invalid_face_ : static_cast<std::uint32_t>(kFaceCount);
    observation.first_invalid_qpc = sample_eligible_ ? first_invalid_qpc_ : 0U;
    observation.renderer_begin_qpc = renderer_begin_;
    observation.pre_present_qpc = pre_present_;
    observation.previous_post_present_qpc = previous_post_present_at_begin_;
    observation.requested_face_mask = sample_eligible_ ? selected_mask_ : 0U;
    observation.face_begin_mask = sample_eligible_ ? face_begin_mask_ : 0U;
    observation.completed_face_mask = sample_eligible_ ? sample_face_mask_ : 0U;
    observation.face_begin_count = sample_eligible_ ? face_begin_count_ : 0U;
    observation.face_end_count = sample_eligible_ ? face_end_count_ : 0U;
    observation.ignored_foreign_face_callbacks = ignored_foreign_face_callbacks_;
    observation.unknown_face_indices = unknown_face_indices_;
    observation.interrupted_renderer_count = interrupted_renderer_count_;
    interrupted_renderer_count_ = 0U;
    observation.selection_valid = selection_made_ && sample_eligible_;
    observation.eligible = observation.selection_valid;
    observation.timing_valid = timing_valid;
    observation.present_succeeded = present_succeeded;
    observation.cadence_healthy = timing_valid && evidence.cadence_healthy;
    observation.full_refresh_pending = full_refresh_pending_;
    if (observation.selection_valid) {
        observation.configured_mode = configured_mode_;
        observation.selected_budget = selected_budget();
    }
    observer_.frame_complete(observer_.context, observation);
}

} // namespace mwperffix::adaptive
