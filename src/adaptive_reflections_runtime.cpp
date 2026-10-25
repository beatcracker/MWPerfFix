#include "adaptive_reflections_runtime_private.hpp"

#include "mwperffix/relay.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <d3d9.h>
#include <limits>
#include <memory>
#include <span>
#include <utility>
#include <vector>
#include <windows.h>

namespace mwperffix::adaptive {
namespace runtime_detail {

publication::Checks g_publications;

[[nodiscard]] void *
read_pointer(const std::uintptr_t base, const std::uintptr_t preferred) noexcept {
    void *value = nullptr;
    std::memcpy(&value, reinterpret_cast<const void *>(base + rva(preferred)), sizeof(value));
    return value;
}

[[nodiscard]] std::uint32_t
read_u32(const std::uintptr_t base, const std::uintptr_t preferred) noexcept {
    std::uint32_t value = 0;
    std::memcpy(&value, reinterpret_cast<const void *>(base + rva(preferred)), sizeof(value));
    return value;
}

void
write_pointer(const std::uintptr_t base, const std::uintptr_t preferred, void *const value) noexcept {
    std::memcpy(reinterpret_cast<void *>(base + rva(preferred)), &value, sizeof(value));
}

void
write_u32(const std::uintptr_t base, const std::uintptr_t preferred, const std::uint32_t value) noexcept {
    std::memcpy(reinterpret_cast<void *>(base + rva(preferred)), &value, sizeof(value));
}

BlendRuntime::BlendRuntime(const blending::Config config, const LogFunction log, void *const log_context) noexcept
    : config_(blending::normalize_config(config)), state_(config_), log_(log), log_context_(log_context) {
}

bool
BlendRuntime::requested() const noexcept {
    return config_.enabled;
}

void
BlendRuntime::disable() noexcept {
    config_.enabled = false;
    cancel();
}

bool
BlendRuntime::failed() const noexcept {
    return failed_;
}

bool
BlendRuntime::active() const noexcept {
    return active_;
}

bool
BlendRuntime::bypassed() const noexcept {
    return mode_ == blending::Mode::off;
}

std::uint32_t
BlendRuntime::steps() const noexcept {
    return config_.steps;
}

void
BlendRuntime::set_base(const std::uintptr_t base) noexcept {
    base_ = base;
}

blending::Mode
BlendRuntime::mode() const noexcept {
    return mode_;
}

bool
BlendRuntime::toggle() noexcept {
    if (!config_.enabled || failed_)
        return false;
    mode_ = blending::next_mode(mode_);
    active_ = false;
    cancel();
    return true;
}

bool
BlendRuntime::set_steps(const std::uint32_t requested) noexcept {
    if (!config_.enabled || failed_)
        return false;
    const auto normalized = blending::normalize_config({true, requested}).steps;
    if (normalized == config_.steps)
        return false;
    cancel();
    // Before face redirection; the next eligible frame allocates lazily.
    release();
    config_.steps = normalized;
    static_cast<void>(state_.set_steps(normalized));
    return true;
}

#if defined(MWPERFFIX_RUNTIME_TEST)
bool
BlendRuntime::test_resources_ready() const noexcept {
    return resources_ready_;
}

std::size_t
BlendRuntime::test_pending_count() const noexcept {
    return state_.pending_count();
}

bool
BlendRuntime::test_weighted_single_face() const noexcept {
    return weighted_single_face_;
}
#endif

void
BlendRuntime::begin_frame(const bool gameplay, const bool adaptive_enabled) noexcept {
#ifdef MWPERFFIX_DIAGNOSTICS
    const BlendTimingScope diagnostic_scope(diagnostic_timing, BlendPhase::begin_frame, &qpc_now, active_, failed_);
#endif
    active_ = false;
    weighted_single_face_ = false;
    configured_eligible_ = false;
    if (!config_.enabled || failed_ || mode_ == blending::Mode::off || base_ == 0U)
        return;
    if (!gameplay) {
        cancel();
        return;
    }
    const std::uint32_t configured_mode = read_u32(base_, kConfiguredMode);
    if (configured_mode != 1U && configured_mode != 3U && !(adaptive_enabled && configured_mode == 2U)) {
        cancel();
        return;
    }
    configured_eligible_ = true;
    if (!resources_ready_)
        allocate();
}

void
BlendRuntime::select_schedule(const Schedule schedule) noexcept {
#ifdef MWPERFFIX_DIAGNOSTICS
    const BlendTimingScope diagnostic_scope(diagnostic_timing, BlendPhase::select_schedule, &qpc_now, active_, failed_);
#endif
    weighted_single_face_ = false;
    if (!config_.enabled || failed_ || mode_ == blending::Mode::off || !configured_eligible_)
        return;
    if (schedule != Schedule::native_single && schedule != Schedule::weighted_single_face) {
        cancel();
        active_ = false;
        return;
    }
    // The compositor restores the target described by 00982A20.
    // Do not call it while that descriptor is unset.
    if (read_pointer(base_, kCurrentTarget) == nullptr) {
        cancel();
        active_ = false;
        return;
    }
    active_ = true;
    weighted_single_face_ = schedule == Schedule::weighted_single_face;
}

void
BlendRuntime::begin_face(const std::size_t face, SavedContext *const context) noexcept {
#ifdef MWPERFFIX_DIAGNOSTICS
    const BlendTimingScope diagnostic_scope(diagnostic_timing, BlendPhase::begin_face, &qpc_now, active_, failed_);
#endif
    if (!active_ || failed_ || !resources_ready_ || context == nullptr || face >= kFaceCount)
        return;
    const std::size_t physical_face = kViewIndexToNativeFace[face];
    const std::uintptr_t view = kFirstView + face * kViewStride;
    void *const native = read_pointer(base_, view + kViewDescriptorOffset);
    void *const native_color = native != nullptr ? read_pointer_from(native, 8U) : nullptr;

    if (weighted_single_face_) {
        // Check the replacement target before retiring its old source.
        // An invalid callback must leave an existing fade untouched.
        const auto *const descriptor = static_cast<const TargetDescriptor *>(native);
        if (native_color == nullptr || descriptor->depth == nullptr || descriptor->width == 0U ||
            descriptor->height == 0U)
            return;
        const std::size_t pending = state_.pending_slot_for_face(physical_face);
        if (pending != blending::BlendState::kNoSlot) {
            // Do not abandon a valid fade for a destination that cannot accept a replacement.
            // The source is reusable after dropping its pending ownership, not before.
            const SlotResources &old = slots_[pending];
            if (old.texture == nullptr || old.surface == nullptr || old.depth == nullptr) {
                fail("Car reflection blending: disabled (retarget source unavailable)");
                return;
            }
            static_cast<void>(state_.drop_pending_face(physical_face));
        }
    }

    advance();
    if (failed_)
        return;
    if (!weighted_single_face_ &&
        !state_.finish_face(physical_face, [this](const std::size_t slot, const std::size_t, const float weight) {
            return publish(slot, weight);
        })) {
        fail(
            "Car reflection blending: disabled (same-face final publication "
            "failed)"
        );
        return;
    }
    if (native == nullptr || native_color == nullptr)
        return;
    const std::size_t slot = state_.reserve(physical_face);
    if (slot == blending::BlendState::kNoSlot)
        return;
    SlotResources &resources = slots_[slot];
    resources.native_descriptor = native;
    resources.native_color = native_color;
    if (resources.texture == nullptr || resources.surface == nullptr || resources.depth == nullptr) {
        static_cast<void>(state_.cancel_reservation(slot));
        fail("Car reflection blending: disabled (temporary target unavailable)");
        return;
    }
    std::memcpy(&resources.descriptor, native, sizeof(TargetDescriptor));
    resources.descriptor.color = resources.surface;
    resources.descriptor.depth = resources.depth;
    // Never sample and render into the same resource.
    if (!unbind_texture(resources.texture)) {
        static_cast<void>(state_.cancel_reservation(slot));
        fail("Car reflection blending: disabled (temporary texture unbind failed)");
        return;
    }
    context->eax = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&resources.descriptor));
}

void
BlendRuntime::end_face(const std::size_t face, SavedContext *const context) noexcept {
#ifdef MWPERFFIX_DIAGNOSTICS
    const BlendTimingScope diagnostic_scope(diagnostic_timing, BlendPhase::end_face, &qpc_now, active_, failed_);
#endif
    static_cast<void>(context);
    if (!active_ || failed_ || face >= kFaceCount)
        return;
    for (std::size_t slot = 0; slot < state_.slot_count(); ++slot) {
        if (!state_.slot_reserved(slot) || state_.slot_face(slot) != kViewIndexToNativeFace[face])
            continue;
        SlotResources &resources = slots_[slot];
        if (resources.native_descriptor == nullptr) {
            static_cast<void>(state_.cancel_reservation(slot));
            return;
        }
        // The native face helper bound the temporary target.
        // Restore the actual device target before any publication call can fail early.
        write_pointer(base_, kCurrentTarget, resources.native_descriptor);
        write_u32(base_, kCurrentEffect, 0U);
        if (!restore_target(resources.native_descriptor)) {
            static_cast<void>(state_.cancel_reservation(slot));
            fail(
                "Car reflection blending: disabled (native target restoration "
                "failed)"
            );
            return;
        }
        if (!publish(slot, blending::mode_step_weight(mode_, 0U, config_.steps))) {
            static_cast<void>(state_.cancel_reservation(slot));
            fail("Car reflection blending: disabled (publication failed)");
            return;
        }
        if (!state_.commit(slot)) {
            static_cast<void>(state_.cancel_reservation(slot));
            return;
        }
        return;
    }
}

void
BlendRuntime::cancel() noexcept {
    state_.reset();
}

void
BlendRuntime::release() noexcept {
    for (SlotResources &resources : slots_) {
        if (resources.surface != nullptr)
            static_cast<IDirect3DSurface9 *>(resources.surface)->Release();
        if (resources.texture != nullptr)
            static_cast<IDirect3DTexture9 *>(resources.texture)->Release();
        if (resources.depth != nullptr)
            static_cast<IDirect3DSurface9 *>(resources.depth)->Release();
        resources = {};
    }
    resources_ready_ = false;
}

void *
BlendRuntime::read_pointer_from(void *const object, const std::size_t offset) const noexcept {
    void *value = nullptr;
    std::memcpy(&value, static_cast<const std::uint8_t *>(object) + offset, sizeof(value));
    return value;
}

void
BlendRuntime::log(const char *const text) noexcept {
    if (log_ != nullptr)
        log_(log_context_, text);
}

void
BlendRuntime::fail(const char *const reason) noexcept {
    failed_ = true;
    active_ = false;
    cancel();
    log(reason);
}

bool
BlendRuntime::restore_target(void *const target) noexcept {
    auto *const device = static_cast<IDirect3DDevice9 *>(read_pointer(base_, kDevice));
    if (device == nullptr || target == nullptr)
        return false;
    const auto *const descriptor = static_cast<const TargetDescriptor *>(target);
    if (descriptor->color == nullptr || descriptor->width == 0U || descriptor->height == 0U)
        return false;
    const D3DVIEWPORT9 viewport{0U, 0U, descriptor->width, descriptor->height, 0.0F, 1.0F};
    const bool color_ok = SUCCEEDED(device->SetRenderTarget(0U, static_cast<IDirect3DSurface9 *>(descriptor->color)));
    const bool depth_ok =
        SUCCEEDED(device->SetDepthStencilSurface(static_cast<IDirect3DSurface9 *>(descriptor->depth)));
    const bool viewport_ok = SUCCEEDED(device->SetViewport(&viewport));
    return color_ok && depth_ok && viewport_ok;
}

void
BlendRuntime::allocate() noexcept {
    auto *const device = static_cast<IDirect3DDevice9 *>(read_pointer(base_, kDevice));
    if (device == nullptr)
        return;
    void *const native = read_pointer(base_, kFirstView + kViewDescriptorOffset);
    if (native == nullptr)
        return;
    void *const color = read_pointer_from(native, 8U);
    void *const depth = read_pointer_from(native, 12U);
    if (color == nullptr || depth == nullptr)
        return;
    D3DSURFACE_DESC color_desc{};
    D3DSURFACE_DESC depth_desc{};
    if (FAILED(static_cast<IDirect3DSurface9 *>(color)->GetDesc(&color_desc)) || color_desc.Width == 0U ||
        color_desc.Height == 0U)
        return;
    if (FAILED(static_cast<IDirect3DSurface9 *>(depth)->GetDesc(&depth_desc)))
        return;
    for (std::size_t slot = 0; slot < state_.slot_count(); ++slot) {
        SlotResources &resources = slots_[slot];
        IDirect3DTexture9 *texture = nullptr;
        if (device->CreateTexture(
                color_desc.Width, color_desc.Height, 1U, D3DUSAGE_RENDERTARGET, color_desc.Format, D3DPOOL_DEFAULT,
                &texture, nullptr
            ) < 0 ||
            texture == nullptr) {
            fail(
                "Car reflection blending: disabled (temporary texture allocation "
                "failed)"
            );
            return;
        }
        resources.texture = texture;
        IDirect3DSurface9 *surface = nullptr;
        if (texture->GetSurfaceLevel(0U, &surface) < 0 || surface == nullptr) {
            fail("Car reflection blending: disabled (temporary surface unavailable)");
            return;
        }
        resources.surface = surface;
        IDirect3DSurface9 *depth_surface = nullptr;
        if (device->CreateDepthStencilSurface(
                color_desc.Width, color_desc.Height, depth_desc.Format, depth_desc.MultiSampleType,
                depth_desc.MultiSampleQuality, TRUE, &depth_surface, nullptr
            ) < 0 ||
            depth_surface == nullptr) {
            fail(
                "Car reflection blending: disabled (temporary depth allocation "
                "failed)"
            );
            return;
        }
        resources.depth = depth_surface;
    }
    resources_ready_ = true;
}

void
BlendRuntime::advance() noexcept {
    const bool ok = state_.advance(
        [this](const std::size_t slot, const std::size_t face, const float weight) {
            static_cast<void>(face);
            return publish(slot, weight);
        },
        mode_
    );
    if (!ok)
        fail("Car reflection blending: disabled (publication failed)");
}

bool
BlendRuntime::publish(const std::size_t slot, const float weight) noexcept {
    auto *const device = static_cast<IDirect3DDevice9 *>(read_pointer(base_, kDevice));
    if (device == nullptr || slot >= state_.slot_count())
        return false;
    SlotResources &resources = slots_[slot];
    if (resources.texture == nullptr || resources.native_color == nullptr)
        return false;
    publication::Scope publication(g_publications);
    if (!publication.active())
        return false;
    // The filter wrapper sets four sampler states.
    // Its end method is empty.
    // Reject a substituted wrapper before invoking it.
    void *const effect = read_pointer(base_, kCompositorFilter);
    if (effect == nullptr ||
        read_pointer_from(effect, 0U) != reinterpret_cast<void *>(base_ + rva(kCompositorFilterVtable)))
        return false;
    std::uint32_t byte = static_cast<std::uint32_t>(weight * 255.0F + 0.5F);
    if (byte > 255U)
        byte = 255U;
    const std::uint32_t packed = byte | (byte << 8U) | (byte << 16U) | (byte << 24U);
    if (FAILED(device->SetRenderState(D3DRS_BLENDFACTOR, packed)))
        return false;
    void *source = resources.texture;
    const std::uint32_t white = 0xFFFFFFFFU;
    const auto compositor = reinterpret_cast<CompositorFn>(base_ + rva(kCompositor));
    void *const target = read_pointer(base_, kCurrentTarget);
    if (target == nullptr)
        return false;
    const long result = compositor(
        resources.native_color, 1, &source, &white, 1, static_cast<int>(kBlendFactorSource),
        static_cast<int>(kBlendFactorDestination), 0xFFFFFFFFU
    );
    // Capture discarded setup/draw results and independently check all three target bindings
    // before allowing the transition to advance.
    const bool restored = restore_target(target);
    return result >= 0 && restored && publication.accepted();
}

bool
BlendRuntime::unbind_texture(void *const texture) noexcept {
    auto *const device = static_cast<IDirect3DDevice9 *>(read_pointer(base_, kDevice));
    if (device == nullptr || texture == nullptr)
        return false;
    for (unsigned stage = 0; stage < kSamplerStages; ++stage) {
        IDirect3DBaseTexture9 *bound = nullptr;
        if (FAILED(device->GetTexture(stage, &bound)))
            return false;
        const bool unbound = bound != texture || SUCCEEDED(device->SetTexture(stage, nullptr));
        // A successful GetTexture with a non-null result acquires a ref.
        if (bound != nullptr)
            bound->Release();
        if (!unbound)
            return false;
    }
    return true;
}

void
State::state_change_thunk(void *const context, const StateChange &change) noexcept {
    static_cast<State *>(context)->on_state_change(change);
}

#if defined(MWPERFFIX_DIAGNOSTICS)
void
State::frame_complete_thunk(void *const context, const FrameObservation &observation) noexcept {
    State &state = *static_cast<State *>(context);
    if (state.downstream_observer.frame_complete != nullptr) {
        FrameObservation enriched = observation;
        const auto &timing = state.blend.diagnostic_timing;
        enriched.blend_timing_valid = timing.valid && observation.timing_valid;
        if (enriched.blend_timing_valid)
            enriched.blend_qpc = timing.ticks;
        enriched.blend_mode =
            state.blend.requested() && !state.blend.failed() ? static_cast<std::uint32_t>(state.blend.mode()) : 0U;
        enriched.blend_steps = state.blend.steps();
        enriched.blend_active = timing.active;
        enriched.blend_failed = timing.failed || state.blend.failed();
        state.downstream_observer.frame_complete(state.downstream_observer.context, enriched);
    }
}
#endif

Observer
State::make_observer(State *const state, const Observer downstream, const bool feedback_enabled) noexcept {
    Observer result{};
    // No callback for quiet release operation; diagnostic consumers still receive all state changes
    // independently of on-screen feedback.
    if (feedback_enabled || downstream.state_change != nullptr)
        result.state_change = &State::state_change_thunk;
#ifdef MWPERFFIX_DIAGNOSTICS
    if (downstream.frame_complete != nullptr)
        result.frame_complete = &State::frame_complete_thunk;
#endif
    result.context = state;
    return result;
}

State::State(
    const std::uintptr_t image, const std::uint64_t frequency, const ReflectionConfig &settings,
    const Observer observer, const LogFunction log, void *const log_context
) noexcept
    : base(image), downstream_observer(observer),
      screen_messages(image, frequency, settings.notifications_enabled, log, log_context),
      controller(frequency, settings.scheduler, make_observer(this, observer, settings.notifications_enabled)),
      blend(settings.blending, log, log_context), controller_enabled(settings.adaptive_enabled),
      controller_installed(settings.adaptive_enabled), hotkey_state(settings.hotkeys), log_function(log),
      log_context(log_context), renderer_clock(frequency) {
    blend.set_base(image);
#ifdef MWPERFFIX_DIAGNOSTICS
    observing_faces.store(settings.adaptive_enabled, std::memory_order_relaxed);
#endif
}

void
State::on_state_change(const StateChange &change) noexcept {
    if (screen_messages.enabled() && controller_enabled) {
        const std::uint32_t configured = read_u32(base, kConfiguredMode);
        const bool gameplay = read_u32(base, kCubemapGameplayGate) == 6U;
        const std::string_view reason =
            notifications::adaptive_reason_label(change.reason, change.previous_budget, change.new_budget);
        if (gameplay && configured >= 1U && configured <= 3U && !reason.empty()) {
            pending_adaptive_transition = {configured, change.previous_budget, change.new_budget, change.reason, true};
        }
    }

    if (downstream_observer.state_change != nullptr) {
        downstream_observer.state_change(downstream_observer.context, change);
    }
}

void
State::resolve_adaptive_transition(
    const std::uint32_t configured, const bool gameplay, const Selection &selection
) noexcept {
    if (!pending_adaptive_transition.valid)
        return;
    if (!gameplay || configured < 1U || configured > 3U) {
        pending_adaptive_transition = {};
        return;
    }
    if (!selection.custom || static_cast<std::uint32_t>(selection.budget) != pending_adaptive_transition.new_budget) {
        return;
    }
    if (configured != pending_adaptive_transition.configured_mode) {
        pending_adaptive_transition = {};
        return;
    }

    const notifications::AdaptiveSnapshot snapshot{
        pending_adaptive_transition.previous_budget, pending_adaptive_transition.new_budget,
        pending_adaptive_transition.reason
    };

    screen_messages.post_adaptive(snapshot);
    pending_adaptive_transition = {};
}

void
State::settings_snapshot() const noexcept {
    std::array<char, 160> line{};
    std::snprintf(
        line.data(), line.size(), "LiveTuning: Adaptive=%s, TargetFPS=%u, Blending=%s, BlendSteps=%u",
        controller_enabled ? "on" : "off", controller.target_fps(),
        blend.requested() && !blend.failed() && !blend.bypassed() ? "on" : "off", blend.steps()
    );
    log(line.data());
}

void
State::apply_hotkey_sample(const bool foreground, const std::array<bool, hotkeys::kCount> &down) noexcept {
    const hotkeys::Actions actions = hotkey_state.sample(foreground, down);
    if (actions.toggle_adaptive && controller_installed) {
        controller_enabled = !controller_enabled;
        controller.suspend();
        pending_adaptive_transition = {};
        screen_messages.clear_adaptive();
        blend.cancel();
#ifdef MWPERFFIX_DIAGNOSTICS
        observing_faces.store(controller_enabled, std::memory_order_relaxed);
        foreign_face_callbacks.store(0U, std::memory_order_relaxed);
        unknown_face_indices.store(0U, std::memory_order_relaxed);
#endif
        screen_messages.post_text(
            controller_enabled ? "Adaptive reflections: ON (automatic)" : "Adaptive reflections: OFF (game setting)"
        );
        settings_snapshot();
    }
    if (controller_installed) {
        const auto adjust = [&](const bool increase) noexcept {
            const std::uint32_t value = controller.target_fps();
            const std::uint32_t next = increase ? (value > kMaximumTargetFps - 5U ? kMaximumTargetFps : value + 5U)
                                                : (value < kMinimumTargetFps + 5U ? kMinimumTargetFps : value - 5U);
            if (controller.set_target_fps(next)) {
                std::array<char, 64> message{};
                std::snprintf(message.data(), message.size(), "Adaptive target: %u FPS", next);
                screen_messages.post_text(message.data());
                settings_snapshot();
            }
        };
        if (actions.target_down)
            adjust(false);
        if (actions.target_up)
            adjust(true);
    }
    if (actions.toggle_blending && blend.toggle()) {
        std::array<char, 64> message{};
        if (notifications::format_blending_mode(blend.mode(), blend.steps(), message))
            screen_messages.post_text(message.data());
        settings_snapshot();
    }
    if (actions.cycle_steps && blend.requested() && !blend.failed()) {
        const std::uint32_t next =
            blend.steps() == blending::kMaximumSteps ? blending::kMinimumSteps : blend.steps() + 1U;
        if (blend.set_steps(next)) {
            std::array<char, 64> message{};
            if (blend.bypassed())
                std::snprintf(
                    message.data(), message.size(), "Reflection blending: off (%u steps ready)", blend.steps()
                );
            else
                static_cast<void>(notifications::format_blending_mode(blend.mode(), blend.steps(), message));
            screen_messages.post_text(message.data());
            settings_snapshot();
        }
    }
}

void
State::begin_renderer(const std::uint64_t ticks, const std::uint32_t frame) noexcept {
#ifdef MWPERFFIX_DIAGNOSTICS
    blend.diagnostic_timing.reset();
#endif
    poll_hotkeys();
    screen_messages.update(ticks);
    const RendererTiming timing = renderer_clock.observe(ticks);
    if (controller_enabled)
        controller.begin_renderer(ticks, frame, timing);
    if (timing.gap || timing.repeated || timing.reversed)
        blend.cancel();
    const std::uint32_t gameplay_gate = read_u32(base, kCubemapGameplayGate);
    const std::uint32_t configured_mode = read_u32(base, kConfiguredMode);
    const bool gameplay = gameplay_gate == 6U;
    if (pending_adaptive_transition.valid && (!gameplay || configured_mode < 1U || configured_mode > 3U)) {
        pending_adaptive_transition = {};
    }
    blend.begin_frame(gameplay, controller_enabled);
}

void
State::reset_device(const std::uint64_t ticks, const std::uint32_t frame) noexcept {
    pending_adaptive_transition = {};
    screen_messages.invalidate();
    if (controller_enabled)
        controller.invalidate_contents(ticks, frame);
    blend.cancel();
    blend.release();
}

std::uintptr_t
State::select_mask(
    const std::uintptr_t native_mask, const std::uint32_t configured, const bool gameplay, const std::uint32_t frame,
    const std::uint64_t ticks
) noexcept {
    // Native mode delegates the exact game-provided pointer for UI 0/1/2/3.
    if (!controller_enabled) {
        blend.select_schedule(classify_native_schedule(configured));
        return native_mask;
    }

    if (pending_adaptive_transition.valid && (!gameplay || configured < 1U || configured > 3U)) {
        pending_adaptive_transition = {};
    }

    if (controller_enabled && controller.active_frame(frame)) {
        const Selection selection = controller.select(configured, gameplay, ticks);
        resolve_adaptive_transition(configured, gameplay, selection);
        if (selection.custom) {
            custom_mask_words = native_mask_words(selection.face_mask);
            // Full updates publish directly.
            // Weighted single-face updates may blend.
            const auto schedule =
                selection.budget == 1U ? Schedule::weighted_single_face : Schedule::native_full_update;
            blend.select_schedule(schedule);
            return reinterpret_cast<std::uintptr_t>(custom_mask_words.data());
        }
    }

    blend.select_schedule(classify_native_schedule(configured));
    return native_mask;
}

void
State::poll_hotkeys() noexcept {
    if (!hotkey_state.configured())
        return;

    const HWND foreground_window = GetForegroundWindow();
    DWORD foreground_process = 0U;
    if (foreground_window != nullptr)
        static_cast<void>(GetWindowThreadProcessId(foreground_window, &foreground_process));
    const bool foreground = foreground_window != nullptr && foreground_process == GetCurrentProcessId();

    const hotkeys::Config config = hotkey_state.config();
    const auto down = [](const std::uint32_t key) noexcept {
        return key != 0U && (GetAsyncKeyState(static_cast<int>(key)) & 0x8000) != 0;
    };
    const auto keys = config.keys();
    std::array<bool, hotkeys::kCount> pressed{};
    for (std::size_t i = 0; i < keys.size(); ++i)
        pressed[i] = down(keys[i]);
    apply_hotkey_sample(foreground, pressed);
}

void
State::log(const char *const text) const noexcept {
    if (log_function != nullptr)
        log_function(log_context, text);
}

namespace {
State *g_state = nullptr;
#if defined(MWPERFFIX_RUNTIME_TEST)
std::unique_ptr<State> g_test_owned_state;
#endif
} // namespace

#if defined(MWPERFFIX_RUNTIME_TEST)
State *
test_bound_state() noexcept {
    return g_state;
}
ScopedTestState::ScopedTestState(State &state) noexcept : previous_(g_state) {
    g_state = &state;
}
ScopedTestState::~ScopedTestState() {
    g_state = previous_;
}
void
destroy_installed_test_state() noexcept {
    if (g_state == g_test_owned_state.get())
        g_state = nullptr;
    g_test_owned_state.reset();
}
#endif

[[nodiscard]] std::uint64_t
qpc_now() noexcept {
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    return static_cast<std::uint64_t>(now.QuadPart);
}

[[nodiscard]] bool
belongs_to_producer(State &state, const Site site) noexcept {
    const DWORD thread = GetCurrentThreadId();
    if (site == Site::renderer_begin && state.producer_thread == 0U)
        state.producer_thread = thread;
    return state.producer_thread != 0U && state.producer_thread == thread;
}

[[nodiscard]] std::size_t
face_index(const State &state, const std::uint32_t esi) noexcept {
    const std::uintptr_t first = state.base + rva(kCubemapViews);
    const std::uintptr_t end = first + kFaceCount * kViewStride;
    const std::uintptr_t address = static_cast<std::uintptr_t>(esi);
    if (address < first || address >= end || (address - first) % kViewStride != 0U)
        return kFaceCount;
    const std::size_t index = (address - first) / kViewStride;
    return index < kFaceCount ? index : kFaceCount;
}

extern "C" void __cdecl
mwperffix_adaptive_dispatch(const std::uint32_t site_value, SavedContext *const context) noexcept {
    State *const state = g_state;
    if (state == nullptr || context == nullptr)
        return;
    const Site site = static_cast<Site>(site_value);
    if (!belongs_to_producer(*state, site)) {
#ifdef MWPERFFIX_DIAGNOSTICS
        if (state->observing_faces.load(std::memory_order_relaxed) &&
            (site == Site::face_begin || site == Site::face_end))
            state->foreign_face_callbacks.fetch_add(1U, std::memory_order_relaxed);
#endif
        return;
    }

    switch (site) {
    case Site::renderer_begin:
        state->begin_renderer(qpc_now(), read_u32(state->base, kGameFrame));
        break;
    case Site::scheduler: {
        const std::uint32_t configured = read_u32(state->base, kConfiguredMode);
        const bool gameplay = read_u32(state->base, kCubemapGameplayGate) == 6U;
        const std::uint32_t frame = read_u32(state->base, kGameFrame);
        const std::uint64_t ticks = state->controller_enabled ? qpc_now() : 0U;
        context->eax = static_cast<std::uint32_t>(state->select_mask(context->eax, configured, gameplay, frame, ticks));
        break;
    }
    case Site::face_begin: {
        const std::size_t face = face_index(*state, context->esi);
#ifdef MWPERFFIX_DIAGNOSTICS
        if (state->controller_enabled && face == kFaceCount)
            state->unknown_face_indices.fetch_add(1U, std::memory_order_relaxed);
#endif
        // Advance older pending transitions and redirect this face's destination
        // before the face timing interval starts,
        // so compositor work stays outside face timing but inside renderer-work timing.
        state->blend.begin_face(face, context);
        if (state->controller_enabled && face < kFaceCount) {
            const auto ticks = qpc_now();
            state->controller.begin_face(kViewIndexToNativeFace[face], ticks);
        }
        break;
    }
    case Site::face_end: {
        const std::size_t face = face_index(*state, context->esi);
#ifdef MWPERFFIX_DIAGNOSTICS
        if (state->controller_enabled && face == kFaceCount)
            state->unknown_face_indices.fetch_add(1U, std::memory_order_relaxed);
#endif
        const std::uint64_t ticks = qpc_now();
        if (state->controller_enabled && face < kFaceCount)
            state->controller.end_face(kViewIndexToNativeFace[face], ticks);
        state->blend.end_face(face, context);
        break;
    }
    case Site::pre_present:
        if (state->controller_enabled)
            state->controller.before_present(qpc_now());
        break;
    case Site::post_present: {
        const bool present_succeeded = (context->eax & 0x80000000U) == 0U;
#ifdef MWPERFFIX_DIAGNOSTICS
        if (state->controller_enabled && state->controller.observation_active())
            state->controller.set_dispatch_rejections(
                state->foreign_face_callbacks.exchange(0U, std::memory_order_relaxed),
                state->unknown_face_indices.exchange(0U, std::memory_order_relaxed)
            );
#endif
        if (state->controller_enabled)
            state->controller.after_present(qpc_now(), present_succeeded);
        if (!present_succeeded) {
            state->blend.cancel();
        }
        break;
    }
    case Site::reset_begin:
        // Release owned resources before the native device Reset.
        // Adaptive timing state is touched only when the adaptive controller is active.
        state->reset_device(state->controller_enabled ? qpc_now() : 0U, read_u32(state->base, kGameFrame));
        break;
    }
}

void
append_u32(std::vector<std::uint8_t> &output, const std::uint32_t value) {
    for (unsigned shift = 0; shift < 32U; shift += 8U) {
        output.push_back(static_cast<std::uint8_t>(value >> shift));
    }
}

void
append_rel32(std::vector<std::uint8_t> &output, const std::uintptr_t instruction, const std::uintptr_t target) {
    append_u32(output, static_cast<std::uint32_t>(target - (instruction + 5U)));
}

void
append_observer(std::vector<std::uint8_t> &output, const std::uintptr_t relay, const Site site) {
    static constexpr std::array<std::uint8_t, 29> prefix{
        0x64,
        0xFF,
        0x35,
        0x34,
        0x00,
        0x00,
        0x00, // push fs:[34h]
        0x9C,
        0x60,
        0x8B,
        0xEC,
        0x81,
        0xEC,
        0x10,
        0x02,
        0x00,
        0x00,
        0x83,
        0xE4,
        0xF0,
        0x0F,
        0xAE,
        0x04,
        0x24, // aligned fxsave
        0x83,
        0xEC,
        0x08, // padding keeps the two-argument call 16-byte aligned
        0x55,
        0x68,
    };
    output.insert(output.end(), prefix.begin(), prefix.end());
    append_u32(output, static_cast<std::uint32_t>(site));
    output.push_back(0xE8);
    const std::uintptr_t call = relay + output.size() - 1U;
    append_rel32(output, call, reinterpret_cast<std::uintptr_t>(&mwperffix_adaptive_dispatch));
    static constexpr std::array<std::uint8_t, 18> suffix{
        0x83,
        0xC4,
        0x10,
        0x0F,
        0xAE,
        0x0C,
        0x24,
        0x8B,
        0xE5,
        0x61,
        0x9D,
        0x64,
        0x8F,
        0x05,
        0x34,
        0x00,
        0x00,
        0x00,
    };
    output.insert(output.end(), suffix.begin(), suffix.end());
}

void
append_replayed(
    std::vector<std::uint8_t> &output, const SiteDefinition &definition, const std::uintptr_t original_address,
    const std::uintptr_t relay
) {
    for (std::size_t offset = 0; offset < definition.expected.size();) {
        if (offset == definition.call_offset && definition.expected[offset] == 0xE8U) {
            const std::uint32_t displacement = static_cast<std::uint32_t>(definition.expected[offset + 1U]) |
                                               (static_cast<std::uint32_t>(definition.expected[offset + 2U]) << 8U) |
                                               (static_cast<std::uint32_t>(definition.expected[offset + 3U]) << 16U) |
                                               (static_cast<std::uint32_t>(definition.expected[offset + 4U]) << 24U);
            const std::uintptr_t target = original_address + offset + 5U + displacement;
            output.push_back(0xE8);
            append_rel32(output, relay + output.size() - 1U, target);
            offset += 5U;
        } else {
            output.push_back(definition.expected[offset]);
            ++offset;
        }
    }
}

[[nodiscard]] std::vector<std::uint8_t>
build_relay(const SiteDefinition &definition, const std::uintptr_t original_address, const std::uintptr_t relay) {
    std::vector<std::uint8_t> result;
    result.reserve(256U);
    if (definition.id == Site::scheduler) {
        // The takeover seam is the native mask-pointer LEA.
        // Preserve its arithmetic first so every inactive/fallback path
        // can return the exact vanilla pointer through saved EAX.
        append_replayed(result, definition, original_address, relay);
        append_observer(result, relay, definition.id);
    } else {
        append_observer(result, relay, definition.id);
        append_replayed(result, definition, original_address, relay);
    }
    result.push_back(0xE9);
    append_rel32(result, relay + result.size() - 1U, original_address + definition.expected.size());
    return result;
}

[[nodiscard]] std::vector<std::uint8_t>
branch_to(const std::uintptr_t source, const std::uintptr_t destination, const std::size_t size) {
    std::vector<std::uint8_t> result(size, 0x90U);
    result[0] = 0xE9U;
    const std::uint32_t displacement = static_cast<std::uint32_t>(destination - (source + 5U));
    for (unsigned shift = 0; shift < 32U; shift += 8U) {
        result[1U + shift / 8U] = static_cast<std::uint8_t>(displacement >> shift);
    }
    return result;
}

[[nodiscard]] bool
matches_or_log(
    const Memory &memory, const std::uintptr_t address, const std::uintptr_t preferred,
    const std::span<const std::uint8_t> expected, const char *const kind, const char *const name, const LogFunction log,
    void *const log_context
) noexcept {
    std::array<std::uint8_t, 144> actual{};
    std::array<char, 192> message{};
    if (expected.size() > actual.size() || !memory.read(address, std::span(actual).first(expected.size()))) {
        const int length = std::snprintf(
            message.data(), message.size(), "Adaptive car reflections: skipped (%s %s at %08llX unreadable)", kind,
            name, static_cast<unsigned long long>(preferred)
        );
        if (length > 0)
            log(log_context, message.data());
        return false;
    }
    std::size_t offset = 0;
    while (offset < expected.size() && actual[offset] == expected[offset])
        ++offset;
    if (offset == expected.size())
        return true;
    const int length = std::snprintf(
        message.data(), message.size(),
        "Adaptive car reflections: skipped (%s %s at %08llX+%zu "
        "expected %02X actual %02X)",
        kind, name, static_cast<unsigned long long>(preferred), offset, static_cast<unsigned>(expected[offset]),
        static_cast<unsigned>(actual[offset])
    );
    if (length > 0)
        log(log_context, message.data());
    return false;
}

} // namespace runtime_detail

using namespace runtime_detail;

Outcome
install(
    Memory &memory, const std::uintptr_t executable_base, const ReflectionConfig &settings, const LogFunction log,
    void *const log_context, const Observer observer
) noexcept {
    const auto &blending_config = settings.blending;
    const bool adaptive_enabled = settings.adaptive_enabled;
    try {
        const bool scheduler_requested = adaptive_enabled || blending_config.enabled;
        std::vector<SiteDefinition> sites;
        sites.reserve(kSites.size());
        for (const SiteDefinition &definition : kSites) {
            if (definition.id != Site::scheduler || scheduler_requested)
                sites.push_back(definition);
        }

        for (const auto &guard : kCommonGuards) {
            const std::uintptr_t address = executable_base + rva(guard.preferred);
            if (!matches_or_log(
                    memory, address, guard.preferred, guard.expected, "context", guard.name, log, log_context
                ))
                return Outcome::mismatch;
        }
        if (scheduler_requested) {
            for (const auto &guard : kSchedulerGuards) {
                const std::uintptr_t address = executable_base + rva(guard.preferred);
                if (!matches_or_log(
                        memory, address, guard.preferred, guard.expected, "scheduler context", guard.name, log,
                        log_context
                    ))
                    return Outcome::mismatch;
            }
        }
        for (const SiteDefinition &definition : sites) {
            const std::uintptr_t address = executable_base + rva(definition.preferred);
            if (!matches_or_log(
                    memory, address, definition.preferred, definition.expected, "site", definition.name, log,
                    log_context
                ))
                return Outcome::mismatch;
        }

        LARGE_INTEGER frequency{};
        if (QueryPerformanceFrequency(&frequency) == FALSE || frequency.QuadPart <= 0) {
            log(log_context, "Reflection runtime: failed (QPC unavailable)");
            return Outcome::failed;
        }

        blending::Config effective_blending = blending::normalize_config(blending_config);
        if (effective_blending.enabled) {
            for (const auto &guard : kBlendGuards) {
                const std::uintptr_t address = executable_base + rva(guard.preferred);
                if (!matches_or_log(
                        memory, address, guard.preferred, guard.expected, "blend context", guard.name, log, log_context
                    )) {
                    log(log_context, "Car reflection blending: skipped (native "
                                     "compositor contract not found)");
                    effective_blending.enabled = false;
                    break;
                }
            }
        }

        if (effective_blending.enabled) {
            for (const auto &call : publication::calls) {
                if (!matches_or_log(
                        memory, executable_base + rva(call.preferred), call.preferred, call.expected(),
                        "publication call", "native result", log, log_context
                    )) {
                    effective_blending.enabled = false;
                    log(log_context, "Car reflection blending: skipped (publication call modified)");
                    break;
                }
            }
        }

        ReflectionConfig effective_settings = settings;
        effective_settings.blending = effective_blending;
        auto state = std::make_unique<State>(
            executable_base, static_cast<std::uint64_t>(frequency.QuadPart), effective_settings, observer, log,
            log_context
        );

        std::vector<RelayPage> relays;
        std::vector<std::vector<std::uint8_t>> replacements;
        relays.reserve(sites.size());
        replacements.reserve(sites.size());
        for (const SiteDefinition &definition : sites) {
            relays.emplace_back(512U);
            RelayPage &relay = relays.back();
            const std::uintptr_t source = executable_base + rva(definition.preferred);
            if (relay.address() == 0U || !relay.seal(build_relay(definition, source, relay.address()))) {
                return Outcome::failed;
            }
            replacements.push_back(branch_to(source, relay.address(), definition.expected.size()));
        }

        PatchTransaction transaction(memory);
        for (std::size_t index = 0; index < sites.size(); ++index) {
            const SiteDefinition &definition = sites[index];
            if (!transaction.add(executable_base + rva(definition.preferred), definition.expected, replacements[index]))
                return Outcome::failed;
        }

        if (!transaction.validate()) {
            log(log_context, "Reflection runtime: skipped (expected instructions not found)");
            return Outcome::mismatch;
        }

        if (effective_blending.enabled) {
            const Outcome publication_outcome = g_publications.install(memory, executable_base);
            if (publication_outcome != Outcome::applied) {
                state->blend.disable();
                effective_blending.enabled = false;
                log(log_context, publication_outcome == Outcome::rollback_incomplete
                                     ? "Car reflection blending: skipped (publication "
                                       "checks rollback "
                                       "incomplete; restart required)"
                                     : "Car reflection blending: skipped (publication "
                                       "checks unavailable)");
            }
        }

        const ApplyResult result = transaction.apply();
        const Outcome outcome = result.applied
                                    ? Outcome::applied
                                    : (result.rollback_complete ? Outcome::failed : Outcome::rollback_incomplete);
        // An incomplete rollback can leave jumps into these pages.
        // They must survive even though unpublished state is destroyed and dispatch is inert.
        if (retains_relays(outcome)) {
            for (RelayPage &relay : relays)
                relay.retain_until_process_exit();
        }

        if (outcome == Outcome::applied) {

            // Startup publishes only after the checked transaction succeeds.
            // The installed relays and callbacks use this state until process exit.
            g_state = state.release();
#if defined(MWPERFFIX_RUNTIME_TEST)
            g_test_owned_state.reset(g_state);
#endif

            if (effective_blending.enabled) {
                std::array<char, 128U> message{};
                std::snprintf(
                    message.data(), message.size(), "Car reflection blending: enabled (native compositor, %u steps)",
                    static_cast<unsigned>(g_state->blend.steps())
                );
                log(log_context, message.data());
            }

            if (adaptive_enabled)
                log(log_context, "Adaptive car reflections: enabled (Native full-update / Weighted "
                                 "single-face)");
            else
                log(log_context, "Adaptive car reflections: disabled");
        } else {
            log(log_context, outcome == Outcome::rollback_incomplete
                                 ? "Reflection runtime: failed (rollback incomplete, restart "
                                   "required)"
                                 : "Reflection runtime: failed (no patches left installed)");
        }
        return outcome;
    } catch (...) {
        log(log_context, "Reflection runtime: failed (no patches left installed)");
        return Outcome::failed;
    }
}

} // namespace mwperffix::adaptive
