#include "screen_messages.hpp"

#include "adaptive_reflections_runtime_private.hpp"
#include <array>
#include <cstdio>
#include <limits>

namespace mwperffix::adaptive::runtime_detail {
namespace {
// Native frontend helpers used by the opt-in transient notification overlay.
#if !defined(MWPERFFIX_RUNTIME_TEST)
constexpr std::uintptr_t kFEngInstance = 0x0091CADCU;
constexpr std::uintptr_t kFEngPushNoControlPackage = 0x00516990U;
constexpr std::uintptr_t kFEPackageManagerGet = 0x00572070U;
constexpr std::uintptr_t kFEPackageManagerFindPackage = 0x00591D90U;
constexpr std::uintptr_t kFEPackageManagerSetUseIdleList = 0x0059ABE0U;
constexpr std::uintptr_t kFEPackageManagerSetPermanent = 0x00591E10U;
constexpr std::uintptr_t kFEHashUpper = 0x005AF1C0U;
constexpr std::uintptr_t kFEngFindString = 0x005251E0U;
constexpr std::uintptr_t kFEngSetInvisible = 0x00514C70U;
constexpr std::uintptr_t kFEngSetVisible = 0x00514CC0U;
constexpr std::uintptr_t kFEColorCreate = 0x005B2D50U;
constexpr std::uintptr_t kFEObjectSetColor = 0x005B8390U;
constexpr std::uintptr_t kFEPrintfString = 0x00515D70U;
constexpr std::uintptr_t kFEngSetTopLeft = 0x00524CC0U;
constexpr std::uintptr_t kFEngGetCenter = 0x00524EE0U;
constexpr std::uintptr_t kFEngSetCenter = 0x00525050U;
constexpr unsigned kScreenPrintfSlotCount = 64U;
constexpr unsigned kAdaptiveScreenPrintfFirstSlot = 60U;
constexpr unsigned kTransientScreenPrintfSlot = 63U;
// Centered under the rearview mirror, with fixed top edges.
constexpr float kTransientScreenPrintfY = -132.0F;
constexpr std::uint32_t kTransientScreenPrintfColor = 0xFF82D9FFU;
constexpr float kAdaptiveScreenPrintfY = -118.0F;
constexpr std::uint32_t kAdaptiveScreenPrintfColor = 0xFFFFAD55U;
#endif

} // namespace

template <typename Fn>
Fn
ScreenMessages::game_function(const std::uintptr_t preferred) const noexcept {
    return reinterpret_cast<Fn>(base_ + rva(preferred));
}

ScreenMessages::ScreenMessages(
    const std::uintptr_t image, const std::uint64_t frequency, const bool enabled, const LogFunction log,
    void *const log_context
) noexcept
    : transient_state_(enabled), adaptive_state_(enabled), base_(image),
      transient_duration_ticks_(
          frequency > std::numeric_limits<std::uint64_t>::max() / 2U ? std::numeric_limits<std::uint64_t>::max()
                                                                     : frequency * 2U
      ),
      adaptive_duration_ticks_(
          frequency > std::numeric_limits<std::uint64_t>::max() / 3U ? std::numeric_limits<std::uint64_t>::max()
                                                                     : frequency * 3U
      ),
      log_(log), log_context_(log_context) {
}

bool
ScreenMessages::enabled() const noexcept {
    return transient_state_.enabled();
}

void
ScreenMessages::post_text(const std::string_view value) noexcept {
    transient_state_.post_text(value);
}

void
ScreenMessages::clear_adaptive() noexcept {
    hide_adaptive();
    adaptive_state_.invalidate();
}

void
ScreenMessages::post_adaptive(const notifications::AdaptiveSnapshot &snapshot) noexcept {
    adaptive_state_.post(snapshot);
}

void
ScreenMessages::update(const std::uint64_t ticks) noexcept {
    if (!transient_state_.enabled() || failed_)
        return;

    if (transient_state_.expired(ticks)) {
        hide_transient();
        transient_state_.hidden();
    }
    if (adaptive_state_.expired(ticks)) {
        hide_adaptive();
        adaptive_state_.hidden();
    }

    const bool transient_pending = transient_state_.pending() != notifications::Message::none;
    const bool adaptive_pending = adaptive_state_.pending();
    if (!ready_ && (transient_pending || adaptive_pending || needs_clear_) && !initialize())
        return;

    if (transient_state_.pending() != notifications::Message::none) {
        const notifications::Message message = transient_state_.take_pending();
        if (message != notifications::Message::none)
            show_transient(message, ticks);
    }

    notifications::AdaptiveSnapshot snapshot{};
    if (adaptive_state_.take_pending(snapshot))
        show_adaptive(snapshot, ticks);
}

void
ScreenMessages::invalidate() noexcept {
    needs_clear_ = ready_ && (transient_state_.visible() || adaptive_state_.visible());
    transient_state_.invalidate();
    adaptive_state_.invalidate();
    ready_ = false;
    transient_string_ = nullptr;
    adaptive_string_ = nullptr;
}

#if defined(MWPERFFIX_RUNTIME_TEST)
notifications::Message
ScreenMessages::test_pending() const noexcept {
    return transient_state_.pending();
}

std::string_view
ScreenMessages::test_pending_text() const noexcept {
    return transient_state_.message_text(transient_state_.pending());
}

bool
ScreenMessages::test_visible() const noexcept {
    return transient_state_.visible();
}

bool
ScreenMessages::test_adaptive_pending() const noexcept {
    return adaptive_state_.pending();
}

bool
ScreenMessages::test_adaptive_visible() const noexcept {
    return adaptive_state_.visible();
}

const notifications::AdaptiveSnapshot &
ScreenMessages::test_adaptive_snapshot() const noexcept {
    return adaptive_state_.pending_snapshot();
}
#endif

bool
ScreenMessages::initialize() noexcept {
    if (ready_ || failed_ || base_ == 0U)
        return ready_;

#if defined(MWPERFFIX_RUNTIME_TEST)
    ready_ = true;
    return true;
#else
    void *const feng = read_pointer(base_, kFEngInstance);
    if (feng == nullptr)
        return false;

    const PackageManagerGetFn package_manager_get = game_function<PackageManagerGetFn>(kFEPackageManagerGet);
    void *package_manager = package_manager_get();
    if (package_manager == nullptr)
        return false;

    static constexpr char package_name[] = "ScreenPrintf.fng";
    const FindPackageFn find_package = game_function<FindPackageFn>(kFEPackageManagerFindPackage);
    void *package = find_package(package_manager, package_name);
    if (package == nullptr) {
        if (!push_attempted_) {
            push_attempted_ = true;
            const PushPackageFn push_package = game_function<PushPackageFn>(kFEngPushNoControlPackage);
            static_cast<void>(push_package(feng, package_name, 104));
        }
        package_manager = package_manager_get();
        if (package_manager == nullptr)
            return false;
        package = find_package(package_manager, package_name);
        if (package == nullptr) {
            fail("Notifications: disabled (ScreenPrintf.fng unavailable)");
            return false;
        }
    }

    const SetUseIdleListFn set_use_idle = game_function<SetUseIdleListFn>(kFEPackageManagerSetUseIdleList);
    const SetPermanentFn set_permanent = game_function<SetPermanentFn>(kFEPackageManagerSetPermanent);
    static_cast<void>(set_use_idle(package_manager, package_name, true));
    static_cast<void>(set_permanent(package_manager, package_name, 1));

    const HashUpperFn hash_upper = game_function<HashUpperFn>(kFEHashUpper);
    const FindStringFn find_string = game_function<FindStringFn>(kFEngFindString);
    const SetVisibilityFn set_invisible = game_function<SetVisibilityFn>(kFEngSetInvisible);

    // ScreenPrintf.fng ships with all 64 placeholder strings visible.
    // Match the game's established Extra Options initialization:
    // resolve and hide the complete pool before exposing our two reserved high slots.
    for (unsigned slot = 0U; slot < kScreenPrintfSlotCount; ++slot) {
        std::array<char, 32> object_name{};
        const int length = std::snprintf(object_name.data(), object_name.size(), "SCREENPRINTF%u", slot);
        if (length <= 0 || static_cast<std::size_t>(length) >= object_name.size()) {
            fail("Notifications: disabled (ScreenPrintf slot name overflow)");
            return false;
        }

        void *const object = find_string(package_name, hash_upper(object_name.data()));
        if (object != nullptr)
            static_cast<void>(set_invisible(object));
        if (slot == kAdaptiveScreenPrintfFirstSlot) {
            adaptive_string_ = object;
        } else if (slot == kTransientScreenPrintfSlot) {
            transient_string_ = object;
        }
    }

    if (transient_string_ == nullptr) {
        fail("Notifications: disabled (SCREENPRINTF63 unavailable)");
        return false;
    }
    if (adaptive_string_ == nullptr) {
        fail("Notifications: disabled (SCREENPRINTF60 unavailable)");
        return false;
    }

    ready_ = true;
    needs_clear_ = false;
    return true;
#endif
}

#if !defined(MWPERFFIX_RUNTIME_TEST)
void
ScreenMessages::render_message(
    void *const object, const char *const message, const float y, const std::uint32_t color_value
) noexcept {
    FEColor color{};
    const ColorCreateFn create_color = game_function<ColorCreateFn>(kFEColorCreate);
    const SetColorFn set_color = game_function<SetColorFn>(kFEObjectSetColor);
    static_cast<void>(create_color(&color, color_value));
    set_color(object, &color, 0);
    const PrintStringFn print_string = game_function<PrintStringFn>(kFEPrintfString);
    static_cast<void>(print_string(object, "%s", message));
    const SetTopLeftFn set_top_left = game_function<SetTopLeftFn>(kFEngSetTopLeft);
    const GetCenterFn get_center = game_function<GetCenterFn>(kFEngGetCenter);
    const SetCenterFn set_center = game_function<SetCenterFn>(kFEngSetCenter);
    set_top_left(object, 0.0F, y);
    float center_x = 0.0F;
    float center_y = 0.0F;
    get_center(object, &center_x, &center_y);
    set_center(object, 0.0F, center_y);
    const SetVisibilityFn set_visible = game_function<SetVisibilityFn>(kFEngSetVisible);
    static_cast<void>(set_visible(object));
}
#endif

#if !defined(MWPERFFIX_RUNTIME_TEST)
void
ScreenMessages::hide_message(void *const object) noexcept {
    if (ready_ && object != nullptr) {
        const SetVisibilityFn set_invisible = game_function<SetVisibilityFn>(kFEngSetInvisible);
        static_cast<void>(set_invisible(object));
    }
}
#endif

void
ScreenMessages::show_transient(const notifications::Message message, const std::uint64_t ticks) noexcept {
#if defined(MWPERFFIX_RUNTIME_TEST)
    static_cast<void>(message);
    transient_state_.shown(ticks, transient_duration_ticks_);
#else
    if (transient_string_ == nullptr)
        return;

    render_message(
        transient_string_, transient_state_.message_text(message).data(), kTransientScreenPrintfY,
        kTransientScreenPrintfColor
    );
    transient_state_.shown(ticks, transient_duration_ticks_);
#endif
}

void
ScreenMessages::show_adaptive(const notifications::AdaptiveSnapshot &snapshot, const std::uint64_t ticks) noexcept {
#if defined(MWPERFFIX_RUNTIME_TEST)
    static_cast<void>(snapshot);
    adaptive_state_.shown(ticks, adaptive_duration_ticks_);
#else
    notifications::AdaptiveText text{};
    if (!notifications::format_adaptive_snapshot(snapshot, text)) {
        fail("Notifications: disabled (adaptive snapshot formatting failed)");
        return;
    }

    if (adaptive_string_ == nullptr) {
        fail("Notifications: disabled (SCREENPRINTF60 unavailable)");
        return;
    }
    render_message(adaptive_string_, text.line1.data(), kAdaptiveScreenPrintfY, kAdaptiveScreenPrintfColor);
    adaptive_state_.shown(ticks, adaptive_duration_ticks_);
#endif
}

void
ScreenMessages::hide_transient() noexcept {
#if !defined(MWPERFFIX_RUNTIME_TEST)
    hide_message(transient_string_);
#endif
}

void
ScreenMessages::hide_adaptive() noexcept {
#if !defined(MWPERFFIX_RUNTIME_TEST)
    hide_message(adaptive_string_);
#endif
}

void
ScreenMessages::fail(const char *const text) noexcept {
    failed_ = true;
    transient_state_.invalidate();
    adaptive_state_.invalidate();
    ready_ = false;
    transient_string_ = nullptr;
    adaptive_string_ = nullptr;
    if (log_ != nullptr)
        log_(log_context_, text);
}

} // namespace mwperffix::adaptive::runtime_detail
