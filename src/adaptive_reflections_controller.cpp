#include "mwperffix/adaptive_reflections.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <limits>

namespace mwperffix::adaptive {
namespace {

constexpr std::uint32_t kMaximumPacingNumerator = 105U;
constexpr std::uint32_t kMaximumPacingDenominator = 100U;
[[nodiscard]] std::uint64_t
saturating_add(const std::uint64_t left, const std::uint64_t right) noexcept {
    return right > std::numeric_limits<std::uint64_t>::max() - left ? std::numeric_limits<std::uint64_t>::max()
                                                                    : left + right;
}

[[nodiscard]] std::uint64_t
saturating_multiply(const std::uint64_t left, const std::uint64_t right) noexcept {
    if (left != 0U && right > std::numeric_limits<std::uint64_t>::max() / left)
        return std::numeric_limits<std::uint64_t>::max();
    return left * right;
}

[[nodiscard]] std::uint64_t
scale(const std::uint64_t frequency, const std::uint64_t numerator, const std::uint64_t denominator) noexcept {
    if (denominator == 0U)
        return 0U;
    if (numerator != 0U && frequency > std::numeric_limits<std::uint64_t>::max() / numerator)
        return std::numeric_limits<std::uint64_t>::max();
    return (frequency * numerator) / denominator;
}

} // namespace

Controller::Controller(const std::uint64_t qpc_frequency, const Config config, const Observer observer) noexcept
    : frequency_(qpc_frequency == 0U ? 1U : qpc_frequency), observer_(observer) {
    static_cast<void>(set_target_fps(config.target_fps));
}

bool
Controller::set_target_fps(const std::uint32_t fps) noexcept {
    const std::uint32_t normalized = normalize_config({fps}).target_fps;
    if (normalized == target_fps_ && target_period_qpc_ != 0U)
        return false;
    target_fps_ = normalized;
    target_period_qpc_ = frequency_ / normalized;
    max_pacing_period_qpc_ =
        scale(frequency_, kMaximumPacingNumerator, kMaximumPacingDenominator * static_cast<std::uint64_t>(normalized));
    good_streak_ = 0U;
    bad_streak_ = 0U;
    recent_upshift_qpc_ = 0U;
    retry_after_qpc_ = 0U;
    return true;
}

void
Controller::suspend() noexcept {
    // Native gameplay may modify any face.
    // On re-entry, seed all six again.
    // No automatic state-change event is caused by this manual bypass.
    clear_learning();
    sample_faces_ = {};
    renderer_begin_ = 0U;
    pre_present_ = 0U;
    renderer_interval_qpc_ = 0U;
    face_begin_ = 0U;
    active_face_ = kFaceCount;
    selected_mask_ = 0U;
    sample_face_mask_ = 0U;
    last_observation_ = 0U;
    renderer_active_ = false;
    selection_made_ = false;
    sample_valid_ = false;
    sample_eligible_ = false;
    present_started_ = false;
    have_frame_ = false;
    configured_enabled_ = false;
    weighted_single_face_ = false;
}

void
Controller::change_state(
    const bool weighted_single_face, const StateChangeReason reason, const std::uint64_t ticks,
    const std::uint32_t frame, const std::uint64_t work, const std::uint64_t predicted_full,
    const std::uint32_t face_mask
) noexcept {
    if (weighted_single_face_ == weighted_single_face)
        return;
    const std::uint32_t previous = weighted_single_face_ ? 1U : 6U;
    const auto recovery_streak = good_streak_;
    const auto pressure_streak = bad_streak_;
    weighted_single_face_ = weighted_single_face;
    good_streak_ = 0U;
    bad_streak_ = 0U;
    phase_ = 0U;
#ifdef MWPERFFIX_DIAGNOSTICS
    ++decision_sequence_;
#endif
    if (observer_.state_change == nullptr)
        return;
    StateChange change{};
    change.cheap_streak = recovery_streak;
    change.expensive_streak = pressure_streak;
#ifdef MWPERFFIX_DIAGNOSTICS
    change.sequence = decision_sequence_;
    change.eligible_frames = eligible_frames_;
#endif
    change.decision_qpc = ticks;
    change.renderer_work_qpc = work;
    change.predicted_full_qpc = predicted_full;
    change.cadence_budget_qpc = target_period_qpc_;
    change.game_frame_counter = frame;
    change.previous_budget = previous;
    change.new_budget = weighted_single_face ? 1U : 6U;
    change.reason = reason;
    change.face_mask = face_mask & kAllFaceMask;
    change.full_refresh_pending = full_refresh_pending_;
    observer_.state_change(observer_.context, change);
}

void
Controller::reset_learning(
    const StateChangeReason reason, const std::uint64_t ticks, const std::uint32_t frame
) noexcept {
    // Unlike manual bypass, a safety reset can occur during an observation and may report a transition.
    // Preserve its active frame/sample evidence.
    change_state(false, reason, ticks, frame, 0U, 0U, 0U);
    clear_learning();
}

void
Controller::clear_learning() noexcept {
    for (Face &face : faces_)
        face = {};
    active_non_reflection_ema_ = 0U;
    previous_post_present_ = 0U;
    active_frame_work_qpc_ = 0U;
    frame_interval_ema_ = 0U;
    frame_interval_known_ = false;
    full_refresh_pending_ = true;
    eligible_updates_ = 0U;
    phase_ = 0U;
    recent_upshift_qpc_ = 0U;
    retry_after_qpc_ = 0U;
    good_streak_ = 0U;
    bad_streak_ = 0U;
#ifdef MWPERFFIX_DIAGNOSTICS
    eligible_frames_ = 0U;
#endif
}

void
Controller::invalidate_sample(const std::uint64_t ticks, const std::uint32_t frame) noexcept {
    sample_valid_ = false;
    // Lost/duplicated callbacks cannot be mistaken for cheap samples.
    // The next eligible selection seeds all six physical faces again.
    reset_learning(StateChangeReason::invalid_sample_safety, ticks, frame);
}

void
Controller::discard_timing() noexcept {
    // All selected faces completed, but their durations are inconsistent.
    // Preserve costs, policy and retry deadline.
    // A rejected observation breaks consecutive pressure/recovery evidence without invalidating cube contents.
    sample_valid_ = false;
    good_streak_ = 0U;
    bad_streak_ = 0U;
    previous_post_present_ = 0U;
    active_frame_work_qpc_ = 0U;
}

void
Controller::invalidate_contents(const std::uint64_t ticks, const std::uint32_t frame) noexcept {
    reset_learning(StateChangeReason::device_reset_refresh, ticks, frame);
    sample_valid_ = false;
}

std::uint64_t
Controller::ticks_for_ms(const std::uint32_t milliseconds) const noexcept {
    const std::uint64_t whole = frequency_ / 1000U;
    const std::uint64_t remainder = frequency_ % 1000U;
    return saturating_add(
        saturating_multiply(whole, milliseconds), saturating_multiply(remainder, milliseconds) / 1000U
    );
}

void
Controller::begin_renderer(const std::uint64_t ticks, const std::uint32_t frame, const RendererTiming timing) noexcept {
    const bool bad_order =
        renderer_active_ || (have_frame_ && active_frame_ == frame) || (have_frame_ && timing.reversed);
#ifdef MWPERFFIX_DIAGNOSTICS
    const InvalidReason begin_failure = renderer_active_ ? InvalidReason::renderer_reentered
                                        : (have_frame_ && active_frame_ == frame)
                                            ? InvalidReason::frame_counter_repeated
                                        : (have_frame_ && timing.reversed) ? InvalidReason::renderer_time_reversed
                                                                           : InvalidReason::none;
    if (renderer_active_ && interrupted_renderer_count_ != UINT32_MAX)
        ++interrupted_renderer_count_;
    const std::uint64_t prior_post = previous_post_present_;
#endif
    renderer_interval_qpc_ = timing.interval_qpc;
    if (timing.gap) {
        reset_learning(StateChangeReason::observation_gap_reset, ticks, frame);
        renderer_interval_qpc_ = 0U;
    }

    renderer_begin_ = ticks;
#ifdef MWPERFFIX_DIAGNOSTICS
    previous_post_present_at_begin_ = prior_post;
    first_invalid_reason_ = InvalidReason::none;
    first_invalid_site_ = InvalidSite::none;
    first_invalid_qpc_ = 0U;
    first_invalid_face_ = kFaceCount;
    face_begin_mask_ = 0U;
    face_begin_count_ = 0U;
    face_end_count_ = 0U;
    if (begin_failure != InvalidReason::none)
        note_invalid(begin_failure, InvalidSite::renderer_begin, ticks);
#endif
    last_observation_ = ticks;
    pre_present_ = 0U;
    active_frame_work_qpc_ = 0U;
    face_begin_ = 0U;
    active_face_ = kFaceCount;
    sample_faces_ = {};
    sample_face_mask_ = 0U;
    selected_mask_ = 0U;
    active_frame_ = frame;
    renderer_active_ = true;
    have_frame_ = true;
    selection_made_ = false;
    present_started_ = false;
    sample_eligible_ = false;
    sample_valid_ = true;
    if (bad_order)
        invalidate_sample(ticks, frame);
}

Selection
Controller::select(const std::uint32_t configured_mode, const bool eligible, const std::uint64_t ticks) noexcept {
    Selection result{};
    if (!renderer_active_)
        return result;
    if (selection_made_) {
#ifdef MWPERFFIX_DIAGNOSTICS
        note_invalid(InvalidReason::selection_repeated, InvalidSite::selection, ticks);
#endif
        invalidate_sample(ticks, active_frame_);
        return result;
    }
    selection_made_ = true;
    if (ticks < last_observation_) {
#ifdef MWPERFFIX_DIAGNOSTICS
        note_invalid(InvalidReason::selection_time_reversed, InvalidSite::selection, ticks);
#endif
        invalidate_sample(ticks, active_frame_);
    }
    last_observation_ = ticks;
#ifdef MWPERFFIX_DIAGNOSTICS
    configured_mode_ = configured_mode;
#endif

    if (configured_mode < 1U || configured_mode > 3U) {
        if (configured_enabled_)
            reset_learning(StateChangeReason::configured_mode_reset, ticks, active_frame_);
        configured_enabled_ = false;
        return result;
    }
    configured_enabled_ = true;
    if (!eligible) {
        // All bypassed frames use the exact native mask.
        // Restore full content before resuming custom masks, without charging a paused frame.
        reset_learning(StateChangeReason::configured_mode_reset, ticks, active_frame_);
        return result;
    }

    sample_eligible_ = true;
    const std::uint8_t mask = full_refresh_pending_ || !weighted_single_face_
                                  ? kAllFaceMask
                                  : static_cast<std::uint8_t>(1U << kFaceUpdateSequence[phase_]);
    if (weighted_single_face_ && !full_refresh_pending_)
        phase_ = (phase_ + 1U) % kFaceUpdateSequence.size();
    selected_mask_ = mask;
    ++eligible_updates_;
    result.face_mask = mask;
    result.budget = static_cast<std::uint8_t>(selected_budget());
    result.custom = true;
    return result;
}

void
Controller::begin_face(const std::size_t face, const std::uint64_t ticks) noexcept {
    if (!renderer_active_ || !sample_eligible_)
        return;
#ifdef MWPERFFIX_DIAGNOSTICS
    ++face_begin_count_;
    if (face < kFaceCount)
        face_begin_mask_ |= static_cast<std::uint8_t>(1U << face);
    if (sample_valid_) {
        const InvalidReason reason = active_face_ != kFaceCount  ? InvalidReason::face_begin_overlap
                                     : face >= kFaceCount        ? InvalidReason::face_begin_unknown
                                     : present_started_          ? InvalidReason::face_begin_after_present
                                     : ticks < last_observation_ ? InvalidReason::face_begin_time_reversed
                                     : (sample_face_mask_ & static_cast<std::uint8_t>(1U << face)) != 0U
                                         ? InvalidReason::face_begin_duplicate
                                         : InvalidReason::none;
        if (reason != InvalidReason::none)
            note_invalid(reason, InvalidSite::face_begin, ticks, face);
    }
#endif
    if (!sample_valid_ || active_face_ != kFaceCount || face >= kFaceCount || present_started_ ||
        ticks < last_observation_ || (sample_face_mask_ & static_cast<std::uint8_t>(1U << face)) != 0U) {
        invalidate_sample(ticks, active_frame_);
        return;
    }
    active_face_ = face;
    face_begin_ = ticks;
    last_observation_ = ticks;
}

void
Controller::end_face(const std::size_t face, const std::uint64_t ticks) noexcept {
    if (!renderer_active_ || !sample_eligible_)
        return;
#ifdef MWPERFFIX_DIAGNOSTICS
    ++face_end_count_;
    if (sample_valid_) {
        const InvalidReason reason = face >= kFaceCount           ? InvalidReason::face_end_unknown
                                     : active_face_ == kFaceCount ? InvalidReason::face_end_without_begin
                                     : active_face_ != face       ? InvalidReason::face_end_mismatch
                                     : ticks < face_begin_        ? InvalidReason::face_end_time_reversed
                                                                  : InvalidReason::none;
        if (reason != InvalidReason::none)
            note_invalid(reason, InvalidSite::face_end, ticks, face);
    }
#endif
    if (!sample_valid_ || face >= kFaceCount || active_face_ != face || ticks < face_begin_) {
        invalidate_sample(ticks, active_frame_);
        active_face_ = kFaceCount;
        return;
    }
    sample_faces_[face] = ticks - face_begin_;
    sample_face_mask_ |= static_cast<std::uint8_t>(1U << face);
    active_face_ = kFaceCount;
    last_observation_ = ticks;
}

void
Controller::before_present(const std::uint64_t ticks) noexcept {
    if (!renderer_active_)
        return;
    if (present_started_ || ticks < last_observation_ || active_face_ != kFaceCount) {
#ifdef MWPERFFIX_DIAGNOSTICS
        if (sample_eligible_)
            note_invalid(
                present_started_            ? InvalidReason::pre_present_repeated
                : ticks < last_observation_ ? InvalidReason::pre_present_time_reversed
                                            : InvalidReason::face_still_open_at_present,
                InvalidSite::pre_present, ticks, active_face_
            );
#endif
        if (sample_eligible_)
            invalidate_sample(ticks, active_frame_);
        return;
    }
    if (previous_post_present_ != 0U && ticks <= previous_post_present_) {
#ifdef MWPERFFIX_DIAGNOSTICS
        note_invalid(InvalidReason::pre_present_before_previous_post, InvalidSite::pre_present, ticks);
#endif
        invalidate_sample(ticks, active_frame_);
        return;
    }
    pre_present_ = ticks;
    active_frame_work_qpc_ = previous_post_present_ != 0U ? ticks - previous_post_present_ : 0U;
    present_started_ = true;
}

void
Controller::after_present(const std::uint64_t ticks, const bool succeeded) noexcept {
    if (!renderer_active_)
        return;

#ifdef MWPERFFIX_DIAGNOSTICS
    if (sample_eligible_ && first_invalid_reason_ == InvalidReason::none) {
        const InvalidReason reason = !succeeded                            ? InvalidReason::present_failed
                                     : !present_started_                   ? InvalidReason::pre_present_missing
                                     : pre_present_ < renderer_begin_      ? InvalidReason::pre_present_time_reversed
                                     : ticks < pre_present_                ? InvalidReason::post_before_pre
                                     : active_face_ != kFaceCount          ? InvalidReason::face_still_open_at_present
                                     : sample_face_mask_ != selected_mask_ ? InvalidReason::face_mask_mismatch
                                                                           : InvalidReason::none;
        if (reason != InvalidReason::none)
            note_invalid(reason, InvalidSite::post_present, ticks, active_face_);
    }
#endif
    const bool contents_complete = sample_valid_ && sample_eligible_ && selection_made_ && succeeded &&
                                   present_started_ && active_face_ == kFaceCount &&
                                   sample_face_mask_ == selected_mask_;
    const bool valid = contents_complete && pre_present_ >= renderer_begin_ && ticks >= pre_present_;
    if (!valid) {
        if (!succeeded) {
            invalidate_contents(ticks, active_frame_);
        } else if (contents_complete) {
            discard_timing();
        } else if (sample_eligible_) {
            invalidate_sample(ticks, active_frame_);
        } else {
            previous_post_present_ = 0U;
            active_frame_work_qpc_ = 0U;
            active_non_reflection_ema_ = 0U;
            good_streak_ = 0U;
            bad_streak_ = 0U;
        }
#ifdef MWPERFFIX_DIAGNOSTICS
        emit_frame_observation(ticks, succeeded, false, 0U, 0U, 0U, 0U, 0U, PolicyEvidence{});
#endif
        renderer_active_ = false;
        return;
    }

    const std::uint64_t work = pre_present_ - renderer_begin_;
    std::uint64_t face_sum = 0U;
    for (std::size_t face = 0; face < kFaceCount; ++face) {
        if ((sample_face_mask_ & static_cast<std::uint8_t>(1U << face)) != 0U)
            face_sum = saturating_add(face_sum, sample_faces_[face]);
    }
    if (work < face_sum) {
#ifdef MWPERFFIX_DIAGNOSTICS
        note_invalid(InvalidReason::face_time_exceeds_renderer_work, InvalidSite::post_present, ticks);
#endif
        discard_timing();
#ifdef MWPERFFIX_DIAGNOSTICS
        emit_frame_observation(ticks, true, false, 0U, 0U, 0U, 0U, 0U, PolicyEvidence{});
#endif
        renderer_active_ = false;
        return;
    }

    const std::uint64_t residual_work = work - face_sum;
    // Renderer work cannot contain Present blocking or an external limiter's sleep between frames.
    // The old post-Present -> pre-Present window can.
    // Exclude its unidentifiable outside-renderer interval from CPU pricing.
    if (active_frame_work_qpc_ != 0U && active_frame_work_qpc_ < face_sum) {
#ifdef MWPERFFIX_DIAGNOSTICS
        note_invalid(InvalidReason::face_time_exceeds_active_work, InvalidSite::post_present, ticks);
#endif
        discard_timing();
#ifdef MWPERFFIX_DIAGNOSTICS
        emit_frame_observation(ticks, true, false, 0U, 0U, 0U, 0U, 0U, PolicyEvidence{});
#endif
        renderer_active_ = false;
        return;
    }

    for (std::size_t face = 0; face < kFaceCount; ++face) {
        if ((sample_face_mask_ & static_cast<std::uint8_t>(1U << face)) == 0U)
            continue;
        Face &state = faces_[face];
        if (state.cost_known)
            update_ema(state.ema, sample_faces_[face]);
        else
            state.ema = sample_faces_[face];
        state.cost_known = true;
        state.last_refresh_update = eligible_updates_;
    }
#ifdef MWPERFFIX_DIAGNOSTICS
    ++eligible_frames_;
#endif
    if (active_non_reflection_ema_ == 0U)
        active_non_reflection_ema_ = residual_work;
    else
        update_ema(active_non_reflection_ema_, residual_work);
    if (renderer_interval_qpc_ != 0U) {
        if (frame_interval_known_)
            update_ema(frame_interval_ema_, renderer_interval_qpc_);
        else
            frame_interval_ema_ = renderer_interval_qpc_;
        frame_interval_known_ = true;
    }

    const std::uint64_t predicted_full = predicted_full_cost();
#ifdef MWPERFFIX_DIAGNOSTICS
    PolicyEvidence evidence{target_period_qpc_, cadence_healthy()};
#endif
    if (full_refresh_pending_ && selected_mask_ == kAllFaceMask) {
        full_refresh_pending_ = false;
        good_streak_ = 0U;
        bad_streak_ = 0U;
    } else {
        // Cost pressure is measured inside the renderer only.
        // Cadence is a separate signal so caps and VSync never become fictitious CPU work.
        const bool pressured = (frame_interval_known_ && !cadence_healthy()) || (work > target_period_qpc_);
        if (!weighted_single_face_) {
            bad_streak_ = pressured ? std::min(bad_streak_ + 1U, 2U) : 0U;
            if (bad_streak_ == 2U) {
                if (recent_upshift_qpc_ != 0U && ticks >= recent_upshift_qpc_ &&
                    ticks - recent_upshift_qpc_ <= ticks_for_ms(500U)) {
                    retry_after_qpc_ = saturating_add(ticks, ticks_for_ms(500U));
                }
                recent_upshift_qpc_ = 0U;
                change_state(
                    true, StateChangeReason::repeated_overload, ticks, active_frame_, work, predicted_full,
                    sample_face_mask_
                );
            } else if (recent_upshift_qpc_ != 0U && ticks - recent_upshift_qpc_ > ticks_for_ms(500U)) {
                recent_upshift_qpc_ = 0U;
            }
        } else {
            const bool affordable = costs_valid() && predicted_full != std::numeric_limits<std::uint64_t>::max() &&
                                    // Leave 5% headroom for recovery.
                                    predicted_full <= (target_period_qpc_ / 20U) * 19U;
            const bool good = !pressured && cadence_healthy() && affordable && ticks >= retry_after_qpc_;
            good_streak_ = good ? std::min(good_streak_ + 1U, 4U) : 0U;
            if (good_streak_ == 4U) {
                recent_upshift_qpc_ = ticks;
                retry_after_qpc_ = 0U;
                change_state(
                    false, StateChangeReason::sustained_headroom, ticks, active_frame_, work, predicted_full,
                    sample_face_mask_
                );
            }
        }
    }

#ifdef MWPERFFIX_DIAGNOSTICS
    emit_frame_observation(
        ticks, true, true, work, face_sum, residual_work, predicted_full, sample_face_mask_, evidence
    );
#endif
    previous_post_present_ = ticks;
    renderer_active_ = false;
}

bool
Controller::costs_valid() const noexcept {
    for (const Face &face : faces_) {
        // Valid secondary-face samples may be eight 30 FPS frames old.
        if (!face.cost_known || eligible_updates_ < face.last_refresh_update ||
            eligible_updates_ - face.last_refresh_update > 8U)
            return false;
    }
    return true;
}

bool
Controller::cadence_healthy() const noexcept {
    return frame_interval_known_ && frame_interval_ema_ <= max_pacing_period_qpc_;
}

std::uint64_t
Controller::predicted_full_cost() const noexcept {
    if (active_non_reflection_ema_ == 0U)
        return std::numeric_limits<std::uint64_t>::max();
    std::uint64_t result = active_non_reflection_ema_;
    for (std::size_t face = 0; face < kFaceCount; ++face) {
        if (!faces_[face].cost_known)
            return std::numeric_limits<std::uint64_t>::max();
        result = saturating_add(result, faces_[face].ema);
    }
    return result;
}

void
Controller::update_ema(std::uint64_t &destination, const std::uint64_t value) noexcept {
    if (value >= destination)
        destination += (value - destination) / 8U;
    else
        destination -= (destination - value) / 8U;
}

} // namespace mwperffix::adaptive
