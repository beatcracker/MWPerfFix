#include "mwperffix/diagnostics/api.hpp"

#include "mwperffix/build_info.hpp"
#include "mwperffix/diagnostics/capture.hpp"

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>

namespace mwperffix::diagnostics {

void
initialize(const InitializeInput &input) noexcept {
    if (GetPrivateProfileIntW(L"Diagnostics", L"Enabled", 0, input.ini_path) == 0) {
        input.log(input.log_context, "Diagnostics: disabled");
        return;
    }
    const bool adaptive_applied = input.fixes[4].requested && input.fixes[4].outcome == Outcome::applied;
    if (!adaptive_applied) {
        input.log(
            input.log_context, "Diagnostics: unavailable (no requested "
                               "diagnostic producer installed)"
        );
        return;
    }
    try {
        std::unique_ptr<Capture> capture(new (std::nothrow) Capture);
        if (capture == nullptr) {
            input.log(input.log_context, "Diagnostics: provider allocation failed");
            return;
        }
        if (!capture->initialize(false)) {
            input.log(input.log_context, "Diagnostics: TraceLogging registration failed");
            return;
        }

        const auto *dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(input.executable_base);
        const auto *nt = reinterpret_cast<const IMAGE_NT_HEADERS32 *>(
            input.executable_base + static_cast<std::uintptr_t>(dos->e_lfanew)
        );
        Metadata metadata{};
        metadata.build_version = build_info::version;
        metadata.build_variant = build_info::variant;
        metadata.executable_base = input.executable_base;
        metadata.executable_timestamp = nt->FileHeader.TimeDateStamp;
        metadata.executable_size = nt->OptionalHeader.SizeOfImage;
        metadata.target_fps = input.adaptive_config.target_fps;
        for (std::size_t index = 0; index < input.fixes.size(); ++index) {
            const std::uint32_t bit = 1U << static_cast<std::uint32_t>(index);
            if (input.fixes[index].requested)
                metadata.fixes.requested_mask |= bit;
            if (input.fixes[index].outcome == Outcome::applied)
                metadata.fixes.applied_mask |= bit;
            metadata.fixes.outcomes |= (static_cast<std::uint32_t>(input.fixes[index].outcome) & 3U)
                                       << (static_cast<std::uint32_t>(index) * 2U);
        }

        capture->set_metadata(metadata);
        publish_capture(capture.release());
        input.log(
            input.log_context, "Diagnostics: ETW provider ready; collection "
                               "begins when a listener is enabled"
        );
    } catch (...) {
        input.log(input.log_context, "Diagnostics: initialization failed");
    }
}

} // namespace mwperffix::diagnostics
