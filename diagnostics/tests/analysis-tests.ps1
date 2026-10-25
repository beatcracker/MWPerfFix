Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '..\tools\analysis-common.ps1')
function Check([bool]$Condition, [string]$Message) { if (-not $Condition) { throw $Message } }
function Reject([scriptblock]$Action, [string]$Message) {
    $rejected = $false
    try { & $Action | Out-Null } catch { $rejected = $true }
    Check $rejected $Message
}
function Row([string]$Name) {
    $fields = [ordered]@{}
    foreach ($field in $script:SchedulerSchema.PSObject.Properties[$Name].Value) {
        $fields[$field[1]] = if ($field[0] -eq 'String') { '' } else { '0' }
    }
    $fields.SchemaVersion = '23'; $fields.ProcessCreationId = '100'
    $result = Convert-SchedulerFields $Name $fields
    $result | Add-Member -NotePropertyName ProcessId -NotePropertyValue 123
    return $result
}
function Payload($Row) {
    $stream = [IO.MemoryStream]::new()
    $writer = [IO.BinaryWriter]::new($stream)
    try {
        foreach ($field in $script:SchedulerSchema.PSObject.Properties[$Row.Event].Value) {
            $value = $Row.($field[1])
            switch ($field[0]) {
                'UInt32' { $writer.Write([uint32]$value) }
                'UInt64' { $writer.Write([uint64]$value) }
                'Boolean' { $writer.Write([byte]$value) }
                'String' { $writer.Write([Text.Encoding]::UTF8.GetBytes([string]$value)); $writer.Write([byte]0) }
            }
        }
        return [BitConverter]::ToString($stream.ToArray()).Replace('-', '')
    } finally { $writer.Dispose(); $stream.Dispose() }
}
$meta = Row 'Metadata'
$meta.SchemaMarker = 'MWPERFFIX_ETW_SCHEMA_23'
$meta.QpcFrequency = 1000; $meta.TargetFPS = 60
$meta.BuildVariant = 'diagnostic'; $meta.BuildVersion = 'test'
$roundtrip = Row 'AdaptiveReflectionFrame'
$roundtrip.Face5QpcTicks = [uint64]4294967298
$roundtrip.Face5EmaQpcTicks = [uint64]4294967299
$roundtrip.BlendBeginFaceQpcTicks = [uint64]4294967300
$hex = Payload $roundtrip
$decoded = Convert-SchedulerPayload $hex
Check ($decoded.Face5QpcTicks -eq 4294967298 -and $decoded.Face5EmaQpcTicks -eq 4294967299 -and $decoded.BlendBeginFaceQpcTicks -eq 4294967300) '64-bit payload failed'
Reject { Convert-SchedulerPayload ('16000000' + $hex.Substring(8)) } 'Schema 22 accepted'
Reject { Convert-SchedulerPayload ($hex + '00') } 'Trailing payload accepted'
Reject { Convert-SchedulerPayload $hex.Substring(0, $hex.Length - 2) } 'Incomplete payload accepted'
Reject { Convert-SchedulerPayload 'xy' } 'Malformed hex accepted'
$xml = [xml]("<Event><System><Execution ProcessID='123'/></System><ProcessingErrorData><EventPayload>$hex</EventPayload></ProcessingErrorData></Event>")
Check ((Convert-SchedulerXml $xml).ProcessId -eq 123) 'Raw XML decode failed'
$fieldsXml = ($script:SchedulerSchema.AdaptiveReflectionFrame | ForEach-Object { "<Data Name='$($_[1])'>$($roundtrip.($_[1]))</Data>" }) -join ''
$xml = [xml]("<Event><System><Execution ProcessID='123'/></System><EventData>$fieldsXml</EventData><RenderingInfo><Task>AdaptiveReflectionFrame</Task></RenderingInfo></Event>")
$named = Convert-SchedulerXml $xml
foreach ($entry in $script:SchedulerSchema.AdaptiveReflectionFrame) {
    $field = $entry[1]
    Check ($decoded.$field -eq $named.$field) "Raw/named mismatch: $field"
}
$xmlTaskless = [xml]("<Event><System><Execution ProcessID='123'/></System><EventData>$fieldsXml</EventData></Event>")
Check ((Convert-SchedulerXml $xmlTaskless).Face5QpcTicks -eq 4294967298) 'Taskless named decode failed'
$missing = [ordered]@{ SchemaVersion = '23' }
Reject { Convert-SchedulerFields 'AdaptiveReflectionFrame' $missing } 'Missing named fields accepted'
$namedFields = [ordered]@{}
foreach ($field in $script:SchedulerSchema.AdaptiveReflectionFrame) { $namedFields[$field[1]] = [string]$roundtrip.($field[1]) }
$namedFields.TimingValid = '2'
Reject { Convert-SchedulerFields 'AdaptiveReflectionFrame' $namedFields } 'Invalid Boolean accepted'
$namedFields.TimingValid = '0'; $namedFields.SchemaVersion = '22'
Reject { Convert-SchedulerFields 'AdaptiveReflectionFrame' $namedFields } 'Old named schema accepted'

$script:baseQpc = [uint64]5000000000
function Frame([int]$Sequence, [int]$Budget) {
    $r = Row 'AdaptiveReflectionFrame'
    $r.FrameSequence = $Sequence; $r.GameFrameCounter = $Sequence
    $r.CompletionQpc = $script:baseQpc + $Sequence * 10
    $r.PrePresentQpc = $r.CompletionQpc - 1
    $r.RendererBeginQpc = $r.CompletionQpc - 8
    $r.SelectionValid = 1; $r.Eligible = 1; $r.TimingValid = 1; $r.PresentSucceeded = 1
    $r.SelectedBudget = $Budget; $r.PolicyBudget = $Budget
    $r.CostsValid = 1; $r.CadenceHealthy = 1; $r.CadenceBudgetQpcTicks = 40
    $r.BlendTimingValid = 1; $r.BlendSteps = 6; $r.BlendMode = 1
    $r.FaceMask = if ($Budget -eq 6) { 63 } else { 1 }
    $r.CompletedFaceMask = $r.FaceMask
    $r.RequestedFaceMask = $r.FaceMask
    $r.ResidualEmaQpcTicks = 9; $r.ResidualWorkQpcTicks = 9; $r.PredictedFullQpcTicks = 30
    for ($face = 0; $face -lt 6; $face++) {
        $r.("Face" + $face + "EmaQpcTicks") = $face + 1
        $r.("Face" + $face + "QpcTicks") = if ($Budget -eq 6) { $face + 1 } elseif ($face -eq 0) { 3 } else { 0 }
        $r.ObservedFaceQpcTicks += $r.("Face" + $face + "QpcTicks")
    }
    $r.RendererWorkQpcTicks = $r.ResidualWorkQpcTicks + $r.ObservedFaceQpcTicks
    return $r
}
function Change([int]$Sequence, $Frame, [int]$Budget, [int]$Reason) {
    $r = Row 'AdaptiveReflectionStateChange'
    $r.Sequence = $Sequence; $r.DecisionQpc = $Frame.CompletionQpc
    $r.GameFrameCounter = $Frame.GameFrameCounter
    $r.PreviousBudget = if ($Budget -eq 6) { 1 } else { 6 }; $r.NewBudget = $Budget; $r.Reason = $Reason
    return $r
}
$f1 = Frame 1 6; $f2 = Frame 2 6; $f3 = Frame 3 1
$f4 = Frame 4 1; $f5 = Frame 5 6; $f6 = Frame 6 6
$c1 = Change 1 $f2 1 3; $c2 = Change 2 $f4 6 4
$f4.DecisionSequence = 2
$rows = @($meta,$f1,$c1,$f2,$f3,$c2,$f4,$f5,$f6)
Assert-SchedulerRows $rows
$pm = @($f1,$f2,$f3,$f4,$f5,$f6) | ForEach-Object {
    [pscustomobject]@{ ProcessID = '123'; QPCTime = [string]$_.PrePresentQpc
        msBetweenPresents = '10.5'; msGPUActive = '2.5'; msInPresentAPI = '0.1' }
}
$analysis = Get-SchedulerAnalysis $rows $pm
Check ($analysis.MatchedFrames -eq 6) 'PresentMon QPC matching failed'
$f3.CadenceBudgetQpcTicks = 5
$mixed = Get-SchedulerAnalysis $rows $pm
Check $mixed.MixedTargets 'Live target edit not identified'
Check ($mixed.PresentMon[0].OverTarget -eq 1) 'Per-frame over-target budget ignored'
$f3.CadenceBudgetQpcTicks = 40
Check ($analysis.Transitions.Count -eq 2) 'Transition count failed'
Check ($analysis.Transitions[1].RecoveryPrediction.PredictedMs -eq 30) 'Recovery prediction failed'
Check ($analysis.FaceDistributionsMs[0].Budget1.Count -eq 2) 'Per-face samples failed'
Check ($analysis.EstimateAges.Count -eq 12) 'Per-face freshness missing'
Check ($analysis.Blending.Count -gt 0) 'Blend overhead missing'
Check ($analysis.WeightedEpisodes.Count -eq 1) 'Weighted episode missing'
# A single PresentMon row must never be assigned to two renderer frames.
$originalPresent = $f2.PrePresentQpc
$f2.PrePresentQpc = $f1.PrePresentQpc
$shared = Get-SchedulerAnalysis $rows $pm
Check ($shared.MatchedFrames -eq 5) 'PresentMon match reused across frames'
$f2.PrePresentQpc = $originalPresent

# An invalid observation splits and censors the weighted residence rather
# than treating the entire wall-clock span as one continuous episode.
$f3.TimingValid = 0
$broken = Get-SchedulerAnalysis $rows $pm
Check ($broken.WeightedEpisodes.Count -eq 1) 'Invalid interval leaked into weighted episodes'
Check ($broken.WeightedEpisodes[0].LeftCensored -and
       -not $broken.WeightedEpisodes[0].RightCensored) 'Weighted episode missing censoring'
$f3.TimingValid = 1
Reject { Assert-SchedulerRows @($meta,$f1,$f3) } 'Frame sequence gap accepted'
Reject { Assert-SchedulerRows @($meta,$f1,$f1) } 'Duplicate frame accepted'
$f1.Face0QpcTicks = 99
Reject { Assert-SchedulerRows $rows } 'Invalid face duration accepted'
$f1.Face0QpcTicks = 1
$f5.TimingValid = 0
$analysis = Get-SchedulerAnalysis $rows $pm
Check ($null -eq $analysis.Transitions[1].RecoveryPrediction) 'Prediction crossed invalid frame'
$f5.TimingValid = 1
Write-Output 'Schema-23 raw/named payload, validity, age, blend and PresentMon tests passed.'