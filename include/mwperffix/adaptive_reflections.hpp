#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>

namespace mwperffix::adaptive {

inline constexpr std::size_t kFaceCount = 6U;
inline constexpr std::uint8_t kAllFaceMask = 0x3fU;
// The fixed 15-update physical face sequence in Weighted single-face.
inline constexpr std::array<std::uint8_t, 15> kFaceUpdateSequence{
    4U,
    0U,
    1U,
    4U,
    2U,
    3U,
    4U,
    5U,
    0U,
    4U,
    1U,
    2U,
    4U,
    3U,
    5U,
};
inline constexpr std::uint32_t kDefaultTargetFps = 60U;
inline constexpr std::uint32_t kMinimumTargetFps = 30U;
inline constexpr std::uint32_t kMaximumTargetFps = 1000U;

struct Config {
    std::uint32_t target_fps = kDefaultTargetFps;
};

[[nodiscard]] constexpr Config
normalize_config(Config config) noexcept {
    if (config.target_fps < kMinimumTargetFps || config.target_fps > kMaximumTargetFps)
        config.target_fps = kDefaultTargetFps;
    return config;
}

// Renderer-entry facts shared by scheduling and native-only blending.
struct RendererTiming {
    std::uint64_t interval_qpc = 0U;
    bool gap = false;
    bool repeated = false;
    bool reversed = false;
};

class RendererClock {
  public:
    explicit RendererClock(const std::uint64_t frequency) noexcept : gap_qpc_((frequency == 0U ? 1U : frequency) / 4U) {
    }

    [[nodiscard]] RendererTiming
    observe(const std::uint64_t ticks) noexcept {
        RendererTiming timing{};
        if (previous_ != 0U) {
            timing.repeated = ticks == previous_;
            timing.reversed = ticks < previous_;
            if (!timing.reversed)
                timing.interval_qpc = ticks - previous_;
            timing.gap = timing.interval_qpc > gap_qpc_;
        }
        previous_ = ticks;
        return timing;
    }

  private:
    std::uint64_t gap_qpc_;
    std::uint64_t previous_ = 0U;
};

struct Selection {
    std::uint8_t face_mask = 0U;
    std::uint8_t budget = 0U;
    bool custom = false;
};

// Description of one committed controller policy transition.
// Native reflection setting values remain separate from this face-count policy.
enum class StateChangeReason : std::uint32_t {
    full_refresh_complete = 1,
    repeated_overload = 3,
    sustained_headroom = 4,
    invalid_sample_safety = 5,
    device_reset_refresh = 6,
    observation_gap_reset = 7,
    configured_mode_reset = 9,
};

struct StateChange {
    std::uint64_t sequence = 0;
    std::uint64_t decision_qpc = 0;
    std::uint64_t renderer_work_qpc = 0;
    std::uint64_t predicted_full_qpc = 0;
    std::uint64_t cadence_budget_qpc = 0;
    std::uint32_t game_frame_counter = 0;
    std::uint32_t previous_budget = 0;
    std::uint32_t new_budget = 0;
    StateChangeReason reason = StateChangeReason::full_refresh_complete;
    std::uint32_t eligible_frames = 0;
    std::uint32_t cheap_streak = 0;
    std::uint32_t expensive_streak = 0;
    std::uint32_t face_mask = 0;
    bool full_refresh_pending = false;
};

#ifdef MWPERFFIX_DIAGNOSTICS
// Stable schema-15 numeric causes.
// Once published, never reorder these values.
enum class InvalidReason : std::uint32_t {
    none = 0,
    renderer_reentered = 1,
    frame_counter_repeated = 2,
    renderer_time_reversed = 3,
    selection_repeated = 4,
    selection_time_reversed = 5,
    face_begin_overlap = 6,
    face_begin_after_present = 7,
    face_begin_time_reversed = 8,
    face_begin_duplicate = 9,
    face_end_without_begin = 10,
    face_end_mismatch = 11,
    face_end_time_reversed = 12,
    pre_present_repeated = 13,
    pre_present_time_reversed = 14,
    face_still_open_at_present = 15,
    pre_present_before_previous_post = 16,
    present_failed = 17,
    pre_present_missing = 18,
    post_before_pre = 19,
    face_mask_mismatch = 20,
    face_time_exceeds_renderer_work = 21,
    face_time_exceeds_active_work = 22,
    face_begin_unknown = 23,
    face_end_unknown = 24,
};

enum class InvalidSite : std::uint32_t {
    none = 0,
    renderer_begin = 1,
    selection = 2,
    face_begin = 3,
    face_end = 4,
    pre_present = 5,
    post_present = 6,
};
#endif

// Diagnostic-only description of one completed renderer lifecycle.
// A bypassed frame has selection_valid=false and zero selected/policy budgets and face mask.
// Raw lifecycle state and invalid-sample evidence remain meaningful.
struct FrameObservation {
    std::uint32_t recovery_streak = 0;
    std::uint64_t retry_after_qpc = 0;
    std::uint64_t completion_qpc = 0;
    std::uint64_t renderer_work_qpc = 0;
    std::uint64_t observed_face_qpc = 0;
    std::uint64_t residual_work_qpc = 0;
    std::uint64_t active_frame_work_qpc = 0;
    std::uint64_t active_non_reflection_qpc = 0;
    std::uint64_t predicted_full_qpc = 0;
    std::uint64_t frame_interval_qpc = 0;
    std::uint64_t cadence_budget_qpc = 0;
    std::uint64_t decision_sequence = 0;
    std::uint32_t game_frame_counter = 0;
    std::uint32_t configured_mode = 0;
    std::uint32_t selected_budget = 0;
    std::uint32_t policy_budget = 0;
    std::uint32_t face_mask = 0;
#ifdef MWPERFFIX_DIAGNOSTICS
    std::array<std::uint64_t, kFaceCount> face_qpc{};
    std::array<std::uint64_t, kFaceCount> face_ema_qpc{};
    std::uint64_t residual_ema_qpc = 0;
    std::uint64_t frame_interval_ema_qpc = 0;
    bool costs_valid = false;
    std::array<std::uint64_t, 4> blend_qpc{};
    std::uint32_t blend_mode = 0;
    std::uint32_t blend_steps = 0;
    bool blend_active = false;
    bool blend_failed = false;
    bool blend_timing_valid = false;
    std::uint64_t first_invalid_qpc = 0;
    std::uint64_t renderer_begin_qpc = 0;
    std::uint64_t pre_present_qpc = 0;
    std::uint64_t previous_post_present_qpc = 0;
    InvalidReason first_invalid_reason = InvalidReason::none;
    InvalidSite first_invalid_site = InvalidSite::none;
    std::uint32_t first_invalid_face = kFaceCount; // 6: no known canonical face
    std::uint32_t requested_face_mask = 0;
    std::uint32_t face_begin_mask = 0;
    std::uint32_t completed_face_mask = 0;
    std::uint32_t face_begin_count = 0;
    std::uint32_t face_end_count = 0;
    std::uint32_t ignored_foreign_face_callbacks = 0;
    std::uint32_t unknown_face_indices = 0;
    std::uint32_t interrupted_renderer_count = 0;
#endif
    bool selection_valid = false;
    bool eligible = false;
    bool timing_valid = false;
    bool present_succeeded = false;
    bool cadence_healthy = false;
    bool full_refresh_pending = false;
};

using StateChangeFunction = void (*)(void *, const StateChange &) noexcept;
using FrameFunction = void (*)(void *, const FrameObservation &) noexcept;

struct Observer {
    StateChangeFunction state_change = nullptr;
    FrameFunction frame_complete = nullptr;
    void *context = nullptr;
};

// Pure timing/policy controller.
// The runtime owns game memory, player/camera sampling and the six-DWORD native mask adapter.
// This class only decides the canonical native-face mask and learns observed work.
class Controller {
  public:
    explicit Controller(std::uint64_t qpc_frequency, Config config = {}, Observer observer = {}) noexcept;
    void begin_renderer(std::uint64_t ticks, std::uint32_t frame, RendererTiming timing) noexcept;
    [[nodiscard]] Selection select(std::uint32_t configured_mode, bool eligible, std::uint64_t ticks) noexcept;
    void begin_face(std::size_t native_face, std::uint64_t ticks) noexcept;
    void end_face(std::size_t native_face, std::uint64_t ticks) noexcept;
    void before_present(std::uint64_t ticks) noexcept;
    void after_present(std::uint64_t ticks, bool succeeded) noexcept;
    void invalidate_contents(std::uint64_t ticks, std::uint32_t frame) noexcept;
    [[nodiscard]] bool set_target_fps(std::uint32_t fps) noexcept;
    [[nodiscard]] std::uint32_t
    target_fps() const noexcept {
        return target_fps_;
    }
    // Native bypass abandons controller observations without a policy transition.
    void suspend() noexcept;
#ifdef MWPERFFIX_DIAGNOSTICS
    void set_dispatch_rejections(std::uint32_t foreign, std::uint32_t unknown) noexcept;
    [[nodiscard]] bool
    observation_active() const noexcept {
        return renderer_active_;
    }
#endif
    [[nodiscard]] std::uint32_t
    effective_budget() const noexcept {
        return weighted_single_face_ && !full_refresh_pending_ ? 1U : 6U;
    }
    [[nodiscard]] bool
    full_refresh_pending() const noexcept {
        return full_refresh_pending_;
    }
    [[nodiscard]] bool
    active_frame(std::uint32_t frame) const noexcept {
        return renderer_active_ && active_frame_ == frame;
    }

  private:
    struct Face {
        std::uint64_t ema = 0;
        std::uint32_t last_refresh_update = 0;
        bool cost_known = false;
    };
    // Cadence evidence accompanies the full-six cost estimate.
    struct PolicyEvidence {
        std::uint64_t cadence_budget_qpc = 0;
        bool cadence_healthy = false;
    };
    void reset_learning(StateChangeReason reason, std::uint64_t ticks, std::uint32_t frame) noexcept;
    void clear_learning() noexcept;
    void invalidate_sample(std::uint64_t ticks, std::uint32_t frame) noexcept;
    void discard_timing() noexcept;
    void change_state(
        bool weighted_single_face, StateChangeReason reason, std::uint64_t ticks, std::uint32_t frame,
        std::uint64_t work, std::uint64_t predicted_full, std::uint32_t face_mask
    ) noexcept;
#ifdef MWPERFFIX_DIAGNOSTICS
    void note_invalid(
        InvalidReason reason, InvalidSite site, std::uint64_t ticks, std::size_t face = kFaceCount
    ) noexcept;
    void emit_frame_observation(
        std::uint64_t completion_qpc, bool present_succeeded, bool timing_valid, std::uint64_t renderer_work_qpc,
        std::uint64_t observed_face_qpc, std::uint64_t residual_work_qpc, std::uint64_t predicted_full_qpc,
        std::uint32_t face_mask, const PolicyEvidence &evidence
    ) noexcept;
#endif
    [[nodiscard]] std::uint64_t ticks_for_ms(std::uint32_t milliseconds) const noexcept;
    [[nodiscard]] std::uint32_t
    selected_budget() const noexcept {
        return static_cast<std::uint32_t>(std::popcount(static_cast<unsigned>(selected_mask_)));
    }
    [[nodiscard]] bool costs_valid() const noexcept;
    [[nodiscard]] bool cadence_healthy() const noexcept;
    [[nodiscard]] std::uint64_t predicted_full_cost() const noexcept;
    static void update_ema(std::uint64_t &destination, std::uint64_t value) noexcept;

    std::uint64_t frequency_;
    std::uint32_t target_fps_ = kDefaultTargetFps;
    Observer observer_{};
    std::array<Face, kFaceCount> faces_{};
    std::array<std::uint64_t, kFaceCount> sample_faces_{};
    std::uint64_t active_non_reflection_ema_ = 0;
    std::uint64_t previous_post_present_ = 0;
    std::uint64_t renderer_begin_ = 0;
    std::uint64_t pre_present_ = 0;
    std::uint64_t active_frame_work_qpc_ = 0;
    std::uint64_t face_begin_ = 0;
    std::uint64_t last_observation_ = 0;
    std::uint64_t target_period_qpc_ = 0;
    std::uint64_t max_pacing_period_qpc_ = 0;
    std::uint64_t frame_interval_ema_ = 0;
    std::uint64_t renderer_interval_qpc_ = 0;
    std::uint64_t recent_upshift_qpc_ = 0;
    std::uint64_t retry_after_qpc_ = 0;
    std::uint32_t active_frame_ = 0;
    std::uint32_t eligible_updates_ = 0;
#ifdef MWPERFFIX_DIAGNOSTICS
    std::uint32_t eligible_frames_ = 0;
#endif
    std::uint32_t good_streak_ = 0;
    std::uint32_t bad_streak_ = 0;
    std::size_t phase_ = 0;
    std::size_t active_face_ = kFaceCount;
    std::uint8_t sample_face_mask_ = 0;
    std::uint8_t selected_mask_ = 0;
#ifdef MWPERFFIX_DIAGNOSTICS
    InvalidReason first_invalid_reason_ = InvalidReason::none;
    InvalidSite first_invalid_site_ = InvalidSite::none;
    std::uint64_t first_invalid_qpc_ = 0;
    std::uint64_t previous_post_present_at_begin_ = 0;
    std::uint8_t face_begin_mask_ = 0;
    std::uint32_t face_begin_count_ = 0;
    std::uint32_t face_end_count_ = 0;
    std::uint32_t first_invalid_face_ = kFaceCount;
    std::uint32_t ignored_foreign_face_callbacks_ = 0;
    std::uint32_t unknown_face_indices_ = 0;
    std::uint32_t interrupted_renderer_count_ = 0;
    std::uint32_t configured_mode_ = 0;
    std::uint64_t decision_sequence_ = 0;
#endif
    bool weighted_single_face_ = false;
    bool renderer_active_ = false;
    bool have_frame_ = false;
    bool frame_interval_known_ = false;
    bool selection_made_ = false;
    bool present_started_ = false;
    bool sample_eligible_ = false;
    bool sample_valid_ = false;
    bool full_refresh_pending_ = true;
    bool configured_enabled_ = false;
};

} // namespace mwperffix::adaptive
