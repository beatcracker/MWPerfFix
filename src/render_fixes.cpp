#include "mwperffix/render_fixes.hpp"

#include "mwperffix/code.hpp"
#include "mwperffix/relay.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <span>

namespace mwperffix {
namespace {

constexpr std::uintptr_t kPreferredImageBase = 0x00400000U;

constexpr std::uintptr_t
rva(const std::uintptr_t address) noexcept {
    return address - kPreferredImageBase;
}

constexpr auto kSweepPrologue = std::to_array<std::uint8_t>({
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
constexpr auto kSweepLoop = std::to_array<std::uint8_t>({
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
constexpr auto kSweepEpilogue =
    std::to_array<std::uint8_t>({0x8A, 0x44, 0x24, 0x13, 0x5F, 0x5E, 0x5D, 0x5B, 0x83, 0xC4, 0x58, 0xC2, 0x08, 0x00});
constexpr auto kSweepOriginal =
    std::to_array<std::uint8_t>({0x0A, 0xD0, 0x46, 0x3B, 0xF3, 0x88, 0x54, 0x24, 0x13, 0x7C, 0x86});

constexpr auto kMatrixInitial = std::to_array<std::uint8_t>({
    0xA1,
    0x38,
    0x2B,
    0x98,
    0x00,
    0x83,
    0xC4,
    0x0C,
    0x85,
    0xC0,
    0x74,
    0x57,
    0x8D,
    0x44,
    0x24,
    0x50,
    0x50,
    0x8D,
    0x4C,
    0x24,
    0x34,
    0x51,
    0x8B,
    0xCE,
    0xE8,
    0xA3,
    0x0E,
    0xE2,
    0xFF,
    0x8B,
    0x15,
    0x04,
    0x2D,
    0x98,
    0x00,
    0x52,
    0x53,
    0x8D,
    0x84,
    0x24,
    0x88,
    0x00,
    0x00,
    0x00,
    0x50,
    0xE8,
    0xFC,
    0xD2,
    0x10,
    0x00,
    0x8D,
    0x4C,
    0x24,
    0x50,
    0x51,
    0x8D,
    0x54,
    0x24,
    0x34,
    0x52,
    0x8D,
    0x84,
    0x24,
    0x88,
    0x00,
    0x00,
    0x00,
    0x50,
    0xE8,
    0x87,
    0x3D,
    0xFE,
    0xFF,
    0x8D,
    0x4C,
    0x24,
    0x5C,
    0x51,
    0x8D,
    0x54,
    0x24,
    0x40,
    0x52,
    0xE8,
    0xB8,
    0xCF,
    0xFF,
    0xFF,
    0x83,
    0xC4,
    0x14,
    0x84,
    0xC0,
    0x0F,
    0x84,
    0x3F,
    0x03,
    0x00,
    0x00,
});
constexpr auto kMatrixRepeat = std::to_array<std::uint8_t>({
    0xA1,
    0x38,
    0x2B,
    0x98,
    0x00,
    0x85,
    0xC0,
    0x74,
    0x6F,
    0x8B,
    0x16,
    0x8B,
    0x46,
    0x04,
    0x8B,
    0x4E,
    0x08,
    0x89,
    0x54,
    0x24,
    0x50,
    0x8B,
    0x56,
    0x0C,
    0x89,
    0x44,
    0x24,
    0x54,
    0x8B,
    0x46,
    0x10,
    0x89,
    0x54,
    0x24,
    0x30,
    0x8B,
    0x15,
    0x04,
    0x2D,
    0x98,
    0x00,
    0x52,
    0x89,
    0x44,
    0x24,
    0x38,
    0x89,
    0x4C,
    0x24,
    0x5C,
    0x8B,
    0x4E,
    0x14,
    0x53,
    0x8D,
    0x84,
    0x24,
    0x88,
    0x00,
    0x00,
    0x00,
    0x50,
    0x89,
    0x4C,
    0x24,
    0x44,
    0xE8,
    0xF9,
    0xD1,
    0x10,
    0x00,
    0x8D,
    0x4C,
    0x24,
    0x30,
    0x51,
    0x8D,
    0x54,
    0x24,
    0x54,
    0x52,
    0x8D,
    0x84,
    0x24,
    0x88,
    0x00,
    0x00,
    0x00,
    0x50,
    0xE8,
    0x84,
    0x3C,
    0xFE,
    0xFF,
    0x8D,
    0x4C,
    0x24,
    0x3C,
    0x51,
    0x8D,
    0x54,
    0x24,
    0x60,
    0x52,
    0xE8,
    0xB5,
    0xCE,
    0xFF,
    0xFF,
    0x83,
    0xC4,
    0x14,
    0x84,
    0xC0,
    0x0F,
    0x84,
    0x8E,
    0x01,
    0x00,
    0x00,
});
constexpr auto kMatrixOriginal = std::to_array<std::uint8_t>({0xE8, 0xF9, 0xD1, 0x10, 0x00});

constexpr auto kBoundsEntry = std::to_array<std::uint8_t>({
    0x55,
    0x8B,
    0xEC,
    0x83,
    0xE4,
    0xF0,
    0x83,
    0xEC,
    0x40,
    0xA1,
    0x14,
    0xAF,
    0x8F,
    0x00,
    0x85,
    0xC0,
    0x0F,
    0x84,
    0xAE,
    0x00,
    0x00,
    0x00,
    0x8B,
    0x45,
    0x08,
    0x8B,
    0x08,
    0x8B,
    0x50,
    0x04,
    0x8B,
    0x40,
    0x08,
    0x89,
    0x44,
    0x24,
    0x18,
    0x8B,
    0x45,
    0x0C,
    0x89,
    0x4C,
    0x24,
    0x10,
});
constexpr auto kBoundsOriginal = std::to_array<std::uint8_t>({0x8B, 0x08, 0x8B, 0xC1, 0x89, 0x54, 0x24, 0x14});
constexpr auto kBoundsTail = std::to_array<std::uint8_t>({
    0x8B,
    0xD1,
    0x89,
    0x4C,
    0x24,
    0x1C,
    0x8D,
    0x4C,
    0x24,
    0x10,
    0x51,
    0xB9,
    0xE0,
    0x7C,
    0x98,
    0x00,
    0x89,
    0x54,
    0x24,
    0x24,
    0x89,
    0x44,
    0x24,
    0x28,
    0xE8,
    0xEF,
    0x1B,
    0xFF,
    0xFF,
    0x83,
    0xF8,
    0x01,
    0x74,
    0x6E,
    0x83,
    0xF8,
    0x02,
    0x74,
    0x69,
});

constexpr auto kDepthBContext = std::to_array<std::uint8_t>(
    {0x39, 0x1D, 0x30, 0x18, 0x90, 0x00, 0x0F, 0x8E, 0xB9, 0x01, 0x00, 0x00, 0x8B, 0x01, 0x8B, 0x51, 0x04}
);
constexpr auto kDepthAContext = std::to_array<std::uint8_t>(
    {0x39, 0x05, 0x30, 0x18, 0x90, 0x00, 0x0F, 0x8E, 0xFF, 0x00, 0x00, 0x00, 0x8B, 0x45, 0x08, 0x8B, 0x10}
);
constexpr auto kDepthBCompare = std::to_array<std::uint8_t>({
    0xD9,
    0x05,
    0x68,
    0xAE,
    0x8F,
    0x00,
    0x83,
    0xC4,
    0x18,
    0xD8,
    0x1D,
    0x50,
    0xAE,
    0x8F,
    0x00,
    0xDF,
    0xE0,
    0xF6,
    0xC4,
    0x41,
    0x0F,
    0x8B,
    0x0B,
    0x01,
    0x00,
    0x00,
});
constexpr auto kDepthACompare = std::to_array<std::uint8_t>({
    0xD9,
    0x05,
    0x68,
    0xAE,
    0x8F,
    0x00,
    0x83,
    0xC4,
    0x18,
    0xD8,
    0x1D,
    0x50,
    0xAE,
    0x8F,
    0x00,
    0xDF,
    0xE0,
    0xF6,
    0xC4,
    0x41,
    0x0F,
    0x8B,
    0xFD,
    0x00,
    0x00,
    0x00,
});
constexpr auto kDepthBZero = std::to_array<std::uint8_t>({0x89, 0x5E, 0x3C, 0xE9, 0x47, 0xFF, 0xFF, 0xFF});
constexpr auto kDepthAZero =
    std::to_array<std::uint8_t>({0xC7, 0x46, 0x3C, 0x00, 0x00, 0x00, 0x00, 0xE9, 0x51, 0xFF, 0xFF, 0xFF});
constexpr auto kDepthBOriginal = std::to_array<std::uint8_t>({0x8B, 0x01, 0x8B, 0x51, 0x04});
constexpr auto kDepthAOriginal = std::to_array<std::uint8_t>({0x8B, 0x45, 0x08, 0x8B, 0x10});

struct Signature {
    std::uintptr_t address;
    std::span<const std::uint8_t> bytes;
};

template <std::size_t Size>
[[nodiscard]] Signature
signature(
    const std::uintptr_t base, const std::uintptr_t address, const std::array<std::uint8_t, Size> &bytes
) noexcept {
    return {base + rva(address), bytes};
}

[[nodiscard]] bool
matches(Memory &memory, const Signature &expected) noexcept {
    std::array<std::uint8_t, 160> actual{};
    if (expected.bytes.size() > actual.size()) {
        return false;
    }
    const std::span output(actual.data(), expected.bytes.size());
    return memory.read(expected.address, output) &&
           std::memcmp(actual.data(), expected.bytes.data(), expected.bytes.size()) == 0;
}

[[nodiscard]] bool
matches_all(Memory &memory, const std::initializer_list<Signature> expected) noexcept {
    for (const Signature &item : expected) {
        if (!matches(memory, item)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] Outcome
apply_transaction(PatchTransaction &transaction) noexcept {
    if (!transaction.validate()) {
        return Outcome::mismatch;
    }
    const ApplyResult result = transaction.apply();
    if (result.applied) {
        return Outcome::applied;
    }
    return result.rollback_complete ? Outcome::failed : Outcome::rollback_incomplete;
}

[[nodiscard]] Outcome
install_sweep(Memory &memory, const std::uintptr_t base) {
    // Widescreen Fix owns only the four-byte shadow-distance pointer operand at 006C9655..006C9658.
    // Keep the surrounding fmul opcode, following instructions, loop tail, epilogue and our replacement span exact.
    constexpr std::size_t kWidescreenOperandOffset = 0x64U;
    constexpr std::size_t kWidescreenOperandSize = 4U;
    const std::span loop(kSweepLoop);
    if (!matches_all(
            memory,
            {
                signature(base, 0x6C9570, kSweepPrologue),
                Signature{base + rva(0x6C95F1), loop.first(kWidescreenOperandOffset)},
                Signature{base + rva(0x6C9659), loop.subspan(kWidescreenOperandOffset + kWidescreenOperandSize)},
                signature(base, 0x6C9671, kSweepEpilogue),
            }
        )) {
        return Outcome::mismatch;
    }
    PatchTransaction transaction(memory);
    if (!transaction.add(base + rva(code::sweep_address), kSweepOriginal, code::sweep)) {
        return Outcome::failed;
    }
    return apply_transaction(transaction);
}

[[nodiscard]] Outcome
install_matrix(Memory &memory, const std::uintptr_t base) {
    if (!matches_all(
            memory, {
                        signature(base, 0x6DAA30, kMatrixInitial),
                        signature(base, 0x6DAB1E, kMatrixRepeat),
                    }
        )) {
        return Outcome::mismatch;
    }
    PatchTransaction transaction(memory);
    if (!transaction.add(base + rva(0x6DAB60), kMatrixOriginal, code::matrix)) {
        return Outcome::failed;
    }
    return apply_transaction(transaction);
}

[[nodiscard]] Outcome
install_bounds(Memory &memory, const std::uintptr_t base) {
    if (!matches_all(
            memory, {
                        signature(base, 0x6D7A40, kBoundsEntry),
                        signature(base, 0x6D7A6C, kBoundsOriginal),
                        signature(base, 0x6D7A74, kBoundsTail),
                    }
        )) {
        return Outcome::mismatch;
    }

    RelayPage relay;
    if (!relay.valid()) {
        return Outcome::failed;
    }
    if (!relay.seal(code::bounds(relay.address(), base))) {
        return Outcome::failed;
    }

    const std::uintptr_t patch_address = base + rva(0x6D7A6C);
    const auto replacement = code::branch(patch_address, relay.address());
    PatchTransaction transaction(memory);
    if (!transaction.add(patch_address, kBoundsOriginal, replacement)) {
        return Outcome::failed;
    }
    const Outcome outcome = apply_transaction(transaction);
    if (retains_relays(outcome)) {
        relay.retain_until_process_exit();
    }
    return outcome;
}

[[nodiscard]] Outcome
install_depth(Memory &memory, const std::uintptr_t base) {
    if (!matches_all(
            memory, {
                        signature(base, 0x6E08C7, kDepthBContext),
                        signature(base, 0x6E095F, kDepthBCompare),
                        signature(base, 0x6E0A84, kDepthBZero),
                        signature(base, 0x6E0C11, kDepthAContext),
                        signature(base, 0x6E0CAB, kDepthACompare),
                        signature(base, 0x6E0DC2, kDepthAZero),
                    }
        )) {
        return Outcome::mismatch;
    }

    RelayPage first;
    RelayPage second;
    if (!first.valid() || !second.valid()) {
        return Outcome::failed;
    }

    if (!first.seal(code::depth(first.address(), base, code::DepthSite::b)) ||
        !second.seal(code::depth(second.address(), base, code::DepthSite::a))) {
        return Outcome::failed;
    }

    const std::uintptr_t first_address = base + rva(0x6E08D3);
    const std::uintptr_t second_address = base + rva(0x6E0C1D);
    const auto first_branch = code::branch(first_address, first.address());
    const auto second_branch = code::branch(second_address, second.address());
    PatchTransaction transaction(memory);
    if (!transaction.add(first_address, kDepthBOriginal, std::span(first_branch).first<5>()) ||
        !transaction.add(second_address, kDepthAOriginal, std::span(second_branch).first<5>())) {
        return Outcome::failed;
    }
    const Outcome outcome = apply_transaction(transaction);
    if (retains_relays(outcome)) {
        first.retain_until_process_exit();
        second.retain_until_process_exit();
    }
    return outcome;
}

} // namespace

Outcome
install_render_fix(const RenderFix fix, Memory &memory, const std::uintptr_t executable_base) {
    switch (fix) {
    case RenderFix::sweep_early_exit:
        return install_sweep(memory, executable_base);
    case RenderFix::shadow_matrix_reuse:
        return install_matrix(memory, executable_base);
    case RenderFix::shadow_bounds_correction:
        return install_bounds(memory, executable_base);
    case RenderFix::early_depth_threshold:
        return install_depth(memory, executable_base);
    }
    return Outcome::failed;
}

} // namespace mwperffix
