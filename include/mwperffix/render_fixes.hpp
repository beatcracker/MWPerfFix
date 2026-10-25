#pragma once

#include "mwperffix/patch.hpp"

#include <cstdint>

namespace mwperffix {

// The four ordinary rendering fixes.
// Each installer validates every site it depends on, including surrounding context, before writing.
// A mismatch skips that fix with Outcome::mismatch and never overwrites unknown modifications.
enum class RenderFix {
    sweep_early_exit,
    shadow_matrix_reuse,
    shadow_bounds_correction,
    early_depth_threshold,
};

[[nodiscard]] Outcome install_render_fix(RenderFix fix, Memory &memory, std::uintptr_t executable_base);

} // namespace mwperffix
