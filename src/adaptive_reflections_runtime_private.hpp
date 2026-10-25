#pragma once

#include "blend_diagnostics.hpp"
#include "mwperffix/adaptive_reflections.hpp"
#include "mwperffix/adaptive_reflections_runtime.hpp"
#include "mwperffix/reflection_blending.hpp"
#include "mwperffix/runtime_hotkeys.hpp"
#include "mwperffix/runtime_notifications.hpp"
#include "reflection_publication.hpp"
#include "screen_messages.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>
#include <windows.h>

// Private game adapter layout and declarations shared with the runtime tests.
// Addresses in the fixed tables are preferred VAs; rva() relocates them from the supported image base.
// They are not pointers into this module.
namespace mwperffix::adaptive::runtime_detail {

extern publication::Checks g_publications;

inline constexpr std::uintptr_t kPreferredImageBase = 0x00400000U;
inline constexpr std::uintptr_t kCubemapViews = 0x00919DC0U;
inline constexpr std::uintptr_t kCubemapGameplayGate = 0x00925E90U;
inline constexpr std::uintptr_t kGameFrame = 0x00982B78U;

inline constexpr std::size_t kViewStride = 0x70U;
inline constexpr std::array<std::uint8_t, 6> kViewIndexToNativeFace{4U, 0U, 5U, 1U, 2U, 3U};

// The scheduler mask is indexed by canonical native face, not renderer view.
// kViewIndexToNativeFace is only for interpreting face hook structures.
[[nodiscard]] constexpr std::array<std::uint32_t, kFaceCount>
native_mask_words(const std::uint8_t face_mask) noexcept {
    std::array<std::uint32_t, kFaceCount> words{};
    for (std::size_t face = 0; face < kFaceCount; ++face)
        words[face] = (face_mask & static_cast<std::uint8_t>(1U << face)) != 0U ? 1U : 0U;
    return words;
}

inline constexpr std::uintptr_t
rva(const std::uintptr_t preferred) noexcept {
    return preferred - kPreferredImageBase;
}

enum class Site : std::uint32_t {
    renderer_begin = 0,
    scheduler,
    face_begin,
    face_end,
    pre_present,
    post_present,
    reset_begin,
};

struct SavedContext {
    std::uint32_t edi;
    std::uint32_t esi;
    std::uint32_t ebp;
    std::uint32_t saved_esp;
    std::uint32_t ebx;
    std::uint32_t edx;
    std::uint32_t ecx;
    std::uint32_t eax;
    std::uint32_t eflags;
    std::uint32_t last_error;
};
// pushad, pushfd, and the saved last-error DWORD form this exact x86 stack layout.
static_assert(sizeof(SavedContext) == 40U);
static_assert(offsetof(SavedContext, esi) == 4U);
static_assert(offsetof(SavedContext, eax) == 28U);
static_assert(offsetof(SavedContext, eflags) == 32U);
static_assert(offsetof(SavedContext, last_error) == 36U);

struct SiteDefinition {
    Site id;
    const char *name;
    std::uintptr_t preferred;
    std::span<const std::uint8_t> expected;
    std::size_t call_offset;
};

#define ADAPTIVE_BYTES(name, ...) inline constexpr auto name = std::to_array<std::uint8_t>({__VA_ARGS__})
// Renderer hook targets the true function entry at 006E7220, not the second instruction at 006E7225.
// The replayed `mov al, [00901AC8]` is that entry instruction.
// The relay then continues at 006E7225
// so the vanilla prologue or ZPatch's BorderlessFixes jump installed there runs in either load order.
ADAPTIVE_BYTES(kRendererBegin, 0xA0, 0xC8, 0x1A, 0x90, 0x00);
ADAPTIVE_BYTES(kScheduler, 0x8D, 0x04, 0xC5, 0x28, 0x90, 0x8F, 0x00);
ADAPTIVE_BYTES(kFaceBegin, 0x6A, 0x01, 0x50, 0xE8, 0x13, 0xED, 0xFD, 0xFF);
ADAPTIVE_BYTES(kFaceEnd, 0x89, 0x1D, 0x80, 0x2C, 0x98, 0x00);
ADAPTIVE_BYTES(kPrePresent, 0xA1, 0xDC, 0x2B, 0x98, 0x00);
ADAPTIVE_BYTES(kPostPresent, 0x3D, 0x68, 0x08, 0x76, 0x88);
ADAPTIVE_BYTES(kResetBegin, 0xBE, 0x78, 0xDE, 0x93, 0x00);
#undef ADAPTIVE_BYTES

inline constexpr std::array kSites{
    SiteDefinition{Site::renderer_begin, "renderer entry", 0x6E7220U, kRendererBegin, 0U},
    SiteDefinition{Site::scheduler, "reflection mask selector", 0x6DA549U, kScheduler, 0U},
    SiteDefinition{Site::face_begin, "cubemap face entry", 0x6DE695U, kFaceBegin, 3U},
    SiteDefinition{Site::face_end, "cubemap face exit", 0x6DE841U, kFaceEnd, 0U},
    SiteDefinition{Site::pre_present, "pre-present", 0x6E75DEU, kPrePresent, 0U},
    SiteDefinition{Site::post_present, "post-present", 0x6E75EDU, kPostPresent, 0U},
    SiteDefinition{Site::reset_begin, "device reset pre-release", 0x6DB0DAU, kResetBegin, 0U},
};
static_assert(kSites.size() == 7U, "Only the seven required reflection hooks are installed");

// Fixed dependencies verified in the supported game database.
// No HD Reflections hook spans are included.
// Tables remain native and are never written.
struct ContextGuard {
    const char *name;
    std::uintptr_t preferred;
    std::span<const std::uint8_t> expected;
};
inline constexpr auto kGuardSchedulerInput = std::to_array<std::uint8_t>(
    {0x8B, 0x0D, 0xB8, 0x17, 0x90, 0x00, 0xA1, 0x78, 0x2B, 0x98, 0x00, 0x33, 0xD2, 0x83, 0xC4, 0x08}
);
inline constexpr auto kGuardSchedulerMask = std::to_array<std::uint8_t>({
    0x8D,
    0x44,
    0x49,
    0xFD,
    0x8B,
    0x0D,
    0x10,
    0x90,
    0x8F,
    0x00,
    0x89,
    0x4C,
    0x24,
    0x64,
    0x8D,
    0x04,
    0x42,
    0x8B,
    0x15,
    0x14,
    0x90,
    0x8F,
    0x00,
    0x8D,
    0x04,
    0x40,
    0x8D,
    0x04,
    0xC5,
    0x28,
    0x90,
    0x8F,
    0x00,
    0x8B,
    0xC8,
    0x2B,
    0xF0,
    0x89,
    0x54,
    0x24,
    0x6C,
    0x89,
    0x4C,
    0x24,
    0x60,
    0x89,
    0x74,
    0x24,
    0x68,
    0xC7,
    0x44,
    0x24,
    0x5C,
    0x06,
    0x00,
    0x00,
    0x00,
});
inline constexpr auto kGuardEligibility = std::to_array<std::uint8_t>({
    0x39,
    0x3D,
    0x90,
    0x5E,
    0x92,
    0x00,
    0xC6,
    0x05,
    0xAC,
    0x2B,
    0x98,
    0x00,
    0x01,
    0xC7,
    0x05,
    0x6C,
    0xAE,
    0x8F,
    0x00,
    0x03,
    0x00,
    0x00,
    0x00,
    0x0F,
    0x85,
    0x19,
    0x02,
    0x00,
    0x00,
    0xBE,
    0xC0,
    0x9D,
    0x91,
    0x00,
});
inline constexpr auto kGuardFaceChecks = std::to_array<std::uint8_t>({
    0x38,
    0x5E,
    0x08,
    0x0F,
    0x84,
    0xC3,
    0x01,
    0x00,
    0x00,
    0x8B,
    0x46,
    0x60,
    0x39,
    0x58,
    0x10,
    0x0F,
    0x84,
    0xB7,
    0x01,
    0x00,
    0x00,
});
inline constexpr auto kGuardFaceLoop = std::to_array<std::uint8_t>({
    0xBF,
    0x06,
    0x00,
    0x00,
    0x00,
    0x83,
    0xC6,
    0x70,
    0x81,
    0xFE,
    0xF0,
    0x9F,
    0x91,
    0x00,
    0x0F,
    0x8E,
    0x25,
    0xFE,
    0xFF,
    0xFF,
});
inline constexpr auto kGuardPreparation = std::to_array<std::uint8_t>(
    {0x8A, 0x46, 0x08, 0x84, 0xC0, 0x74, 0x3F, 0x8B, 0x56, 0x60, 0x8B, 0x42, 0x10, 0x85, 0xC0, 0x74, 0x35}
);
inline constexpr auto kGuardPresentCall =
    std::to_array<std::uint8_t>({0x8B, 0x08, 0x57, 0x57, 0x57, 0x57, 0x50, 0xFF, 0x51, 0x44});
inline constexpr auto kGuardResetContext = std::to_array<std::uint8_t>({0xE8, 0x26, 0x70, 0xFF, 0xFF});
// Three native DWORD periods (6, 4, 1), kept as literal little-endian guard bytes.
// clang-format off
inline constexpr auto kGuardPeriods = std::to_array<std::uint8_t>({
    0x06, 0x00, 0x00, 0x00,
    0x04, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x00,
});
// clang-format on
inline constexpr auto kGuardSingleMasks = std::to_array<std::uint8_t>({
    0x01,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x01,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x01,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x01,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x01,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x01,
    0x00,
    0x00,
    0x00,
});
inline constexpr auto kGuardFullMask = std::to_array<std::uint8_t>({
    0x01,
    0x00,
    0x00,
    0x00,
    0x01,
    0x00,
    0x00,
    0x00,
    0x01,
    0x00,
    0x00,
    0x00,
    0x01,
    0x00,
    0x00,
    0x00,
    0x01,
    0x00,
    0x00,
    0x00,
    0x01,
    0x00,
    0x00,
    0x00,
});
inline constexpr std::array kCommonGuards{
    ContextGuard{"eligibility", 0x6DE658U, kGuardEligibility},
    ContextGuard{"face checks", 0x6DE680U, kGuardFaceChecks},
    ContextGuard{"face loop", 0x6DE847U, kGuardFaceLoop},
    ContextGuard{"cubemap preparation", 0x6BFE85U, kGuardPreparation},
    ContextGuard{"present call", 0x6E75E3U, kGuardPresentCall},
    ContextGuard{"reset call", 0x6DB0D5U, kGuardResetContext},
};

inline constexpr std::array kSchedulerGuards{
    ContextGuard{"scheduler input", 0x6DA518U, kGuardSchedulerInput},
    ContextGuard{"scheduler mask", 0x6DA52FU, kGuardSchedulerMask},
    ContextGuard{"reflection periods", 0x8F901CU, kGuardPeriods},
    ContextGuard{"single-face masks", 0x8F9028U, kGuardSingleMasks},
    ContextGuard{"full-face mask", 0x8F9148U, kGuardFullMask},
};

// Blending validates compositor dispatch, helper bodies and target restoration independently of scheduling.
// Modified dependencies disable blending only.
inline constexpr auto kGuardCompositorEntry = std::to_array<std::uint8_t>(
    {0x81, 0xEC, 0x40, 0x01, 0x00, 0x00, 0x55, 0x56, 0x8B, 0xB4, 0x24, 0x4C, 0x01, 0x00, 0x00, 0x8B}
);
inline constexpr auto kGuardCompositorRestore = std::to_array<std::uint8_t>(
    {0x89, 0x35, 0x20, 0x2A, 0x98, 0x00, 0x5E, 0x89, 0x2D, 0x80, 0x2C, 0x98, 0x00, 0x5D, 0x81, 0xC4}
);
inline constexpr auto kGuardTargetHelperEntry = std::to_array<std::uint8_t>(
    {0x83, 0xEC, 0x18, 0x56, 0xE8, 0x97, 0x5B, 0x02, 0x00, 0x8B, 0x74, 0x24, 0x20, 0x8B, 0x56, 0x08}
);
inline constexpr auto kGuardFilterMethods =
    std::to_array<std::uint8_t>({0x90, 0x5C, 0x6C, 0x00, 0xF0, 0x5C, 0x6C, 0x00});
inline constexpr auto kGuardFilterEnd = std::to_array<std::uint8_t>({0xC3});
inline constexpr auto kGuardCompositorDispatch = std::to_array<std::uint8_t>(
    {0xD9, 0x9C, 0x24, 0x8C, 0x00, 0x00, 0x00, 0xD9, 0x9C, 0x24, 0xB8, 0x00, 0x00, 0x00, 0xD9, 0x9C, 0x24, 0xE8, 0x00,
        0x00, 0x00, 0x75, 0x5C, 0x89, 0x2D}
);
inline constexpr auto kGuardCompositorDepth =
    std::to_array<std::uint8_t>({0x39, 0x2D, 0x08, 0x18, 0x90, 0x00, 0x7E, 0x0F, 0xA1, 0xDC});
inline constexpr auto kGuardParameterUpload = std::to_array<std::uint8_t>(
    {0x8B, 0x0D, 0x80, 0x2C, 0x98, 0x00, 0x8B, 0x41, 0x48, 0x8B, 0x49, 0x44, 0x8B, 0x89, 0x9C, 0x00, 0x00, 0x00, 0x8B,
        0x10, 0x6A, 0x05, 0x68, 0x20, 0xDE, 0x93, 0x00, 0x51, 0x50, 0xFF, 0x52, 0x70, 0xC3}
);
inline constexpr auto kGuardCompositorVectorBranch =
    std::to_array<std::uint8_t>({0x83, 0x7E, 0x04, 0x0B, 0x5F, 0x5B, 0x75, 0x20});
inline constexpr auto kGuardCompositorEffectBinding = std::to_array<std::uint8_t>(
    {0x8B, 0x35, 0xA4, 0xDE, 0x93, 0x00, 0x89, 0x35, 0x80, 0x2C, 0x98, 0x00, 0x8B, 0x16, 0x8B, 0xCE, 0xFF, 0x52, 0x04}
);
inline constexpr auto kGuardBlendInputsCall = std::to_array<std::uint8_t>({0xE8, 0x84, 0x29, 0xFF, 0xFF});
inline constexpr auto kGuardAlphaInputsCall = std::to_array<std::uint8_t>({0xE8, 0xE7, 0x27, 0xFF, 0xFF});
inline constexpr auto kGuardAlphaInputsDispatch =
    std::to_array<std::uint8_t>({0xA1, 0x80, 0x2C, 0x98, 0x00, 0x85, 0xC0, 0x75, 0x2B});
inline constexpr auto kGuardAlphaInputsStaged = std::to_array<std::uint8_t>(
    {0x8B, 0x44, 0x24, 0x04, 0x8B, 0x4C, 0x24, 0x08, 0xA3, 0x20, 0xDE, 0x93, 0x00, 0x89, 0x0D, 0x24, 0xDE, 0x93, 0x00,
        0xC3}
);
inline constexpr auto kGuardBlendInputsDispatch =
    std::to_array<std::uint8_t>({0xA1, 0x80, 0x2C, 0x98, 0x00, 0x85, 0xC0, 0x75, 0x40});
inline constexpr auto kGuardBlendInputsStaged = std::to_array<std::uint8_t>(
    {0x8B, 0x44, 0x24, 0x04, 0x8B, 0x4C, 0x24, 0x08, 0x8B, 0x54, 0x24, 0x0C, 0xA3, 0x28, 0xDE, 0x93, 0x00, 0x89, 0x0D,
        0x2C, 0xDE, 0x93, 0x00, 0x89, 0x15, 0x30, 0xDE, 0x93, 0x00, 0xC3}
);
inline constexpr auto kGuardFilterVectorBranch =
    std::to_array<std::uint8_t>({0x83, 0x7E, 0x04, 0x0B, 0x75, 0x4A, 0x83});
inline constexpr auto kGuardFinalViewport =
    std::to_array<std::uint8_t>({0x8B, 0x08, 0x50, 0xFF, 0x91, 0xBC, 0x00, 0x00, 0x00});
inline constexpr auto kGuardFilterEndCall = std::to_array<std::uint8_t>({0x8B, 0x06, 0x8B, 0xCE, 0xFF, 0x50, 0x08});
inline constexpr std::array kBlendGuards{
    ContextGuard{"active filter binding", 0x6D3E0DU, kGuardCompositorEffectBinding},
    ContextGuard{"blend inputs call", 0x6D3E87U, kGuardBlendInputsCall},
    ContextGuard{"alpha inputs call", 0x6D3FD4U, kGuardAlphaInputsCall},
    ContextGuard{"alpha inputs dispatch", 0x6C67C0U, kGuardAlphaInputsDispatch},
    ContextGuard{"alpha staged inputs", 0x6C67F4U, kGuardAlphaInputsStaged},
    ContextGuard{"blend inputs dispatch", 0x6C6810U, kGuardBlendInputsDispatch},
    ContextGuard{"blend staged inputs", 0x6C6859U, kGuardBlendInputsStaged},
    ContextGuard{"filter vector dispatch", 0x6D3F82U, kGuardFilterVectorBranch},
    ContextGuard{"final viewport result", 0x6D40BFU, kGuardFinalViewport},
    ContextGuard{"filter end dispatch", 0x6D4046U, kGuardFilterEndCall},
    ContextGuard{"filter wrapper methods", 0x8AF268U, kGuardFilterMethods},
    ContextGuard{"filter wrapper end", 0x6C5CF0U, kGuardFilterEnd},
    ContextGuard{"one-source dispatch", 0x6D3D9AU, kGuardCompositorDispatch},
    ContextGuard{"optional depth branch", 0x6D3BAFU, kGuardCompositorDepth},
    ContextGuard{"parameter upload helper", 0x6C83A0U, kGuardParameterUpload},
    ContextGuard{"optional vector branch", 0x6D3FE1U, kGuardCompositorVectorBranch},
    ContextGuard{"compositor entry", 0x6D3B80U, kGuardCompositorEntry},
    ContextGuard{"compositor target restore", 0x6D40C8U, kGuardCompositorRestore},
    ContextGuard{"target helper entry", 0x6BD3B0U, kGuardTargetHelperEntry},
};

// Blending keeps the native cubemap visible while selected faces render into temporary targets.
// Native mode 1 and Weighted single-face publish through the one-source filter compositor.
// Native full-update bypasses blending.
// The adapter owns surfaces and target bindings.
// BlendState owns fade progress.

inline constexpr std::uintptr_t kDevice = 0x982BDCU;
inline constexpr std::uintptr_t kCurrentTarget = 0x982A20U;
inline constexpr std::uintptr_t kCurrentEffect = 0x982C80U;
inline constexpr std::uintptr_t kConfiguredMode = 0x9017B8U;
inline constexpr std::uintptr_t kFirstView = 0x919DC0U;
inline constexpr std::uintptr_t kViewDescriptorOffset = 0x60U;
inline constexpr std::uintptr_t kCompositorFilter = 0x93DEA4U;
inline constexpr std::uintptr_t kCompositorFilterVtable = 0x8AF264U;
inline constexpr std::uintptr_t kCompositor = 0x6D3B80U;
inline constexpr std::uint32_t kBlendFactorSource = 14U;      // D3DBLEND_BLENDFACTOR
inline constexpr std::uint32_t kBlendFactorDestination = 15U; // D3DBLEND_INVBLENDFACTOR
inline constexpr std::size_t kSamplerStages = 8U;

// Native per-face target descriptor.
// 006BD3B0 reads the color surface at +8, the depth surface at +0Ch and the viewport size at +14h/+18h.
struct TargetDescriptor {
    std::uint32_t type;
    std::uint32_t field4;
    void *color;
    void *depth;
    std::uint32_t field10;
    std::uint32_t width;
    std::uint32_t height;
};
static_assert(offsetof(TargetDescriptor, color) == 8U);
static_assert(offsetof(TargetDescriptor, depth) == 12U);
static_assert(offsetof(TargetDescriptor, width) == 20U);
static_assert(offsetof(TargetDescriptor, height) == 24U);
static_assert(sizeof(TargetDescriptor) == 28U);

struct Viewport {
    std::uint32_t x;
    std::uint32_t y;
    std::uint32_t width;
    std::uint32_t height;
    float min_z;
    float max_z;
};
static_assert(sizeof(Viewport) == 24U);

using CompositorFn = long(__cdecl *)(void *, int, void **, const std::uint32_t *, int, int, int, std::uint32_t);

[[nodiscard]] void *read_pointer(std::uintptr_t base, std::uintptr_t preferred) noexcept;
[[nodiscard]] std::uint32_t read_u32(std::uintptr_t base, std::uintptr_t preferred) noexcept;
void write_pointer(std::uintptr_t base, std::uintptr_t preferred, void *value) noexcept;
void write_u32(std::uintptr_t base, std::uintptr_t preferred, std::uint32_t value) noexcept;

// Owns default-pool COM references in slots_; release() runs before native Reset.
class BlendRuntime {
  public:
    BlendRuntime(const BlendRuntime &) = delete;
    BlendRuntime &operator=(const BlendRuntime &) = delete;
    BlendRuntime(BlendRuntime &&) = delete;
    BlendRuntime &operator=(BlendRuntime &&) = delete;

    BlendRuntime(const blending::Config config, const LogFunction log, void *const log_context) noexcept;

    [[nodiscard]] bool requested() const noexcept;
    void disable() noexcept;

    [[nodiscard]] bool failed() const noexcept;
    [[nodiscard]] bool active() const noexcept;
    [[nodiscard]] bool bypassed() const noexcept;
    [[nodiscard]] std::uint32_t steps() const noexcept;

    void set_base(const std::uintptr_t base) noexcept;

    [[nodiscard]] blending::Mode mode() const noexcept;

#ifdef MWPERFFIX_DIAGNOSTICS
    BlendTiming diagnostic_timing{};
#endif

    // Called only on a foreground press edge before any face redirection.
    // Drop superseded sources without publishing them to the native cubemap.
    [[nodiscard]] bool toggle() noexcept;

    [[nodiscard]] bool set_steps(const std::uint32_t requested) noexcept;

#if defined(MWPERFFIX_RUNTIME_TEST)
    [[nodiscard]] bool test_resources_ready() const noexcept;
    [[nodiscard]] std::size_t test_pending_count() const noexcept;
    [[nodiscard]] bool test_weighted_single_face() const noexcept;
#endif

    // Before scheduling.
    // Allocate here, never inside a face interval.
    void begin_frame(const bool gameplay = true, const bool adaptive_enabled = false) noexcept;

    // Requires the effective schedule.
    // Defer publication until face entry, after native BeginScene.
    // Full updates cancel pending fades.
    void select_schedule(const Schedule schedule) noexcept;

    // Requires native BeginScene.
    // Before advancing other fades, discard the same-face source in weighted mode or finish it in native mode 1.
    // Validate the temporary target before redirecting saved EAX.
    void begin_face(const std::size_t face, SavedContext *const context) noexcept;

    // Restore the native descriptor before first publication.
    // Publication failure cancels the reservation and latches blending off.
    void end_face(const std::size_t face, SavedContext *const context) noexcept;

    void cancel() noexcept;

    // Releases owned default-pool resources.
    // Must run before native Reset.
    void release() noexcept;

  private:
    struct SlotResources {
        void *texture = nullptr;
        void *surface = nullptr;
        void *depth = nullptr;
        void *native_descriptor = nullptr;
        void *native_color = nullptr;
        TargetDescriptor descriptor{};
    };

    [[nodiscard]] void *read_pointer_from(void *const object, const std::size_t offset) const noexcept;

    void log(const char *const text) noexcept;

    void fail(const char *const reason) noexcept;

    // Match the native target helper's color, depth and viewport binding.
    // The compositor only reports its final viewport call,
    // so restore all three explicitly after it returns as well as before first publication.
    [[nodiscard]] bool restore_target(void *const target) noexcept;

    void allocate() noexcept;

    void advance() noexcept;

    [[nodiscard]] bool publish(const std::size_t slot, const float weight) noexcept;

    [[nodiscard]] bool unbind_texture(void *const texture) noexcept;

    blending::Config config_{};
    blending::BlendState state_;
    LogFunction log_ = nullptr;
    void *log_context_ = nullptr;
    std::array<SlotResources, blending::kMaximumSteps - 1U> slots_{};
    std::uintptr_t base_ = 0U;
    bool resources_ready_ = false;
    bool active_ = false;
    bool weighted_single_face_ = false;
    bool configured_eligible_ = false;
    blending::Mode mode_ = blending::Mode::on;
    bool failed_ = false;
};

struct PendingAdaptiveTransition {
    std::uint32_t configured_mode = 0U;
    std::uint32_t previous_budget = 0U;
    std::uint32_t new_budget = 0U;
    StateChangeReason reason = StateChangeReason::full_refresh_complete;
    bool valid = false;
};

// Observer callbacks retain this address; installed state lives until process exit.
struct State {
    State(const State &) = delete;
    State &operator=(const State &) = delete;
    State(State &&) = delete;
    State &operator=(State &&) = delete;

    static void state_change_thunk(void *const context, const StateChange &change) noexcept;

#ifdef MWPERFFIX_DIAGNOSTICS
    static void frame_complete_thunk(void *const context, const FrameObservation &observation) noexcept;
#endif

    [[nodiscard]] static Observer make_observer(
        State *const state, const Observer downstream, const bool feedback_enabled
    ) noexcept;

    explicit State(
        const std::uintptr_t image, const std::uint64_t frequency, const ReflectionConfig &settings,
        const Observer observer = {}, const LogFunction log = nullptr, void *const log_context = nullptr
    ) noexcept;

    void on_state_change(const StateChange &change) noexcept;

    void resolve_adaptive_transition(
        const std::uint32_t configured, const bool gameplay, const Selection &selection
    ) noexcept;

    void settings_snapshot() const noexcept;

    void apply_hotkey_sample(const bool foreground, const std::array<bool, hotkeys::kCount> &down) noexcept;

    void begin_renderer(const std::uint64_t ticks, const std::uint32_t frame) noexcept;

    void reset_device(const std::uint64_t ticks, const std::uint32_t frame) noexcept;

    [[nodiscard]] std::uintptr_t select_mask(
        const std::uintptr_t native_mask, const std::uint32_t configured, const bool gameplay,
        const std::uint32_t frame, const std::uint64_t ticks
    ) noexcept;

    void poll_hotkeys() noexcept;

    void log(const char *const text) const noexcept;

    std::uintptr_t base;
    Observer downstream_observer{};
    ScreenMessages screen_messages;
    Controller controller;
    BlendRuntime blend;
    std::array<std::uint32_t, kFaceCount> custom_mask_words{};
    PendingAdaptiveTransition pending_adaptive_transition{};
    bool controller_enabled = true;
    bool controller_installed = true;
    hotkeys::State hotkey_state{};
    LogFunction log_function = nullptr;
    void *log_context = nullptr;
    RendererClock renderer_clock;
    DWORD producer_thread = 0;
#ifdef MWPERFFIX_DIAGNOSTICS
    // Atomic because rejected callbacks may arrive on non-producer threads.
    // Exchanged by post-Present: counts are since the preceding completed observation,
    // including activity in interrupted lifecycles.
    std::atomic<bool> observing_faces{true};
    std::atomic<std::uint32_t> foreign_face_callbacks{0U};
    std::atomic<std::uint32_t> unknown_face_indices{0U};
#endif
};

[[nodiscard]] std::uint64_t qpc_now() noexcept;
[[nodiscard]] std::size_t face_index(const State &state, std::uint32_t esi) noexcept;
extern "C" void __cdecl mwperffix_adaptive_dispatch(std::uint32_t site_value, SavedContext *context) noexcept;
void append_rel32(std::vector<std::uint8_t> &output, std::uintptr_t instruction, std::uintptr_t target);
[[nodiscard]] std::vector<std::uint8_t> build_relay(
    const SiteDefinition &definition, std::uintptr_t original_address, std::uintptr_t relay
);
[[nodiscard]] std::vector<std::uint8_t> branch_to(std::uintptr_t source, std::uintptr_t destination, std::size_t size);

#if defined(MWPERFFIX_RUNTIME_TEST)
// Borrowed state is bound only for a test scope; it is never deleted by dispatch.
[[nodiscard]] State *test_bound_state() noexcept;
class ScopedTestState {
  public:
    explicit ScopedTestState(State &state) noexcept;
    ~ScopedTestState();
    ScopedTestState(const ScopedTestState &) = delete;
    ScopedTestState &operator=(const ScopedTestState &) = delete;

  private:
    State *previous_;
};
// Call after all borrowed bindings end, only for state allocated by install().
// Retained executable relays remain allocated, just as in production.
void destroy_installed_test_state() noexcept;
#endif

} // namespace mwperffix::adaptive::runtime_detail
