#include "mwperffix/patch.hpp"

#include <algorithm>
#include <array>
#include <limits>

namespace mwperffix {

bool
address_range_valid(const std::uintptr_t address, const std::size_t size) noexcept {
    return size == 0 || address <= std::numeric_limits<std::uintptr_t>::max() - (size - 1);
}

PatchTransaction::PatchTransaction(Memory &memory) noexcept : memory_(memory) {
}

bool
PatchTransaction::add(
    const std::uintptr_t address, const std::span<const std::uint8_t> expected,
    const std::span<const std::uint8_t> replacement
) {
    if (expected.empty() || expected.size() != replacement.size() || !address_range_valid(address, expected.size())) {
        return false;
    }
    edits_.push_back(
        Edit{
            address,
            std::vector<std::uint8_t>(expected.begin(), expected.end()),
            std::vector<std::uint8_t>(replacement.begin(), replacement.end()),
        }
    );
    return true;
}

bool
PatchTransaction::equals(const std::uintptr_t address, const std::span<const std::uint8_t> bytes) const noexcept {
    std::array<std::uint8_t, 256> current{};
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const std::size_t chunk_size = std::min(current.size(), bytes.size() - offset);
        const std::span output(current.data(), chunk_size);
        if (!memory_.read(address + offset, output) || !std::ranges::equal(output, bytes.subspan(offset, chunk_size))) {
            return false;
        }
        offset += chunk_size;
    }
    return true;
}

bool
PatchTransaction::validate() const {
    return std::ranges::all_of(edits_, [this](const Edit &edit) { return equals(edit.address, edit.expected); });
}

bool
PatchTransaction::rollback(const std::size_t applied_count) noexcept {
    bool complete = true;
    for (std::size_t index = applied_count; index > 0; --index) {
        Edit &edit = edits_[index - 1];
        if (!equals(edit.address, edit.replacement)) {
            complete = false;
            continue;
        }
        if (!memory_.write(edit.address, edit.expected)) {
            complete = false;
        }
    }
    return complete;
}

ApplyResult
PatchTransaction::apply() noexcept {
    if (!validate()) {
        return {false, true};
    }

    std::size_t applied_count = 0;
    for (Edit &edit : edits_) {
        if (!equals(edit.address, edit.expected)) {
            return {false, rollback(applied_count)};
        }
        if (!memory_.write(edit.address, edit.replacement)) {
            bool known_state = true;
            if (equals(edit.address, edit.replacement)) {
                ++applied_count;
            } else {
                // A failed/partial write must not be reported as clean rollback.
                known_state = equals(edit.address, edit.expected);
            }
            const bool restored = rollback(applied_count);
            return {false, restored && known_state};
        }
        ++applied_count;
    }
    return {true, true};
}

} // namespace mwperffix
