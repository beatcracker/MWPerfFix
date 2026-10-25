#include "machine-state.hpp"

// Assembly references external C symbols directly.
// These snapshots never ship.
// The observer and driver are naked functions that seed and compare the full machine state around a relay invocation.
// The context parameter is untyped because the assembly reads the stack directly.
extern "C" {
alignas(16) std::uint8_t machine_saved_fp[512]{};
alignas(16) std::uint8_t machine_before_fp[512]{};
alignas(16) std::uint8_t machine_after_fp[512]{};
alignas(16) std::uint8_t machine_xmm[128]{};
std::uint32_t machine_before_gpr[9]{};
std::uint32_t machine_after_gpr[9]{};
std::uintptr_t machine_target = 0;
std::uint32_t machine_alignment = 99;
std::uint16_t machine_control = 0x077F;
std::uint32_t machine_mxcsr = 0x3F80;

__declspec(naked) void __cdecl
machine_observer(std::uint32_t, const void *) noexcept {
    __asm__ volatile("leal 4(%esp), %eax\n\tandl $15, %eax\n\tmovl %eax, "
                     "_machine_alignment\n\t"
                     "movl $0x7777, %fs:0x34\n\t"
                     "fninit\n\tfldz\n\t"
                     "pxor %xmm0, %xmm0\n\tpxor %xmm1, %xmm1\n\t"
                     "pxor %xmm2, %xmm2\n\tpxor %xmm3, %xmm3\n\t"
                     "pxor %xmm4, %xmm4\n\tpxor %xmm5, %xmm5\n\t"
                     "pxor %xmm6, %xmm6\n\tpxor %xmm7, %xmm7\n\t"
                     "pushl $0x1F80\n\tldmxcsr (%esp)\n\taddl $4, %esp\n\t"
                     "xorl %eax, %eax\n\txorl %ecx, %ecx\n\txorl %edx, %edx\n\tclc\n\tret");
}

__declspec(naked) void __cdecl
machine_driver() {
    __asm__ volatile("pushfl\n\tpushal\n\tfxsave _machine_saved_fp\n\t"
                     "fninit\n\tfld1\n\tfldpi\n\tfldcw _machine_control\n\tldmxcsr "
                     "_machine_mxcsr\n\t"
                     "movdqu _machine_xmm, %xmm0\n\tmovdqu _machine_xmm+16, %xmm1\n\t"
                     "movdqu _machine_xmm+32, %xmm2\n\tmovdqu _machine_xmm+48, %xmm3\n\t"
                     "movdqu _machine_xmm+64, %xmm4\n\tmovdqu _machine_xmm+80, %xmm5\n\t"
                     "movdqu _machine_xmm+96, %xmm6\n\tmovdqu _machine_xmm+112, %xmm7\n\t"
                     "movl $0x11223344, %eax\n\tmovl $0x22334455, %ecx\n\tmovl $0x33445566, "
                     "%edx\n\t"
                     "movl $0x44556677, %ebx\n\tmovl $0x55667788, %ebp\n\t"
                     "movl $0x66778899, %esi\n\tmovl $0x778899AA, %edi\n\t"
                     "pushl $0x247\n\tpopfl\n\t"
                     "pushfl\n\tpushal\n\tfxsave _machine_before_fp\n\t"
                     "movl %esp, %esi\n\tmovl $_machine_before_gpr, %edi\n\tmovl $9, "
                     "%ecx\n\tcld\n\trep "
                     "movsl\n\t"
                     "popal\n\tpopfl\n\tcall *_machine_target\n\t"
                     "pushfl\n\tpushal\n\tfxsave _machine_after_fp\n\t"
                     "movl %esp, %esi\n\tmovl $_machine_after_gpr, %edi\n\tmovl $9, "
                     "%ecx\n\tcld\n\trep movsl\n\t"
                     "popal\n\tpopfl\n\tfxrstor _machine_saved_fp\n\tpopal\n\tpopfl\n\tret");
}
}
