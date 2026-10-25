# Patch contracts

Addresses below are preferred virtual addresses for the verified US 1.3
executable, image base `00400000`. Runtime installation uses module-relative
addresses.

Patch bytes and guards live in [code.hpp](../include/mwperffix/code.hpp), [render_fixes.cpp](../src/render_fixes.cpp) and [reflection runtime definitions](../src/adaptive_reflections_runtime_private.hpp). Preserve the native contracts below when changing them.

## Sweep early exit

At `006C965D`, `6C9570` calls the read-only six-plane candidate test `6C9510`.
The original continuation at `006C9662` ORs every result into a stack byte and
continues through every generated candidate.

The first `mov dl,[esp+13h]` is preserved. The 11-byte replacement starts at
`006C9666` and tests `AL`. Success branches to `006C9675`, which is the existing
pop sequence and leaves `AL=1`. Failure increments the candidate index and
returns to `006C95F7`. Candidate construction, ordering, radius scaling, false
return and the `retn 8` convention are unchanged.

Widescreen Fix scans `D8 0D ? ? ? ? D9 5C 24 ? E8 ? ? ? ? 8A` at
`006C9653..006C9662` and rewrites the shadow-distance pointer operand at
`006C9655..006C9658`. Widescreen Fix owns that operand. MWPerfFix owns the
surrounding instructions and control flow.

Only the four shadow-distance operand bytes may differ. Keep the surrounding
instructions and control flow guarded so either mod load order remains valid.

## Shadow matrix reuse

`6DA9B0` reserves its locals once per model submission. The initial
`D3DXMatrixMultiply` at `006DAA5D` writes the 64-byte matrix at stable stack
offset `+80h..+BFh`. Bounds locals use `+30h..+67h`, and resource-override state
begins at `+C0h`.

Between the initial multiplication and the repeated call, the routine calls
`6BE800`, `6D7A40`, `504F70` and `6CF2B0`. Their arguments do not include the
matrix local as writable storage. The shadow-view pointer has one static writer
at `006E47C6`, outside this submission path. The shadow flag is set at
`006E50D4` and cleared at `006E5497`, around submission of the full shadow view.

The repeated call at `006DAB60` is replaced with `add esp, 0Ch` plus padding. This removes only the repeated
stdcall multiplication and retains the three argument pushes, the initial
calculation, per-submesh bounds transform and visibility checks. No matrix is
cached across model submissions or views.

## Shadow bounds correction

At `006D7A6C`, the original code loads maximum X into `ECX`, copies it to `EAX`, and preserves the still-live
minimum Y. The following instruction at `006D7A74` copies maximum X to `EDX`, so
the existing stores construct `(max.x, max.x, max.x)` instead of the real Y and
Z extents.

The 8-byte patch jumps to a relay. The relay preserves minimum Y, then loads
maximum X into `ECX`, maximum Y into `EDX`, and maximum Z into `EAX`. It returns
to `006D7A76`, the existing maximum-X store, skipping only the incorrect
`mov edx,ecx` at `006D7A74`. The existing stores at `006D7A76`, `006D7A84` and
`006D7A88` then consume those registers. The guard covers the skipped instruction
and all three stores. The box classification, sphere fallback and sweep are
unchanged. This is an intentional rendering correction and may increase or
decrease work.

## Early depth threshold

The two original comparisons are at `006E095F` and `006E0CAB`:

```text
fld current
fcomp threshold
fnstsw ax
test ah, 41h
jnp zero_effect
```

Relays entered at `006E08D3` and `006E0C1D` perform that exact x87 sequence
before bounds transformation, center calculation and view-space transformation.
The compare pops its x87 operand, so the stack is balanced. `jnp` selects the
original zero-effect destinations for ordered values less than or equal to the
threshold. Unordered values continue, matching the original behavior. The
original later comparisons remain in place for the nonzero path.

## Adaptive car reflections

The adapter never writes
`g_CarEnvironmentMapEnable` at `009017B8`.

When adaptive scheduling is enabled, configured value `0` remains native-off
and configured values `1/2/3` all enter the same custom scheduling policy.
The policy owns the exact six-DWORD face mask for eligible gameplay frames.
Native mode numbers are not adaptive states.

### Mask ownership and face mapping

The native scheduler computes:

```text
period = 008F9018 + mode*4
mask   = 008F9028 + (mode-1)*144 + remainder*24
```

The adaptive relay takes over only at the final mask-pointer LEA. This leaves
the native frame-counter division and all face-loop rendering machinery intact.

Policy masks use canonical native cubemap order:

```text
0=+X  1=-X  2=+Y  3=-Y  4=+Z  5=-Z
```

The renderer's six view slots use `{4,0,5,1,2,3}`. The runtime converts the
canonical six-bit mask into six persistent DWORDs in renderer-view order before
returning the custom pointer. Face timing converts view index back to native
face ID, so selected masks, age state and cost state use one canonical identity.

When the adapter is inactive, outside eligible gameplay, configured reflections
are zero, or a custom selection is unavailable, the callback returns the exact
native pointer produced by the replayed LEA.

### Compatibility

No branch enters an overwritten instruction interior. The face loop reaches
`006DE84C` from disabled/resource-invalid faces before the entry hook, so only
actual updates are timed. Known HD Reflections v2 hook spans remain untouched.
The renderer entry stops at `006E7224`, preserving ZPatch chaining at
`006E7225`.

Widescreen Fix owns `006DB0D0..006DB0D4`. MWPerfFix owns only
`006DB0DA..006DB0DE`, after the intact native `call 006D2100`. The hooks
therefore chain in either load order.

Assembly evidence is required before reducing relay preservation. Native Present
comparison flags must survive callbacks. ScreenPrintf is substituted in tests,
so native notification behavior requires user playtest feedback.

## Car reflection blending

A selected face renders into a temporary color/depth target. Face entry advances
pending fades before redirecting the target descriptor in saved `EAX`. Face exit
restores the native descriptor at `00982A20`, clears current-effect identity at
`00982C80`, rebinds color/depth/viewport and publishes the first blend step.
Publication falls outside face timing and inside renderer-work timing.

### Publication

The native compositor at `006D3B80` receives a destination surface, source count
`1`, one source texture, white weight, blending enabled, source factor `14`
(`D3DBLEND_BLENDFACTOR`) and destination factor `15`
(`D3DBLEND_INVBLENDFACTOR`). The adapter sets `D3DRS_BLENDFACTOR` before invocation.

| Native boundary | Contract |
| --- | --- |
| Scene | BeginScene at `006E754F`, face rendering at `006E759C`, EndScene at `006E75B7`. Publication requires successful scene entry. |
| Face entry | `006DE695` precedes the target helper's flush at `006BD3B4`. The queue must already be empty before publication changes state. |
| Face exit | Geometry flushes at `006DE749`, `006DE79F`, `006DE7CA`. The immediate sprite/light path ends its effect at `006DE83B`, before publication at `006DE841`. |
| Queue | `006E2F50` reads count `00982CDC`, sorts and dispatches through `006E0200`. `006E0701` clears the count. The compositor flushes at `006D4065` while its destination is still bound. |
| Return | The draw HRESULT at `006D4034` is discarded at `006D403A`. The compositor returns only SetViewport's result at `006D40C2`. |

Publication succeeds only when all monitored results succeed, exactly one draw
occurs, the compositor returns success and explicit color/depth/viewport
restoration succeeds. The call-site inventory and expected bytes are in
[`reflection_publication.hpp`](../src/reflection_publication.hpp).

Result capture covers surface description, target binding, the four sampler
calls in `006C5C90`, declaration/effect setup, uploads, drawing and cleanup.
CPU setters `006C67C0` and `006C6810` stage parameters. Their return values are
not HRESULTs. Their dispatch and staging bodies, the upload helper `006C83A0`
and filter-wrapper methods are guarded dependencies. The five-source compositor
branch is outside this invocation.

Failed GetDesc returns before the compositor consumes its output or changes
render state. Other failures retain native cleanup. The adapter attempts checked
target restoration, cancels pending fades and latches blending off for the session.
A device that refuses restoration requires the game's reset path.

### State ownership

The adapter restores the target described by `00982A20`, including its full-target
viewport. Subsequent native rendering owns the remaining state:

- Native declaration binding at `006E02E4..006E0300` and `006C5450..006C547E`
  must precede the next dependent draw. The compositor clears current-effect
  identity at `006D40CF`.
- Draws using constant blend factors must set their own `D3DRS_BLENDFACTOR`.
  Other blend modes do not consume it.
- Integers `0093DE20..0093DE30` are staged alpha-test and blend parameters.
  Material setup replaces them through `006C67C0`/`006C6810` before uploading
  them with SetIntArray. They are not a device-state cache.

A nonempty queue must be drained under its original target/effect contract before
publication, or publication must use a clean seam. Restoring a target after drawing
cannot repair an earlier draw to the wrong destination. Additional state restoration
requires a demonstrated consumer dependency. Request [user playtest feedback](validation.md) for visible defects with the installed
mod combination. Playtest results alone do not prove internal state ownership.

## Startup timing

- UAL invokes `InitializeASI` after `LoadLibraryW` returns. `DllMain` only disables
  thread notifications. Supported use is normal startup loading, not late injection.
- With the installed default configuration, UAL's IAT hooks include
  `GetCommandLineA`. The verified executable calls it at `007C4144`, before
  `_main` at `007C41BE`, so startup loading precedes the renderer.
  See [UAL 9.7.4 source](https://github.com/ThirteenAG/Ultimate-ASI-Loader/blob/v9.7.4/source/dllmain.cpp)
  (`FindFiles`, `CustomGetCommandLineA`, `kernel32Customs`, `Init`).

## Executable validation

Relaxing executable identity restrictions requires structural checks for every
hard-coded native dependency.

See [ETW diagnostics](diagnostics.md) for measurement interpretation.
