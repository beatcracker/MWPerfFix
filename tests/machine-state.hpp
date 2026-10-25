#pragma once

#include <cstdint>

// Test-only external C names are referenced directly by the x86 assembly driver.
extern "C" {
extern std::uint8_t machine_saved_fp[512];
extern std::uint8_t machine_before_fp[512];
extern std::uint8_t machine_after_fp[512];
extern std::uint8_t machine_xmm[128];
extern std::uint32_t machine_before_gpr[9];
extern std::uint32_t machine_after_gpr[9];
extern std::uintptr_t machine_target;
extern std::uint32_t machine_alignment;
extern std::uint16_t machine_control;
extern std::uint32_t machine_mxcsr;
void __cdecl machine_observer(std::uint32_t, const void *) noexcept;
void __cdecl machine_driver();
}
