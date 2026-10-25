#include "mwperffix/code.hpp"
#include "mwperffix/patch.hpp"
#include "test_error_mode.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <span>
#include <vector>

namespace {

class OwnedMemory final : public mwperffix::Memory {
  public:
    explicit OwnedMemory(const std::size_t size) : bytes_(size) {
    }

    [[nodiscard]] bool
    read(const std::uintptr_t address, const std::span<std::uint8_t> output) const noexcept override {
        if (address > bytes_.size() || output.size() > bytes_.size() - address) {
            return false;
        }
        for (std::size_t index = 0; index < output.size(); ++index) {
            output[index] = bytes_[address + index];
        }
        return true;
    }

    [[nodiscard]] bool
    write(const std::uintptr_t address, const std::span<const std::uint8_t> input) noexcept override {
        ++write_calls_;
        if (address > bytes_.size() || input.size() > bytes_.size() - address) {
            return false;
        }
        if (write_calls_ == fail_on_write_ || write_calls_ == fail_on_write_again_) {
            if (foreign_change_ && address >= 4) {
                bytes_[address - 4] = 99;
            }
            const std::size_t count = std::min(bytes_before_failure_, input.size());
            std::copy_n(input.begin(), count, bytes_.begin() + static_cast<std::ptrdiff_t>(address));
            return false;
        }
        for (std::size_t index = 0; index < input.size(); ++index) {
            bytes_[address + index] = input[index];
        }
        return true;
    }

    void
    set(const std::uintptr_t address, const std::span<const std::uint8_t> input) {
        for (std::size_t index = 0; index < input.size(); ++index) {
            bytes_[address + index] = input[index];
        }
    }

    void
    fail_on_write(
        const std::size_t call, const std::size_t again = 0, const std::size_t copied = 0, const bool foreign = false
    ) noexcept {
        fail_on_write_ = call;
        fail_on_write_again_ = again;
        bytes_before_failure_ = copied;
        foreign_change_ = foreign;
    }

    [[nodiscard]] std::span<const std::uint8_t>
    at(const std::uintptr_t address, const std::size_t size) const noexcept {
        return {bytes_.data() + address, size};
    }

  private:
    std::vector<std::uint8_t> bytes_;
    std::size_t write_calls_ = 0;
    std::size_t fail_on_write_ = 0;
    std::size_t fail_on_write_again_ = 0;
    std::size_t bytes_before_failure_ = 0;
    bool foreign_change_ = false;
};

[[noreturn]] void
fail(const char *expression, const int line) {
    std::cerr << "check failed at line " << line << ": " << expression << '\n';
    std::exit(EXIT_FAILURE);
}

#define CHECK(expression) ((expression) ? static_cast<void>(0) : fail(#expression, __LINE__))

void
test_transaction_success() {
    OwnedMemory memory(16);
    constexpr std::array<std::uint8_t, 2> old_a{1, 2};
    constexpr std::array<std::uint8_t, 2> new_a{3, 4};
    constexpr std::array<std::uint8_t, 2> old_b{5, 6};
    constexpr std::array<std::uint8_t, 2> new_b{7, 8};
    memory.set(2, old_a);
    memory.set(8, old_b);

    mwperffix::PatchTransaction transaction(memory);
    CHECK(transaction.add(2, old_a, new_a));
    CHECK(transaction.add(8, old_b, new_b));
    CHECK(transaction.validate());
    const auto result = transaction.apply();
    CHECK(result.applied);
    CHECK(result.rollback_complete);
    CHECK(std::ranges::equal(memory.at(2, 2), new_a));
    CHECK(std::ranges::equal(memory.at(8, 2), new_b));
}

void
test_transaction_mismatch() {
    OwnedMemory memory(8);
    constexpr std::array<std::uint8_t, 2> expected{1, 2};
    constexpr std::array<std::uint8_t, 2> replacement{3, 4};
    memory.set(2, std::array<std::uint8_t, 2>{9, 9});
    mwperffix::PatchTransaction transaction(memory);
    CHECK(transaction.add(2, expected, replacement));
    CHECK(!transaction.validate());
    const auto result = transaction.apply();
    CHECK(!result.applied);
    CHECK(result.rollback_complete);
    CHECK(memory.at(2, 2)[0] == 9);
}

void
test_transaction_failure_cleanup() {
    OwnedMemory memory(16);
    constexpr std::array<std::uint8_t, 2> old_a{1, 2};
    constexpr std::array<std::uint8_t, 2> new_a{3, 4};
    constexpr std::array<std::uint8_t, 2> old_b{5, 6};
    constexpr std::array<std::uint8_t, 2> new_b{7, 8};
    memory.set(2, old_a);
    memory.set(8, old_b);
    memory.fail_on_write(2);

    mwperffix::PatchTransaction transaction(memory);
    CHECK(transaction.add(2, old_a, new_a));
    CHECK(transaction.add(8, old_b, new_b));
    const auto result = transaction.apply();
    CHECK(!result.applied);
    CHECK(result.rollback_complete);
    CHECK(std::ranges::equal(memory.at(2, 2), old_a));
    CHECK(std::ranges::equal(memory.at(8, 2), old_b));
}

void
test_transaction_failure_states() {
    constexpr std::array<std::uint8_t, 2> old_bytes{1, 2}, new_bytes{3, 4};
    // Failure before a write, after a full write, during rollback, after a partial write,
    // and after a foreign change to the first installed site.
    for (int scenario = 0; scenario < 5; ++scenario) {
        OwnedMemory memory(12);
        memory.set(2, old_bytes);
        memory.set(6, old_bytes);
        memory.fail_on_write(2, scenario == 2 ? 3U : 0U, scenario == 1 ? 2U : (scenario == 3 ? 1U : 0U), scenario == 4);
        mwperffix::PatchTransaction transaction(memory);
        CHECK(transaction.add(2, old_bytes, new_bytes));
        CHECK(transaction.add(6, old_bytes, new_bytes));
        const auto result = transaction.apply();
        CHECK(!result.applied);
        CHECK(result.rollback_complete == (scenario < 2));
        if (scenario == 2) {
            CHECK(std::ranges::equal(memory.at(2, 2), new_bytes));
        } else if (scenario == 4) {
            CHECK(memory.at(2,
                      2)[0] == 99); // Never overwrite someone else's edit.
        } else {
            CHECK(std::ranges::equal(memory.at(2, 2), old_bytes));
        }
        if (scenario < 2) {
            CHECK(std::ranges::equal(memory.at(6, 2), old_bytes));
        }
    }
    CHECK(mwperffix::retains_relays(mwperffix::Outcome::applied));
    CHECK(mwperffix::retains_relays(mwperffix::Outcome::rollback_incomplete));
    CHECK(!mwperffix::retains_relays(mwperffix::Outcome::failed));
    CHECK(!mwperffix::retains_relays(mwperffix::Outcome::mismatch));

    OwnedMemory memory(8);
    mwperffix::PatchTransaction transaction(memory);
    CHECK(!transaction.add(0, {}, {}));
    CHECK(!transaction.add(0, old_bytes, std::span(new_bytes).first<1>()));
    CHECK(!transaction.add(std::numeric_limits<std::uintptr_t>::max(), old_bytes, new_bytes));
}

std::uint32_t
read_u32(const std::span<const std::uint8_t> bytes, std::size_t offset) {
    CHECK(offset <= bytes.size() && bytes.size() - offset >= 4);
    std::uint32_t value = 0;
    for (unsigned i = 0; i < 4; ++i) {
        value |= static_cast<std::uint32_t>(bytes[offset + i]) << (8 * i);
    }
    return value;
}

void
test_patch_payloads() {
    namespace code = mwperffix::code;
    constexpr auto sweep_instructions = std::to_array<std::uint8_t>({
        0x84,
        0xC0,
        0x75,
        0x0B,
        0x46,
        0x3B,
        0xF3,
        0x7C,
        0x88,
        0x90,
        0x90,
    });
    CHECK(code::sweep == sweep_instructions);
    CHECK(code::sweep_address + 4 + code::sweep[3] == 0x6C9675U); // True: pop epilogue.
    const auto loop_displacement = static_cast<std::int8_t>(code::sweep[8]);
    CHECK(static_cast<std::int32_t>(code::sweep_address) + 9 + loop_displacement == 0x6C95F7);
    CHECK(code::sweep_address + code::sweep.size() == 0x6C9671U); // Exhausted: saved false result.

    // Apply the runtime payload to real bytes at 6C9653,
    // then check that Widescreen Fix can still find its shadow-distance operand.
    auto sweep_context = std::to_array<std::uint8_t>({
        0xD8,
        0x0D,
        0xC4,
        0x10,
        0x89,
        0x00,
        0xD9,
        0x5C,
        0x24,
        0x38,
        0xE8,
        0xAE,
        0xFE,
        0xFF,
        0xFF,
        0x8A,
        0x54,
        0x24,
        0x13,
        0x0A,
        0xD0,
        0x46,
        0x3B,
        0xF3,
        0x88,
        0x54,
        0x24,
        0x13,
        0x7C,
        0x86,
    });
    constexpr auto patch_offset = code::sweep_address - 0x6C9653U;
    static_assert(patch_offset + code::sweep.size() <= sweep_context.size());
    std::ranges::copy(code::sweep, sweep_context.begin() + patch_offset);
    constexpr auto widescreen_pattern = std::to_array<int>({
        0xD8,
        0x0D,
        -1,
        -1,
        -1,
        -1,
        0xD9,
        0x5C,
        0x24,
        -1,
        0xE8,
        -1,
        -1,
        -1,
        -1,
        0x8A,
    });
    for (std::size_t i = 0; i < widescreen_pattern.size(); ++i) {
        CHECK(widescreen_pattern[i] == -1 || widescreen_pattern[i] == sweep_context[i]);
    }
    constexpr auto retained_mov = std::to_array<std::uint8_t>({0x8A, 0x54, 0x24, 0x13});
    CHECK(std::ranges::equal(std::span(sweep_context).subspan<15, 4>(), retained_mov));
    constexpr auto matrix_instructions = std::to_array<std::uint8_t>({0x83, 0xC4, 0x0C, 0x90, 0x90});
    CHECK(code::matrix == matrix_instructions); // Pop exactly the three retained pushes.

    for (const std::uint32_t relay : {0x10000000U, 0xF0000000U}) {
        for (const std::uint32_t image : {0x400000U, 0x500000U}) {
            const auto bounds = code::bounds(relay, image);
            constexpr auto loads = std::to_array<std::uint8_t>({
                0x89,
                0x54,
                0x24,
                0x14,
                0x8B,
                0x08,
                0x8B,
                0x50,
                0x04,
                0x8B,
                0x40,
                0x08,
                0xE9,
            });
            CHECK(std::ranges::equal(std::span(bounds).first<13>(), loads));
            CHECK(relay + 17 + read_u32(bounds,
                                   13) == image + 0x2D7A76U); // Must store max X.

            const auto branch = code::branch(image + 0x2D7A6C, relay);
            CHECK(branch[0] == 0xE9 && branch[5] == 0x90 && branch[6] == 0x90 && branch[7] == 0x90);
            CHECK(image + 0x2D7A6C + 5 + read_u32(branch, 1) == relay);

            for (const auto site : {code::DepthSite::a, code::DepthSite::b}) {
                const auto depth = code::depth(relay, image, site);
                CHECK(depth[0] == 0xD9 && depth[1] == 0x05); // fld
                CHECK(depth[6] == 0xD8 && depth[7] == 0x1D); // fcomp, not fcom
                CHECK(read_u32(depth, 2) == image + 0x4FAE68U);
                CHECK(read_u32(depth, 8) == image + 0x4FAE50U);
                constexpr auto compare = std::to_array<std::uint8_t>({0xDF, 0xE0, 0xF6, 0xC4, 0x41, 0x0F, 0x8B});
                CHECK(std::ranges::equal(std::span(depth).subspan<12, 7>(),
                    compare)); // Ordered <=, NaN falls through.
                const bool a = site == code::DepthSite::a;
                CHECK(relay + 23 + read_u32(depth, 19) == image + (a ? 0x2E0DC2U : 0x2E0A84U));
                constexpr auto replay_a = std::to_array<std::uint8_t>({0x8B, 0x45, 0x08, 0x8B, 0x10});
                constexpr auto replay_b = std::to_array<std::uint8_t>({0x8B, 0x01, 0x8B, 0x51, 0x04});
                CHECK(std::ranges::equal(std::span(depth).subspan<23, 5>(), a ? replay_a : replay_b));
                CHECK(depth[28] == 0xE9);
                CHECK(relay + 33 + read_u32(depth, 29) == image + (a ? 0x2E0C22U : 0x2E08D8U));
            }
        }
    }
}

} // namespace

int
main() {
    mwperffix::tests::disable_windows_crash_dialogs();
    test_transaction_success();
    test_transaction_mismatch();
    test_transaction_failure_cleanup();
    test_transaction_failure_states();
    test_patch_payloads();
    std::cout << "all tests passed\n";
}
