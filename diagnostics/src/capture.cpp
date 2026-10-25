#include "mwperffix/diagnostics/capture.hpp"

// The TraceLogging header depends on Windows base types, so windows.h must come first.
// Keep this order fixed against include sorting.
// clang-format off
#include <windows.h>
#include <mwperffix_tracelogging.hpp>
// clang-format on

#include <atomic>

TRACELOGGING_DEFINE_PROVIDER(
    g_mwperffix_provider, "MWPerfFix.Diagnostics",
    (0x9e8a7dcb, 0x4fa5, 0x45e2, 0xa1, 0x26, 0x4d, 0x96, 0x2b, 0x7e, 0xa3, 0xf1)
);

namespace mwperffix::diagnostics {
namespace {

[[nodiscard]] std::uint64_t
filetime_value(const FILETIME &value) noexcept {
    return (static_cast<std::uint64_t>(value.dwHighDateTime) << 32U) | static_cast<std::uint64_t>(value.dwLowDateTime);
}

// Process-lifetime, allocation-free bridge.
// The observer may fire before the capture is published or while no listener is enabled.
// Both are no-ops.
std::atomic<Capture *> g_capture{nullptr};

void
forward_state_change(void *, const adaptive::StateChange &change) noexcept {
    Capture *const capture = g_capture.load(std::memory_order_acquire);
    if (capture != nullptr && capture->enabled()) {
        capture->emit_state_change(change);
    }
}

void
forward_frame(void *, const adaptive::FrameObservation &observation) noexcept {
    Capture *const capture = g_capture.load(std::memory_order_acquire);
    if (capture != nullptr && capture->enabled()) {
        capture->emit_frame(observation);
    }
}

} // namespace

adaptive::Observer
adaptive_observer() noexcept {
    return {&forward_state_change, &forward_frame, nullptr};
}

void
publish_capture(Capture *const capture) noexcept {
    g_capture.store(capture, std::memory_order_release);
}

Capture::~Capture() {
    if (registered_)
        TraceLoggingUnregister(g_mwperffix_provider);
}

bool
Capture::initialize(const bool synthetic) noexcept {
    if (registered_)
        return true;
    if (TraceLoggingRegister(g_mwperffix_provider) != 0)
        return false;
    registered_ = true;
    synthetic_ = synthetic;
    FILETIME creation{}, exit{}, kernel{}, user{};
    if (GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user) != FALSE) {
        process_creation_id_ = filetime_value(creation);
    }
    LARGE_INTEGER frequency{};
    if (QueryPerformanceFrequency(&frequency) != FALSE) {
        qpc_frequency_ = static_cast<std::uint64_t>(frequency.QuadPart);
    }
    return true;
}

bool
Capture::enabled() const noexcept {
    return registered_ && TraceLoggingProviderEnabled(g_mwperffix_provider, kProviderLevel, kProviderKeyword) != FALSE;
}

void
Capture::emit_metadata(const Metadata &metadata) noexcept {
    metadata_ = metadata;
    TraceLoggingWrite(
        g_mwperffix_provider, "Metadata", TraceLoggingLevel(kProviderLevel), TraceLoggingKeyword(kProviderKeyword),
        TraceLoggingUInt32(kSchemaVersion, "SchemaVersion"), TraceLoggingString(kSchemaMarker, "SchemaMarker"),
        TraceLoggingString(metadata.build_version, "BuildVersion"),
        TraceLoggingString(metadata.build_variant, "BuildVariant"),
        TraceLoggingUInt64(process_creation_id_, "ProcessCreationId"),
        TraceLoggingUInt64(metadata.executable_base, "ExecutableBase"),
        TraceLoggingUInt32(metadata.executable_timestamp, "ExecutableTimeDateStamp"),
        TraceLoggingUInt32(metadata.executable_size, "ExecutableSizeOfImage"),
        TraceLoggingUInt32(metadata.target_fps, "TargetFPS"), TraceLoggingUInt64(qpc_frequency_, "QpcFrequency"),
        TraceLoggingUInt32(metadata.fixes.requested_mask, "RequestedFixMask"),
        TraceLoggingUInt32(metadata.fixes.applied_mask, "AppliedFixMask"),
        TraceLoggingUInt32(metadata.fixes.outcomes, "FixOutcomes"), TraceLoggingString("QPC ticks", "ElapsedUnit"),
        TraceLoggingBoolean(synthetic_ ? TRUE : FALSE, "Synthetic")
    );
    metadata_due_ = false;
    frames_since_metadata_ = 0;
}

void
Capture::emit_frame(const adaptive::FrameObservation &observation) noexcept {
    if (metadata_due_ || frames_since_metadata_ >= kMetadataFrameInterval) {
        // Re-emit metadata before this frame when due.
        // State-change events can precede it, so consumers match metadata by process identity anywhere in the capture.
        // The stored copy is refreshed by emit_metadata.
        emit_metadata(metadata_);
    }
    ++frame_sequence_;
    TraceLoggingWrite(
        g_mwperffix_provider, "AdaptiveReflectionFrame", TraceLoggingLevel(kProviderLevel),
        TraceLoggingKeyword(kProviderKeyword), TraceLoggingUInt32(kSchemaVersion, "SchemaVersion"),
        TraceLoggingUInt64(process_creation_id_, "ProcessCreationId"),
        TraceLoggingUInt64(frame_sequence_, "FrameSequence"),
        TraceLoggingUInt32(observation.game_frame_counter, "GameFrameCounter"),
        TraceLoggingUInt64(observation.completion_qpc, "CompletionQpc"),
        TraceLoggingUInt32(observation.configured_mode, "ConfiguredMode"),
        TraceLoggingUInt32(observation.selected_budget, "SelectedBudget"),
        TraceLoggingUInt32(observation.policy_budget, "PolicyBudget"),
        TraceLoggingUInt32(observation.recovery_streak, "RecoveryStreak"),
        TraceLoggingUInt64(observation.retry_after_qpc, "RetryAfterQpc"),
        TraceLoggingBoolean(observation.selection_valid ? TRUE : FALSE, "SelectionValid"),
        TraceLoggingBoolean(observation.eligible ? TRUE : FALSE, "Eligible"),
        TraceLoggingBoolean(observation.timing_valid ? TRUE : FALSE, "TimingValid"),
        TraceLoggingBoolean(observation.present_succeeded ? TRUE : FALSE, "PresentSucceeded"),
        TraceLoggingBoolean(observation.full_refresh_pending ? TRUE : FALSE, "FullRefreshPending"),
        TraceLoggingUInt64(observation.renderer_work_qpc, "RendererWorkQpcTicks"),
        TraceLoggingUInt64(observation.observed_face_qpc, "ObservedFaceQpcTicks"),
        TraceLoggingUInt64(observation.residual_work_qpc, "ResidualWorkQpcTicks"),
        TraceLoggingUInt64(observation.active_frame_work_qpc, "ActiveFrameQpcTicks"),
        TraceLoggingUInt64(observation.active_non_reflection_qpc, "ActiveNonReflectionQpcTicks"),
        TraceLoggingUInt64(observation.predicted_full_qpc, "PredictedFullQpcTicks"),
        TraceLoggingUInt64(observation.frame_interval_qpc, "FrameIntervalQpcTicks"),
        TraceLoggingBoolean(observation.cadence_healthy ? TRUE : FALSE, "CadenceHealthy"),
        TraceLoggingUInt64(observation.cadence_budget_qpc, "CadenceBudgetQpcTicks"),
        TraceLoggingUInt32(observation.face_mask, "FaceMask"),
        TraceLoggingUInt32(static_cast<std::uint32_t>(observation.first_invalid_reason), "FirstInvalidReason"),
        TraceLoggingUInt32(static_cast<std::uint32_t>(observation.first_invalid_site), "FirstInvalidSite"),
        TraceLoggingUInt32(observation.first_invalid_face, "FirstInvalidFace"),
        TraceLoggingUInt64(observation.first_invalid_qpc, "FirstInvalidQpc"),
        TraceLoggingUInt32(observation.requested_face_mask, "RequestedFaceMask"),
        TraceLoggingUInt32(observation.face_begin_mask, "FaceBeginMask"),
        TraceLoggingUInt32(observation.completed_face_mask, "CompletedFaceMask"),
        TraceLoggingUInt32(observation.face_begin_count, "FaceBeginCount"),
        TraceLoggingUInt32(observation.face_end_count, "FaceEndCount"),
        TraceLoggingUInt64(observation.renderer_begin_qpc, "RendererBeginQpc"),
        TraceLoggingUInt64(observation.pre_present_qpc, "PrePresentQpc"),
        TraceLoggingUInt64(observation.previous_post_present_qpc, "PreviousPostPresentQpc"),
        TraceLoggingUInt32(observation.ignored_foreign_face_callbacks, "IgnoredForeignFaceCallbacks"),
        TraceLoggingUInt32(observation.unknown_face_indices, "UnknownFaceIndices"),
        TraceLoggingUInt32(observation.interrupted_renderer_count, "InterruptedRendererCount"),
        TraceLoggingUInt64(observation.decision_sequence, "DecisionSequence"),
        TraceLoggingBoolean(synthetic_ ? TRUE : FALSE, "Synthetic"),
        TraceLoggingUInt64(observation.face_qpc[0], "Face0QpcTicks"),
        TraceLoggingUInt64(observation.face_qpc[1], "Face1QpcTicks"),
        TraceLoggingUInt64(observation.face_qpc[2], "Face2QpcTicks"),
        TraceLoggingUInt64(observation.face_qpc[3], "Face3QpcTicks"),
        TraceLoggingUInt64(observation.face_qpc[4], "Face4QpcTicks"),
        TraceLoggingUInt64(observation.face_qpc[5], "Face5QpcTicks"),
        TraceLoggingUInt64(observation.face_ema_qpc[0], "Face0EmaQpcTicks"),
        TraceLoggingUInt64(observation.face_ema_qpc[1], "Face1EmaQpcTicks"),
        TraceLoggingUInt64(observation.face_ema_qpc[2], "Face2EmaQpcTicks"),
        TraceLoggingUInt64(observation.face_ema_qpc[3], "Face3EmaQpcTicks"),
        TraceLoggingUInt64(observation.face_ema_qpc[4], "Face4EmaQpcTicks"),
        TraceLoggingUInt64(observation.face_ema_qpc[5], "Face5EmaQpcTicks"),
        TraceLoggingUInt64(observation.residual_ema_qpc, "ResidualEmaQpcTicks"),
        TraceLoggingUInt64(observation.frame_interval_ema_qpc, "FrameIntervalEmaQpcTicks"),
        TraceLoggingUInt64(observation.blend_qpc[0], "BlendBeginFrameQpcTicks"),
        TraceLoggingUInt64(observation.blend_qpc[1], "BlendSelectScheduleQpcTicks"),
        TraceLoggingUInt64(observation.blend_qpc[2], "BlendBeginFaceQpcTicks"),
        TraceLoggingUInt64(observation.blend_qpc[3], "BlendEndFaceQpcTicks"),
        TraceLoggingUInt32(observation.blend_mode, "BlendMode"),
        TraceLoggingUInt32(observation.blend_steps, "BlendSteps"),
        TraceLoggingBoolean(observation.costs_valid ? TRUE : FALSE, "CostsValid"),
        TraceLoggingBoolean(observation.blend_active ? TRUE : FALSE, "BlendActive"),
        TraceLoggingBoolean(observation.blend_failed ? TRUE : FALSE, "BlendFailed"),
        TraceLoggingBoolean(observation.blend_timing_valid ? TRUE : FALSE, "BlendTimingValid")
    );
    ++frames_since_metadata_;
}

void
Capture::emit_state_change(const adaptive::StateChange &change) noexcept {
    TraceLoggingWrite(
        g_mwperffix_provider, "AdaptiveReflectionStateChange", TraceLoggingLevel(kProviderLevel),
        TraceLoggingKeyword(kProviderKeyword), TraceLoggingUInt32(kSchemaVersion, "SchemaVersion"),
        TraceLoggingUInt64(process_creation_id_, "ProcessCreationId"), TraceLoggingUInt64(change.sequence, "Sequence"),
        TraceLoggingUInt64(change.decision_qpc, "DecisionQpc"),
        TraceLoggingUInt32(change.game_frame_counter, "GameFrameCounter"),
        TraceLoggingUInt32(change.previous_budget, "PreviousBudget"),
        TraceLoggingUInt32(change.new_budget, "NewBudget"),
        TraceLoggingUInt32(static_cast<std::uint32_t>(change.reason), "Reason"),
        TraceLoggingUInt64(change.renderer_work_qpc, "RendererWorkQpcTicks"),
        TraceLoggingUInt64(change.predicted_full_qpc, "PredictedFullQpcTicks"),
        TraceLoggingUInt64(change.cadence_budget_qpc, "CadenceBudgetQpcTicks"),
        TraceLoggingUInt32(change.eligible_frames, "EligibleFrames"),
        TraceLoggingUInt32(change.cheap_streak, "CheapStreak"),
        TraceLoggingUInt32(change.expensive_streak, "ExpensiveStreak"),
        TraceLoggingUInt32(change.face_mask, "FaceMask"),
        TraceLoggingBoolean(change.full_refresh_pending ? TRUE : FALSE, "FullRefreshPending"),
        TraceLoggingBoolean(synthetic_ ? TRUE : FALSE, "Synthetic")
    );
}

} // namespace mwperffix::diagnostics
