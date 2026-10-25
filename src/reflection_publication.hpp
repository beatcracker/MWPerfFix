#pragma once

#include "mwperffix/patch.hpp"
#include "mwperffix/relay.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>
#include <windows.h>

namespace mwperffix::adaptive::publication {

static_assert(sizeof(void *) == 4U);

// Fixed US 1.3 one-source compositor calls.
// Three-byte calls include the next whole instruction.
// Result capture runs before that instruction is replayed.
enum class Kind {
    setup,
    draw,
    description
};
struct CallSite {
    std::uintptr_t preferred;
    std::array<std::uint8_t, 10> bytes;
    std::size_t size;
    std::size_t call_size;
    Kind kind;
    [[nodiscard]] std::span<const std::uint8_t>
    expected() const noexcept {
        return {bytes.data(), size};
    }
};
inline constexpr std::array calls{
    CallSite{0x6D3B9AU, {0xFF, 0x50, 0x30, 0xA1, 0xDC, 0x2B, 0x98, 0x00}, 8U, 3U, Kind::description},
    CallSite{0x6D3BA9U, {0xFF, 0x92, 0x94, 0x00, 0x00, 0x00}, 6U, 6U, Kind::setup},
    CallSite{0x6D3BC0U, {0xFF, 0x91, 0x9C, 0x00, 0x00, 0x00}, 6U, 6U, Kind::setup},
    CallSite{0x6D3E2CU, {0xFF, 0x91, 0x5C, 0x01, 0x00, 0x00}, 6U, 6U, Kind::setup},
    CallSite{0x6D3E3EU, {0xFF, 0x91, 0xFC, 0x00, 0x00, 0x00}, 6U, 6U, Kind::setup},
    CallSite{0x6D3E4EU, {0xFF, 0x91, 0x00, 0x01, 0x00, 0x00}, 6U, 6U, Kind::setup},
    CallSite{0x6D3E6CU, {0xFF, 0x52, 0x68, 0x8B, 0x94, 0x24, 0x6C, 0x01, 0x00, 0x00}, 10U, 3U, Kind::setup},
    CallSite{0x6D3F00U, {0xFF, 0x92, 0xD0, 0x00, 0x00, 0x00}, 6U, 6U, Kind::setup},
    CallSite{0x6D3FB6U, {0xFF, 0x92, 0xD0, 0x00, 0x00, 0x00}, 6U, 6U, Kind::setup},
    CallSite{0x6D3FDCU, {0xE8, 0xBF, 0x43, 0xFF, 0xFF}, 5U, 5U, Kind::setup},
    CallSite{0x6D4003U, {0xFF, 0x92, 0x90, 0x00, 0x00, 0x00}, 6U, 6U, Kind::setup},
    CallSite{0x6D401BU, {0xFF, 0x91, 0x04, 0x01, 0x00, 0x00}, 6U, 6U, Kind::setup},
    CallSite{0x6D4034U, {0xFF, 0x92, 0x4C, 0x01, 0x00, 0x00}, 6U, 6U, Kind::draw},
    CallSite{0x6D4040U, {0xFF, 0x92, 0x08, 0x01, 0x00, 0x00}, 6U, 6U, Kind::setup},
    CallSite{0x6D4053U, {0xFF, 0x91, 0x0C, 0x01, 0x00, 0x00}, 6U, 6U, Kind::setup},
    CallSite{0x6D4077U, {0xFF, 0x92, 0x94, 0x00, 0x00, 0x00}, 6U, 6U, Kind::setup},
    CallSite{0x6D4089U, {0xFF, 0x92, 0x9C, 0x00, 0x00, 0x00}, 6U, 6U, Kind::setup},
    CallSite{0x6C5C9EU, {0xFF, 0x91, 0x0C, 0x01, 0x00, 0x00}, 6U, 6U, Kind::setup},
    CallSite{0x6C5CB2U, {0xFF, 0x92, 0x0C, 0x01, 0x00, 0x00}, 6U, 6U, Kind::setup},
    CallSite{0x6C5CC6U, {0xFF, 0x91, 0x0C, 0x01, 0x00, 0x00}, 6U, 6U, Kind::setup},
    CallSite{0x6C5CDAU, {0xFF, 0x92, 0x0C, 0x01, 0x00, 0x00}, 6U, 6U, Kind::setup},
};

struct Result {
    std::uint32_t failed = 0;
    std::uint32_t draws = 0;
    [[nodiscard]] bool
    accepted() const noexcept {
        return failed == 0U && draws == 1U;
    }
};
static_assert(sizeof(Result) == 8U);

inline void
append_u32_le(std::vector<std::uint8_t> &out, const std::uintptr_t value) {
    const auto v = static_cast<std::uint32_t>(value);
    for (unsigned shift = 0; shift < 32U; shift += 8U)
        out.push_back(static_cast<std::uint8_t>(v >> shift));
}
inline void
relative(std::vector<std::uint8_t> &out, const std::uintptr_t entry, const std::uintptr_t target) {
    append_u32_le(out, target - (entry + out.size() + 4U));
}

// TlsAlloc's x86 TEB slots: first 64 at E10, expansion pointer at F94.
// Result capture saves ECX/flags and uses no calls, FP instructions or last-error writes.
inline std::vector<std::uint8_t>
relay(const CallSite &site, const std::uintptr_t source, const std::uintptr_t entry, const DWORD tls) {
    std::vector<std::uint8_t> out;
    if (site.bytes[0] == 0xE8U) {
        std::int32_t displacement = 0;
        std::memcpy(&displacement, site.bytes.data() + 1U, 4U);
        out.push_back(0xE8U);
        relative(out, entry, source + 5U + static_cast<std::uint32_t>(displacement));
    } else {
        out.insert(out.end(), site.bytes.begin(), site.bytes.begin() + site.call_size);
    }
    out.insert(out.end(), {0x9C, 0x51, 0x64, 0x8B, 0x0D}); // pushfd; push ecx; mov ecx,fs:[]
    append_u32_le(out, tls < 64U ? 0xE10U + tls * 4U : 0xF94U);
    std::array<std::size_t, 3> exits{};
    std::size_t exit_count = 0U;
    const auto empty_exit = [&] {
        out.insert(out.end(), {0x85, 0xC9, 0x0F, 0x84}); // test ecx; jz done
        exits[exit_count++] = out.size();
        append_u32_le(out, 0U);
    };
    empty_exit();
    if (tls >= 64U) {
        out.insert(out.end(), {0x8B, 0x89});
        append_u32_le(out, (tls - 64U) * 4U);
        empty_exit();
    }
    if (site.kind == Kind::draw)
        out.insert(out.end(), {0xFF, 0x41, 0x04});   // inc [ecx+4]
    out.insert(out.end(), {0x85, 0xC0, 0x0F, 0x89}); // test eax; jns done
    exits[exit_count++] = out.size();
    append_u32_le(out, 0U);
    out.insert(out.end(), {0xC7, 0x01, 1, 0, 0, 0}); // sticky failure
    if (site.kind == Kind::description) {
        // GetDesc failed before the compositor changed any render state.
        // Restore ECX/flags, then unwind the saved ESI/EBP and 140h-byte native frame.
        out.insert(out.end(), {0x59, 0x9D, 0x5E, 0x5D, 0x81, 0xC4, 0x40, 0x01, 0, 0, 0xC3});
    }
    for (const auto offset : std::span(exits).first(exit_count)) {
        const auto displacement = static_cast<std::uint32_t>(out.size() - (offset + 4U));
        std::memcpy(out.data() + offset, &displacement, 4U);
    }
    out.insert(out.end(), {0x59, 0x9D});
    out.insert(out.end(), site.bytes.begin() + site.call_size, site.bytes.begin() + site.size);
    out.push_back(0xE9U);
    relative(out, entry, source + site.size);
    return out;
}

class Checks {
  public:
    Checks() = default;
    Checks(const Checks &) = delete;
    Checks &operator=(const Checks &) = delete;
    ~Checks() {
        if (tls_ != TLS_OUT_OF_INDEXES && !retained_)
            TlsFree(tls_);
    }
    [[nodiscard]] bool
    prepare() noexcept {
        if (tls_ == TLS_OUT_OF_INDEXES)
            tls_ = TlsAlloc();
        return tls_ != TLS_OUT_OF_INDEXES;
    }
    [[nodiscard]] DWORD
    index() const noexcept {
        return tls_;
    }
    [[nodiscard]] Result *
    current() const noexcept {
        const DWORD error = GetLastError();
        auto *result = tls_ == TLS_OUT_OF_INDEXES ? nullptr : static_cast<Result *>(TlsGetValue(tls_));
        SetLastError(error);
        return result;
    }
    [[nodiscard]] bool
    set(Result *result) const noexcept {
        const DWORD error = GetLastError();
        const bool ok = tls_ != TLS_OUT_OF_INDEXES && TlsSetValue(tls_, result) != FALSE;
        SetLastError(error);
        return ok;
    }
    // Successful or incompletely rolled-back patches reference only the TLS index, never this object.
    // Reserve that index for process lifetime.
    [[nodiscard]] Outcome
    install(Memory &memory, const std::uintptr_t base) noexcept {
        try {
            if (!prepare())
                return Outcome::failed;
            std::vector<RelayPage> pages;
            pages.reserve(calls.size());
            PatchTransaction transaction(memory);
            for (const auto &site : calls) {
                const auto source = base + site.preferred - 0x400000U;
                pages.emplace_back(192U);
                auto &page = pages.back();
                if (!page.valid() || !page.seal(relay(site, source, page.address(), tls_)))
                    return Outcome::failed;
                std::vector<std::uint8_t> replacement{0xE9U};
                relative(replacement, source, page.address());
                replacement.resize(site.size, 0x90U);
                if (!transaction.add(source, site.expected(), replacement))
                    return Outcome::failed;
            }
            if (!transaction.validate())
                return Outcome::mismatch;
            const auto applied = transaction.apply();
            const auto outcome = applied.applied
                                     ? Outcome::applied
                                     : (applied.rollback_complete ? Outcome::failed : Outcome::rollback_incomplete);
            if (retains_relays(outcome)) {
                retained_ = true;
                for (auto &page : pages)
                    page.retain_until_process_exit();
            }
            return outcome;
        } catch (...) {
            // All throwing preparation precedes apply().
            // The retained path after apply performs no allocations.
            return Outcome::failed;
        }
    }

  private:
    DWORD tls_ = TLS_OUT_OF_INDEXES;
    bool retained_ = false;
};

class Scope {
  public:
    explicit Scope(const Checks &checks) noexcept : checks_(checks) {
        if (auto *outer = checks.current())
            outer->failed = 1U;
        else
            active_ = checks.set(&result_);
    }
    Scope(const Scope &) = delete;
    Scope &operator=(const Scope &) = delete;
    ~Scope() {
        if (active_)
            static_cast<void>(checks_.set(nullptr));
    }
    [[nodiscard]] bool
    active() const noexcept {
        return active_;
    }
    [[nodiscard]] bool
    accepted() const noexcept {
        return active_ && result_.accepted();
    }

  private:
    const Checks &checks_;
    Result result_{};
    bool active_ = false;
};

} // namespace mwperffix::adaptive::publication
