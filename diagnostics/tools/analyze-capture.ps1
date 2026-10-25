param(
    [Parameter(Mandatory = $true)][string]$EtlPath,
    [Parameter(Mandatory = $true)][string]$PresentMonCsv,
    [string]$OutputDirectory = ''
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'analysis-common.ps1')
$project = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$build = Join-Path $project 'build'
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $build ('scheduler-analysis-' + (Get-Date -Format 'yyyyMMdd-HHmmss')) }
$output = [IO.Path]::GetFullPath($OutputDirectory)
if (-not $output.StartsWith($build + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) { throw 'Analysis output must be below the project build directory' }
if (Test-Path -LiteralPath $output) { throw 'Choose a new analysis output directory' }
$etl = (Resolve-Path -LiteralPath $EtlPath).Path
$present = @(Import-Csv -LiteralPath $PresentMonCsv)
if ($present.Count -eq 0) { throw 'PresentMon CSV is empty' }
foreach ($field in @('ProcessID','QPCTime','msBetweenPresents','msGPUActive','msInPresentAPI')) {
    if ($null -eq $present[0].PSObject.Properties[$field]) { throw "Missing PresentMon column: $field" }
}
New-Item -ItemType Directory -Path $output | Out-Null
$rows = [Collections.Generic.List[object]]::new()
$raw = Join-Path $output 'scheduler-provider.raw'
$partial = "$raw.partial"
try {
    $extracted = [MwPerfEtwReader]::Extract($etl, $partial)
    Move-Item -LiteralPath $partial -Destination $raw -ErrorAction Stop
    Write-Output "Extracted $extracted provider-filtered ETW events."
} catch {
    Remove-Item -LiteralPath $partial -ErrorAction SilentlyContinue
    throw "Provider extraction failed; no analysis report produced: $_"
}
$reader = [IO.StreamReader]::new($raw)
$writer = [IO.StreamWriter]::new((Join-Path $output 'scheduler.jsonl'), $false)
try {
    $decodedCount = 0
    while ($null -ne ($line = $reader.ReadLine())) {
        $separator = $line.IndexOf([char]9)
        if ($separator -lt 1) { throw "Bad provider extraction line: $decodedCount" }
        $process = [uint32]::Parse($line.Substring(0,$separator))
        $payload = $line.Substring($separator + 1)
        if ($payload.Length -lt 2) { throw "Empty provider record at $decodedCount" }
        switch ($payload[0]) {
            'R' {
                $row = Convert-SchedulerPayload $payload.Substring(1)
                $row | Add-Member -NotePropertyName ProcessId -NotePropertyValue $process
            }
            'N' {
                $eventXml = [Text.Encoding]::UTF8.GetString([Convert]::FromBase64String($payload.Substring(1)))
                $row = Convert-SchedulerXml ([xml]$eventXml)
                if ($row.ProcessId -ne $process) { throw "Named provider process mismatch at $decodedCount" }
            }
            default { throw "Unknown provider record encoding at $decodedCount" }
        }
        $rows.Add($row)
        $writer.WriteLine(($row | ConvertTo-Json -Compress -Depth 5))
        ++$decodedCount
        if ($decodedCount % 10000 -eq 0) { Write-Output "Decoded $decodedCount scheduler events." }
    }
    if ($decodedCount -ne $extracted) { throw "Extraction/decoding count mismatch" }
} finally { $reader.Dispose(); $writer.Dispose() }
Write-Output "Decoded complete provider stream: $decodedCount events."
Assert-SchedulerRows $rows.ToArray()
$loss = [ordered]@{ Status = 'unverified'; Source = 'no readable ETW trace-header counters'; EventsLost = $null; BuffersLost = $null; BlocksAcceptance = $true }
$xperf = Get-Command xperf.exe -ErrorAction SilentlyContinue
if ($null -ne $xperf) {
    try {
        $rawStats = (& $xperf.Source '-i' $etl '-a' 'tracestats' 2>&1 | Out-String)
        if ($LASTEXITCODE -eq 0) {
            $eventMatch = [regex]::Match($rawStats, '(?im)^\s*(?:Events\s+Lost|Lost\s+Events)\s*:\s*(\d+)')
            $bufferMatch = [regex]::Match($rawStats, '(?im)^\s*(?:Buffers\s+Lost|Lost\s+Buffers)\s*:\s*(\d+)')
            if ($eventMatch.Success -and $bufferMatch.Success) {
                $loss.EventsLost = [uint64]::Parse($eventMatch.Groups[1].Value)
                $loss.BuffersLost = [uint64]::Parse($bufferMatch.Groups[1].Value)
                $loss.Source = 'xperf -i <etl> -a tracestats'
                $loss.Status = if ($loss.EventsLost -or $loss.BuffersLost) { 'loss-detected' } else { 'verified-zero-loss' }
                $loss.BlocksAcceptance = [bool]($loss.EventsLost -or $loss.BuffersLost)
            }
        }
    } catch { $loss.Source = "xperf unavailable or failed: $_" }
}
$deliveryText = if ($loss.Status -eq 'loss-detected') {
    "ETW LOSS DETECTED: events=$($loss.EventsLost), buffers=$($loss.BuffersLost); acceptance blocked."
} elseif ($loss.Status -eq 'verified-zero-loss') {
    'ETW trace-header event and buffer loss counters verified zero by xperf.'
} else {
    'ETW delivery unverified: trace-header loss counters unavailable. Contiguous frame sequences alone do not prove loss-free capture.'
}
$results = @()
foreach ($group in @($rows | Group-Object ProcessId, ProcessCreationId)) {
    $results += Get-SchedulerAnalysis $group.Group $present
}
$report = [ordered]@{
    Schema = 23
    SourceEtl = $etl
    PresentMonCsv = (Resolve-Path -LiteralPath $PresentMonCsv).Path
    Delivery = $deliveryText
    DeliveryCounters = $loss
    Interpretation = 'CPU elapsed timings include hook overhead, descheduling and synchronous waits, not GPU work. Recovery comparisons are selection-biased; mode-conditioned pacing is observational, not causal.'
    Processes = $results
}
$report | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath (Join-Path $output 'analysis.json') -Encoding UTF8
$lines = @(
    '# Adaptive reflections - capture report (schema 23)', '',
    "ETW: $etl", "PresentMon: $($report.PresentMonCsv)", '',
    "Delivery: $($report.Delivery)", '',
    $report.Interpretation, '',
    '## Frame pacing and validity', ''
)
foreach ($result in $results) {
    $meta = $result.Metadata
    $lines += "### Process $($meta.ProcessId), $($meta.TargetFPS) FPS startup target"
    if ($result.MixedTargets) {
        $lines += 'Mixed-target recording: effective per-frame cadence budgets vary; over-target counts use each frame budget.'
    }
    $lines += ''
    $lines += "$($result.ValidFrames)/$($result.Frames) eligible timing-valid frames; PresentMon matched $($result.MatchedFrames), unmatched $($result.UnmatchedFrames). Build $($meta.BuildVersion) ($($meta.BuildVariant)); executable timestamp $($meta.ExecutableTimeDateStamp)."
    $lines += ''
    $lines += '| Selected budget | Matched | Present interval P50 / P95 / P99 (ms) | Over target |'
    $lines += '| --- | ---: | ---: | ---: |'
    foreach ($pace in $result.PresentMon) {
        $budgetName = if ($pace.Budget -eq 0) { 'All matched' } elseif ($pace.Budget -eq 6) { 'Full (6)' } else { 'Weighted (1)' }
        $interval = if ($pace.Matched) { '{0:N2} / {1:N2} / {2:N2}' -f $pace.IntervalMs.P50, $pace.IntervalMs.P95, $pace.IntervalMs.P99 } else { 'n/a' }
        $lines += "| $budgetName | $($pace.Matched) | $interval | $($pace.OverTarget) |"
    }
    $lines += ''
    $lines += '## Transitions and recovery'
    $lines += ''
    $lines += "Downshifts $($result.Downshifts), recoveries $($result.Recoveries), resets $($result.Resets)."
    $full = @($result.FullEpisodes)
    $weighted = @($result.WeightedEpisodes)
    $shortFull = @($full | Where-Object { $_.DurationSeconds -lt 0.5 })
    $longWeighted = @($weighted | Where-Object { $_.DurationSeconds -ge 1.0 })
    $lines += "Full periods: $($full.Count) ($($shortFull.Count) shorter than 0.5 s); weighted periods: $($weighted.Count) ($($longWeighted.Count) at least 1 s). Capture and invalid boundaries are censored."
    $predictions = @($result.Transitions | Where-Object { $null -ne $_.RecoveryPrediction })
    $medianError = if ($predictions.Count) { '{0:N3}' -f $result.RecoveryPredictionErrorsMs.P50 } else { 'n/a' }
    $lines += "Recovery comparisons: $($predictions.Count), median signed error $medianError ms (positive = overestimate). Only successful recoveries are sampled."
    $lines += ''
    $lines += '| Transition | Reason | Prior -> new budget | Predicted / actual / error (ms) |'
    $lines += '| ---: | ---: | --- | --- |'
    foreach ($change in $result.Transitions) {
        $prediction = if ($null -eq $change.RecoveryPrediction) { 'n/a' } else {
            '{0:N2} / {1:N2} / {2:+0.00;-0.00;0.00}' -f $change.RecoveryPrediction.PredictedMs, $change.RecoveryPrediction.ActualMs, $change.RecoveryPrediction.ErrorMs
        }
        $lines += "| $($change.Sequence) | $($change.Reason) | $($change.PreviousBudget) -> $($change.NewBudget) | $prediction |"
    }
    $lines += ''
    $lines += '## Face cost and freshness'
    $lines += ''
    $lines += '| Face | Full P50 (ms) | Weighted P50 (ms) | Weighted age P95 (ms) |'
    $lines += '| ---: | ---: | ---: | ---: |'
    foreach ($face in $result.FaceDistributionsMs) {
        $age = @($result.EstimateAges | Where-Object { $_.Face -eq $face.Face -and $_.Budget -eq 1 })
        $stale = if ($age.Count -eq 1 -and $age[0].StalenessMs.Count -gt 0) { '{0:N2}' -f $age[0].StalenessMs.P95 } else { 'unknown' }
        $f6 = if ($face.Budget6.Count) { '{0:N2}' -f $face.Budget6.P50 } else { 'n/a' }
        $f1 = if ($face.Budget1.Count) { '{0:N2}' -f $face.Budget1.P50 } else { 'n/a' }
        $lines += "| $($face.Face) | $f6 | $f1 | $stale |"
    }
    $lines += ''
    $lines += '## Blending CPU elapsed time'
    $lines += ''
    $lines += '| Budget / mode / steps | Frames (timed) | Begin frame P50 | Select P50 | Begin face P50 | End face P50 (ms) |'
    $lines += '| --- | ---: | ---: | ---: | ---: | ---: |'
    foreach ($blend in $result.Blending) {
        $timed = if ($blend.ValidBlendTimingFrames) {
            ('{0:N3} | {1:N3} | {2:N3} | {3:N3}' -f $blend.BlendBeginFrameQpcTicksMs.P50, $blend.BlendSelectScheduleQpcTicksMs.P50, $blend.BlendBeginFaceQpcTicksMs.P50, $blend.BlendEndFaceQpcTicksMs.P50)
        } else { 'n/a | n/a | n/a | n/a' }
        $lines += "| $($blend.Budget) / $($blend.BlendMode) / $($blend.BlendSteps) | $($blend.Frames) ($($blend.ValidBlendTimingFrames)) | $timed |"
    }
    $lines += ''
    $lines += 'See analysis.json for per-face distributions, invalid coverage, recovery blockers, censored episode details and freshness counts. Unknown initial face ages are not zeros. Unmatched PresentMon frames are excluded.'
    $lines += ''
}
$lines | Set-Content -LiteralPath (Join-Path $output 'REPORT.md') -Encoding UTF8
Write-Output "Analysis saved: $output"