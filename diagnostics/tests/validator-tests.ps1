param([string]$Validator, [string]$Work)
$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force -Path $Work | Out-Null
function Event([string]$Name, [hashtable]$Fields) {
    $data = foreach ($key in $Fields.Keys) { '<Data Name="' + $key + '">' + $Fields[$key] + '</Data>' }
    return '<Event><System><Provider Name="MWPerfFix.Diagnostics"/></System><EventData>' +
        ($data -join '') + '</EventData><RenderingInfo><Task>' + $Name + '</Task></RenderingInfo></Event>'
}
function Check([string]$Name, [string]$Events, [bool]$Pass) {
    $file = Join-Path $Work "$Name.xml"
    Set-Content -LiteralPath $file -Value ('<Events>' + $Events + '</Events>')
    $ErrorActionPreference = 'Continue'
    & powershell -NoProfile -ExecutionPolicy Bypass -File $Validator -XmlPath $file -RequireSynthetic *> (Join-Path $Work "$Name.log")
    $ErrorActionPreference = 'Stop'
    if (($LASTEXITCODE -eq 0) -ne $Pass) { throw "Unexpected validator result: $Name" }
}
$Metadata = @{'SchemaVersion' = '23'; 'SchemaMarker' = 'MWPERFFIX_ETW_SCHEMA_23'; 'BuildVersion' = 'test'; 'BuildVariant' = 'test'; 'ProcessCreationId' = '100'; 'ExecutableBase' = '0'; 'ExecutableTimeDateStamp' = '0'; 'ExecutableSizeOfImage' = '0'; 'TargetFPS' = '60'; 'QpcFrequency' = '1000'; 'RequestedFixMask' = '0'; 'AppliedFixMask' = '0'; 'FixOutcomes' = '0'; 'ElapsedUnit' = 'test'; 'Synthetic' = '1'}
$AdaptiveReflectionFrame = @{'SchemaVersion' = '23'; 'ProcessCreationId' = '100'; 'FrameSequence' = '1'; 'GameFrameCounter' = '0'; 'CompletionQpc' = '0'; 'ConfiguredMode' = '0'; 'SelectedBudget' = '0'; 'PolicyBudget' = '0'; 'RecoveryStreak' = '0'; 'RetryAfterQpc' = '0'; 'SelectionValid' = '0'; 'Eligible' = '0'; 'TimingValid' = '0'; 'PresentSucceeded' = '0'; 'FullRefreshPending' = '0'; 'RendererWorkQpcTicks' = '0'; 'ObservedFaceQpcTicks' = '0'; 'ResidualWorkQpcTicks' = '0'; 'ActiveFrameQpcTicks' = '0'; 'ActiveNonReflectionQpcTicks' = '0'; 'PredictedFullQpcTicks' = '0'; 'FrameIntervalQpcTicks' = '0'; 'CadenceHealthy' = '0'; 'CadenceBudgetQpcTicks' = '0'; 'FaceMask' = '0'; 'FirstInvalidReason' = '0'; 'FirstInvalidSite' = '0'; 'FirstInvalidFace' = '0'; 'FirstInvalidQpc' = '0'; 'RequestedFaceMask' = '0'; 'FaceBeginMask' = '0'; 'CompletedFaceMask' = '0'; 'FaceBeginCount' = '0'; 'FaceEndCount' = '0'; 'RendererBeginQpc' = '0'; 'PrePresentQpc' = '0'; 'PreviousPostPresentQpc' = '0'; 'IgnoredForeignFaceCallbacks' = '0'; 'UnknownFaceIndices' = '0'; 'InterruptedRendererCount' = '0'; 'DecisionSequence' = '0'; 'Synthetic' = '1'}
$AdaptiveReflectionStateChange = @{'SchemaVersion' = '23'; 'ProcessCreationId' = '100'; 'Sequence' = '1'; 'DecisionQpc' = '0'; 'GameFrameCounter' = '0'; 'PreviousBudget' = '0'; 'NewBudget' = '0'; 'Reason' = '0'; 'RendererWorkQpcTicks' = '0'; 'PredictedFullQpcTicks' = '0'; 'CadenceBudgetQpcTicks' = '0'; 'EligibleFrames' = '0'; 'CheapStreak' = '0'; 'ExpensiveStreak' = '0'; 'FaceMask' = '0'; 'FullRefreshPending' = '0'; 'Synthetic' = '1'}

foreach ($field in @('Face0QpcTicks', 'Face1QpcTicks', 'Face2QpcTicks', 'Face3QpcTicks', 'Face4QpcTicks', 'Face5QpcTicks', 'Face0EmaQpcTicks', 'Face1EmaQpcTicks', 'Face2EmaQpcTicks', 'Face3EmaQpcTicks', 'Face4EmaQpcTicks', 'Face5EmaQpcTicks', 'ResidualEmaQpcTicks', 'FrameIntervalEmaQpcTicks', 'BlendBeginFrameQpcTicks', 'BlendSelectScheduleQpcTicks', 'BlendBeginFaceQpcTicks', 'BlendEndFaceQpcTicks', 'BlendMode', 'BlendSteps', 'CostsValid', 'BlendActive', 'BlendFailed', 'BlendTimingValid')) { $AdaptiveReflectionFrame[$field] = '0' }
$m = Event 'Metadata' $Metadata
$f = Event 'AdaptiveReflectionFrame' $AdaptiveReflectionFrame
$c = Event 'AdaptiveReflectionStateChange' $AdaptiveReflectionStateChange
Check 'valid' ($m + $f + $c) $true
Check 'late-metadata' ($f + $c + $m) $true
Check 'no-metadata' $f $false
Check 'duplicate' ($m + $f + $f) $false
$AdaptiveReflectionFrame.FrameSequence = '3'
Check 'lost-frame' ($m + $f + (Event 'AdaptiveReflectionFrame' $AdaptiveReflectionFrame)) $false
$AdaptiveReflectionFrame.SchemaVersion = '18'
Check 'old-schema' ($m + (Event 'AdaptiveReflectionFrame' $AdaptiveReflectionFrame)) $false
$AdaptiveReflectionFrame.SchemaVersion = '23'
$AdaptiveReflectionFrame.Face0QpcTicks = 'bad'
Check 'bad-face-value' ($m + (Event 'AdaptiveReflectionFrame' $AdaptiveReflectionFrame)) $false
$AdaptiveReflectionFrame.Face0QpcTicks = '4294967297'
Check 'wide-face-value' ($m + (Event 'AdaptiveReflectionFrame' $AdaptiveReflectionFrame)) $true
$AdaptiveReflectionFrame.Remove('BlendEndFaceQpcTicks')
Check 'missing-blend-value' ($m + (Event 'AdaptiveReflectionFrame' $AdaptiveReflectionFrame)) $false
$AdaptiveReflectionFrame.BlendEndFaceQpcTicks = '0'
$AdaptiveReflectionFrame.TimingValid = '2'
Check 'bad-boolean' ($m + (Event 'AdaptiveReflectionFrame' $AdaptiveReflectionFrame)) $false
$AdaptiveReflectionFrame.Remove('TimingValid')
Check 'missing-field' ($m + (Event 'AdaptiveReflectionFrame' $AdaptiveReflectionFrame)) $false
Check 'loss' ($m + $f + '<Event><EventData><Data Name="EventsLost">1</Data></EventData></Event>') $false
Write-Output 'Validator tests passed.'