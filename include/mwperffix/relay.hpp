#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

#include <windows.h>

namespace mwperffix {

// Move-only owner of one executable relay page.
// Installed code can reference the page until process exit.
// retain_until_process_exit() detaches ownership after installation or when rollback cannot be established.
// Relay byte generation stays feature-specific and is not part of this type.
class RelayPage {
  public:
    explicit RelayPage(const std::size_t capacity = 64U) noexcept
        : capacity_(capacity), memory_(VirtualAlloc(nullptr, capacity, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE)) {
    }

    RelayPage(const RelayPage &) = delete;
    RelayPage &operator=(const RelayPage &) = delete;

    RelayPage(RelayPage &&other) noexcept : capacity_(other.capacity_), memory_(other.memory_) {
        other.memory_ = nullptr;
    }

    RelayPage &
    operator=(RelayPage &&other) noexcept {
        if (this != &other) {
            if (memory_ != nullptr) {
                VirtualFree(memory_, 0, MEM_RELEASE);
            }
            capacity_ = other.capacity_;
            memory_ = other.memory_;
            other.memory_ = nullptr;
        }
        return *this;
    }

    ~RelayPage() {
        if (memory_ != nullptr) {
            VirtualFree(memory_, 0, MEM_RELEASE);
        }
    }

    [[nodiscard]] std::uintptr_t
    address() const noexcept {
        return reinterpret_cast<std::uintptr_t>(memory_);
    }

    [[nodiscard]] bool
    valid() const noexcept {
        return memory_ != nullptr;
    }

    [[nodiscard]] bool
    seal(const std::span<const std::uint8_t> code) noexcept {
        if (memory_ == nullptr || code.size() > capacity_) {
            return false;
        }
        std::memcpy(memory_, code.data(), code.size());
        DWORD old_protection = 0;
        return VirtualProtect(memory_, capacity_, PAGE_EXECUTE_READ, &old_protection) != FALSE &&
               FlushInstructionCache(GetCurrentProcess(), memory_, code.size()) != FALSE;
    }

    void
    retain_until_process_exit() noexcept {
        memory_ = nullptr;
    }

  private:
    std::size_t capacity_;
    void *memory_;
};

} // namespace mwperffix
