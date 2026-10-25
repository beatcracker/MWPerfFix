#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace mwperffix {

enum class Outcome {
    applied,
    mismatch,
    failed,
    rollback_incomplete
};

[[nodiscard]] constexpr bool
retains_relays(Outcome outcome) noexcept {
    return outcome == Outcome::applied || outcome == Outcome::rollback_incomplete;
}

class Memory {
  public:
    virtual ~Memory() = default;
    [[nodiscard]] virtual bool read(std::uintptr_t address, std::span<std::uint8_t> output) const noexcept = 0;
    // Failure may be reported after bytes changed (for example, cache flush or protection restoration failed).
    // Read back and check ownership before rollback.
    [[nodiscard]] virtual bool write(std::uintptr_t address, std::span<const std::uint8_t> input) noexcept = 0;
};

struct ApplyResult {
    bool applied;
    bool rollback_complete;
};

class PatchTransaction {
  public:
    explicit PatchTransaction(Memory &memory) noexcept;

    [[nodiscard]] bool add(
        std::uintptr_t address, std::span<const std::uint8_t> expected, std::span<const std::uint8_t> replacement
    );
    [[nodiscard]] bool validate() const;
    [[nodiscard]] ApplyResult apply() noexcept;

  private:
    struct Edit {
        std::uintptr_t address;
        std::vector<std::uint8_t> expected;
        std::vector<std::uint8_t> replacement;
    };

    [[nodiscard]] bool equals(std::uintptr_t address, std::span<const std::uint8_t> bytes) const noexcept;
    [[nodiscard]] bool rollback(std::size_t applied_count) noexcept;

    Memory &memory_;
    std::vector<Edit> edits_;
};

[[nodiscard]] bool address_range_valid(std::uintptr_t address, std::size_t size) noexcept;

} // namespace mwperffix
