#include "mwperffix/runtime_hotkeys.hpp"
#include "test_error_mode.hpp"

#include <cstdio>
#include <cstdlib>

namespace {

using namespace mwperffix::hotkeys;

[[noreturn]] void
fail(const char *const expression, const int line) {
    std::fprintf(stderr, "line %d: %s\n", line, expression);
    std::exit(EXIT_FAILURE);
}
#define CHECK(expression) ((expression) ? static_cast<void>(0) : fail(#expression, __LINE__))

void
test_parsing() {
    CHECK(parse_virtual_key(L"0x70") == 0x70U);
    CHECK(parse_virtual_key(L"0X7b") == 0x7bU);
    CHECK(parse_virtual_key(L" 118\t") == 118U);
    CHECK(parse_virtual_key(L"0x01") == 0U);
    CHECK(parse_virtual_key(L"254") == 0xfeU);

    CHECK(parse_virtual_key(L"") == 0U);
    CHECK(parse_virtual_key(L"   ") == 0U);
    CHECK(parse_virtual_key(L"0") == 0U);
    CHECK(parse_virtual_key(L"0x00") == 0U);
    CHECK(parse_virtual_key(L"255") == 0U);
    CHECK(parse_virtual_key(L"0xff") == 0U);
    CHECK(parse_virtual_key(L"F7") == 0U);
    CHECK(parse_virtual_key(L"0x") == 0U);
    CHECK(parse_virtual_key(L"0x7g") == 0U);
    CHECK(parse_virtual_key(L"12x") == 0U);
}

void
test_no_binding() {
    State state;
    CHECK(!state.configured());
    const Actions toggle = state.sample(true, {true});
    CHECK(!toggle.toggle_blending);
}

void
test_edges_and_focus() {
    State state({0U, 0U, 0U, 0x78U}); // F9 is available like any other blending key.
    CHECK(state.configured());
    CHECK(!state.sample(true, {false, false, false, true}).toggle_blending); // held on initial focus
    CHECK(!state.sample(true, {}).toggle_blending);
    CHECK(state.sample(true, {false, false, false, true}).toggle_blending);
    CHECK(!state.sample(true, {false, false, false, true}).toggle_blending); // no repeat
    CHECK(!state.sample(false, {}).toggle_blending);
    CHECK(!state.sample(true, {false, false, false, true}).toggle_blending); // held on focus regain
    CHECK(!state.sample(true, {}).toggle_blending);
    CHECK(state.sample(true, {false, false, false, true}).toggle_blending);
}

void
test_multi_action_edges_and_collisions() {
    const Config source{0x77U, 0x78U, 0x78U, 0x7aU, 0x77U};
    const auto duplicates = collisions(source);
    CHECK(duplicates[0] == 0x77U && duplicates[4] == 0x77U);
    CHECK(duplicates[1] == 0x78U && duplicates[2] == 0x78U);
    CHECK(duplicates[3] == 0U);
    const auto filtered = remove_collisions(source);
    CHECK(filtered.toggle_adaptive == 0U && filtered.target_down == 0U);
    CHECK(filtered.target_up == 0U && filtered.toggle_blending == 0x7aU);
    CHECK(filtered.cycle_steps == 0U);

    State state({0x77U, 0x78U, 0x79U, 0x7aU, 0x7bU});
    CHECK(!state.sample(true, {true, true, true, true, true}).toggle_adaptive);
    CHECK(!state.sample(true, {true, true, true, true, true}).cycle_steps);
    CHECK(!state.sample(false, {}).target_up);
    CHECK(!state.sample(true, {true, true, true, true, true}).target_down);
    static_cast<void>(state.sample(true, {}));
    const auto all = state.sample(true, {true, true, true, true, true});
    CHECK(all.toggle_adaptive && all.target_down && all.target_up);
    CHECK(all.toggle_blending && all.cycle_steps);
    CHECK(!state.sample(true, {true, true, true, true, true}).target_up);
}

} // namespace

int
main() {
    mwperffix::tests::disable_windows_crash_dialogs();
    test_parsing();
    test_no_binding();
    test_edges_and_focus();
    test_multi_action_edges_and_collisions();
    return 0;
}
