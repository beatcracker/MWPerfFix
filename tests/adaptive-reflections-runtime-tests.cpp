// Runtime is compiled separately with deterministic game-call substitutions.
// Production-derived images below test transactions; independent fixtures test supported binary bytes.
// Neither proves native UI behavior or visual quality.
#include "adaptive_reflections_runtime_private.hpp"
#include "machine-state.hpp"
#include "mwperffix/code.hpp"
#include "mwperffix/render_fixes.hpp"
#include "test_error_mode.hpp"

#ifdef MWPERFFIX_DIAGNOSTICS
#include "mwperffix/diagnostics/api.hpp"
#endif

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <d3d9.h>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <vector>
#include <windows.h>

extern "C" {
std::uint32_t adaptive_result_eax = 0;
std::uint32_t adaptive_result_edx = 0;
}
extern "C" __declspec(naked) std::uint32_t __cdecl
adaptive_invoke(std::uintptr_t, std::uint32_t, std::uint32_t, std::uint32_t) {
    __asm__ volatile("pushal\n\tmovl 36(%esp), %edi\n\tmovl 40(%esp), %esi\n\t"
                     "movl 44(%esp), %ecx\n\tmovl 48(%esp), %eax\n\txorl %edx, %edx\n\t"
                     "call *%edi\n\tmovl %eax, _adaptive_result_eax\n\tmovl %edx, "
                     "_adaptive_result_edx\n\t"
                     "movl %ecx, 28(%esp)\n\tpopal\n\tret");
}

namespace {

using namespace mwperffix;
using namespace mwperffix::adaptive;
using namespace mwperffix::adaptive::runtime_detail;

[[noreturn]] void
fail(const char *const expression, const int line) {
    std::fprintf(stderr, "line %d: %s\n", line, expression);
    std::exit(EXIT_FAILURE);
}
#define CHECK(expression) ((expression) ? static_cast<void>(0) : fail(#expression, __LINE__))

// Vanilla bytes immediately after the relocated renderer entry.
// The renderer site spans 006E7220..006E7224 only,
// so these bytes model the untouched continuation (the start of the vanilla prologue) at 006E7225.
constexpr auto kVanillaRendererTail =
    std::to_array<std::uint8_t>({0x83, 0xEC, 0x10, 0x53, 0x55, 0x33, 0xED, 0x84, 0xC0});

constexpr auto kVanillaResetEntry = std::to_array<std::uint8_t>({0x83, 0xEC, 0x08, 0x56, 0x57});
constexpr auto kVanillaResetPreRelease =
    std::to_array<std::uint8_t>({0xE8, 0x26, 0x70, 0xFF, 0xFF, 0xBE, 0x78, 0xDE, 0x93, 0x00});

// Independent supported-executable fixture for the ordinary sweep fix.
// Do not build this from render_fixes.cpp constants:
// this regression must catch a production guard that accidentally starts depending on its own definitions.
constexpr auto kSweepFixturePrologue = std::to_array<std::uint8_t>({
    0x83,
    0xEC,
    0x58,
    0x53,
    0x55,
    0x8B,
    0x6C,
    0x24,
    0x64,
    0x56,
    0x33,
    0xDB,
    0x57,
    0x8B,
    0x7C,
    0x24,
    0x70,
    0x89,
    0x4C,
    0x24,
    0x18,
    0x88,
    0x5C,
    0x24,
    0x13,
    0x8B,
    0xF1,
    0xC7,
    0x44,
    0x24,
    0x14,
    0x06,
    0x00,
    0x00,
    0x00,
});
constexpr auto kSweepFixtureLoop = std::to_array<std::uint8_t>({
    0x33,
    0xF6,
    0x85,
    0xDB,
    0x7E,
    0x7A,
    0xD9,
    0x45,
    0x0C,
    0x8B,
    0xD5,
    0xD9,
    0x44,
    0xB4,
    0x38,
    0x8B,
    0x02,
    0xD9,
    0xC0,
    0x8B,
    0x4A,
    0x04,
    0xD8,
    0x0F,
    0x8B,
    0x52,
    0x08,
    0xD9,
    0xC1,
    0x89,
    0x44,
    0x24,
    0x28,
    0xD8,
    0x4F,
    0x04,
    0x89,
    0x4C,
    0x24,
    0x2C,
    0x8B,
    0x4C,
    0x24,
    0x18,
    0x89,
    0x54,
    0x24,
    0x30,
    0xD9,
    0x5C,
    0x24,
    0x20,
    0x8D,
    0x44,
    0x24,
    0x28,
    0xD9,
    0xC9,
    0x50,
    0xD8,
    0x4F,
    0x08,
    0xD9,
    0x5C,
    0x24,
    0x28,
    0xD8,
    0x44,
    0x24,
    0x2C,
    0xD9,
    0x5C,
    0x24,
    0x2C,
    0xD9,
    0x44,
    0x24,
    0x30,
    0xD8,
    0x44,
    0x24,
    0x24,
    0xD9,
    0x5C,
    0x24,
    0x30,
    0xD9,
    0x44,
    0x24,
    0x34,
    0xD8,
    0x44,
    0x24,
    0x28,
    0xD9,
    0x5C,
    0x24,
    0x34,
    0xD8,
    0x0D,
    0xC4,
    0x10,
    0x89,
    0x00,
    0xD9,
    0x5C,
    0x24,
    0x38,
    0xE8,
    0xAE,
    0xFE,
    0xFF,
    0xFF,
    0x8A,
    0x54,
    0x24,
    0x13,
    0x0A,
    0xD0,
    0x46,
    0x3B,
    0xF3,
    0x88,
    0x54,
    0x24,
    0x13,
    0x7C,
    0x86,
});
constexpr auto kSweepFixtureEpilogue =
    std::to_array<std::uint8_t>({0x8A, 0x44, 0x24, 0x13, 0x5F, 0x5E, 0x5D, 0x5B, 0x83, 0xC4, 0x58, 0xC2, 0x08, 0x00});

class Image final : public Memory {
  public:
    Image() : bytes_(0x00600000U) {
        for (const SiteDefinition &site : kSites) {
            std::memcpy(bytes_.data() + rva(site.preferred), site.expected.data(), site.expected.size());
        }
        for (const auto &guard : kCommonGuards) {
            std::memcpy(bytes_.data() + rva(guard.preferred), guard.expected.data(), guard.expected.size());
        }
        for (const auto &guard : kSchedulerGuards) {
            std::memcpy(bytes_.data() + rva(guard.preferred), guard.expected.data(), guard.expected.size());
        }
        for (const auto &guard : kBlendGuards) {
            std::memcpy(bytes_.data() + rva(guard.preferred), guard.expected.data(), guard.expected.size());
        }
        for (const auto &call : publication::calls) {
            std::memcpy(bytes_.data() + rva(call.preferred), call.expected().data(), call.size);
        }
        std::memcpy(bytes_.data() + rva(0x6E7225U), kVanillaRendererTail.data(), kVanillaRendererTail.size());
        std::memcpy(bytes_.data() + rva(0x6DB0D0U), kVanillaResetEntry.data(), kVanillaResetEntry.size());
    }
    [[nodiscard]] std::uintptr_t
    base() const noexcept {
        return reinterpret_cast<std::uintptr_t>(bytes_.data());
    }
    [[nodiscard]] bool
    read(const std::uintptr_t address, const std::span<std::uint8_t> output) const noexcept override {
        if (address < base() || address - base() > bytes_.size() || output.size() > bytes_.size() - (address - base()))
            return false;
        std::memcpy(output.data(), reinterpret_cast<const void *>(address), output.size());
        return true;
    }
    [[nodiscard]] bool
    write(const std::uintptr_t address, const std::span<const std::uint8_t> input) noexcept override {
        ++writes;
        if (writes == fail_write)
            return false;
        std::memcpy(reinterpret_cast<void *>(address), input.data(), input.size());
        return true;
    }
    std::vector<std::uint8_t> bytes_;
    std::uint32_t writes = 0;
    std::uint32_t fail_write = 0;
};

void
seed_reset_fixture(Image &image) {
    std::memcpy(image.bytes_.data() + rva(0x6DB0D5U), kVanillaResetPreRelease.data(), kVanillaResetPreRelease.size());
}

void
seed_sweep_fixture(Image &image) {
    std::memcpy(image.bytes_.data() + rva(0x6C9570U), kSweepFixturePrologue.data(), kSweepFixturePrologue.size());
    std::memcpy(image.bytes_.data() + rva(0x6C95F1U), kSweepFixtureLoop.data(), kSweepFixtureLoop.size());
    std::memcpy(image.bytes_.data() + rva(0x6C9671U), kSweepFixtureEpilogue.data(), kSweepFixtureEpilogue.size());
}

void
quiet(void *, const std::string_view) noexcept {
}

std::uint32_t
u32(const std::span<const std::uint8_t> bytes, const std::size_t offset) {
    return static_cast<std::uint32_t>(bytes[offset]) | (static_cast<std::uint32_t>(bytes[offset + 1U]) << 8U) |
           (static_cast<std::uint32_t>(bytes[offset + 2U]) << 16U) |
           (static_cast<std::uint32_t>(bytes[offset + 3U]) << 24U);
}

void
put_u32(std::vector<std::uint8_t> &bytes, const std::size_t offset, const std::uint32_t value) {
    for (unsigned i = 0; i < 4U; ++i)
        bytes[offset + i] = static_cast<std::uint8_t>(value >> (8U * i));
}

void
test_replayed_call_and_continuation() {
    const SiteDefinition &face = kSites[2U];
    constexpr std::uintptr_t relay = 0x10000000U;
    const auto code = build_relay(face, face.preferred, relay);
    // The observer includes ABI stack padding.
    // Displaced push/push/call follows.
    CHECK(code.size() > 65U);
    CHECK(code[56U] == 0x6AU && code[57U] == 0x01U && code[58U] == 0x50U && code[59U] == 0xE8U);
    CHECK(relay + 64U + u32(code, 60U) == 0x006BD3B0U);
    CHECK(code.size() == 69U);
    CHECK(code[code.size() - 5U] == 0xE9U);
    CHECK(relay + code.size() + u32(code, code.size() - 4U) == face.preferred + face.expected.size());
}

// The relocated renderer hook replays `mov al, [00901AC8]` and then continues at 006E7225.
// Because the relay owns 006E7220, whatever occupies 006E7225 at install time
// - vanilla prologue or ZPatch's BorderlessFixes jump - becomes the continuation,
// and ZPatch may also replace 006E7225 after MWPerfFix installs.

void
test_renderer_relay_chains() {
    const SiteDefinition &renderer = kSites[0U];
    CHECK(renderer.preferred == 0x6E7220U);
    CHECK(renderer.expected.size() == 5U);
    constexpr std::uintptr_t relay = 0x10000000U;
    const auto code = build_relay(renderer, renderer.preferred, relay);
    // Replayed entry instruction immediately follows the 56-byte observer frame.
    CHECK(code[56U] == 0xA0U);
    CHECK(u32(code, 57U) == 0x00901AC8U);
    // Continuation is 006E7225, not the vanilla 006E722A.
    CHECK(code[code.size() - 5U] == 0xE9U);
    CHECK(relay + code.size() + u32(code, code.size() - 4U) == 0x6E7225U);
}

void
test_load_order_chaining() {
    const std::size_t entry = rva(0x6E7220U);
    const std::size_t next = rva(0x6E7225U);

    // 1. Vanilla bytes at 006E7225.
    {
        Image image;
        CHECK(install(image, image.base(), ReflectionConfig{}, quiet, nullptr, {}) == Outcome::applied);
        CHECK(image.bytes_[entry] == 0xE9U);
        CHECK(image.bytes_[next] == 0x83U && image.bytes_[next + 1U] == 0xECU);
        destroy_installed_test_state();
    }

    // 2. ZPatch's BorderlessFixes jump already present at 006E7225.
    {
        Image image;
        image.bytes_[next] = 0xE9U;
        put_u32(image.bytes_, next + 1U, 0x10003620U - (0x6E7225U + 5U));
        const auto zpatch_displacement = image.bytes_[next + 1U];
        CHECK(install(image, image.base(), ReflectionConfig{}, quiet, nullptr, {}) == Outcome::applied);
        CHECK(image.bytes_[entry] == 0xE9U); // MWPerfFix owns 006E7220.
        CHECK(image.bytes_[next] == 0xE9U);  // ZPatch still owns 006E7225.
        CHECK(image.bytes_[next + 1U] == zpatch_displacement);
        destroy_installed_test_state();
    }

    // 3. ZPatch replaces 006E7225 after MWPerfFix installs.
    // The MWPerfFix branch at 006E7220 is untouched, so the chain remains valid.
    {
        Image image;
        CHECK(install(image, image.base(), ReflectionConfig{}, quiet, nullptr, {}) == Outcome::applied);
        std::array<std::uint8_t, 5> installed{};
        std::memcpy(installed.data(), image.bytes_.data() + entry, installed.size());
        image.bytes_[next] = 0xE9U;
        put_u32(image.bytes_, next + 1U, 0x10003620U - (0x6E7225U + 5U));
        CHECK(std::memcmp(image.bytes_.data() + entry, installed.data(), installed.size()) == 0);
        destroy_installed_test_state();
    }
}

void
test_reset_relay_chains() {
    const SiteDefinition &reset = kSites[6U];
    CHECK(reset.preferred == 0x6DB0DAU);
    CHECK(reset.expected.size() == 5U);
    CHECK(reset.expected[0] == 0xBEU);
    CHECK(u32(reset.expected, 1U) == 0x0093DE78U);

    constexpr std::uintptr_t relay = 0x10000000U;
    const auto code = build_relay(reset, reset.preferred, relay);
    CHECK(code[56U] == 0xBEU);
    CHECK(u32(code, 57U) == 0x0093DE78U);
    CHECK(code[code.size() - 5U] == 0xE9U);
    CHECK(relay + code.size() + u32(code, code.size() - 4U) == 0x6DB0DFU);

    const std::size_t entry = rva(0x6DB0D0U);
    const std::size_t observation = rva(0x6DB0DAU);

    // Vanilla reset entry remains untouched.
    // MWPerfFix owns only DA..DE.
    {
        Image image;
        seed_reset_fixture(image);
        CHECK(install(image, image.base(), ReflectionConfig{}, quiet, nullptr, {}) == Outcome::applied);
        CHECK(std::memcmp(image.bytes_.data() + entry, kVanillaResetEntry.data(), kVanillaResetEntry.size()) == 0);
        CHECK(image.bytes_[observation] == 0xE9U);
        destroy_installed_test_state();
    }

    // Widescreen Fix already owns D0..D4.
    // Its trampoline resumes at D5, so the exact native call at D5..D9 naturally reaches MWPerfFix at DA.
    {
        Image image;
        seed_reset_fixture(image);
        image.bytes_[entry] = 0xE9U;
        put_u32(image.bytes_, entry + 1U, 0x10004000U - (0x6DB0D0U + 5U));
        std::array<std::uint8_t, 5> widescreen_entry{};
        std::memcpy(widescreen_entry.data(), image.bytes_.data() + entry, widescreen_entry.size());
        CHECK(install(image, image.base(), ReflectionConfig{}, quiet, nullptr, {}) == Outcome::applied);
        CHECK(std::memcmp(image.bytes_.data() + entry, widescreen_entry.data(), widescreen_entry.size()) == 0);
        CHECK(image.bytes_[observation] == 0xE9U);
        destroy_installed_test_state();
    }

    // Widescreen Fix may install after MWPerfFix without touching DA..DE.
    {
        Image image;
        seed_reset_fixture(image);
        CHECK(install(image, image.base(), ReflectionConfig{}, quiet, nullptr, {}) == Outcome::applied);
        std::array<std::uint8_t, 5> installed{};
        std::memcpy(installed.data(), image.bytes_.data() + observation, installed.size());
        image.bytes_[entry] = 0xE9U;
        put_u32(image.bytes_, entry + 1U, 0x10004000U - (0x6DB0D0U + 5U));
        CHECK(std::memcmp(image.bytes_.data() + observation, installed.data(), installed.size()) == 0);
        destroy_installed_test_state();
    }

    // The native call immediately before the observation remains a real dependency.
    // Unknown reset flow is rejected.
    {
        Image changed;
        seed_reset_fixture(changed);
        changed.bytes_[rva(0x6DB0D5U)] ^= 1U;
        CHECK(install(changed, changed.base(), ReflectionConfig{}, quiet, nullptr, {}) == Outcome::mismatch);
        CHECK(changed.writes == 0U);
    }
    {
        Image changed;
        seed_reset_fixture(changed);
        changed.bytes_[observation + 1U] ^= 1U;
        CHECK(install(changed, changed.base(), ReflectionConfig{}, quiet, nullptr, {}) == Outcome::mismatch);
        CHECK(changed.writes == 0U);
    }
}

void
test_sweep_compatibility_and_initialization_order() {
    // Adaptive has no dependency on the sweep implementation.
    // With the ordinary fix disabled, independently specified vanilla sweep bytes remain untouched.
    {
        Image image;
        seed_sweep_fixture(image);
        CHECK(install(image, image.base(), ReflectionConfig{}, quiet, nullptr, {}) == Outcome::applied);
        CHECK(
            std::memcmp(image.bytes_.data() + rva(0x6C95F1U), kSweepFixtureLoop.data(), kSweepFixtureLoop.size()) == 0
        );
        destroy_installed_test_state();
    }

    // Ordinary fixes are installed before adaptive at runtime.
    // Reproduce that order against an independently specified executable fixture.
    {
        Image image;
        seed_sweep_fixture(image);
        CHECK(install_render_fix(RenderFix::sweep_early_exit, image, image.base()) == Outcome::applied);
        CHECK(install(image, image.base(), ReflectionConfig{}, quiet, nullptr, {}) == Outcome::applied);
        destroy_installed_test_state();
    }

    // Widescreen Fix may rewrite only the four-byte shadow-distance operand.
    // The actual sweep installer, not adaptive, owns this compatibility rule.
    {
        Image image;
        seed_sweep_fixture(image);
        constexpr auto widescreen_operand = std::to_array<std::uint8_t>({0x44, 0x33, 0x22, 0x11});
        std::memcpy(image.bytes_.data() + rva(0x6C9655U), widescreen_operand.data(), widescreen_operand.size());
        CHECK(install_render_fix(RenderFix::sweep_early_exit, image, image.base()) == Outcome::applied);
        CHECK(install(image, image.base(), ReflectionConfig{}, quiet, nullptr, {}) == Outcome::applied);
        destroy_installed_test_state();
    }

    // Opcode and control-flow ownership remain exact.
    for (const std::uintptr_t address : {0x6C9653U, 0x6C9659U, 0x6C966FU}) {
        Image changed;
        seed_sweep_fixture(changed);
        changed.bytes_[rva(address)] ^= 1U;
        CHECK(install_render_fix(RenderFix::sweep_early_exit, changed, changed.base()) == Outcome::mismatch);
        CHECK(changed.writes == 0U);
    }
}

void
test_checked_group_installation() {
    Image mismatch;
    mismatch.bytes_[rva(kSites[4U].preferred)] = 0xCCU;
    CHECK(install(mismatch, mismatch.base(), ReflectionConfig{}, quiet, nullptr, {}) == Outcome::mismatch);
    CHECK(mismatch.writes == 0U);

    for (const auto &guard : kCommonGuards) {
        Image changed;
        changed.bytes_[rva(guard.preferred) + guard.expected.size() - 1U] ^= 1U;
        CHECK(install(changed, changed.base(), ReflectionConfig{}, quiet, nullptr, {}) == Outcome::mismatch);
        CHECK(changed.writes == 0U);
    }
    for (const auto &guard : kSchedulerGuards) {
        Image changed;
        changed.bytes_[rva(guard.preferred) + guard.expected.size() - 1U] ^= 1U;
        CHECK(install(changed, changed.base(), ReflectionConfig{}, quiet, nullptr, {}) == Outcome::mismatch);
        CHECK(changed.writes == 0U);
    }

    for (std::uint32_t failure = 1U; failure <= 7U; ++failure) {
        Image image;
        const auto original = image.bytes_;
        image.fail_write = failure;
        CHECK(install(image, image.base(), ReflectionConfig{}, quiet, nullptr, {}) == Outcome::failed);
        CHECK(image.bytes_ == original);
        CHECK(test_bound_state() == nullptr);
    }

    Image image;
    CHECK(install(image, image.base(), ReflectionConfig{}, quiet, nullptr, {}) == Outcome::applied);
    CHECK(test_bound_state() != nullptr);
    for (const SiteDefinition &site : kSites) {
        const std::size_t offset = rva(site.preferred);
        CHECK(image.bytes_[offset] == 0xE9U);
    }
    destroy_installed_test_state();
}

void
test_scheduler_not_required_when_no_schedule_consumer() {
    Image native_scheduler;
    const SiteDefinition &scheduler = kSites[1U];
    CHECK(
        install(
            native_scheduler, native_scheduler.base(), ReflectionConfig{.adaptive_enabled = false}, quiet, nullptr, {}
        ) == Outcome::applied
    );
    CHECK(native_scheduler.bytes_[rva(scheduler.preferred)] == scheduler.expected[0]);
    CHECK(test_bound_state() != nullptr);
    destroy_installed_test_state();

    Image changed_scheduler_context;
    const ContextGuard &scheduler_guard = kSchedulerGuards[0U];
    changed_scheduler_context.bytes_[rva(scheduler_guard.preferred) + scheduler_guard.expected.size() - 1U] ^= 1U;
    CHECK(
        install(
            changed_scheduler_context, changed_scheduler_context.base(), ReflectionConfig{.adaptive_enabled = false},
            quiet, nullptr, {}
        ) == Outcome::applied
    );
    CHECK(changed_scheduler_context.bytes_[rva(scheduler.preferred)] == scheduler.expected[0]);
    CHECK(test_bound_state() != nullptr);
    destroy_installed_test_state();
}

void
test_state_binding_and_installed_lifetime() {
    Image image;
    State first(image.base(), 1000U, ReflectionConfig{});
    State second(image.base(), 1000U, ReflectionConfig{});
    CHECK(test_bound_state() == nullptr);
    {
        const ScopedTestState outer(first);
        CHECK(test_bound_state() == &first);
        {
            const ScopedTestState inner(second);
            CHECK(test_bound_state() == &second);
        }
        CHECK(test_bound_state() == &first);
    }
    CHECK(test_bound_state() == nullptr);

    CHECK(install(image, image.base(), ReflectionConfig{}, quiet, nullptr) == Outcome::applied);
    State *const installed = test_bound_state();
    CHECK(installed != nullptr);
    {
        const ScopedTestState borrowed(first);
        CHECK(test_bound_state() == &first);
    }
    CHECK(test_bound_state() == installed);

    // State cleanup must not free pages still referenced by the simulated image.
    const std::uintptr_t source = image.base() + rva(kSites[0].preferred);
    std::uint32_t displacement = 0U;
    std::memcpy(&displacement, reinterpret_cast<const void *>(source + 1U), sizeof(displacement));
    const std::uintptr_t relay = source + 5U + displacement;
    MEMORY_BASIC_INFORMATION before{}, after{};
    CHECK(VirtualQuery(reinterpret_cast<void *>(relay), &before, sizeof(before)) != 0U);
    CHECK(before.State == MEM_COMMIT && before.Protect == PAGE_EXECUTE_READ);
    destroy_installed_test_state();
    CHECK(test_bound_state() == nullptr);
    CHECK(VirtualQuery(reinterpret_cast<void *>(relay), &after, sizeof(after)) != 0U);
    CHECK(after.State == before.State && after.Protect == before.Protect);
    CHECK(after.AllocationBase == before.AllocationBase);
}

void
test_face_addresses_and_refresh() {
    State state(kPreferredImageBase, 1000000U, ReflectionConfig{});
    CHECK(face_index(state, 0x919DBFU) == kFaceCount);
    CHECK(face_index(state, 0x919DC1U) == kFaceCount);
    CHECK(face_index(state, 0x91A060U) == kFaceCount);
    state.controller.begin_renderer(1000U, 1U, state.renderer_clock.observe(1000U));
    const Selection selected = state.controller.select(3U, true, 1001U);
    CHECK(selected.custom);
    CHECK(selected.face_mask == kAllFaceMask);
    for (std::size_t i = 0; i < kFaceCount; ++i) {
        const auto index = face_index(state, static_cast<std::uint32_t>(0x919DC0U + i * 0x70U));
        CHECK(index == i);
        const std::size_t native_face = kViewIndexToNativeFace[index];
        state.controller.begin_face(native_face, 1010U + i * 100U);
        state.controller.end_face(native_face, 1100U + i * 100U);
    }
    state.controller.before_present(2000U);
    state.controller.after_present(2100U, true);
    CHECK(!state.controller.full_refresh_pending());
    CHECK(state.controller.effective_budget() == 6U);
}

#ifdef MWPERFFIX_DIAGNOSTICS
struct DispatchRecorder {
    std::vector<FrameObservation> frames;
    static void
    frame(void *const user, const FrameObservation &observation) noexcept {
        static_cast<DispatchRecorder *>(user)->frames.push_back(observation);
    }
};

DWORD WINAPI
rejected_face_on_foreign_thread(LPVOID context) {
    mwperffix_adaptive_dispatch(static_cast<std::uint32_t>(Site::face_begin), static_cast<SavedContext *>(context));
    return 0U;
}

void
test_dispatch_rejections_and_canonical_face_mapping() {
    Image image;
    DispatchRecorder recorder;
    State state(image.base(), 1000000U, ReflectionConfig{}, Observer{nullptr, &DispatchRecorder::frame, &recorder});
    state.producer_thread = GetCurrentThreadId();
    const ScopedTestState binding(state);

    for (std::uint32_t frame = 1U; frame <= 2U; ++frame) {
        const std::uint64_t begin = qpc_now();
        state.begin_renderer(begin, frame);
        CHECK(state.blend.diagnostic_timing.valid);
        CHECK(state.blend.diagnostic_timing.ticks[2] == 0U);
        const Selection selected = state.controller.select(3U, true, qpc_now());
        CHECK(selected.face_mask == kAllFaceMask);

        for (std::size_t view = 0U; view < kFaceCount; ++view) {
            SavedContext context{};
            context.esi = static_cast<std::uint32_t>(image.base() + rva(kCubemapViews) + view * kViewStride);
            CHECK(face_index(state, context.esi) == view);
            mwperffix_adaptive_dispatch(static_cast<std::uint32_t>(Site::face_begin), &context);
            mwperffix_adaptive_dispatch(static_cast<std::uint32_t>(Site::face_end), &context);
        }
        if (frame == 1U) {
            SavedContext unknown{};
            unknown.esi = static_cast<std::uint32_t>(image.base() + rva(kCubemapViews) + 1U);
            mwperffix_adaptive_dispatch(static_cast<std::uint32_t>(Site::face_begin), &unknown);
            mwperffix_adaptive_dispatch(static_cast<std::uint32_t>(Site::face_end), &unknown);

            SavedContext foreign{};
            foreign.esi = static_cast<std::uint32_t>(image.base() + rva(kCubemapViews));
            HANDLE thread = CreateThread(nullptr, 0, &rejected_face_on_foreign_thread, &foreign, 0U, nullptr);
            CHECK(thread != nullptr);
            CHECK(WaitForSingleObject(thread, INFINITE) == WAIT_OBJECT_0);
            CHECK(CloseHandle(thread) != FALSE);
        }

        // Verify that the runtime bridge forwards this frame's measurements
        // without altering controller face accounting or retaining the prior frame.
        state.blend.diagnostic_timing.ticks = {1U, 2U, frame * 3U, 4U};
        SavedContext present{};
        mwperffix_adaptive_dispatch(static_cast<std::uint32_t>(Site::pre_present), &present);
        mwperffix_adaptive_dispatch(static_cast<std::uint32_t>(Site::post_present), &present);
        CHECK(recorder.frames.size() == frame);
        const FrameObservation &sample = recorder.frames.back();
        CHECK(sample.timing_valid);
        CHECK(sample.blend_timing_valid);
        CHECK(sample.blend_qpc[2] == frame * 3U);
        CHECK(sample.blend_mode == 0U);
        std::uint64_t face_sum = 0U;
        for (const auto cost : sample.face_qpc)
            face_sum += cost;
        CHECK(face_sum == sample.observed_face_qpc);
        CHECK(sample.requested_face_mask == kAllFaceMask);
        CHECK(sample.face_begin_mask == kAllFaceMask);
        CHECK(sample.completed_face_mask == kAllFaceMask);
        CHECK(sample.face_begin_count == kFaceCount);
        CHECK(sample.face_end_count == kFaceCount);
        CHECK(sample.unknown_face_indices == (frame == 1U ? 2U : 0U));
        CHECK(sample.ignored_foreign_face_callbacks == (frame == 1U ? 1U : 0U));
    }
}
#endif

void
test_native_mask_word_order() {
    // The game's scheduler reads these six DWORDs in native-face order.
    // Every one-hot case catches a renderer-view permutation of the mask.
    struct MaskCase {
        std::uint8_t face_mask;
        std::array<std::uint32_t, kFaceCount> words;
    };
    constexpr std::array cases{
        MaskCase{0x01U, {1U, 0U, 0U, 0U, 0U, 0U}},
        MaskCase{0x02U, {0U, 1U, 0U, 0U, 0U, 0U}},
        MaskCase{0x04U, {0U, 0U, 1U, 0U, 0U, 0U}},
        MaskCase{0x08U, {0U, 0U, 0U, 1U, 0U, 0U}},
        MaskCase{0x10U, {0U, 0U, 0U, 0U, 1U, 0U}},
        MaskCase{0x20U, {0U, 0U, 0U, 0U, 0U, 1U}},
        MaskCase{0x28U, {0U, 0U, 0U, 1U, 0U, 1U}},
        MaskCase{0x24U, {0U, 0U, 1U, 0U, 0U, 1U}},
        MaskCase{0x11U, {1U, 0U, 0U, 0U, 1U, 0U}},
        MaskCase{0x3FU, {1U, 1U, 1U, 1U, 1U, 1U}},
        MaskCase{0x00U, {0U, 0U, 0U, 0U, 0U, 0U}},
    };
    for (const auto &test : cases)
        CHECK(native_mask_words(test.face_mask) == test.words);
    for (unsigned bits = 1U; bits <= kAllFaceMask; ++bits) {
        const auto words = native_mask_words(static_cast<std::uint8_t>(bits));
        for (std::size_t face = 0U; face < kFaceCount; ++face)
            CHECK(words[face] == ((bits >> face) & 1U));
    }
}

void
test_two_face_mask_materialization_and_setting_ownership() {
    Image image;
    constexpr std::uint32_t configured = 2U;
    std::memcpy(image.bytes_.data() + rva(kConfiguredMode), &configured, sizeof(configured));

    State state(image.base(), 1000000U, ReflectionConfig{});
    state.controller.begin_renderer(1000U, 1U, state.renderer_clock.observe(1000U));
    const Selection refresh = state.controller.select(2U, true, 1001U);
    CHECK(refresh.face_mask == kAllFaceMask);

    std::uint64_t cursor = 1010U;
    for (std::size_t face = 0; face < kFaceCount; ++face) {
        state.controller.begin_face(face, cursor);
        cursor += 3000U;
        state.controller.end_face(face, cursor);
        ++cursor;
    }
    state.controller.before_present(cursor);
    state.controller.after_present(cursor + 1U, true);
    CHECK(state.controller.effective_budget() == 6U);

    state.controller.begin_renderer(30000U, 2U, state.renderer_clock.observe(30000U));
    const std::uintptr_t native_mask = image.base() + rva(0x8F9028U);
    const std::uintptr_t selected = state.select_mask(native_mask, configured, true, 2U, 30001U);
    CHECK(selected != native_mask);

    const auto *const words = reinterpret_cast<const std::uint32_t *>(selected);
    unsigned count = 0U;
    for (std::size_t view = 0; view < kFaceCount; ++view) {
        CHECK(words[view] <= 1U);
        count += words[view];
    }
    CHECK(count == state.controller.effective_budget());
    std::uint8_t physical_mask = 0U;
    for (std::size_t face = 0U; face < kFaceCount; ++face)
        physical_mask |= static_cast<std::uint8_t>(words[face] << face);
    CHECK(native_mask_words(physical_mask) == state.custom_mask_words);

    std::uint32_t configured_after = 0U;
    std::memcpy(&configured_after, image.bytes_.data() + rva(kConfiguredMode), sizeof(configured_after));
    CHECK(configured_after == configured);
}

void
test_adaptive_disabled_native_pass_through() {
    Image image;
    constexpr std::uintptr_t original = 0x12345678U;
    State disabled(image.base(), 1000U, ReflectionConfig{.adaptive_enabled = false}, {}, quiet);
    CHECK(!disabled.controller_enabled);
    for (const auto ui : {0U, 1U, 2U, 3U}) {
        CHECK(disabled.select_mask(original, ui, true, 1U, 100U) == original);
        CHECK(disabled.select_mask(original, ui, false, 1U, 100U) == original);
    }
    State enabled(image.base(), 1000U, ReflectionConfig{}, {}, quiet);
    enabled.controller.begin_renderer(100U, 1U, enabled.renderer_clock.observe(100U));
    CHECK(enabled.select_mask(original, 0U, true, 1U, 101U) == original);
    CHECK(enabled.select_mask(original, 3U, false, 1U, 101U) == original);
}

void
test_weighted_single_face_materializes_native_faces() {
    Image image;
    State state(image.base(), 720000U, ReflectionConfig{}, {}, quiet);
    constexpr std::uintptr_t original = 0x12345678U;
    constexpr std::array<std::uint8_t, 15> faces{4U, 0U, 1U, 4U, 2U, 3U, 4U, 5U, 0U, 4U, 1U, 2U, 4U, 3U, 5U};
    for (std::uint32_t frame = 1U; frame <= 18U; ++frame) {
        const std::uint64_t start = 720000U + static_cast<std::uint64_t>(frame) * 25000U;
        state.controller.begin_renderer(start, frame, state.renderer_clock.observe(start));
        const auto pointer = state.select_mask(original, 3U, true, frame, start + 1U);
        CHECK(pointer != original);
        const auto *words = reinterpret_cast<const std::uint32_t *>(pointer);
        std::uint8_t mask = 0U;
        std::uint64_t cursor = start + 20U;
        for (std::size_t face = 0; face < kFaceCount; ++face) {
            CHECK(words[face] <= 1U);
            mask |= static_cast<std::uint8_t>(words[face] << face);
            if (words[face] == 0U)
                continue;
            state.controller.begin_face(face, cursor);
            cursor += 3000U;
            state.controller.end_face(face, cursor);
            ++cursor;
        }
        const std::uint8_t expected =
            frame <= 3U ? kAllFaceMask : static_cast<std::uint8_t>(1U << faces[(frame - 4U) % faces.size()]);
        CHECK(mask == expected);
        CHECK(state.custom_mask_words == native_mask_words(expected));
        state.controller.before_present(start + 20000U);
        state.controller.after_present(start + 20100U, true);
    }
}

// Blending is an optional extension of the reflection runtime.
// Enabling it must not change the checked adaptive installation,
// and the blend runtime must remain inert until a renderer frame supplies a device and target.
void
test_blending_installation_and_inert_state() {
    Image image;
    const blending::Config blending{true, 3U};
    CHECK(install(image, image.base(), ReflectionConfig{.blending = blending}, quiet, nullptr, {}) == Outcome::applied);
    CHECK(test_bound_state() != nullptr);
    CHECK(test_bound_state()->blend.requested());
    CHECK(test_bound_state()->blend.steps() == 3U);
    CHECK(!test_bound_state()->blend.failed());
    // No renderer frame has run, so no temporary resources were allocated and no compositor draw can occur.
    test_bound_state()->blend.begin_frame();
    test_bound_state()->blend.select_schedule(Schedule::native_single);
    test_bound_state()->blend.cancel();
    test_bound_state()->blend.release();
    CHECK(!test_bound_state()->blend.failed());
    destroy_installed_test_state();

    // An invalid step count normalizes to two without affecting installation.
    Image normalized;
    CHECK(
        install(
            normalized, normalized.base(), ReflectionConfig{.blending = blending::Config{true, 9U}}, quiet, nullptr, {}
        ) == Outcome::applied
    );
    CHECK(test_bound_state()->blend.steps() == 2U);
    destroy_installed_test_state();

    // A changed compositor contract disables blending while the adaptive feature still installs.
    // The blend guards are checked only when blending is requested.
    for (const auto &guard : kBlendGuards) {
        Image changed;
        changed.bytes_[rva(guard.preferred) + guard.expected.size() - 1U] ^= 1U;
        CHECK(
            install(changed, changed.base(), ReflectionConfig{.blending = blending}, quiet, nullptr, {}) ==
            Outcome::applied
        );
        CHECK(test_bound_state() != nullptr);
        CHECK(!test_bound_state()->blend.requested());
        destroy_installed_test_state();
    }

    // With blending disabled the blend guards are not a prerequisite.
    {
        Image changed;
        changed.bytes_[rva(kBlendGuards[0].preferred)] ^= 1U;
        CHECK(install(changed, changed.base(), ReflectionConfig{}, quiet, nullptr, {}) == Outcome::applied);
        CHECK(test_bound_state() != nullptr);
        CHECK(!test_bound_state()->blend.requested());
        destroy_installed_test_state();
    }
}

struct MockBlendDevice {
    struct Object {
        void **vtable;
    };

    static constexpr std::size_t kImageSize = 0x600000U;
    static constexpr long kFailure = static_cast<long>(0x80004005U);
    static MockBlendDevice *current;

    std::array<void *, 66> device_vtable{};
    std::array<void *, 19> resource_vtable{};
    Object device{device_vtable.data()};
    Object native_color{resource_vtable.data()};
    Object native_depth{resource_vtable.data()};
    Object texture{resource_vtable.data()};
    Object surface{resource_vtable.data()};
    Object depth{resource_vtable.data()};
    TargetDescriptor native{5U, 0U, &native_color, &native_depth, 1U, 256U, 256U};
    void *image = nullptr;
    void *bound_texture = nullptr;
    void *actual_color = nullptr;
    void *actual_depth = nullptr;
    Viewport actual_viewport{};
    unsigned composite_calls = 0U;
    unsigned texture_creations = 0U;
    unsigned resource_releases = 0U;
    unsigned last_blend_byte = 0U;
    std::vector<unsigned> composite_blend_bytes{};
    bool reject_get_texture = false;
    bool reject_unbind = false;
    bool reject_blend_state = false;
    bool reject_composite = false;
    bool discarded_failure = false;
    unsigned observed_draws = 1U;

    static long __stdcall
    get_desc(void *, D3DSURFACE_DESC *const desc) {
        *desc = {
            D3DFMT_A8R8G8B8, D3DRTYPE_SURFACE, D3DUSAGE_RENDERTARGET, D3DPOOL_DEFAULT, D3DMULTISAMPLE_NONE, 0U, 256U,
            256U
        };
        return 0;
    }
    static long __stdcall
    create_texture(void *, unsigned, unsigned, unsigned, unsigned, unsigned, unsigned, void **const out, void *) {
        ++current->texture_creations;
        *out = &current->texture;
        return 0;
    }
    static long __stdcall
    get_surface(void *, unsigned, void **const out) {
        *out = &current->surface;
        return 0;
    }
    static long __stdcall
    create_depth(void *, unsigned, unsigned, unsigned, unsigned, unsigned, int, void **const out, void *) {
        *out = &current->depth;
        return 0;
    }
    static unsigned long __stdcall
    release_object(void *) {
        ++current->resource_releases;
        return 1U;
    }
    static long __stdcall
    get_texture(void *, const unsigned stage, void **const out) {
        if (current->reject_get_texture)
            return kFailure;
        *out = stage == 0U ? current->bound_texture : nullptr;
        return 0;
    }
    static long __stdcall
    set_texture(void *, unsigned, void *const value) {
        if (current->reject_unbind && value == nullptr)
            return kFailure;
        current->bound_texture = value;
        return 0;
    }
    static long __stdcall
    set_state(void *, unsigned, const unsigned value) {
        current->last_blend_byte = value & 0xFFU;
        return current->reject_blend_state ? kFailure : 0;
    }
    static long __stdcall
    set_target(void *, unsigned, void *const color) {
        current->actual_color = color;
        return 0;
    }
    static long __stdcall
    set_depth(void *, void *const depth_surface) {
        current->actual_depth = depth_surface;
        return 0;
    }
    static long __stdcall
    set_viewport(void *, const Viewport *const viewport) {
        current->actual_viewport = *viewport;
        return 0;
    }
    static long __cdecl
    composite(void *, int, void **, const std::uint32_t *, int, int, int, std::uint32_t) {
        if (auto *result = g_publications.current()) {
            result->draws += current->observed_draws;
            if (current->reject_composite || current->discarded_failure)
                result->failed = 1U;
        }
        ++current->composite_calls;
        current->composite_blend_bytes.push_back(current->last_blend_byte);
        // Model a native helper that returns a successful final viewport call
        // while earlier target restoration was lost.
        current->actual_color = &current->surface;
        current->actual_depth = &current->depth;
        current->actual_viewport = {0U, 0U, 1U, 1U, 0.0F, 1.0F};
        return current->reject_composite ? kFailure : 0;
    }

    MockBlendDevice() {
        current = this;
        CHECK(g_publications.prepare());
        resource_vtable[2U] = reinterpret_cast<void *>(&release_object);
        resource_vtable[12U] = reinterpret_cast<void *>(&get_desc);
        resource_vtable[18U] = reinterpret_cast<void *>(&get_surface);
        device_vtable[23U] = reinterpret_cast<void *>(&create_texture);
        device_vtable[29U] = reinterpret_cast<void *>(&create_depth);
        device_vtable[37U] = reinterpret_cast<void *>(&set_target);
        device_vtable[39U] = reinterpret_cast<void *>(&set_depth);
        device_vtable[47U] = reinterpret_cast<void *>(&set_viewport);
        device_vtable[57U] = reinterpret_cast<void *>(&set_state);
        device_vtable[64U] = reinterpret_cast<void *>(&get_texture);
        device_vtable[65U] = reinterpret_cast<void *>(&set_texture);
        image = VirtualAlloc(nullptr, kImageSize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        CHECK(image != nullptr);
        const auto base = reinterpret_cast<std::uintptr_t>(image);
        const auto entry = base + rva(kCompositor);
        const auto address = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&composite));
        const std::array<std::uint8_t, 2> jump{0xFFU, 0xE0U};
        *reinterpret_cast<std::uint8_t *>(entry) = 0xB8U; // mov eax, composite
        std::memcpy(reinterpret_cast<void *>(entry + 1U), &address, sizeof(address));
        std::memcpy(reinterpret_cast<void *>(entry + 5U), jump.data(), jump.size());
        CHECK(FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void *>(entry), 7U) != FALSE);
        void *const effect = reinterpret_cast<void *>(base + rva(0x93DF00U));
        *static_cast<void **>(effect) = reinterpret_cast<void *>(base + rva(kCompositorFilterVtable));
        write_pointer(base, kCompositorFilter, effect);
        write_u32(base, kConfiguredMode, 3U);
        write_u32(base, kCubemapGameplayGate, 6U);
        write_pointer(base, kDevice, &device);
        write_pointer(base, kFirstView + kViewDescriptorOffset, &native);
        write_pointer(base, kCurrentTarget, &native);
    }
    ~MockBlendDevice() {
        CHECK(VirtualFree(image, 0U, MEM_RELEASE) != FALSE);
        current = nullptr;
    }

    [[nodiscard]] std::uintptr_t
    base() const {
        return reinterpret_cast<std::uintptr_t>(image);
    }
    void
    simulate_native_face_begin(const SavedContext &context) {
        const auto *const redirected = reinterpret_cast<const TargetDescriptor *>(context.eax);
        actual_color = redirected->color;
        actual_depth = redirected->depth;
        actual_viewport = {0U, 0U, redirected->width, redirected->height, 0.0F, 1.0F};
        write_pointer(base(), kCurrentTarget, const_cast<TargetDescriptor *>(redirected));
    }
    [[nodiscard]] bool
    native_target_bound() const {
        return actual_color == native.color && actual_depth == native.depth && actual_viewport.width == native.width &&
               actual_viewport.height == native.height;
    }
};

MockBlendDevice *MockBlendDevice::current = nullptr;

void
test_blending_fixed_mode1() {
    MockBlendDevice device;
    write_u32(device.base(), kConfiguredMode, 1U);
    BlendRuntime blend({true, 3U}, quiet, nullptr);
    blend.set_base(device.base());
    blend.begin_frame();
    CHECK(!blend.failed());
    blend.select_schedule(Schedule::native_single);
    CHECK(blend.active());
    blend.cancel();
    blend.release();
}

void
test_blending_weighted_single_face_retarget_and_native_control() {
    for (const bool weighted_single_face : {false, true}) {
        MockBlendDevice device;
        write_u32(device.base(), kConfiguredMode, 1U);
        for (const std::size_t view : {0U, 1U, 3U})
            write_pointer(device.base(), kFirstView + view * kViewStride + kViewDescriptorOffset, &device.native);

        BlendRuntime blend({true, 6U}, quiet, nullptr);
        blend.set_base(device.base());
        const auto capture = [&](const std::size_t view) {
            blend.begin_frame(true, weighted_single_face);
            blend.select_schedule(weighted_single_face ? Schedule::weighted_single_face : Schedule::native_single);
            CHECK(blend.test_weighted_single_face() == weighted_single_face);
            SavedContext context{};
            context.eax = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&device.native));
            blend.begin_face(view, &context);
            CHECK(context.eax != reinterpret_cast<std::uintptr_t>(&device.native));
            device.simulate_native_face_begin(context);
            blend.end_face(view, &context);
            CHECK(!blend.failed());
            CHECK(device.native_target_bound());
        };
        capture(0U); // physical +Z
        capture(1U); // physical +X
        capture(3U); // physical +Y
        CHECK(blend.test_pending_count() == 3U);
        const unsigned before = device.composite_calls;
        capture(0U); // +Z still has a pending 6-step fade
        // Weighted single-face advances two other sources and publishes the new 1/6.
        // Native mode 1 also advances and finishes the old +Z to 100%.
        CHECK(device.composite_calls - before == (weighted_single_face ? 3U : 5U));
        CHECK(device.composite_blend_bytes.size() == device.composite_calls);
        if (weighted_single_face) {
            for (std::size_t i = before; i < device.composite_calls; ++i)
                CHECK(device.composite_blend_bytes[i] != 255U);
        } else {
            CHECK(device.composite_blend_bytes[before + 3U] == 255U);
        }
        CHECK(device.composite_blend_bytes.back() == 43U); // new capture at 1/6
        CHECK(blend.test_pending_count() == 3U);

        // An invalid native descriptor must not discard a valid pending +Z.
        write_pointer(device.base(), kFirstView + kViewDescriptorOffset, nullptr);
        blend.begin_frame(true, weighted_single_face);
        blend.select_schedule(weighted_single_face ? Schedule::weighted_single_face : Schedule::native_single);
        SavedContext context{};
        context.eax = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&device.native));
        const std::size_t old_pending = blend.test_pending_count();
        blend.begin_face(0U, &context);
        CHECK(context.eax == reinterpret_cast<std::uintptr_t>(&device.native));
        CHECK(blend.test_pending_count() <= old_pending);
        CHECK(blend.test_pending_count() >= 1U);
        write_pointer(device.base(), kFirstView + kViewDescriptorOffset, &device.native);

        if (weighted_single_face) {
            device.native.width = 0U;
            blend.begin_frame(true, true);
            blend.select_schedule(Schedule::weighted_single_face);
            context.eax = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&device.native));
            blend.begin_face(0U, &context);
            CHECK(context.eax == reinterpret_cast<std::uintptr_t>(&device.native));
            CHECK(!blend.failed());
            CHECK(blend.test_pending_count() >= 1U);
            device.native.width = 256U;
        }

        blend.select_schedule(Schedule::native_other);
        CHECK(!blend.test_weighted_single_face());
        CHECK(blend.test_pending_count() == 0U);
        blend.release();
    }
}

void
test_blending_weighted_single_face_retarget_unbind_failure() {
    MockBlendDevice device;
    BlendRuntime blend({true, 6U}, quiet, nullptr);
    blend.set_base(device.base());
    blend.begin_frame(true, true);
    blend.select_schedule(Schedule::weighted_single_face);
    SavedContext context{};
    context.eax = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&device.native));
    blend.begin_face(0U, &context);
    device.simulate_native_face_begin(context);
    blend.end_face(0U, &context);
    CHECK(blend.test_pending_count() == 1U);

    blend.begin_frame(true, true);
    blend.select_schedule(Schedule::weighted_single_face);
    device.bound_texture = &device.texture;
    device.reject_unbind = true;
    context.eax = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&device.native));
    const unsigned before = device.composite_calls;
    blend.begin_face(0U, &context);
    CHECK(blend.failed());
    CHECK(blend.test_pending_count() == 0U);
    CHECK(context.eax == reinterpret_cast<std::uintptr_t>(&device.native));
    CHECK(device.composite_calls == before); // no forced old-source publication
    blend.release();
}

void
test_blending_runtime_toggle() {
    MockBlendDevice device;
    write_u32(device.base(), kConfiguredMode, 1U);
    BlendRuntime blend({true, 3U}, quiet, nullptr);
    blend.set_base(device.base());
    CHECK(blend.mode() == blending::Mode::on);
    CHECK(!blend.bypassed());

    const auto capture = [&](const std::size_t view) {
        blend.begin_frame();
        blend.select_schedule(Schedule::native_single);
        SavedContext context{};
        context.eax = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&device.native));
        blend.begin_face(view, &context);
        if (blend.mode() == blending::Mode::off) {
            CHECK(context.eax == reinterpret_cast<std::uintptr_t>(&device.native));
        } else {
            CHECK(context.eax != reinterpret_cast<std::uintptr_t>(&device.native));
            device.simulate_native_face_begin(context);
            blend.end_face(view, &context);
        }
        CHECK(!blend.failed());
        CHECK(device.native_target_bound());
    };
    capture(0U);
    CHECK(blend.test_pending_count() == 1U);
    CHECK(device.composite_blend_bytes.back() == 107U); // on at three steps
    unsigned before = device.composite_calls;
    before = device.composite_calls;
    CHECK(blend.toggle()); // on -> off, no old-curve advancement
    CHECK(blend.bypassed());
    CHECK(blend.test_pending_count() == 0U);
    capture(1U);
    CHECK(device.composite_calls == before); // direct native write
    CHECK(blend.toggle());                   // off -> on
    CHECK(blend.mode() == blending::Mode::on);
    capture(0U);
    CHECK(device.composite_blend_bytes.back() == 107U);
    CHECK(blend.test_pending_count() == 1U);

    // Native full-update bypasses the compositor regardless of profile.
    blend.select_schedule(Schedule::native_other);
    CHECK(blend.test_pending_count() == 0U);
    CHECK(blend.mode() == blending::Mode::on);
    CHECK(blend.toggle()); // off
    blend.release();
    CHECK(!blend.test_resources_ready());

    // A latched compositor failure must never be undone by the hotkey.
    MockBlendDevice failed_device;
    write_u32(failed_device.base(), kConfiguredMode, 1U);
    BlendRuntime failed({true, 2U}, quiet, nullptr);
    failed.set_base(failed_device.base());
    failed.begin_frame();
    failed.select_schedule(Schedule::native_single);
    SavedContext context{};
    context.eax = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&failed_device.native));
    failed.begin_face(0U, &context);
    failed_device.simulate_native_face_begin(context);
    failed_device.reject_blend_state = true;
    failed.end_face(0U, &context);
    CHECK(failed.failed());
    CHECK(!failed.toggle());
    CHECK(failed.mode() == blending::Mode::on);
    failed.release();
}

void
test_hotkey_feature_integration() {
    Image image;
    State state(
        image.base(), 1000U,
        ReflectionConfig{.blending = {true, 6U}, .hotkeys = {0U, 0U, 0U, 0x78U}, .notifications_enabled = true}, {},
        quiet
    );
    state.apply_hotkey_sample(true, {});
    state.apply_hotkey_sample(true, {false, false, false, true});
    CHECK(state.blend.mode() == blending::Mode::off);
    CHECK(state.screen_messages.test_pending() == notifications::Message::custom);
    state.apply_hotkey_sample(true, {false, false, false, true});
    CHECK(state.blend.mode() == blending::Mode::off);
    state.apply_hotkey_sample(true, {});
    state.apply_hotkey_sample(true, {false, false, false, true});
    CHECK(state.blend.mode() == blending::Mode::on);
    CHECK(state.controller.effective_budget() == 6U);
}

struct NotificationChangeRecorder {
    unsigned count = 0U;
    StateChange last{};

    static void
    record(void *const context, const StateChange &change) noexcept {
        auto &recorder = *static_cast<NotificationChangeRecorder *>(context);
        ++recorder.count;
        recorder.last = change;
    }

    [[nodiscard]] Observer
    observer() noexcept {
        return {&record, nullptr, this};
    }
};

struct AdaptiveSnapshotLog {
    unsigned count = 0U;
    std::array<char, 256> last{};

    static void
    write(void *const context, const std::string_view message) noexcept {
        auto &self = *static_cast<AdaptiveSnapshotLog *>(context);
        ++self.count;
        self.last.fill('\0');
        const std::size_t length = std::min(message.size(), self.last.size() - 1U);
        std::memcpy(self.last.data(), message.data(), length);
    }
};

void
test_live_tuning_actions_and_native_passthrough() {
    Image image;
    write_u32(image.base(), kConfiguredMode, 3U);
    write_u32(image.base(), kCubemapGameplayGate, 6U);
    AdaptiveSnapshotLog log;
    State state(
        image.base(), 1000000U,
        ReflectionConfig{
            .scheduler = Config{75U},
            .blending = {true, 6U},
            .hotkeys = {0x77U, 0x78U, 0x79U, 0x7aU, 0x7bU},
            .notifications_enabled = true
        },
        {}, &AdaptiveSnapshotLog::write, &log
    );
    state.apply_hotkey_sample(true, {}); // first foreground baseline
    const auto press = [&](std::size_t index) {
        std::array<bool, hotkeys::kCount> down{};
        down[index] = true;
        state.apply_hotkey_sample(true, {});
        state.apply_hotkey_sample(true, down);
    };
    press(0U); // adaptive OFF, use the exact game pointer for every setting
    CHECK(!state.controller_enabled && state.controller_installed);
    CHECK(state.controller.full_refresh_pending());
    const auto native = image.base() + 0x100U;
    for (std::uint32_t mode = 0U; mode < 4U; ++mode)
        CHECK(state.select_mask(native, mode, true, 1U, 100U) == native);
    CHECK(log.count == 1U);
    CHECK(std::string_view(log.last.data()).find("Adaptive=off") != std::string_view::npos);
    press(1U); // FPS decreases even while native bypass is active
    CHECK(state.controller.target_fps() == 70U);
    press(2U);
    CHECK(state.controller.target_fps() == 75U);
    press(3U);
    CHECK(state.blend.mode() == blending::Mode::off);
    press(4U); // pending new step count while blending is OFF
    CHECK(state.blend.steps() == 2U && state.blend.mode() == blending::Mode::off);
    CHECK(log.count == 5U);
    CHECK(std::string_view(log.last.data()).find("BlendSteps=2") != std::string_view::npos);
    press(0U);
    CHECK(state.controller_enabled);
    CHECK(state.controller.full_refresh_pending());
    CHECK(state.controller.target_fps() == 75U);
    CHECK(log.count == 6U);
    state.begin_renderer(100U, 2U);
    CHECK(state.select_mask(native, 3U, true, 2U, 100U) != native);
    for (std::size_t i = 0; i < kFaceCount; ++i)
        CHECK(state.custom_mask_words[i] == 1U);
}

void
test_notification_state_change_integration() {
    Image image;
    write_u32(image.base(), kConfiguredMode, 3U);
    write_u32(image.base(), kCubemapGameplayGate, 6U);
    NotificationChangeRecorder recorder;
    AdaptiveSnapshotLog log;
    State state(
        image.base(), 1000U, ReflectionConfig{.notifications_enabled = true}, recorder.observer(),
        &AdaptiveSnapshotLog::write, &log
    );
    StateChange down{};
    down.previous_budget = 6U;
    down.new_budget = 1U;
    down.reason = StateChangeReason::repeated_overload;
    down.cadence_budget_qpc = 17U;
    state.on_state_change(down);
    CHECK(recorder.count == 1U);
    CHECK(state.pending_adaptive_transition.valid);
    Selection stale{};
    stale.face_mask = 0x3fU;
    stale.budget = 6U;
    stale.custom = true;
    state.resolve_adaptive_transition(3U, true, stale);
    CHECK(log.count == 0U);
    Selection weighted_single_face{};
    weighted_single_face.face_mask = 0x10U;
    weighted_single_face.budget = 1U;
    weighted_single_face.custom = true;
    state.resolve_adaptive_transition(3U, true, weighted_single_face);
    CHECK(!state.pending_adaptive_transition.valid);
    CHECK(state.screen_messages.test_adaptive_pending());
    CHECK(log.count == 0U);
    state.screen_messages.update(1U);
    CHECK(state.screen_messages.test_adaptive_visible());
    StateChange up{};
    up.previous_budget = 1U;
    up.new_budget = 6U;
    up.reason = StateChangeReason::sustained_headroom;
    state.on_state_change(up);
    Selection full{};
    full.face_mask = 0x3fU;
    full.budget = 6U;
    full.custom = true;
    state.resolve_adaptive_transition(3U, true, full);
    CHECK(log.count == 0U);

    AdaptiveSnapshotLog quiet_log;
    State quiet_state(image.base(), 1000U, ReflectionConfig{}, {}, &AdaptiveSnapshotLog::write, &quiet_log);
    // With LiveTuning off there is no UI transition bookkeeping or callback in the quiet release variant,
    // but diagnostics must still receive events.
    CHECK(State::make_observer(&quiet_state, {}, false).state_change == nullptr);
    quiet_state.on_state_change(down);
    CHECK(!quiet_state.pending_adaptive_transition.valid);
    quiet_state.resolve_adaptive_transition(3U, true, weighted_single_face);
    CHECK(quiet_log.count == 0U);
    CHECK(!quiet_state.screen_messages.test_adaptive_pending());

    State diagnostic_quiet(image.base(), 1000U, ReflectionConfig{}, recorder.observer());
    const Observer forwarded = State::make_observer(&diagnostic_quiet, recorder.observer(), false);
    CHECK(forwarded.state_change != nullptr);
    forwarded.state_change(forwarded.context, down);
    CHECK(recorder.count == 3U);
    CHECK(!diagnostic_quiet.pending_adaptive_transition.valid);

    state.on_state_change(down);
    CHECK(state.pending_adaptive_transition.valid);
    state.resolve_adaptive_transition(3U, false, {});
    CHECK(!state.pending_adaptive_transition.valid);

    write_u32(image.base(), kConfiguredMode, 0U);
    State zero(
        image.base(), 1000U, ReflectionConfig{.notifications_enabled = true}, {}, &AdaptiveSnapshotLog::write,
        &quiet_log
    );
    zero.on_state_change(down);
    CHECK(!zero.pending_adaptive_transition.valid);
}

void
test_blending_gap_without_adaptive_controller() {
    MockBlendDevice device;
    write_u32(device.base(), kConfiguredMode, 1U);
    State state(device.base(), 1000U, ReflectionConfig{.blending = {true, 2U}, .adaptive_enabled = false}, {}, quiet);
    CHECK(!state.controller_enabled);

    state.begin_renderer(100U, 1U);
    const std::uintptr_t native_mask = reinterpret_cast<std::uintptr_t>(&device.native);
    CHECK(state.select_mask(native_mask, 1U, true, 1U, 100U) == native_mask);
    SavedContext context{};
    context.eax = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&device.native));
    state.blend.begin_face(0U, &context);
    device.simulate_native_face_begin(context);
    state.blend.end_face(0U, &context);
    CHECK(device.composite_calls == 1U);

    // The shared runtime retains the controller's >250 ms invalidation rule even when adaptive learning is disabled.
    state.begin_renderer(351U, 2U);
    CHECK(state.select_mask(native_mask, 1U, true, 2U, 351U) == native_mask);
    context.eax = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&device.native));
    state.blend.begin_face(0U, &context);
    CHECK(device.composite_calls == 1U);
    CHECK(context.eax != reinterpret_cast<std::uintptr_t>(&device.native));
    state.blend.cancel();
    state.blend.release();
}

void
test_adaptive_weighted_single_face_blending_ui2_and_full_bypass() {
    MockBlendDevice device;
    write_u32(device.base(), kConfiguredMode, 2U);
    State state(device.base(), 1000000U, ReflectionConfig{.blending = {true, 2U}}, {}, quiet);
    constexpr std::uintptr_t native_mask = 0x12345678U;
    // Initial refresh uses all faces, never a blend redirected to an uninitialized cubemap.
    state.begin_renderer(1000U, 1U);
    auto selected = state.select_mask(native_mask, 2U, true, 1U, 1001U);
    CHECK(selected != native_mask);
    CHECK(!state.blend.active());
    CHECK(state.controller.full_refresh_pending());
    std::uint64_t cursor = 1100U;
    for (unsigned f = 0U; f < 6U; ++f) {
        state.controller.begin_face(f, cursor);
        cursor += 100U;
        state.controller.end_face(f, cursor);
        ++cursor;
    }
    state.controller.before_present(2000U);
    state.controller.after_present(2100U, true);
    CHECK(!state.controller.full_refresh_pending());
    // Two completed overloaded frames move the automatic policy to one face.
    for (std::uint32_t frame = 2U; frame <= 3U; ++frame) {
        const std::uint64_t start = 1000U + (frame - 1U) * 30000U;
        state.begin_renderer(start, frame);
        static_cast<void>(state.select_mask(native_mask, 2U, true, frame, start + 1U));
        for (unsigned face = 0U; face < 6U; ++face) {
            state.controller.begin_face(face, start + 100U + face * 200U);
            state.controller.end_face(face, start + 200U + face * 200U);
        }
        state.controller.before_present(start + 20000U);
        state.controller.after_present(start + 20100U, true);
    }
    state.begin_renderer(91000U, 4U);
    selected = state.select_mask(native_mask, 2U, true, 4U, 91001U);
    const auto *const words = reinterpret_cast<const std::uint32_t *>(selected);
    CHECK(words[4U] == 1U);
    for (std::size_t f = 0; f < 6U; ++f)
        if (f != 4U)
            CHECK(words[f] == 0U);
    CHECK(state.blend.active());
    // Weighted selection is distinct from native mode 1.
    CHECK(state.blend.test_weighted_single_face());
    SavedContext context{};
    context.eax = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&device.native));
    // Face 4 is renderer view 0.
    // Adapter must keep descriptor indexing separate from the physical-face scheduler mask.
    state.blend.begin_face(0U, &context);
    CHECK(context.eax != reinterpret_cast<std::uintptr_t>(&device.native));
    device.simulate_native_face_begin(context);
    state.blend.end_face(0U, &context);
    CHECK(device.composite_calls == 1U);
    CHECK(state.blend.test_pending_count() == 1U);
    // A shared gap must both request fresh contents and retire the old fade.
    state.begin_renderer(400001U, 5U);
    CHECK(state.controller.full_refresh_pending());
    CHECK(state.blend.test_pending_count() == 0U);
    static_cast<void>(state.select_mask(native_mask, 2U, true, 5U, 400002U));
    CHECK(!state.blend.active());
    state.blend.release();
}

void
test_blending_observation_gap() {
    for (const std::uint32_t steps : {2U, 3U, 4U, 5U, 6U}) {
        for (const std::uint64_t next : {99U, 100U, 101U, 350U, 351U}) {
            MockBlendDevice device;
            State state(
                device.base(), 1000U, ReflectionConfig{.blending = {true, steps}, .adaptive_enabled = false}, {}, quiet
            );
            const std::uintptr_t native_mask = reinterpret_cast<std::uintptr_t>(&device.native);

            state.begin_renderer(100U, 1U);
            CHECK(state.select_mask(native_mask, 1U, true, 1U, 100U) == native_mask);
            SavedContext context{};
            context.eax = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&device.native));
            state.blend.begin_face(0U, &context);
            device.simulate_native_face_begin(context);
            state.blend.end_face(0U, &context);
            CHECK(device.composite_calls == 1U);

            state.begin_renderer(next, 2U);
            CHECK(state.select_mask(native_mask, 1U, true, 2U, next) == native_mask);
            context.eax = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&device.native));
            state.blend.begin_face(0U, &context);
            // Ordinary cadence and exactly 250 ms retain the old fade.
            // Equal/reversed timestamps and longer gaps discard it.
            const bool cancelled = next <= 100U || next > 350U;
            CHECK(device.composite_calls == (cancelled ? 1U : (steps == 2U ? 2U : 3U)));
            CHECK(context.eax != reinterpret_cast<std::uintptr_t>(&device.native));
            CHECK(!state.blend.failed());
            state.blend.cancel();
            state.blend.release();
        }
    }
}

void
test_device_reset_and_failed_present() {
    for (const bool adaptive : {false, true}) {
        MockBlendDevice device;
        write_u32(device.base(), kConfiguredMode, 1U);
        State state(
            device.base(), 1000U,
            ReflectionConfig{.blending = {true, 3U}, .adaptive_enabled = adaptive, .notifications_enabled = true}, {},
            quiet
        );
        state.producer_thread = GetCurrentThreadId();
        const ScopedTestState binding(state);
        const auto capture = [&] {
            state.blend.select_schedule(Schedule::native_single);
            SavedContext face{};
            face.eax = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&device.native));
            state.blend.begin_face(0U, &face);
            device.simulate_native_face_begin(face);
            state.blend.end_face(0U, &face);
            CHECK(state.blend.test_pending_count() == 1U);
        };

        state.begin_renderer(100U, 1U);
        capture();
        const unsigned allocated = device.texture_creations;
        CHECK(allocated != 0U);
        SavedContext context{};
        context.eax = 0x80004005U;
        mwperffix_adaptive_dispatch(static_cast<std::uint32_t>(Site::post_present), &context);
        CHECK(state.blend.test_pending_count() == 0U);
        CHECK(state.blend.test_resources_ready());
        CHECK(device.resource_releases == 0U);

        state.begin_renderer(200U, 2U);
        if (adaptive) {
            CHECK(state.controller.select(3U, true, 201U).face_mask == kAllFaceMask);
            for (std::size_t face = 0U; face < kFaceCount; ++face) {
                state.controller.begin_face(face, 202U + face * 2U);
                state.controller.end_face(face, 203U + face * 2U);
            }
            state.controller.before_present(220U);
            state.controller.after_present(221U, true);
            CHECK(!state.controller.full_refresh_pending());
        }
        capture();
        state.pending_adaptive_transition.valid = true;
        state.screen_messages.post_text("blending on");
        CHECK(state.screen_messages.test_pending() != notifications::Message::none);
        mwperffix_adaptive_dispatch(static_cast<std::uint32_t>(Site::reset_begin), &context);
        CHECK(!state.pending_adaptive_transition.valid);
        CHECK(state.screen_messages.test_pending() == notifications::Message::none);
        CHECK(state.blend.test_pending_count() == 0U);
        CHECK(!state.blend.test_resources_ready());
        CHECK(state.controller.full_refresh_pending());
        const unsigned released = device.resource_releases;
        CHECK(released == allocated * 3U);
        mwperffix_adaptive_dispatch(static_cast<std::uint32_t>(Site::reset_begin), &context);
        CHECK(device.resource_releases == released);

        state.begin_renderer(300U, 3U);
        CHECK(state.blend.test_resources_ready());
        CHECK(device.texture_creations == allocated * 2U);
        capture();
        CHECK(!state.blend.failed());
        state.blend.cancel();
        state.blend.release();
    }
}

void
test_blending_device_failures() {
    // An early SetRenderState error must leave the actual device on the native face,
    // not only change the game's target descriptor bookkeeping.
    {
        MockBlendDevice device;
        BlendRuntime blend({true, 2U}, quiet, nullptr);
        blend.set_base(device.base());
        blend.begin_frame();
        blend.select_schedule(Schedule::native_single);
        SavedContext context{};
        context.eax = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&device.native));
        blend.begin_face(0U, &context);
        CHECK(context.eax != reinterpret_cast<std::uintptr_t>(&device.native));
        device.simulate_native_face_begin(context);
        device.reject_blend_state = true;
        blend.end_face(0U, &context);
        CHECK(blend.failed());
        CHECK(device.composite_calls == 0U);
        CHECK(device.native_target_bound());
        CHECK(read_pointer(device.base(), kCurrentTarget) == &device.native);
        blend.release();
    }
    // Either sampler API error cancels the reservation before EAX is changed.
    for (const bool fail_get : {true, false}) {
        MockBlendDevice device;
        BlendRuntime blend({true, 2U}, quiet, nullptr);
        blend.set_base(device.base());
        blend.begin_frame();
        blend.select_schedule(Schedule::native_single);
        device.bound_texture = &device.texture;
        device.reject_get_texture = fail_get;
        device.reject_unbind = !fail_get;
        SavedContext context{};
        context.eax = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&device.native));
        blend.begin_face(0U, &context);
        CHECK(blend.failed());
        CHECK(context.eax == reinterpret_cast<std::uintptr_t>(&device.native));
        CHECK(device.bound_texture == &device.texture);
        blend.release();
    }
    // The native compositor can return a successful viewport result after an earlier restore error.
    // Our checked restore repairs the actual device.
    for (const bool fail_composite : {false, true}) {
        MockBlendDevice device;
        device.reject_composite = fail_composite;
        BlendRuntime blend({true, 2U}, quiet, nullptr);
        blend.set_base(device.base());
        blend.begin_frame();
        blend.select_schedule(Schedule::native_single);
        SavedContext context{};
        context.eax = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&device.native));
        blend.begin_face(0U, &context);
        device.simulate_native_face_begin(context);
        blend.end_face(0U, &context);
        CHECK(device.composite_calls == 1U);
        CHECK(device.native_target_bound());
        CHECK(blend.failed() == fail_composite);
        blend.release();
    }
}

void
test_blending_discarded_publication_results() {
    for (const unsigned scenario : {0U, 1U, 2U, 3U}) {
        MockBlendDevice device;
        device.observed_draws = scenario == 0U ? 0U : scenario == 1U ? 2U : 1U;
        device.discarded_failure = scenario == 2U;
        BlendRuntime blend({true, 3U}, quiet, nullptr);
        blend.set_base(device.base());
        blend.begin_frame();
        blend.select_schedule(Schedule::native_single);
        SavedContext context{};
        context.eax = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&device.native));
        blend.begin_face(0U, &context);
        device.simulate_native_face_begin(context);
        blend.end_face(0U, &context);
        CHECK(blend.failed() == (scenario != 3U));
        CHECK(blend.test_pending_count() == (scenario == 3U ? 1U : 0U));
        CHECK(device.native_target_bound());
        if (scenario != 3U) {
            const auto calls = device.composite_calls;
            blend.begin_frame();
            blend.select_schedule(Schedule::native_single);
            CHECK(!blend.active());
            CHECK(!blend.toggle());
            CHECK(device.composite_calls == calls);
        }
        blend.release();
    }
}

// The machine-state driver seeds all GPRs, EFLAGS, x87, MXCSR and XMM0..7, snapshots them and invokes machine_target.
// Reuse it for the adaptive relays.
void
test_executable_contracts() {
    for (std::size_t site_index = 0; site_index < kSites.size(); ++site_index) {
        // Two copies: native instructions versus an adaptive relay
        // whose observer deliberately clobbers volatile/FP state and last-error.
        void *const page = VirtualAlloc(nullptr, 4096U, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        CHECK(page != nullptr);
        const auto base = reinterpret_cast<std::uintptr_t>(page);
        const auto data = base + 3500U;
        std::array<std::array<std::uint32_t, 9>, 2> gpr{};
        std::array<std::array<std::uint8_t, 512>, 2> fp{};
        const auto &site = kSites[site_index];
        for (unsigned variant = 0; variant < 2U; ++variant) {
            const auto entry = base + variant * 1024U;
            const auto original = entry + 64U;
            const auto relay = entry + 256U;
            std::vector<std::uint8_t> setup;
            if (site.id == Site::scheduler) {
                setup = {0xB9, 3, 0, 0, 0, 0xB8, 17, 0, 0, 0, 0xBA, 2, 0, 0, 0};
                *reinterpret_cast<std::uint32_t *>(data + 12U) = 6U;
            }
            if (site.id == Site::post_present)
                setup = {0xB8, 0x68, 0x08, 0x76, 0x88};
            setup.push_back(0xE9);
            append_rel32(setup, entry + setup.size() - 1U, original);
            std::memcpy(reinterpret_cast<void *>(entry), setup.data(), setup.size());
            auto replay = std::vector<std::uint8_t>(site.expected.begin(), site.expected.end());
            if (site.id == Site::renderer_begin)
                put_u32(replay, 1U,
                    data); // Relocate the absolute operand into test memory.
            if (site.id == Site::scheduler)
                put_u32(replay, 3U, data);
            if (site.id == Site::face_end)
                put_u32(replay, 2U, data);
            if (site.id == Site::pre_present)
                put_u32(replay, 1U, data);
            if (site.id == Site::face_begin) {
                // Native helper is cdecl:
                // both arguments remain on the stack across the diagnostic post-helper observer until continuation.
                const std::array<std::uint8_t, 5> callee{0x8B, 0x44, 0x24, 0x04, 0xC3};
                std::memcpy(reinterpret_cast<void *>(entry + 192U), callee.data(), callee.size());
                put_u32(replay, 4U, static_cast<std::uint32_t>(entry + 192U - (original + 8U)));
            }
            std::memcpy(reinterpret_cast<void *>(original), replay.data(), replay.size());
            std::vector<std::uint8_t> tail{0xC3};
            if (site.id == Site::face_begin)
                tail = {0x83, 0xC4, 0x08, 0xC3};
            std::memcpy(reinterpret_cast<void *>(original + replay.size()), tail.data(), tail.size());
            if (variant == 1U) {
                SiteDefinition local = site;
                local.expected = replay;
                auto code = build_relay(local, original, relay);
                const std::size_t observer_displacement = site.id == Site::scheduler ? 41U : 34U;
                put_u32(
                    code, observer_displacement,
                    static_cast<std::uint32_t>(
                        reinterpret_cast<std::uintptr_t>(&machine_observer) - (relay + observer_displacement + 4U)
                    )
                );
                std::memcpy(reinterpret_cast<void *>(relay), code.data(), code.size());
                const auto branch = branch_to(original, relay, replay.size());
                std::memcpy(reinterpret_cast<void *>(original), branch.data(), branch.size());
            }
        }
        DWORD old = 0;
        // Data slots must remain writable for the native face-end store.
        CHECK(VirtualProtect(page, 4096U, PAGE_EXECUTE_READWRITE, &old) != FALSE);
        CHECK(FlushInstructionCache(GetCurrentProcess(), page, 4096U) != FALSE);
        for (unsigned variant = 0; variant < 2U; ++variant) {
            *reinterpret_cast<std::uint32_t *>(data) = 0x12345678U;
            machine_target = base + variant * 1024U;
            SetLastError(0x1234);
            machine_driver();
            CHECK(GetLastError() == 0x1234);
            std::memcpy(gpr[variant].data(), machine_after_gpr, sizeof(machine_after_gpr));
            std::memcpy(fp[variant].data(), machine_after_fp, sizeof(machine_after_fp));
        }
        CHECK(machine_alignment == 0U);
        CHECK(gpr[0] == gpr[1]);
        CHECK(std::memcmp(fp[0].data(), fp[1].data(), 5U) == 0);
        CHECK(std::memcmp(fp[0].data() + 6U, fp[1].data() + 6U, 26U) == 0);
        for (std::size_t i = 0; i < 8U; ++i) {
            CHECK(std::memcmp(fp[0].data() + 32U + i * 16U, fp[1].data() + 32U + i * 16U, 10U) == 0);
        }
        CHECK(std::memcmp(fp[0].data() + 160U, fp[1].data() + 160U, 128U) == 0);
        CHECK(VirtualFree(page, 0, MEM_RELEASE) != FALSE);
    }
}

void
test_relay_lifecycle(const bool benchmark = false) {
    Image image;
    const std::uint32_t gameplay = 6U;
    std::memcpy(image.bytes_.data() + rva(kCubemapGameplayGate), &gameplay, 4U);
    void *const page = VirtualAlloc(nullptr, 16384U, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    CHECK(page != nullptr);
    const auto base = reinterpret_cast<std::uintptr_t>(page);
    std::array<std::uintptr_t, kSites.size()> entries{};
    for (std::size_t index = 0; index < kSites.size(); ++index) {
        const auto &site = kSites[index];
        const auto source = base + index * 1024U;
        const auto relay = source + 128U;
        entries[index] = source;
        auto replay = std::vector<std::uint8_t>(site.expected.begin(), site.expected.end());
        if (site.id == Site::renderer_begin)
            put_u32(replay, 1U, image.base() + rva(0x901AC8U));
        if (site.id == Site::scheduler)
            put_u32(replay, 3U, image.base() + rva(0x8F9028U));
        if (site.id == Site::face_end)
            put_u32(replay, 2U, image.base() + rva(0x982C80U));
        if (site.id == Site::pre_present)
            put_u32(replay, 1U, image.base() + rva(0x982BDCU));
        if (site.id == Site::face_begin) {
            const std::array<std::uint8_t, 3> callee{0xC2, 0x08, 0x00};
            std::memcpy(reinterpret_cast<void *>(source + 64U), callee.data(), callee.size());
            put_u32(replay, 4U,
                56U); // call at source+3, next at +8, target +64
        }
        SiteDefinition local = site;
        local.expected = replay;
        const auto code = build_relay(local, source, relay);
        std::memcpy(reinterpret_cast<void *>(relay), code.data(), code.size());
        const auto branch = branch_to(source, relay, replay.size());
        std::memcpy(reinterpret_cast<void *>(source), branch.data(), branch.size());
        std::vector<std::uint8_t> tail{0xC3};
        std::memcpy(reinterpret_cast<void *>(source + replay.size()), tail.data(), tail.size());
    }

    DWORD old = 0;
    CHECK(VirtualProtect(page, 16384U, PAGE_EXECUTE_READ, &old) != FALSE);
    CHECK(FlushInstructionCache(GetCurrentProcess(), page, 16384U) != FALSE);
    LARGE_INTEGER frequency{};
    CHECK(QueryPerformanceFrequency(&frequency) != FALSE && frequency.QuadPart > 0);
    const std::uint32_t configured = 3U;
    std::memcpy(image.bytes_.data() + rva(kConfiguredMode), &configured, 4U);
    for (unsigned faces : {1U, 6U}) {
        State state(image.base(), static_cast<std::uint64_t>(frequency.QuadPart), ReflectionConfig{});
        const ScopedTestState binding(state);
        std::uint64_t elapsed = 0U;
        const std::uint32_t frames = benchmark ? 20000U : 2U;
        for (std::uint32_t frame = 1U; frame <= frames; ++frame) {
            state.controller = Controller(static_cast<std::uint64_t>(frequency.QuadPart), Config{});
            state.renderer_clock = RendererClock(static_cast<std::uint64_t>(frequency.QuadPart));
            if (faces == 1U) {
                // Synthetic Native full-update seed followed by two pressured frames.
                // All timestamps precede the real relay frame being measured.
                const auto ticks_per_second = static_cast<std::uint64_t>(frequency.QuadPart);
                const std::uint64_t seed = qpc_now() - ticks_per_second / 10U;
                for (unsigned warm = 0U; warm < 3U; ++warm) {
                    const std::uint64_t start = seed + warm * (ticks_per_second / 40U);
                    state.controller.begin_renderer(start, frame + warm + 1U, state.renderer_clock.observe(start));
                    const Selection selection = state.controller.select(3U, true, start + 1U);
                    CHECK(selection.face_mask == kAllFaceMask);
                    for (std::size_t face = 0; face < kFaceCount; ++face) {
                        state.controller.begin_face(face, start + 2U + face * 2U);
                        state.controller.end_face(face, start + 3U + face * 2U);
                    }
                    const std::uint64_t expensive = start + ticks_per_second / 50U;
                    state.controller.before_present(expensive);
                    state.controller.after_present(expensive + 1U, true);
                }
                CHECK(!state.controller.full_refresh_pending());
                CHECK(state.controller.effective_budget() == 1U);
            }

            std::memcpy(image.bytes_.data() + rva(kGameFrame), &frame, 4U);
            const auto begin = qpc_now();
            static_cast<void>(adaptive_invoke(entries[0], 0U, 0U, 0x5AU));
            static_cast<void>(adaptive_invoke(entries[1], 0U, 3U, 0U));
            const auto *const mask = reinterpret_cast<const std::uint32_t *>(adaptive_result_eax);
            CHECK(mask != nullptr);
            unsigned selected = 0U;
            // Mask words are native-face indexed.
            // Face callbacks identify a renderer view, so translate only when fabricating callback ESI.
            constexpr std::array<std::size_t, kFaceCount> native_to_view{1U, 3U, 4U, 5U, 0U, 2U};
            for (std::size_t face = 0; face < kFaceCount; ++face) {
                CHECK(mask[face] <= 1U);
                if (mask[face] == 0U)
                    continue;
                ++selected;
                const auto esi =
                    static_cast<std::uint32_t>(image.base() + rva(kCubemapViews) + native_to_view[face] * kViewStride);
                static_cast<void>(adaptive_invoke(entries[2], esi, 0U, 0U));
                static_cast<void>(adaptive_invoke(entries[3], esi, 0U, 0U));
            }
            CHECK(selected == faces);
            static_cast<void>(adaptive_invoke(entries[4], 0U, 0U, 0U));
            static_cast<void>(adaptive_invoke(entries[5], 0U, 0U, 0U));
            elapsed += qpc_now() - begin;
            CHECK(!state.controller.full_refresh_pending());
        }
        std::printf(
            "adaptive valid %u-face frame: %.3f us/frame (all relays, QPC, "
            "dispatch, "
            "controller; synthetic harness included)\n",
            faces, static_cast<double>(elapsed) * 1000000.0 / static_cast<double>(frequency.QuadPart) / frames
        );
    }
    // Execute the production renderer-entry relay under the hostile machine driver.
    // The observer clobbers AL/FP state and last-error deliberately.
    State state(image.base(), static_cast<std::uint64_t>(frequency.QuadPart), ReflectionConfig{});
    const ScopedTestState binding(state);
    machine_target = entries[0];
    SetLastError(0x1234);
    machine_driver();
    CHECK(GetLastError() == 0x1234);
    CHECK(state.controller.active_frame(read_u32(state.base, kGameFrame)));
    // A reset after a completed refresh invalidates contents even if Present succeeds.
    // Execute the seventh production relay, not just a method call.
    static_cast<void>(adaptive_invoke(entries[6], 0U, 0U, 0U));
    CHECK(state.controller.full_refresh_pending());
    CHECK(VirtualFree(page, 0, MEM_RELEASE) != FALSE);
}

} // namespace

// Run every real call encoding through its relay, including both indirect and relocated direct calls,
// against a tiny integer-only native callee.
DWORD WINAPI
publication_foreign_thread(void *context) {
    return static_cast<publication::Checks *>(context)->current() == nullptr ? 0U : 1U;
}

DWORD WINAPI
publication_foreign_relay(void *entry) {
    SetLastError(0x5678);
    reinterpret_cast<void(__cdecl *)()>(entry)();
    return GetLastError() == 0x5678 ? 0U : 1U;
}

void
test_publication_relays() {
    publication::Checks checks;
    CHECK(checks.prepare());
    std::vector<DWORD> extra_slots;
    DWORD expanded = TLS_OUT_OF_INDEXES;
    do {
        expanded = TlsAlloc();
        CHECK(expanded != TLS_OUT_OF_INDEXES);
        extra_slots.push_back(expanded);
    } while (expanded < 64U);
    for (const DWORD tls : {checks.index(), expanded}) {
        for (const auto &original_site : publication::calls) {
            for (const bool failed : {false, true}) {
                for (const bool active : {false, true}) {
                    RelayPage page(4096U);
                    CHECK(page.valid());
                    const auto base = page.address();
                    const auto source = base + 128U;
                    const auto callee = base + 192U;
                    const auto entry = base + 512U;
                    const auto table = base + 2048U;
                    publication::CallSite site = original_site;
                    if (site.bytes[0] == 0xE8U) {
                        const auto displacement = static_cast<std::uint32_t>(callee - source - 5U);
                        std::memcpy(site.bytes.data() + 1U, &displacement, 4U);
                    } else {
                        // All indirect encodings use EAX, ECX or EDX as table base.
                        // Populate the whole table with the same no-argument callee.
                        for (unsigned i = 0; i < 128U; ++i)
                            *reinterpret_cast<std::uintptr_t *>(table + i * 4U) = callee;
                    }
                    // The description's displaced absolute load is test-local.
                    if (site.kind == publication::Kind::description) {
                        const auto address = static_cast<std::uint32_t>(table);
                        std::memcpy(site.bytes.data() + 4U, &address, 4U);
                    }
                    auto code = publication::relay(site, source, entry, tls);
                    std::memcpy(reinterpret_cast<void *>(entry), code.data(), code.size());
                    std::vector<std::uint8_t> prefix{
                        0x81, 0xEC, 0x40, 0x01, 0, 0, 0x55,
                        0x56, // native GetDesc frame
                        0xB8
                    };
                    publication::append_u32_le(prefix, table);
                    prefix.push_back(0xB9);
                    publication::append_u32_le(prefix, table);
                    prefix.push_back(0xBA);
                    publication::append_u32_le(prefix, table);
                    prefix.push_back(0xE9);
                    publication::relative(prefix, base, entry);
                    std::memcpy(reinterpret_cast<void *>(base), prefix.data(), prefix.size());
                    // Tail marker makes the failed-description early return observable.
                    std::vector<std::uint8_t> tail{0xC7, 0x05};
                    publication::append_u32_le(tail, base + 3500U);
                    publication::append_u32_le(tail, 1U);
                    tail.insert(tail.end(), {0x5E, 0x5D, 0x81, 0xC4, 0x40, 0x01, 0, 0, 0xC3});
                    std::memcpy(reinterpret_cast<void *>(source + site.size), tail.data(), tail.size());
                    std::vector<std::uint8_t> stub{0xB8};
                    publication::append_u32_le(stub, failed ? 0x80004005U : 0U);
                    stub.push_back(0xC3);
                    std::memcpy(reinterpret_cast<void *>(callee), stub.data(), stub.size());
                    DWORD old = 0;
                    CHECK(VirtualProtect(reinterpret_cast<void *>(base), 4096U, PAGE_EXECUTE_READWRITE, &old));
                    CHECK(FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void *>(base), 4096U));
                    publication::Result result;
                    CHECK(TlsSetValue(tls, active ? &result : nullptr));
                    if (tls == expanded && active && failed && site.kind == publication::Kind::draw) {
                        HANDLE thread = CreateThread(
                            nullptr, 0U, publication_foreign_relay, reinterpret_cast<void *>(base), 0U, nullptr
                        );
                        CHECK(thread != nullptr);
                        CHECK(WaitForSingleObject(thread, INFINITE) == WAIT_OBJECT_0);
                        DWORD status = 1U;
                        CHECK(GetExitCodeThread(thread, &status) && status == 0U);
                        CHECK(CloseHandle(thread));
                        CHECK(result.draws == 0U && result.failed == 0U);
                    }
                    SetLastError(0x1234);
                    machine_target = base;
                    machine_driver();
                    CHECK(GetLastError() == 0x1234);
                    CHECK(TlsSetValue(tls, nullptr));
                    CHECK(result.failed == (failed && active ? 1U : 0U));
                    CHECK(result.draws == (active && site.kind == publication::Kind::draw ? 1U : 0U));
                    CHECK(
                        *reinterpret_cast<std::uint32_t *>(base + 3500U) ==
                        (active && failed && site.kind == publication::Kind::description ? 0U : 1U)
                    );
                    // Compare to the original instructions for paths without an intentional early return.
                    if (!(active && failed && site.kind == publication::Kind::description)) {
                        std::array<std::uint32_t, 9> gpr;
                        std::array<std::uint8_t, 512> fp;
                        std::memcpy(gpr.data(), machine_after_gpr, sizeof(machine_after_gpr));
                        std::memcpy(fp.data(), machine_after_fp, sizeof(machine_after_fp));
                        std::memcpy(reinterpret_cast<void *>(source), site.bytes.data(), site.size);
                        const auto displacement = static_cast<std::uint32_t>(source - (base + prefix.size()));
                        std::memcpy(reinterpret_cast<void *>(base + prefix.size() - 4U), &displacement, 4U);
                        CHECK(FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void *>(base), 4096U));
                        SetLastError(0x1234);
                        machine_driver();
                        CHECK(GetLastError() == 0x1234);
                        CHECK(std::memcmp(gpr.data(), machine_after_gpr, sizeof(machine_after_gpr)) == 0);
                        CHECK(std::memcmp(fp.data(), machine_after_fp, 5U) == 0);
                        CHECK(std::memcmp(fp.data() + 6U, machine_after_fp + 6U, 26U) == 0);
                        for (std::size_t i = 0; i < 8U; ++i)
                            CHECK(std::memcmp(fp.data() + 32U + i * 16U, machine_after_fp + 32U + i * 16U, 10U) == 0);
                        CHECK(std::memcmp(fp.data() + 160U, machine_after_fp + 160U, 128U) == 0);
                    }
                }
            }
        }
    }
    for (const auto slot : extra_slots)
        CHECK(TlsFree(slot));
    // Missing/multiple draws, sticky failures, reentrancy and foreign-thread isolation.
    SetLastError(123);
    {
        publication::Scope outer(checks);
        CHECK(outer.active());
        CHECK(!outer.accepted());
        ++checks.current()->draws;
        CHECK(outer.accepted());
        auto thread = CreateThread(nullptr, 0U, publication_foreign_thread, &checks, 0U, nullptr);
        CHECK(thread != nullptr);
        CHECK(WaitForSingleObject(thread, INFINITE) == WAIT_OBJECT_0);
        DWORD result = 1;
        CHECK(GetExitCodeThread(thread, &result) && result == 0U);
        CHECK(CloseHandle(thread));
        {
            publication::Scope nested(checks);
            CHECK(!nested.active());
        }
        CHECK(!outer.accepted());
    }
    CHECK(checks.current() == nullptr);
    {
        publication::Scope scope(checks);
        checks.current()->draws = 2U;
        CHECK(!scope.accepted());
    }
}

void
benchmark_publication_checks() {
    publication::Checks checks;
    CHECK(checks.prepare());
    RelayPage page(1024U);
    CHECK(page.valid());
    const auto base = page.address();
    const auto source = base + 512U;
    const auto callee = base + 768U;
    publication::CallSite site{0U, {0xE8}, 5U, 5U, publication::Kind::setup};
    const auto displacement = static_cast<std::uint32_t>(callee - source - 5U);
    std::memcpy(site.bytes.data() + 1U, &displacement, 4U);
    std::memcpy(reinterpret_cast<void *>(source), site.bytes.data(), site.size);
    *reinterpret_cast<std::uint8_t *>(source + 5U) = 0xC3;
    const std::array<std::uint8_t, 3> stub{0x31, 0xC0, 0xC3};
    std::memcpy(reinterpret_cast<void *>(callee), stub.data(), stub.size());
    CHECK(page.seal(publication::relay(site, source, base, checks.index())));
    LARGE_INTEGER frequency{};
    CHECK(QueryPerformanceFrequency(&frequency));
    const auto measure = [&](const std::uintptr_t address) {
        std::array<double, 5> timings{};
        for (auto &time : timings) {
            LARGE_INTEGER start{}, end{};
            QueryPerformanceCounter(&start);
            for (unsigned i = 0; i < 1000000U; ++i)
                static_cast<void>(reinterpret_cast<long(__cdecl *)()>(address)());
            QueryPerformanceCounter(&end);
            time =
                static_cast<double>(end.QuadPart - start.QuadPart) * 1000.0 / static_cast<double>(frequency.QuadPart);
        }
        std::sort(timings.begin(), timings.end());
        return timings[2]; // ns/call for one million iterations
    };
    const double native = measure(source);
    const double inactive = measure(base);
    publication::Scope scope(checks);
    const double active = measure(base);
    std::printf(
        "Publication relay median ns/call: native=%.2f inactive=%.2f "
        "active=%.2f\n",
        native, inactive, active
    );
}

void
test_publication_optional_installation() {
    struct FailedRollback final : Memory {
        Image image;
        unsigned writes = 0U;
        bool
        read(std::uintptr_t address, std::span<std::uint8_t> out) const noexcept override {
            return image.read(address, out);
        }
        bool
        write(std::uintptr_t address, std::span<const std::uint8_t> bytes) noexcept override {
            ++writes;
            if (writes == 2U || writes == 3U)
                return false;
            return image.write(address, bytes);
        }
    } broken;
    DWORD retained_index = TLS_OUT_OF_INDEXES;
    {
        publication::Checks checks;
        CHECK(checks.install(broken, broken.image.base()) == Outcome::rollback_incomplete);
        retained_index = checks.index();
    }
    CHECK(broken.image.bytes_[rva(publication::calls[0].preferred)] == 0xE9U);
    const DWORD next_index = TlsAlloc();
    CHECK(next_index != TLS_OUT_OF_INDEXES && next_index != retained_index);
    CHECK(TlsFree(next_index));
    {
        Image image;
        image.fail_write = 1U;
        CHECK(
            install(image, image.base(), ReflectionConfig{.blending = {true, 2U}}, quiet, nullptr, {}) ==
            Outcome::applied
        );
        CHECK(!test_bound_state()->blend.requested() && test_bound_state()->controller_enabled);
        destroy_installed_test_state();
    }
    for (const auto &call : publication::calls) {
        Image image;
        image.bytes_[rva(call.preferred)] ^= 1U;
        CHECK(
            install(image, image.base(), ReflectionConfig{.blending = {true, 2U}}, quiet, nullptr, {}) ==
            Outcome::applied
        );
        CHECK(!test_bound_state()->blend.requested());
        destroy_installed_test_state();
    }
    for (std::uint32_t failure = 1U; failure <= publication::calls.size(); ++failure) {
        Image image;
        image.fail_write = failure;
        publication::Checks checks;
        CHECK(checks.install(image, image.base()) == Outcome::failed);
        for (const auto &call : publication::calls)
            CHECK(std::memcmp(image.bytes_.data() + rva(call.preferred), call.bytes.data(), call.size) == 0);
    }
}

#ifdef MWPERFFIX_DIAGNOSTICS
std::uint64_t diagnostic_test_clock = 0U;
std::uint64_t
diagnostic_clock() noexcept {
    return diagnostic_test_clock;
}
void
test_diagnostic_installation_status() {
    const std::optional<Outcome> unattempted;
    CHECK(!unattempted.has_value());
    const auto disabled = diagnostics::installation_status(false, unattempted);
    CHECK(!disabled.requested && disabled.outcome == Outcome::mismatch);
    const auto pending = diagnostics::installation_status(true, unattempted);
    CHECK(pending.requested && pending.outcome == Outcome::mismatch);
    for (const Outcome outcome : {Outcome::applied, Outcome::mismatch, Outcome::failed, Outcome::rollback_incomplete}) {
        const auto attempted = diagnostics::installation_status(true, outcome);
        CHECK(attempted.requested && attempted.outcome == outcome);
    }
    // Blending can install the shared runtime while adaptive remains unrequested.
    const auto blending_only = diagnostics::installation_status(false, Outcome::applied);
    CHECK(!blending_only.requested && blending_only.outcome == Outcome::applied);
}

void
test_blend_diagnostic_timing() {
    static_assert(static_cast<std::size_t>(BlendPhase::begin_frame) == 0U);
    static_assert(static_cast<std::size_t>(BlendPhase::select_schedule) == 1U);
    static_assert(static_cast<std::size_t>(BlendPhase::begin_face) == 2U);
    static_assert(static_cast<std::size_t>(BlendPhase::end_face) == 3U);
    using namespace mwperffix::adaptive;
    using namespace mwperffix::adaptive::runtime_detail;
    BlendTiming timing;
    timing.reset();
    bool active = false;
    bool failed = false;
    for (unsigned i = 0; i < 2U; ++i) {
        diagnostic_test_clock = 10U;
        const BlendTimingScope scope(timing, BlendPhase::begin_face, &diagnostic_clock, active, failed);
        active = true;
        diagnostic_test_clock = 40U;
    }
    CHECK(timing.ticks[2] == 60U);
    CHECK(timing.active && timing.valid);
    timing.reset();
    CHECK(timing.ticks[2] == 0U && !timing.active && !timing.failed);
    // The previous frame's active state must not leak through begin_frame.
    diagnostic_test_clock = 50U;
    {
        const BlendTimingScope scope(timing, BlendPhase::begin_frame, &diagnostic_clock, active, failed);
        active = false;
        diagnostic_test_clock = 55U;
    }
    CHECK(!timing.active && timing.ticks[0] == 5U);
    const auto early_failure = [&] {
        const BlendTimingScope scope(timing, BlendPhase::end_face, &diagnostic_clock, active, failed);
        failed = true;
        diagnostic_test_clock = 70U;
        return;
    };
    early_failure();
    CHECK(timing.failed && timing.ticks[3] == 15U);
    timing.add(BlendPhase::begin_frame, 100U, 99U);
    CHECK(!timing.valid && timing.ticks[0] == 0U && timing.ticks[3] == 0U);
    timing.reset();
    timing.ticks[1] = UINT64_MAX;
    timing.add(BlendPhase::select_schedule, 1U, 2U);
    CHECK(!timing.valid && timing.ticks[1] == 0U);

    // An actual disabled blending call still has balanced scope accounting.
    BlendRuntime blend({}, nullptr, nullptr);
    blend.diagnostic_timing.reset();
    blend.begin_frame();
    blend.select_schedule(Schedule::native_full_update);
    blend.begin_face(0U, nullptr);
    blend.end_face(0U, nullptr);
    CHECK(blend.diagnostic_timing.valid);
    CHECK(!blend.diagnostic_timing.active && !blend.diagnostic_timing.failed);
}
#endif

int
main(const int argc, char **argv) {
#ifdef MWPERFFIX_DIAGNOSTICS
    test_diagnostic_installation_status();
    test_blend_diagnostic_timing();
#endif

    mwperffix::tests::disable_windows_crash_dialogs();

    if (argc == 2 && std::strcmp(argv[1], "--publication-benchmark") == 0) {
        benchmark_publication_checks();
        return 0;
    }
    if (argc == 2 && std::strcmp(argv[1], "--benchmark") == 0) {
        test_relay_lifecycle(true);
        return 0;
    }
    CHECK(argc == 1);
    test_replayed_call_and_continuation();
    test_renderer_relay_chains();
    test_load_order_chaining();
    test_reset_relay_chains();
    test_sweep_compatibility_and_initialization_order();
    test_checked_group_installation();
    test_scheduler_not_required_when_no_schedule_consumer();
    test_state_binding_and_installed_lifetime();
    test_face_addresses_and_refresh();
#ifdef MWPERFFIX_DIAGNOSTICS
    test_dispatch_rejections_and_canonical_face_mapping();
#endif
    test_native_mask_word_order();
    test_two_face_mask_materialization_and_setting_ownership();
    test_adaptive_disabled_native_pass_through();
    test_weighted_single_face_materializes_native_faces();
    test_publication_relays();
    test_publication_optional_installation();
    test_blending_installation_and_inert_state();
    test_blending_fixed_mode1();
    test_blending_weighted_single_face_retarget_and_native_control();
    test_blending_weighted_single_face_retarget_unbind_failure();
    test_blending_runtime_toggle();
    test_hotkey_feature_integration();
    test_live_tuning_actions_and_native_passthrough();
    test_notification_state_change_integration();
    test_blending_gap_without_adaptive_controller();
    test_blending_device_failures();
    test_blending_discarded_publication_results();
    test_adaptive_weighted_single_face_blending_ui2_and_full_bypass();
    test_blending_observation_gap();
    test_device_reset_and_failed_present();
    test_executable_contracts();
    test_relay_lifecycle();
    std::puts("adaptive runtime tests passed");
    return 0;
}
