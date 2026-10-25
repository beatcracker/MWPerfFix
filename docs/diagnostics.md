# ETW diagnostics

Use [mise tasks](../mise.toml) for capture analysis and schema validation. Use [user playtest feedback](validation.md) to request capture context. Captures should reflect representative driving without workload changes made to force scheduler transitions.

## Interpretation limits

- CPU elapsed timings can include synchronous driver waits. They do not measure GPU execution time.
- Full and weighted modes select different workloads. Their pacing averages do not establish causal gains. Recovery comparisons include only episodes that already met recovery conditions.
- Initial face age is unknown until a completed update is observed. Invalid frames, resets and long gaps break continuity. Do not fill missing observations.
- `Metadata.TargetFPS` is the startup value. Each valid frame's `CadenceBudgetQpcTicks` supplies the effective target after live edits.
- Adaptive bypass stops observations. Gaps alone do not prove a manual toggle. Re-entry starts with a full refresh without a fabricated transition.
- Contiguous frame sequences do not prove loss-free delivery. Require trace-header loss counters for that claim. Missing counters leave delivery unverified. Nonzero loss blocks capture acceptance.

## Archived evidence

Retain decoded 90%/95% policy captures as research evidence. Their workloads were unmatched and cannot support a policy performance comparison. Earlier geometry phase/counter observations were invalid and must not be used as evidence of a scheduler defect.
