#pragma once

#include <windows.h>

namespace mwperffix::tests {

inline void
disable_windows_crash_dialogs() noexcept {
    SetErrorMode(GetErrorMode() | SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
}

} // namespace mwperffix::tests
