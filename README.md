# 🚨 MWPerfFix

_Naming is hard, eh?_

A Need for Speed: Most Wanted (2005) US 1.3 performance-improvement mod.

The game renderer is CPU-bound and leaves performance on the table in a few spots. This mod fixes the worst offenders and adds optional adaptive car reflections (which dynamically balance full vs. single-face updates based on performance) plus smooth frame blending to soften the transition jumps.

> [!TIP]
> Download latest version from [Releases](https://github.com/beatcracker/MWPerfFix/releases).

## 🔥 Performance fixes

### SweepEarlyExit

Stops a shadow visibility check at the first successful candidate. If none passes, it checks them all. Same result, less work.

### ReuseShadowMatrix

When submitting a model for shadow rendering, the game computes a matrix and then computes the same matrix again. This fix reuses the first result within that model submission.

### EarlyDepthThreshold

Moves the game's zero-effect threshold check before costly transforms in two rendering paths. Objects that would be discarded skip those calculations.

## 🛠️ Correctness fixes

### CorrectShadowBounds

The game uses maximum X for all three axes of a shadow bounding box, instead of using the actual X, Y and Z values. This fixes those bounds so visibility checks use the right box. Unlike the perf fixes above, it intentionally changes rendering decisions and can increase or decrease shadow work.

## 🌗 Reflection blending

Enable `[CarReflectionBlending] Enabled = 1` to dissolve an old reflection face into its new image, making the update less abrupt. This works with the game's native mode 1 and with adaptive Weighted single-face updates. Full updates bypass blending.

`BlendSteps` controls the dissolve length, from 2 to 6 steps. More steps spread the change out longer, which can look smoother but also introduce blur or ghosting. Blending adds rendering work.

## ⚖️ Adaptive reflection scheduler

Car reflections use a [cubemap](https://en.wikipedia.org/wiki/Cube_mapping): six views of the surroundings, one for each face of a cube. Updating those views means more scene rendering and CPU work to prepare it. This mod keeps the game's existing renderer and changes when those faces are updated.

Enable `[AdaptiveCarReflections] Enabled = 1` to let it switch between:

- **Native full-update:** all six cubemap faces update every frame, matching the game's highest car-reflection update setting.
- **Weighted single-face:** one face updates per frame, similar to the game's lowest enabled reflection setting, but with a custom update sequence that refreshes selected directions more often.

The scheduler measures renderer CPU work and frame cadence against `TargetFPS` (60 by default).
It reduces reflection work under sustained load and returns to full updates when all six faces are estimated to fit with room to spare.

> [!NOTE]
> `TargetFPS` is a _target_ for these decisions, it does not limit FPS.

Any nonzero in-game car-reflection setting uses the same adaptive policy. Reflections set to off stay off. With adaptive disabled, the game's original update schedule is used.

## Configuration

Use [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) with normal startup loading. Copy `MWPerfFix.asi` and `MWPerfFix.ini` into the game's `scripts/` folder. Settings are read at startup, so restart the game after editing the INI.

Each rendering fix has its own switch under `[Fixes]`. The reflection features have separate sections and can be enabled independently. `TargetFPS` accepts 30–1000, `BlendSteps` accepts 2–6.

Changes show brief in-game messages and settings snapshots in `MWPerfFix.log`, next to the ASI. Once you find values you like, exit the game, copy them into the INI and disable LiveTuning if you no longer need it. Nothing is saved to the INI automatically.

The log also reports startup status, skipped fixes and failures and is capped at 256 KiB per launch. If a feature is not working, start there. If the log asks for a restart after an incomplete rollback, restart the game.

To uninstall, close the game and remove `MWPerfFix.asi`, `MWPerfFix.ini` and `MWPerfFix.log` from `scripts/`.

### 🎚️ Live tuning

For in-game tuning, **enable the features you want to try in the INI**, then set `[LiveTuning] Enabled = 1` and uncomment the key bindings you want. There are no default bound keys. The examples in the INI use:

| Action | Example key | What it does |
| --- | --- | --- |
| `ToggleAdaptive` | F8 (`0x77`) | Toggles adaptive scheduling. Off restores the game's native schedule. |
| `TargetFPSDown` | F9 (`0x78`) | Lowers the target by 5, down to 30. |
| `TargetFPSUp` | F10 (`0x79`) | Raises the target by 5, up to 1000. |
| `ToggleBlending` | F11 (`0x7A`) | Toggles reflection blending. |
| `CycleBlendSteps` | F12 (`0x7B`) | Cycles 2 → 3 → 4 → 5 → 6 → 2. |

Bindings take a single [Windows virtual-key number](https://learn.microsoft.com/en-us/windows/win32/inputdev/virtual-key-codes), in decimal or hexadecimal. Invalid or duplicate bindings are disabled.

## Notes

### Compatibility

This targets the Windows x86 US 1.3 executable. It checks the executable layout and the instructions each fix needs before patching. Unexpected changes skip the affected feature and get a log entry. Startup checks cannot prevent another mod from overwriting that code later.

Some shared areas have explicit compatibility handling:

- [Widescreen Fix](https://github.com/ThirteenAG/WidescreenFixesPack): allows its shadow-distance change and preserves its device-reset hook.
- [ZPatch](https://zolika.dev/mods/nfsmwpatch): leaves room for its `BorderlessFixes` renderer-entry hook.
- [HD Reflections v2](https://github.com/AeroWidescreen/NFSHDReflections): leaves its known hook locations untouched, including the `RestoreCars` area.
- [Extra Options](https://github.com/ExOptsTeam/NFSMWExOpts): keep `DoScreenPrintf` off when using LiveTuning messages, as it can conflict with the game's notification path.

> [!TIP]
> If you just want to use the mod, you can stop reading here ⛐.

### Diagnostics

A separate diagnostic build exposes scheduler and blending CPU measurements through [ETW](https://learn.microsoft.com/en-us/windows/win32/etw/about-event-tracing). To record those, enable both diagnostics and adaptive reflections in the INI and use Windows Performance Recorder (WPR) with the supplied `diagnostics/MWPerfFix.wprp` profile. The release build has no ETW provider.

`mise run check-diagnostic` builds and checks that variant in `build/x86-diagnostic/`. `mise run smoke-etw` tests recording with synthetic events.

### Docs

Implementation details are in [docs](docs/). Agent guidance is in [AGENTS.md](AGENTS.md).

### Making changes / building

Install mise, then run these from the project folder on Windows:

```powershell
mise install
mise run build
```

mise installs the project's configured build tools. After that, normal builds work offline and do not need game files. Release artifacts, including the ASI and INI, go into `build/x86-release/`.

After making changes:

```powershell
mise run fmt
mise run check
```

`check` builds the release, runs the correctness tests and checks the delivery artifacts. It does not install anything into the game or launch it. `mise run benchmark` measures synthetic relay overhead.

### Releasing

Set the version in CMake, commit, and run the Release workflow manually on the desired branch.
It checks both variants and publishes versioned normal, diagnostic, and symbols ZIPs.
Existing version tags are rejected. Development commits can share a version until release.

`mise run package` checks and creates local archives in `build/dist/`.
`mise run release` also tags and publishes through the `origin` GitHub remote.
Local publishing requires GitHub CLI authentication. Logs and ETW identify the commit and tracked-file dirty state.

### License

MIT. See [LICENSE](LICENSE).
