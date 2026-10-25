#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace mwperffix::code {

// Actual x86 patch payloads, shared with the byte/branch regression checks.
// Leave mov dl,[esp+13h] at 6C9662 intact: Widescreen Fix scans its opcode.
inline constexpr std::uintptr_t sweep_address = 0x6C9666U;
inline constexpr auto sweep = std::to_array<std::uint8_t>({
    0x84,
    0xC0, // test al,al
    0x75,
    0x0B, // jne 6C9675 (true return)
    0x46,
    0x3B,
    0xF3, // inc esi / cmp esi,ebx
    0x7C,
    0x88, // jl 6C95F7 (next candidate)
    0x90,
    0x90,
});
inline constexpr auto matrix = std::to_array<std::uint8_t>({0x83, 0xC4, 0x0C, 0x90, 0x90});

// Fixed operand slots, checked at compile time.
// x86 rel32 arithmetic wraps modulo 2^32.
template <std::size_t Offset, std::size_t Size>
constexpr void
operand(std::array<std::uint8_t, Size> &bytes, std::uint32_t value) noexcept {
    static_assert(Offset + 4 <= Size);
    for (unsigned index = 0; index < 4; ++index) {
        bytes[Offset + index] = static_cast<std::uint8_t>(value >> (index * 8));
    }
}

[[nodiscard]] constexpr auto
branch(std::uint32_t source, std::uint32_t target) noexcept {
    std::array<std::uint8_t, 8> bytes{0xE9, 0, 0, 0, 0, 0x90, 0x90, 0x90};
    operand<1>(bytes, target - (source + 5));
    return bytes;
}

[[nodiscard]] constexpr auto
bounds(std::uint32_t relay, std::uint32_t image) noexcept {
    auto bytes = std::to_array<std::uint8_t>({
        0x89,
        0x54,
        0x24,
        0x14, // mov [esp+14h],edx  (minimum Y)
        0x8B,
        0x08, // mov ecx,[eax]      (maximum X)
        0x8B,
        0x50,
        0x04, // mov edx,[eax+4]    (maximum Y)
        0x8B,
        0x40,
        0x08, // mov eax,[eax+8]    (maximum Z)
        0xE9,
        0,
        0,
        0,
        0,
    });
    // Resume at the max-X store, skipping only the original mov edx,ecx.
    operand<13>(bytes, (image + 0x2D7A76U) - (relay + 17));
    return bytes;
}

enum class DepthSite {
    a,
    b
};

[[nodiscard]] constexpr auto
depth(std::uint32_t relay, std::uint32_t image, DepthSite site) noexcept {
    auto bytes = std::to_array<std::uint8_t>({
        0xD9,
        0x05,
        0,
        0,
        0,
        0, // fld current
        0xD8,
        0x1D,
        0,
        0,
        0,
        0, // fcomp threshold (pops operand)
        0xDF,
        0xE0, // fnstsw ax
        0xF6,
        0xC4,
        0x41, // test ah,41h
        0x0F,
        0x8B,
        0,
        0,
        0,
        0, // jnp original zero-effect path
        0x8B,
        0x45,
        0x08,
        0x8B,
        0x10, // A: mov eax,[ebp+8] / mov edx,[eax]
        0xE9,
        0,
        0,
        0,
        0,
    });
    const bool is_a = site == DepthSite::a;
    if (!is_a) {
        // B: mov eax,[ecx] / mov edx,[ecx+4].
        // Both paths overwrite AX before use.
        bytes[24] = 0x01;
        bytes[25] = 0x8B;
        bytes[26] = 0x51;
        bytes[27] = 0x04;
    }
    operand<2>(bytes, image + 0x4FAE68U);
    operand<8>(bytes, image + 0x4FAE50U);
    operand<19>(bytes, (image + (is_a ? 0x2E0DC2U : 0x2E0A84U)) - (relay + 23));
    operand<29>(bytes, (image + (is_a ? 0x2E0C22U : 0x2E08D8U)) - (relay + 33));
    return bytes;
}

} // namespace mwperffix::code
