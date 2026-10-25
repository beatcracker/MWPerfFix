#include "runtime.hpp"

#include <windows.h>

extern "C" __declspec(dllexport) void
InitializeASI() noexcept {
    mwperffix::initialize();
}

BOOL WINAPI
DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
    }
    return TRUE;
}
