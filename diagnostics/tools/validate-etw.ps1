param(
    [Parameter(Mandatory = $true)][string]$XmlPath,
    [switch]$RequireSynthetic
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Field($Event, [string]$Name) {
    $node = $Event.SelectSingleNode(".//*[local-name()='Data'][@Name='$Name']")
    if ($null -eq $node) { throw "Missing field $Name" }
    return $node.InnerText.Trim()
}
$schema = @{
    'Metadata' = @{
        UInt32 = @('SchemaVersion', 'ExecutableTimeDateStamp', 'ExecutableSizeOfImage', 'TargetFPS', 'RequestedFixMask', 'AppliedFixMask', 'FixOutcomes')
        UInt64 = @('ProcessCreationId', 'ExecutableBase', 'QpcFrequency')
        Boolean = @('Synthetic')
        String = @('SchemaMarker', 'BuildVersion', 'BuildVariant', 'ElapsedUnit')
    }
    'AdaptiveReflectionFrame' = @{
        UInt32 = @('SchemaVersion', 'GameFrameCounter', 'ConfiguredMode', 'SelectedBudget', 'PolicyBudget', 'RecoveryStreak', 'FaceMask', 'FirstInvalidReason', 'FirstInvalidSite', 'FirstInvalidFace', 'RequestedFaceMask', 'FaceBeginMask', 'CompletedFaceMask', 'FaceBeginCount', 'FaceEndCount', 'IgnoredForeignFaceCallbacks', 'UnknownFaceIndices', 'InterruptedRendererCount')
        UInt64 = @('ProcessCreationId', 'FrameSequence', 'CompletionQpc', 'RetryAfterQpc', 'RendererWorkQpcTicks', 'ObservedFaceQpcTicks', 'ResidualWorkQpcTicks', 'ActiveFrameQpcTicks', 'ActiveNonReflectionQpcTicks', 'PredictedFullQpcTicks', 'FrameIntervalQpcTicks', 'CadenceBudgetQpcTicks', 'FirstInvalidQpc', 'RendererBeginQpc', 'PrePresentQpc', 'PreviousPostPresentQpc', 'DecisionSequence')
        Boolean = @('SelectionValid', 'Eligible', 'TimingValid', 'PresentSucceeded', 'FullRefreshPending', 'CadenceHealthy', 'Synthetic')
        String = @()
    }
    'AdaptiveReflectionStateChange' = @{
        UInt32 = @('SchemaVersion', 'GameFrameCounter', 'PreviousBudget', 'NewBudget', 'Reason', 'EligibleFrames', 'CheapStreak', 'ExpensiveStreak', 'FaceMask')
        UInt64 = @('ProcessCreationId', 'Sequence', 'DecisionQpc', 'RendererWorkQpcTicks', 'PredictedFullQpcTicks', 'CadenceBudgetQpcTicks')
        Boolean = @('FullRefreshPending', 'Synthetic')
        String = @()
    }
}

$schema.AdaptiveReflectionFrame.UInt64 += @('Face0QpcTicks', 'Face1QpcTicks', 'Face2QpcTicks', 'Face3QpcTicks', 'Face4QpcTicks', 'Face5QpcTicks', 'Face0EmaQpcTicks', 'Face1EmaQpcTicks', 'Face2EmaQpcTicks', 'Face3EmaQpcTicks', 'Face4EmaQpcTicks', 'Face5EmaQpcTicks', 'ResidualEmaQpcTicks', 'FrameIntervalEmaQpcTicks', 'BlendBeginFrameQpcTicks', 'BlendSelectScheduleQpcTicks', 'BlendBeginFaceQpcTicks', 'BlendEndFaceQpcTicks')
$schema.AdaptiveReflectionFrame.UInt32 += @('BlendMode', 'BlendSteps')
$schema.AdaptiveReflectionFrame.Boolean += @('CostsValid', 'BlendActive', 'BlendFailed', 'BlendTimingValid')

. (Join-Path $PSScriptRoot 'analysis-common.ps1')

$metadata = @{}
$seen = [Collections.Generic.HashSet[string]]::new()
$sequences = @{}
$counts = @{}
$lossVisible = $false
$reader = [Xml.XmlReader]::Create((Resolve-Path -LiteralPath $XmlPath).Path)
try {
    while ($reader.Read()) {
        if ($reader.NodeType -ne [Xml.XmlNodeType]::Element -or $reader.LocalName -ne 'Event') { continue }
        $doc = [Xml.XmlDocument]::new()
        $subtree = $reader.ReadSubtree()
        try { $doc.Load($subtree) } finally { $subtree.Dispose() }
        $event = $doc.DocumentElement
        $provider = $event.SelectSingleNode("./*[local-name()='System']/*[local-name()='Provider']")
        if ($null -eq $provider -or $provider.GetAttribute('Name') -ne 'MWPerfFix.Diagnostics') {
            foreach ($name in @('EventsLost', 'BuffersLost')) {
                $node = $event.SelectSingleNode(".//*[local-name()='Data'][@Name='$name']")
                if ($null -ne $node) {
                    $lossVisible = $true
                    if ([UInt64]::Parse($node.InnerText) -ne 0) { throw "ETW $name is nonzero" }
                }
            }
            continue
        }
        $task = $event.SelectSingleNode("./*[local-name()='RenderingInfo']/*[local-name()='Task']")
        if ($null -eq $task -or -not $schema.ContainsKey($task.InnerText)) { throw 'Unknown scheduler event' }
        $name = $task.InnerText
        $fields = [ordered]@{}
        foreach ($node in $event.SelectNodes(".//*[local-name()='Data'][@Name]")) {
            if ($fields.Contains($node.GetAttribute('Name'))) { throw 'Duplicate field' }
            $fields[$node.GetAttribute('Name')] = $node.InnerText.Trim()
        }
        $parsed = Convert-SchedulerFields $name $fields
        foreach ($field in $schema[$name].UInt32) { [void][UInt32]::Parse((Field $event $field)) }
        foreach ($field in $schema[$name].UInt64) { [void][UInt64]::Parse((Field $event $field)) }
        foreach ($field in $schema[$name].String) { [void](Field $event $field) }
        foreach ($field in $schema[$name].Boolean) {
            if ((Field $event $field) -notin @('0', '1')) { throw "Invalid Boolean $field" }
        }
        if ((Field $event 'SchemaVersion') -ne '23') { throw 'Expected schema 23' }
        if ($RequireSynthetic -and (Field $event 'Synthetic') -ne '1') { throw 'Expected synthetic event' }
        $identity = Field $event 'ProcessCreationId'
        if ($identity -eq '0') { throw 'Missing process identity' }
        if ($name -eq 'Metadata') {
            if ((Field $event 'SchemaMarker') -ne 'MWPERFFIX_ETW_SCHEMA_23') { throw 'Invalid schema marker' }
            if ([UInt64]::Parse((Field $event 'QpcFrequency')) -eq 0) { throw 'Missing QPC frequency' }
            $metadata[$identity] = $true
        } else {
            [void]$seen.Add($identity)
            $field = if ($name -eq 'AdaptiveReflectionFrame') { 'FrameSequence' } else { 'Sequence' }
            $sequence = [UInt64]::Parse((Field $event $field))
            $key = "$identity/$name"
            if ($sequence -eq 0) { throw 'Zero sequence' }
            if ($sequences.ContainsKey($key)) {
                if ($sequence -le $sequences[$key]) { throw 'Non-increasing sequence' }
                # Frames are numbered on emission. Decisions can occur with no listener.
                if ($name -eq 'AdaptiveReflectionFrame' -and $sequence -ne $sequences[$key] + 1) {
                    throw 'Missing frame events'
                }
            }
            $sequences[$key] = $sequence
        }
        if (-not $counts.ContainsKey($name)) { $counts[$name] = 0 }
        ++$counts[$name]
    }
} finally { $reader.Dispose() }
if ($seen.Count -eq 0) { throw 'No scheduler observations' }
foreach ($identity in $seen) {
    if (-not $metadata.ContainsKey($identity)) { throw "No metadata for process $identity" }
}
foreach ($name in $counts.Keys) { Write-Output "$name : $($counts[$name])" }
if (-not $lossVisible) { Write-Output 'ETW loss fields unavailable: delivery completeness is unverified.' }
Write-Output 'Scheduler schema and identity checks passed.'