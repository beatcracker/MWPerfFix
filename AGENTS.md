# MWPerfFix

Small Windows x86 ASI mod. Preserve intended game behavior and keep scope practical.

## Runtime boundaries

- Use Ultimate ASI Loader's startup `InitializeASI` entry and the adjacent INI. Read configuration once. No custom loader, late injection or hot reload.
- Use checked direct patches and fixed x86 relays. No hooking framework, generic conflict resolver or automatic chaining.
- Validate every required site/context before enabling a fix. Skip unknown modifications and log why. Roll back only still-owned writes. Retain relays when rollback cannot safely remove their users.
- Preserve ABI, registers, flags, stack and relevant x87/SSE state. Assembly establishes contracts; decompiler signatures do not.
- Patch process memory only. Keep feature failures independent where possible.
- Normal hot paths must avoid allocation, locks and diagnostic work. Sparse logs and optional native ScreenPrintf state/hotkey messages are allowed through existing callbacks. No overlay hook or background thread.
- `MWPERFFIX_DIAGNOSTICS` allows the separate scheduler ETW recorder only. No diagnostic rendering/readback machinery.

## Implementation

- Use restrained C++23, small abstractions and explicit ownership/lifetime. Prefer documented D3D interfaces for public calls and explicit layouts for game-private data.
- Handle fallible operations, bounds, overflow and narrowing. Guarantee string termination.
- Keep focused correctness tests. Static checks do not prove in-game speedup or visual quality.

## Documentation

Treat documentation as a scarce context budget. Keep only information that cannot be readily derived from code, tests, configuration or existing guidance. On every documentation creation or refinement, remove repetition, narration and implementation summaries. Group related facts into a tight structure with a clear reading order. Before finishing, ask: "What can I delete without losing necessary knowledge?" Prefer deletion over rephrasing.

Keep README human-facing. Use these docs for agent-facing project knowledge.

Consult the relevant document for the task:

- [Patch contracts](docs/patches.md): native ABI, reverse-engineering evidence and mod compatibility boundaries when changing patches or rendering integration.
- [User playtest feedback](docs/validation.md): questions to ask about user-run playtests. The user performs live validation. Record untested behavior separately from confirmed results.
- [ETW diagnostics](docs/diagnostics.md): measurement limits and evidence caveats when interpreting captures or comparing performance.

## Tools and workflow

- Use project-local tools through `mise`. No global installations. Tool changes use `mise use`, then `mise lock`. Keep explicit non-asdf backends and lockfile mode.
- Keep task composition in `mise.toml`, longer operations in small scripts with explicit inputs. No nested mise calls or hidden discovery/state.
- Use target-based CMake and separate out-of-source presets. Normal builds must be offline and require no game files. Strict warnings apply to owned code.
- Run `mise run fmt`, then `mise run check` after completed changes. Checks must not rewrite tracked files, deploy, launch the game or change game settings.
- Run `check-diagnostic` for recorder changes. Blending resource/failure tests are part of `check`. Benchmarking and WPR capture are explicit separate tasks.
- Keep writes inside this project. Preserve user changes and ignored research. Do not clean the surrounding game installation.
- Use short technical prose and Conventional Commits when committing. Do not claim live acceptance from automated checks.

## Writing style

- Use short, direct technical prose and sentence-case headings.
- Reserve bold for critical constraints or easily missed facts.
- No filler, preambles, artificially balanced sentences, em-dashes or semicolons.
- Describe principles. Include exact commands or snippets only when essential.
