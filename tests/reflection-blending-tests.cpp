#include "mwperffix/reflection_blending.hpp"
#include "test_error_mode.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {

using mwperffix::blending::blend_fraction;
using mwperffix::blending::blend_step_weight;
using mwperffix::blending::BlendState;
using mwperffix::blending::Config;
using mwperffix::blending::kFaceCount;
using mwperffix::blending::Mode;
using mwperffix::blending::mode_step_weight;
using mwperffix::blending::next_mode;
using mwperffix::blending::normalize_config;

[[noreturn]] void
fail(const char *const expression, const int line) {
    std::fprintf(stderr, "line %d: %s\n", line, expression);
    std::exit(EXIT_FAILURE);
}
#define CHECK(expression) ((expression) ? static_cast<void>(0) : fail(#expression, __LINE__))

[[nodiscard]] bool
close(const float left, const float right) {
    const float difference = left - right;
    return (difference < 0.0F ? -difference : difference) < 0.0001F;
}

void
test_extended_cycle() {
    for (std::uint32_t steps = 2U; steps <= 6U; ++steps) {
        CHECK(normalize_config(Config{true, steps}).steps == steps);
        BlendState state(Config{true, steps});
        CHECK(state.slot_count() == steps - 1U);
        std::array<float, kFaceCount> fractions{};
        std::array<std::uint32_t, kFaceCount> publications{};
        for (std::size_t frame = 0; frame < 60U; ++frame) {
            CHECK(state.advance([&](std::size_t, std::size_t face, float weight) {
                fractions[face] += (1.0F - fractions[face]) * weight;
                ++publications[face];
                CHECK(close(fractions[face], blend_fraction(publications[face], steps)));
                return true;
            }));
            const auto face = frame % kFaceCount;
            if (frame >= kFaceCount) {
                CHECK(close(fractions[face], 1.0F));
                CHECK(publications[face] == steps);
            }
            const auto slot = state.reserve(face);
            CHECK(slot != BlendState::kNoSlot);
            fractions[face] = blend_step_weight(0U, steps);
            publications[face] = 1U;
            CHECK(state.commit(slot));
            CHECK(state.pending_count() <= steps - 1U);
        }
    }
}

void
test_same_face_capture_finalizes_old_blend() {
    BlendState state(Config{true, 6U});
    const std::size_t first = state.reserve(4U);
    CHECK(state.commit(first));
    int writes = 0;
    for (int frame = 0; frame != 3; ++frame) {
        CHECK(state.advance([&](std::size_t, std::size_t face, float) {
            CHECK(face == 4U);
            ++writes;
            return true;
        }));
    }
    CHECK(writes == 3);
    CHECK(state.slot_pending(first));
    CHECK(state.finish_face(4U, [&](std::size_t, std::size_t face, float weight) {
        CHECK(face == 4U && close(weight, 1.0F));
        ++writes;
        return true;
    }));
    CHECK(writes == 4);
    CHECK(!state.slot_pending(first));
    const std::size_t next = state.reserve(4U);
    CHECK(next != BlendState::kNoSlot);
    CHECK(state.commit(next));
    CHECK(state.advance([&](std::size_t, std::size_t face, float weight) {
        CHECK(face == 4U && weight < 1.0F);
        return true;
    }));
    CHECK(!state.finish_face(4U, [](std::size_t, std::size_t, float) { return false; }));
    CHECK(state.slot_pending(next)); // failed publish is not silently accepted
    state.reset();
    CHECK(state.pending_count() == 0U);
}

void
test_drop_pending_face_without_publication() {
    BlendState state(Config{true, 6U});
    const auto first = state.reserve(4U);
    CHECK(first != BlendState::kNoSlot);
    CHECK(state.commit(first));
    const auto other = state.reserve(0U);
    CHECK(other != BlendState::kNoSlot);
    CHECK(state.commit(other));

    CHECK(state.reserve(4U) == BlendState::kNoSlot);
    CHECK(state.pending_slot_for_face(4U) == first);
    CHECK(!state.drop_pending_face(kFaceCount));
    CHECK(!state.drop_pending_face(3U));
    CHECK(state.drop_pending_face(4U)); // no callback, no opaque publication
    CHECK(state.pending_slot_for_face(4U) == BlendState::kNoSlot);
    CHECK(!state.slot_pending(first));
    CHECK(state.slot_pending(other));
    CHECK(state.pending_count() == 1U);

    const auto replacement = state.reserve(4U);
    CHECK(replacement == first);         // same bounded slot, now owned by new capture
    CHECK(!state.drop_pending_face(4U)); // a reservation is not pending
    CHECK(state.slot_reserved(replacement));
    CHECK(state.commit(replacement));
    unsigned publications = 0U;
    CHECK(state.advance([&](const std::size_t slot, const std::size_t face, const float) {
        CHECK(slot == other || slot == replacement);
        CHECK(face == (slot == other ? 0U : 4U));
        ++publications;
        return true;
    }));
    CHECK(publications == 2U);
    CHECK(state.pending_count() == 2U);
}

void
test_weighted_single_face_retarget_sequence() {
    constexpr std::array<std::size_t, 15> sequence{4U, 0U, 1U, 4U, 2U, 3U, 4U, 5U, 0U, 4U, 1U, 2U, 4U, 3U, 5U};
    for (const Mode mode : {Mode::on}) {
        for (const std::uint32_t steps : {2U, 3U, 6U}) {
            BlendState state(Config{true, steps});
            std::array<float, kFaceCount> displayed{};
            std::array<float, kFaceCount> latest{};
            std::array<unsigned, kFaceCount> generations{};
            std::array<unsigned, mwperffix::blending::kMaximumSteps - 1U> slot_generation{};
            std::array<float, mwperffix::blending::kMaximumSteps - 1U> slot_value{};
            unsigned retargets = 0U;
            unsigned publications = 0U;

            for (std::size_t frame = 0; frame < sequence.size() * 2U; ++frame) {
                const std::size_t face = sequence[frame % sequence.size()];
                const std::size_t pending = state.pending_slot_for_face(face);
                const float before = displayed[face];
                const bool retargeting = pending != BlendState::kNoSlot;
                if (retargeting) {
                    CHECK(slot_generation[pending] == generations[face]);
                    CHECK(state.drop_pending_face(face));
                    CHECK(close(displayed[face],
                        before)); // native destination stays intact
                    ++retargets;
                }
                CHECK(state.advance(
                    [&](const std::size_t slot, const std::size_t target, const float weight) {
                        CHECK(target < kFaceCount);
                        CHECK(!retargeting || target != face);               // no old-source draw this frame
                        CHECK(slot_generation[slot] == generations[target]); // latest only
                        CHECK(weight > 0.0F && weight <= 1.0F);
                        displayed[target] += (slot_value[slot] - displayed[target]) * weight;
                        ++publications;
                        return true;
                    },
                    mode
                ));
                const auto slot = state.reserve(face);
                CHECK(slot != BlendState::kNoSlot);
                const float value = static_cast<float>(frame + 1U) / 31.0F;
                latest[face] = value;
                ++generations[face];
                slot_generation[slot] = generations[face];
                slot_value[slot] = value;
                displayed[face] += (value - displayed[face]) * mode_step_weight(mode, 0U, steps);
                if (retargeting)
                    CHECK(close(displayed[face], before + (value - before) * mode_step_weight(mode, 0U, steps)));
                ++publications;
                CHECK(state.commit(slot));
                CHECK(state.pending_count() <= state.slot_count());
                CHECK(state.pending_slot_for_face(face) == slot);
            }
            if (steps == 6U)
                CHECK(retargets >= 6U); // +Z repeatedly replaces a partial blend
            // With no more captures every face converges to its latest source.
            for (std::uint32_t i = 0; i < steps; ++i)
                CHECK(state.advance(
                    [&](const std::size_t slot, const std::size_t face, const float weight) {
                        CHECK(slot_generation[slot] == generations[face]);
                        displayed[face] += (slot_value[slot] - displayed[face]) * weight;
                        return true;
                    },
                    mode
                ));
            CHECK(state.pending_count() == 0U);
            for (std::size_t face = 0; face < kFaceCount; ++face)
                CHECK(close(displayed[face], latest[face]));
            CHECK(publications >= sequence.size() * 2U);
        }
    }
}

void
test_config_normalization() {
    CHECK(normalize_config(Config{}).steps == 2U);
    CHECK(normalize_config(Config{false, 2U}).steps == 2U);
    CHECK(normalize_config(Config{false, 3U}).steps == 3U);
    CHECK(normalize_config(Config{false, 0U}).steps == 2U);
    CHECK(normalize_config(Config{false, 1U}).steps == 2U);
    CHECK(normalize_config(Config{false, 4U}).steps == 4U);
    CHECK(normalize_config(Config{false, 99U}).steps == 2U);
    CHECK(!normalize_config(Config{}).enabled);
    CHECK(normalize_config(Config{true, 3U}).enabled);
}

void
test_blend_curve() {
    CHECK(next_mode(Mode::on) == Mode::off);
    CHECK(next_mode(Mode::off) == Mode::on);
    constexpr std::array<float, 7> knots{0.0F, 1.0F / 6.0F, 0.42F, 0.68F, 0.85F, 0.95F, 1.0F};
    for (std::uint32_t i = 0U; i <= 6U; ++i)
        CHECK(close(blend_fraction(i, 6U), knots[i]));
    for (std::uint32_t steps = 2U; steps <= 6U; ++steps) {
        float cumulative = 0.0F;
        float packed_cumulative = 0.0F;
        float last = 0.0F;
        for (std::uint32_t step = 0U; step < steps; ++step) {
            const float q = blend_fraction(step + 1U, steps);
            const float weight = blend_step_weight(step, steps);
            CHECK(q > last && q <= 1.0F);
            CHECK(weight > 0.0F && weight <= 1.0F);
            cumulative += (1.0F - cumulative) * weight;
            // The compositor packs the source factor into an eight-bit D3D9 state.
            const auto packed = static_cast<std::uint32_t>(weight * 255.0F + 0.5F);
            packed_cumulative += (1.0F - packed_cumulative) * (static_cast<float>(packed) / 255.0F);
            CHECK(close(cumulative, q));
            const float error = packed_cumulative - q;
            CHECK(error > -0.01F && error < 0.01F);
            if (steps == 6U && step == 2U)
                CHECK(close(cumulative, 0.68F));
            last = q;
        }
        CHECK(close(blend_step_weight(steps - 1U, steps), 1.0F));
        CHECK(close(cumulative, 1.0F));
        CHECK(close(packed_cumulative, 1.0F));
    }
    CHECK(close(blend_step_weight(0U, 6U), 1.0F / 6.0F));
    CHECK(close(blend_step_weight(0U, 1U), 1.0F));
}

void
test_slot_count() {
    CHECK(BlendState(Config{false, 2U}).slot_count() == 1U);
    CHECK(BlendState(Config{false, 3U}).slot_count() == 2U);
    // Invalid steps normalize to two, so the slot count follows.
    CHECK(BlendState(Config{false, 7U}).slot_count() == 1U);
}

void
test_reserve_commit_advance_two_steps() {
    BlendState state(Config{false, 2U});
    const std::size_t slot = state.reserve(3U);
    CHECK(slot == 0U);
    CHECK(state.slot_reserved(slot));
    CHECK(state.slot_face(slot) == 3U);
    // A second reservation fails: only one slot exists for two steps.
    CHECK(state.reserve(4U) == BlendState::kNoSlot);
    CHECK(state.commit(slot));
    CHECK(!state.slot_reserved(slot));
    CHECK(state.slot_pending(slot));

    std::vector<std::pair<std::size_t, float>> published;
    const bool ok = state.advance([&](const std::size_t index, const std::size_t face, const float weight) {
        CHECK(face == 3U);
        published.emplace_back(index, weight);
        return true;
    });
    CHECK(ok);
    CHECK(published.size() == 1U);
    CHECK(published[0].first == 0U);
    // Step zero was published at face end, so the first advance is the final opaque step for a two-step dissolve.
    CHECK(close(published[0].second, 1.0F));
    // The final step completes the transition and frees the slot.
    CHECK(!state.slot_pending(slot));
    CHECK(state.pending_count() == 0U);
    CHECK(state.reserve(5U) == 0U);
}

void
test_three_step_lifetime_and_reuse() {
    BlendState state(Config{false, 3U});
    const std::size_t first = state.reserve(0U);
    CHECK(first == 0U);
    CHECK(state.commit(first));
    // The second slot is still free while the first is pending.
    const std::size_t second = state.reserve(1U);
    CHECK(second == 1U);
    CHECK(state.commit(second));
    CHECK(state.pending_count() == 2U);
    CHECK(state.reserve(2U) == BlendState::kNoSlot);

    std::vector<float> weights;
    CHECK(state.advance([&](const std::size_t, const std::size_t, const float weight) {
        weights.push_back(weight);
        return true;
    }));
    CHECK(weights.size() == 2U);
    // Step zero was published at face end, so the first advance is step one.
    CHECK(close(weights[0], 0.43F / 0.58F));
    CHECK(close(weights[1], 0.43F / 0.58F));
    CHECK(state.pending_count() == 2U);

    weights.clear();
    CHECK(state.advance([&](const std::size_t, const std::size_t, const float weight) {
        weights.push_back(weight);
        return true;
    }));
    CHECK(weights.size() == 2U);
    CHECK(close(weights[0], 1.0F));
    CHECK(close(weights[1], 1.0F));
    CHECK(state.pending_count() == 0U);
    // Both slots are reusable after completion.
    CHECK(state.reserve(4U) == 0U);
    CHECK(state.reserve(5U) == 1U);
}

void
test_cancel_reservation_and_reset() {
    BlendState state(Config{false, 3U});
    const std::size_t slot = state.reserve(2U);
    CHECK(state.cancel_reservation(slot));
    CHECK(!state.slot_reserved(slot));
    CHECK(!state.slot_pending(slot));
    // Cancelling a non-reserved slot is a no-op.
    CHECK(!state.cancel_reservation(slot));
    CHECK(!state.cancel_reservation(BlendState::kNoSlot));

    const std::size_t pending = state.reserve(1U);
    CHECK(state.commit(pending));
    CHECK(state.pending_count() == 1U);
    state.reset();
    CHECK(state.pending_count() == 0U);
    CHECK(!state.slot_pending(pending));
    CHECK(state.reserve(0U) == 0U);
}

void
test_invalid_face_rejected() {
    BlendState state(Config{false, 2U});
    CHECK(state.reserve(kFaceCount) == BlendState::kNoSlot);
    CHECK(state.reserve(99U) == BlendState::kNoSlot);
    CHECK(state.slot_face(BlendState::kNoSlot) == kFaceCount);
}

void
test_advance_without_pending_is_noop() {
    BlendState state(Config{false, 3U});
    std::size_t calls = 0U;
    CHECK(state.advance([&](const std::size_t, const std::size_t, const float) {
        ++calls;
        return true;
    }));
    CHECK(calls == 0U);
}

// A failed composite must stop the advance and leave the slot pending so its texture is not reused.
// This is the bookkeeping half of the publication fallback.
void
test_failed_publish_keeps_slot_pending() {
    BlendState state(Config{false, 3U});
    const std::size_t first = state.reserve(0U);
    CHECK(state.commit(first));
    const std::size_t second = state.reserve(1U);
    CHECK(state.commit(second));

    std::size_t calls = 0U;
    const bool ok = state.advance([&](const std::size_t, const std::size_t, const float) {
        ++calls;
        return false;
    });
    CHECK(!ok);
    // The first attempted composite failed, so no slot advanced.
    CHECK(calls == 1U);
    CHECK(state.pending_count() == 2U);
    CHECK(state.slot_pending(first));
    CHECK(state.slot_pending(second));
    // A later successful advance still completes both transitions.
    CHECK(state.advance([&](const std::size_t, const std::size_t, const float) { return true; }));
    CHECK(state.pending_count() == 2U);
    CHECK(state.advance([&](const std::size_t, const std::size_t, const float) { return true; }));
    CHECK(state.pending_count() == 0U);
}

void
test_live_step_reconfiguration() {
    BlendState state(Config{true, 6U});
    const auto pending = state.reserve(4U);
    CHECK(state.commit(pending));
    const auto reserved = state.reserve(0U);
    CHECK(state.slot_reserved(reserved));
    CHECK(!state.set_steps(6U));
    CHECK(state.pending_count() == 1U); // unchanged does not cancel
    CHECK(state.set_steps(2U));
    CHECK(state.steps() == 2U);
    CHECK(state.slot_count() == 1U);
    CHECK(state.pending_count() == 0U);
    CHECK(!state.slot_reserved(reserved));
    unsigned publications = 0U;
    CHECK(state.advance([&](std::size_t, std::size_t, float) {
        ++publications;
        return true;
    }));
    CHECK(publications == 0U); // canceled sources never publish
    CHECK(state.reserve(2U) == 0U);
    CHECK(state.set_steps(4U));
    CHECK(state.steps() == 4U);
    CHECK(state.slot_count() == 3U);
    CHECK(state.reserve(3U) == 0U);
    CHECK(state.set_steps(99U)); // invalid normalizes to 2
    CHECK(state.steps() == 2U);
}

} // namespace

int
main() {
    mwperffix::tests::disable_windows_crash_dialogs();
    test_extended_cycle();
    test_live_step_reconfiguration();
    test_same_face_capture_finalizes_old_blend();
    test_drop_pending_face_without_publication();
    test_weighted_single_face_retarget_sequence();
    test_config_normalization();
    test_blend_curve();
    test_slot_count();
    test_reserve_commit_advance_two_steps();
    test_three_step_lifetime_and_reuse();
    test_cancel_reservation_and_reset();
    test_invalid_face_rejected();
    test_advance_without_pending_is_noop();
    test_failed_publish_keeps_slot_pending();
    std::puts("reflection blending tests passed");
}
