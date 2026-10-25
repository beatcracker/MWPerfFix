#include "runtime.hpp"

#include "mwperffix/adaptive_reflections_runtime.hpp"
#include "mwperffix/build_info.hpp"
#include "mwperffix/patch.hpp"
#include "mwperffix/render_fixes.hpp"

#ifdef MWPERFFIX_DIAGNOSTICS
#include "mwperffix/diagnostics/api.hpp"
#endif

#include <windows.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace mwperffix {
namespace {

constexpr std::uintptr_t kPreferredImageBase = 0x00400000U;
constexpr std::uint32_t kTimeDateStamp = 0x438E4C8CU;
constexpr std::uint32_t kEntryPoint = 0x003C4040U;
constexpr std::uint32_t kSizeOfImage = 0x00678E4EU;
constexpr std::uint16_t kSectionCount = 5;

class ProcessMemory final : public Memory {
  public:
    [[nodiscard]] bool
    read(const std::uintptr_t address, const std::span<std::uint8_t> output) const noexcept override {
        SIZE_T transferred = 0;
        return ReadProcessMemory(
                   GetCurrentProcess(), reinterpret_cast<const void *>(address), output.data(), output.size(),
                   &transferred
               ) != FALSE &&
               transferred == output.size();
    }

    [[nodiscard]] bool
    write(const std::uintptr_t address, const std::span<const std::uint8_t> input) noexcept override {
        DWORD old_protection = 0;
        void *destination = reinterpret_cast<void *>(address);
        if (VirtualProtect(destination, input.size(), PAGE_EXECUTE_READWRITE, &old_protection) == FALSE) {
            return false;
        }
        std::memcpy(destination, input.data(), input.size());
        const bool flushed = FlushInstructionCache(GetCurrentProcess(), destination, input.size()) != FALSE;
        DWORD ignored = 0;
        const bool restored = VirtualProtect(destination, input.size(), old_protection, &ignored) != FALSE;
        return flushed && restored;
    }
};

class Logger {
  public:
    explicit Logger(const std::wstring &path) noexcept
        : file_(CreateFileW(
              path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr
          )) {
    }

    Logger(const Logger &) = delete;
    Logger &operator=(const Logger &) = delete;
    Logger(Logger &&) = delete;
    Logger &operator=(Logger &&) = delete;

    ~Logger() {
        if (file_ != INVALID_HANDLE_VALUE)
            CloseHandle(file_);
    }

    void
    line(const std::string_view text) noexcept {
        static constexpr std::string_view notice = "MWPerfFix: log limit reached; further messages suppressed\r\n";
        constexpr std::size_t limit = 256U * 1024U;
        if (file_ == INVALID_HANDLE_VALUE || capped_)
            return;
        // Reserve space for one final notice, even when a regression logs every frame.
        if (text.size() > limit - notice.size() - bytes_ - 2U) {
            write(notice);
            capped_ = true;
            return;
        }
        write(text);
        write("\r\n");
    }

  private:
    void
    write(const std::string_view text) noexcept {
        if (file_ == INVALID_HANDLE_VALUE)
            return;
        DWORD written = 0;
        if (WriteFile(file_, text.data(), static_cast<DWORD>(text.size()), &written, nullptr) == FALSE ||
            written != text.size()) {
            capped_ = true;
            return;
        }
        bytes_ += written;
    }

    HANDLE file_;
    std::size_t bytes_ = 0U;
    bool capped_ = false;
};

[[nodiscard]] std::wstring
module_directory() {
    HMODULE module = nullptr;
    static int module_marker = 0;
    if (GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&module_marker), &module
        ) == FALSE) {
        return {};
    }
    std::array<wchar_t, 32768> path{};
    const DWORD length = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
    if (length == 0 || length >= path.size()) {
        return {};
    }
    std::wstring result(path.data(), length);
    const std::size_t separator = result.find_last_of(L"\\/");
    if (separator == std::wstring::npos) {
        return {};
    }
    result.resize(separator + 1);
    return result;
}

[[nodiscard]] bool
supported_executable(const std::uintptr_t base) noexcept {
    const auto *dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0) {
        return false;
    }
    const auto *nt = reinterpret_cast<const IMAGE_NT_HEADERS32 *>(base + static_cast<std::uintptr_t>(dos->e_lfanew));
    return nt->Signature == IMAGE_NT_SIGNATURE && nt->FileHeader.Machine == IMAGE_FILE_MACHINE_I386 &&
           nt->FileHeader.NumberOfSections == kSectionCount && nt->FileHeader.TimeDateStamp == kTimeDateStamp &&
           nt->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC &&
           nt->OptionalHeader.AddressOfEntryPoint == kEntryPoint &&
           nt->OptionalHeader.ImageBase == kPreferredImageBase && nt->OptionalHeader.SizeOfImage == kSizeOfImage;
}

void
report(Logger &logger, const std::string_view name, const Outcome outcome) {
    std::string line(name);
    switch (outcome) {
    case Outcome::applied:
        line += ": enabled";
        break;
    case Outcome::mismatch:
        line += ": skipped (expected instructions not found)";
        break;
    case Outcome::failed:
        line += ": failed (no patches left installed)";
        break;
    case Outcome::rollback_incomplete:
        line += ": failed (rollback incomplete, relays retained, restart required)";
        break;
    }
    logger.line(line);
}

[[nodiscard]] std::optional<Outcome>
install_if_enabled(
    Logger &logger, Memory &memory, const std::uintptr_t base, const std::wstring &ini, const wchar_t *option,
    const std::string_view label, const RenderFix fix
) {
    if (GetPrivateProfileIntW(L"Fixes", option, 1, ini.c_str()) == 0) {
        std::string line(label);
        line += ": disabled";
        logger.line(line);
        return std::nullopt;
    }
    const Outcome outcome = install_render_fix(fix, memory, base);
    report(logger, label, outcome);
    return outcome;
}

void
runtime_log(void *context, const std::string_view text) noexcept {
    static_cast<Logger *>(context)->line(text);
}

// INI values load at startup.
// LiveTuning changes effective values without reloading.
[[nodiscard]] adaptive::Config
adaptive_config(const std::wstring &ini) {
    adaptive::Config config{};
    config.target_fps = static_cast<std::uint32_t>(
        GetPrivateProfileIntW(L"AdaptiveCarReflections", L"TargetFPS", static_cast<INT>(config.target_fps), ini.c_str())
    );
    return adaptive::normalize_config(config);
}

[[nodiscard]] blending::Config
blending_config(const std::wstring &ini) {
    blending::Config config{};
    config.enabled = GetPrivateProfileIntW(L"CarReflectionBlending", L"Enabled", 0, ini.c_str()) != 0;
    config.steps = static_cast<std::uint32_t>(
        GetPrivateProfileIntW(L"CarReflectionBlending", L"BlendSteps", static_cast<INT>(config.steps), ini.c_str())
    );
    return blending::normalize_config(config);
}

[[nodiscard]] std::string
hotkey_text(const std::wstring_view value) {
    std::string result;
    result.reserve(value.size());
    for (const wchar_t character : value) {
        result.push_back(character >= 0 && character <= 0x7f ? static_cast<char>(character) : '?');
    }
    return result;
}

[[nodiscard]] std::uint32_t
read_hotkey(Logger &logger, const std::wstring &ini, const wchar_t *const key) {
    std::array<wchar_t, 32> buffer{};
    const DWORD length = GetPrivateProfileStringW(
        L"LiveTuning", key, L"", buffer.data(), static_cast<DWORD>(buffer.size()), ini.c_str()
    );
    if (length == 0U)
        return 0U;
    const std::wstring_view value = hotkeys::trim({buffer.data(), length});
    const std::uint32_t vk = hotkeys::parse_virtual_key(value);
    if (vk == 0U) {
        std::string warning("LiveTuning: invalid ");
        for (const wchar_t character : std::wstring_view{key})
            warning.push_back(static_cast<char>(character));
        warning += " binding \"";
        warning += hotkey_text(value);
        warning += "\" (disabled)";
        logger.line(warning);
    }
    return vk;
}

[[nodiscard]] hotkeys::Config
tuning_keys(Logger &logger, const std::wstring &ini) {
    hotkeys::Config keys{
        read_hotkey(logger, ini, L"ToggleAdaptive"),
        read_hotkey(logger, ini, L"TargetFPSDown"),
        read_hotkey(logger, ini, L"TargetFPSUp"),
        read_hotkey(logger, ini, L"ToggleBlending"),
        read_hotkey(logger, ini, L"CycleBlendSteps"),
    };
    const auto duplicates = hotkeys::collisions(keys);
    for (std::size_t i = 0; i < duplicates.size(); ++i) {
        if (duplicates[i] == 0U)
            continue;
        bool already_reported = false;
        for (std::size_t j = 0; j < i; ++j)
            already_reported |= duplicates[j] == duplicates[i];
        if (!already_reported) {
            std::array<char, 96> line{};
            std::snprintf(
                line.data(), line.size(), "LiveTuning: duplicate VK 0x%02X (conflicting actions disabled)",
                duplicates[i]
            );
            logger.line(line.data());
        }
    }
    keys = hotkeys::remove_collisions(keys);
    std::array<char, 192> map{};
    std::snprintf(
        map.data(), map.size(),
        "LiveTuning keys: Adaptive=0x%02X, FPS-=0x%02X, FPS+=0x%02X, "
        "Blend=0x%02X, "
        "Steps=0x%02X (0=unbound)",
        keys.toggle_adaptive, keys.target_down, keys.target_up, keys.toggle_blending, keys.cycle_steps
    );
    logger.line(map.data());
    return keys;
}

void
initialize_impl() {
    static_assert(sizeof(void *) == 4, "MW PerfFix must be compiled for x86");
    const std::wstring directory = module_directory();
    if (directory.empty()) {
        OutputDebugStringA("MW PerfFix: cannot locate ASI directory, initialization skipped\n");
        return;
    }
    // Blend callbacks retain this logger after initialization returns.
    static Logger logger(directory + build_info::log_filename);
    logger.line(build_info::banner);

    const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    if (base == 0 || !supported_executable(base)) {
        logger.line("All fixes: skipped (unsupported executable layout)");
        return;
    }

    ProcessMemory memory;
    const std::wstring ini = directory + build_info::ini_filename;
    const std::array fixes{
        install_if_enabled(
            logger, memory, base, ini, L"SweepEarlyExit", "Sweep early exit", RenderFix::sweep_early_exit
        ),
        install_if_enabled(
            logger, memory, base, ini, L"ReuseShadowMatrix", "Shadow matrix reuse", RenderFix::shadow_matrix_reuse
        ),
        install_if_enabled(
            logger, memory, base, ini, L"CorrectShadowBounds", "Shadow bounds correction",
            RenderFix::shadow_bounds_correction
        ),
        install_if_enabled(
            logger, memory, base, ini, L"EarlyDepthThreshold", "Early depth threshold", RenderFix::early_depth_threshold
        ),
    };
    const bool adaptive_requested = GetPrivateProfileIntW(L"AdaptiveCarReflections", L"Enabled", 0, ini.c_str()) != 0;
    const adaptive::Config adaptive_settings = adaptive_config(ini);
    const blending::Config blending_settings = blending_config(ini);
    const bool tuning_enabled = GetPrivateProfileIntW(L"LiveTuning", L"Enabled", 0, ini.c_str()) != 0;
    logger.line(tuning_enabled ? "LiveTuning: enabled" : "LiveTuning: disabled");
    std::array<char, 120> reflection_settings{};
    std::snprintf(
        reflection_settings.data(), reflection_settings.size(), "Reflection settings: TargetFPS=%u, BlendSteps=%u",
        adaptive_settings.target_fps, blending_settings.steps
    );
    logger.line(reflection_settings.data());
#ifdef MWPERFFIX_DIAGNOSTICS
    const bool diagnostics_requested = GetPrivateProfileIntW(L"Diagnostics", L"Enabled", 0, ini.c_str()) != 0;
#endif
    const hotkeys::Config hotkey_settings = tuning_enabled ? tuning_keys(logger, ini) : hotkeys::Config{};
    const bool reflection_runtime_requested = adaptive_requested || blending_settings.enabled;
    std::optional<Outcome> adaptive_outcome;
    if (!reflection_runtime_requested) {
        logger.line("Reflection runtime: disabled (adaptive and blending off)");
    } else {
        const adaptive::ReflectionConfig settings{
            .scheduler = adaptive_settings,
            .blending = blending_settings,
            .adaptive_enabled = adaptive_requested,
            .hotkeys = hotkey_settings,
            .notifications_enabled = tuning_enabled,
        };
        adaptive::Observer observer{};
#ifdef MWPERFFIX_DIAGNOSTICS
        if (diagnostics_requested)
            observer = diagnostics::adaptive_observer();
#endif
        adaptive_outcome = adaptive::install(memory, base, settings, runtime_log, &logger, observer);
    }

#ifdef MWPERFFIX_DIAGNOSTICS
    std::array<diagnostics::FixStatus, 5> diagnostic_fixes{};
    for (std::size_t index = 0; index < fixes.size(); ++index) {
        diagnostic_fixes[index] = diagnostics::installation_status(fixes[index].has_value(), fixes[index]);
    }
    diagnostic_fixes[4] = diagnostics::installation_status(adaptive_requested, adaptive_outcome);
    diagnostics::initialize({memory, base, ini.c_str(), adaptive_settings, diagnostic_fixes, runtime_log, &logger});
#else
    static_cast<void>(fixes);
    static_cast<void>(adaptive_outcome);
#endif
}

BOOL CALLBACK
initialize_once(PINIT_ONCE, PVOID, PVOID *) noexcept {
    try {
        initialize_impl();
    } catch (...) {
        OutputDebugStringA("MW PerfFix: initialization failed with an unexpected exception\n");
    }
    return TRUE;
}

} // namespace

void
initialize() noexcept {
    static INIT_ONCE once = INIT_ONCE_STATIC_INIT;
    InitOnceExecuteOnce(&once, initialize_once, nullptr, nullptr);
}

} // namespace mwperffix
