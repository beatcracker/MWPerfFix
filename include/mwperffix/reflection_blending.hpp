#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

// The runtime adapter owns textures and native draws.
// This layer owns fade state.
namespace mwperffix::blending {

inline constexpr std::size_t kFaceCount = 6U;
inline constexpr std::uint32_t kDefaultSteps = 2U;
inline constexpr std::uint32_t kMinimumSteps = 2U;
inline constexpr std::uint32_t kMaximumSteps = 6U;

enum class Mode : std::uint8_t {
    off,
    on
};

[[nodiscard]] constexpr Mode
next_mode(const Mode mode) noexcept {
    return mode == Mode::off ? Mode::on : Mode::off;
}

// Startup values.
// LiveTuning can change effective settings during play.
struct Config {
    bool enabled = false;
    std::uint32_t steps = kDefaultSteps;
};

[[nodiscard]] constexpr Config
normalize_config(Config config) noexcept {
    if (config.steps < kMinimumSteps || config.steps > kMaximumSteps)
        config.steps = kDefaultSteps;
    return config;
}

// Cumulative fresh-source fraction sampled from a fixed six-segment curve.
// Intermediate knots favour newer captures.
[[nodiscard]] constexpr float
blend_fraction(const std::uint32_t step, const std::uint32_t steps) noexcept {
    constexpr std::array<float, 7> knots{0.0F, 1.0F / 6.0F, 0.42F, 0.68F, 0.85F, 0.95F, 1.0F};
    if (steps < kMinimumSteps || steps > kMaximumSteps || step >= steps)
        return step == 0U ? 0.0F : 1.0F;
    const std::uint32_t scaled = step * 6U;
    const std::size_t segment = scaled / steps;
    const float fraction = static_cast<float>(scaled % steps) / static_cast<float>(steps);
    return knots[segment] + (knots[segment + 1U] - knots[segment]) * fraction;
}

// Incremental compositor weight, not the cumulative fraction.
// The final step is opaque, using the existing native 8-bit weight quantization.
[[nodiscard]] constexpr float
blend_step_weight(const std::uint32_t step, const std::uint32_t steps) noexcept {
    if (steps < kMinimumSteps || steps > kMaximumSteps || step >= steps - 1U)
        return 1.0F;
    const float previous = blend_fraction(step, steps);
    const float next = blend_fraction(step + 1U, steps);
    return (next - previous) / (1.0F - previous);
}

[[nodiscard]] constexpr float
mode_step_weight(const Mode mode, const std::uint32_t step, const std::uint32_t steps) noexcept {
    return mode == Mode::off ? 1.0F : blend_step_weight(step, steps);
}

// A slot is free, reserved for capture, or pending publication.
struct Slot {
    std::size_t face = kFaceCount;
    std::uint32_t next_step = 0U;
    bool reserved = false;
    bool pending = false;
};

// Owns at most `steps - 1` pending transitions.
// The runtime adapter owns the matching temporary textures by slot index.
class BlendState {
  public:
    static constexpr std::size_t kNoSlot = kFaceCount;

    explicit BlendState(const Config config = {}) noexcept : config_(normalize_config(config)) {
    }

    [[nodiscard]] std::uint32_t
    steps() const noexcept {
        return config_.steps;
    }
    [[nodiscard]] std::size_t
    slot_count() const noexcept {
        return static_cast<std::size_t>(config_.steps - 1U);
    }

    // Renderer entry only.
    // The adapter must release obsolete resources first.
    [[nodiscard]] bool
    set_steps(const std::uint32_t steps) noexcept {
        const std::uint32_t normalized = normalize_config({config_.enabled, steps}).steps;
        if (normalized == config_.steps)
            return false;
        reset();
        config_.steps = normalized;
        return true;
    }

    // The caller retains ownership of resources.
    void
    reset() noexcept {
        for (Slot &slot : slots_)
            slot = {};
    }

    // The caller publishes step zero before committing the capture.
    [[nodiscard]] std::size_t
    reserve(const std::size_t face) noexcept {
        if (face >= kFaceCount)
            return kNoSlot;
        // Never allow a superseded capture to remain pending for this face.
        for (std::size_t index = 0; index < slot_count(); ++index) {
            if (slots_[index].face == face && (slots_[index].reserved || slots_[index].pending))
                return kNoSlot;
        }
        for (std::size_t index = 0; index < slot_count(); ++index) {
            if (!slots_[index].reserved && !slots_[index].pending) {
                slots_[index] = Slot{face, 1U, true, false};
                return index;
            }
        }
        return kNoSlot;
    }

    [[nodiscard]] bool
    cancel_reservation(const std::size_t slot) noexcept {
        if (slot >= slot_count() || !slots_[slot].reserved)
            return false;
        slots_[slot] = {};
        return true;
    }

    // Requires completed face rendering and successful step-zero publication.
    [[nodiscard]] bool
    commit(const std::size_t slot) noexcept {
        if (slot >= slot_count() || !slots_[slot].reserved)
            return false;
        slots_[slot].reserved = false;
        slots_[slot].pending = true;
        return true;
    }

    // publish(slot, face, weight) reports success.
    // Failure stops advancement and leaves the slot pending so its texture cannot be reused.
    template <typename Publish>
    bool
    advance(Publish &&publish, const Mode mode = Mode::on) noexcept {
        for (std::size_t index = 0; index < slot_count(); ++index) {
            Slot &slot = slots_[index];
            if (!slot.pending)
                continue;
            if (!publish(index, slot.face, mode_step_weight(mode, slot.next_step, config_.steps)))
                return false;
            ++slot.next_step;
            if (slot.next_step >= config_.steps)
                slot = {};
        }
        return true;
    }

    // A new capture of the same physical face must not be overwritten
    // by the previous capture's long-running dissolve on a later frame.
    template <typename Publish>
    bool
    finish_face(const std::size_t face, Publish &&publish) noexcept {
        for (std::size_t index = 0; index < slot_count(); ++index) {
            Slot &slot = slots_[index];
            if (!slot.pending || slot.face != face)
                continue;
            if (!publish(index, slot.face, 1.0F))
                return false;
            slot = {};
        }
        return true;
    }

    // Retargeting leaves the displayed native cubemap face untouched.
    // Only a pending source is retired.
    // An in-progress reservation is never dropped.
    [[nodiscard]] std::size_t
    pending_slot_for_face(const std::size_t face) const noexcept {
        if (face >= kFaceCount)
            return kNoSlot;
        for (std::size_t index = 0; index < slot_count(); ++index)
            if (slots_[index].pending && slots_[index].face == face)
                return index;
        return kNoSlot;
    }

    [[nodiscard]] bool
    drop_pending_face(const std::size_t face) noexcept {
        const std::size_t slot = pending_slot_for_face(face);
        if (slot == kNoSlot)
            return false;
        slots_[slot] = {};
        return true;
    }

    [[nodiscard]] bool
    slot_pending(const std::size_t slot) const noexcept {
        return slot < slot_count() && slots_[slot].pending;
    }
    [[nodiscard]] bool
    slot_reserved(const std::size_t slot) const noexcept {
        return slot < slot_count() && slots_[slot].reserved;
    }
    [[nodiscard]] std::size_t
    slot_face(const std::size_t slot) const noexcept {
        return slot < slot_count() ? slots_[slot].face : kFaceCount;
    }
    [[nodiscard]] std::size_t
    pending_count() const noexcept {
        std::size_t count = 0U;
        for (const Slot &slot : slots_)
            count += slot.pending ? 1U : 0U;
        return count;
    }

  private:
    Config config_{};
    std::array<Slot, kMaximumSteps - 1U> slots_{};
};

} // namespace mwperffix::blending
