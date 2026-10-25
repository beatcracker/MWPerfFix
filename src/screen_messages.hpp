#pragma once

#include "mwperffix/adaptive_reflections_runtime.hpp"
#include "mwperffix/runtime_notifications.hpp"
#include <cstdint>
#include <string_view>

namespace mwperffix::adaptive::runtime_detail {
// Owns ScreenPrintf bookkeeping and borrows game-owned frontend objects.
// The test variant substitutes frontend calls; it does not validate native UI ABI.
class ScreenMessages {
  public:
    ScreenMessages(
        const std::uintptr_t image, const std::uint64_t frequency, const bool enabled, const LogFunction log,
        void *const log_context
    ) noexcept;

    [[nodiscard]] bool enabled() const noexcept;

    void post_text(const std::string_view value) noexcept;

    void clear_adaptive() noexcept;

    void post_adaptive(const notifications::AdaptiveSnapshot &snapshot) noexcept;

    void update(const std::uint64_t ticks) noexcept;

    void invalidate() noexcept;

#if defined(MWPERFFIX_RUNTIME_TEST)
    [[nodiscard]] notifications::Message test_pending() const noexcept;
    [[nodiscard]] std::string_view test_pending_text() const noexcept;
    [[nodiscard]] bool test_visible() const noexcept;
    [[nodiscard]] bool test_adaptive_pending() const noexcept;
    [[nodiscard]] bool test_adaptive_visible() const noexcept;
    [[nodiscard]] const notifications::AdaptiveSnapshot &test_adaptive_snapshot() const noexcept;
#endif

  private:
    struct FEColor {
        std::uint32_t red = 0U;
        std::uint32_t green = 0U;
        std::uint32_t blue = 0U;
        std::uint32_t alpha = 0U;
    };

    using PushPackageFn = int(__thiscall *)(void *, const char *, int);
    using PackageManagerGetFn = void *(__cdecl *)();
    using FindPackageFn = void *(__thiscall *)(void *, const char *);
    using SetUseIdleListFn = void *(__thiscall *)(void *, const char *, bool);
    using SetPermanentFn = void *(__thiscall *)(void *, const char *, int);
    using HashUpperFn = unsigned int(__cdecl *)(char *);
    using FindStringFn = void *(__cdecl *)(const char *, unsigned int);
    using SetVisibilityFn = int(__cdecl *)(void *);
    using ColorCreateFn = unsigned int(__thiscall *)(void *, std::uint32_t);
    using SetColorFn = void(__thiscall *)(void *, const FEColor *, int);
    using PrintStringFn = int(__cdecl *)(void *, const char *, ...);
    using SetTopLeftFn = void(__cdecl *)(void *, float, float);
    using GetCenterFn = void(__cdecl *)(void *, float *, float *);
    using SetCenterFn = void(__cdecl *)(void *, float, float);

    template <typename Fn> [[nodiscard]] Fn game_function(std::uintptr_t preferred) const noexcept;

    [[nodiscard]] bool initialize() noexcept;

#if !defined(MWPERFFIX_RUNTIME_TEST)
    void render_message(void *const object, const char *const message, float y, std::uint32_t color_value) noexcept;

    void hide_message(void *const object) noexcept;

#endif
    void show_transient(const notifications::Message message, const std::uint64_t ticks) noexcept;

    void show_adaptive(const notifications::AdaptiveSnapshot &snapshot, const std::uint64_t ticks) noexcept;

    void hide_transient() noexcept;

    void hide_adaptive() noexcept;

    void fail(const char *const text) noexcept;

    notifications::State transient_state_{};
    notifications::AdaptiveState adaptive_state_{};
    std::uintptr_t base_ = 0U;
    std::uint64_t transient_duration_ticks_ = 0U;
    std::uint64_t adaptive_duration_ticks_ = 0U;
    LogFunction log_ = nullptr;
    void *log_context_ = nullptr;
    void *transient_string_ = nullptr;
    void *adaptive_string_ = nullptr;
    bool ready_ = false;
    bool failed_ = false;
    bool needs_clear_ = false;
#if !defined(MWPERFFIX_RUNTIME_TEST)
    bool push_attempted_ = false;
#endif
};

} // namespace mwperffix::adaptive::runtime_detail
