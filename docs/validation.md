# User playtest feedback

The user performs playtests. Ask only about behavior relevant to the change and request missing context. Automated checks do not establish visual quality, compatibility or gameplay speedup. Record untested behavior separately from user-confirmed results, with the tested build and conditions.

## Context to request

- Build identity and release or diagnostic variant.
- Game executable, loader and active mod versions, including Widescreen Fix, ZPatch, HD Reflections and Extra Options where installed.
- Native reflection mode, adaptive and blending settings, TargetFPS, BlendSteps and limiter/VSync/VRR settings.
- Scene, race or camera, action taken, expected result and observed result. Request reproduction steps for problems and use logs, screenshots or video if available.

## Rendering and blending

Ask about ordinary driving with the release build:

- Do reflections remain correctly oriented and complete when update frequency changes? Are there flashes, blank faces, excessive staleness or rapid switching?
- With blending off and on, how do ghosting, blur and smoothness compare in native mode 1 and adaptive single-face operation? Which blend step counts were used? Full updates should bypass blending.
- Do repeated updates leave old images stuck or cause abrupt changes during a fade?
- After race entry/exit, camera changes, rearview, pause or device reset, do reflections recover without stale images or corrupted rendering?
- Does the installed mod combination introduce visual defects, crashes or behavior absent without those mods?

## Live tuning and notifications

Ask which bindings were used and what happened:

- Does each press act once? Do held keys, background input or returning focus trigger unintended changes?
- Does disabling adaptive scheduling restore native reflection behavior, including native-off? Does re-enabling it refresh stale reflections?
- Do target FPS, blending and blend-step adjustments produce the expected feedback without flashes or stuck fades?
- Are notifications readable and transient, with no unsolicited tuning messages when LiveTuning is disabled? Extra Options `DoScreenPrintf` must be off because it can conflict with native notifications.
- Does restarting restore INI settings? If the log reports a disabled or failed feature, does it remain unavailable through hotkeys?

## Diagnostic captures

If the user made a capture, ask for the ETL, matching PresentMon CSV, build and settings, plus any live adjustments and observed transitions. Ask whether the workload was representative and whether any comparison used matched conditions. Apply the [diagnostic interpretation limits](diagnostics.md). A trace does not establish visual acceptance.
