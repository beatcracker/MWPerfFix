Set-StrictMode -Version Latest
$script:SchedulerSchema = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'schema-23.json') -Raw | ConvertFrom-Json
if (-not ('MwPerfEtwReader' -as [type])) {
    # Keep transient compiler outputs within the project boundary.
    $projectTemp = Join-Path $PSScriptRoot '..\..\build'
    $previousTemp = $env:TEMP; $previousTmp = $env:TMP
    try {
        $env:TEMP = $projectTemp; $env:TMP = $projectTemp
        Add-Type -Path (Join-Path $PSScriptRoot 'etw-reader.cs') -ErrorAction Stop
    } finally { $env:TEMP = $previousTemp; $env:TMP = $previousTmp }
}

function Convert-SchedulerFields([string]$EventName, $Fields) {
    $schema = $script:SchedulerSchema.PSObject.Properties[$EventName]
    if ($null -eq $schema) { throw "Unknown scheduler event: $EventName" }
    if ($Fields.Count -ne $schema.Value.Count) { throw "Unexpected field count for $EventName" }
    $result = [ordered]@{ Event = $EventName }
    foreach ($field in $schema.Value) {
        $type, $name = $field
        if (-not $Fields.Contains($name)) { throw "Missing $EventName field: $name" }
        $value = $Fields[$name]
        $result[$name] = switch ($type) {
            'UInt32' { [UInt32]::Parse([string]$value, [Globalization.CultureInfo]::InvariantCulture) }
            'UInt64' { [UInt64]::Parse([string]$value, [Globalization.CultureInfo]::InvariantCulture) }
            'Boolean' {
                if ([string]$value -notin @('0', '1')) { throw "Invalid Boolean: $name" }
                [int]$value
            }

            'String' { [string]$value }
            default { throw "Unknown field type: $type" }
        }
    }
    if ($result.SchemaVersion -ne 23) { throw 'Expected scheduler schema 23; schema 22 raw ETW is unsupported; older captures require their original decoder.' }

    return [pscustomobject]$result
}

function Convert-SchedulerPayload([string]$Hex) {
    if ($Hex -notmatch '\A(?:[0-9a-fA-F]{2})+\z') { throw 'Malformed scheduler payload hex' }
    $bytes = [MwPerfEtwReader]::DecodeHex($Hex)
    if ($bytes.Length -lt 4 -or [BitConverter]::ToUInt32($bytes, 0) -ne 23) { throw 'Expected scheduler schema 23; schema 22 raw ETW is unsupported' }
    $candidates = [Collections.Generic.List[object]]::new()
    foreach ($schema in $script:SchedulerSchema.PSObject.Properties) {
        $stream = [IO.MemoryStream]::new($bytes, $false)
        $reader = [IO.BinaryReader]::new($stream)
        try {
            $fields = [ordered]@{}
            foreach ($field in $schema.Value) {
                $type, $name = $field
                $fields[$name] = switch ($type) {
                    'UInt32' { $reader.ReadUInt32() }
                    'UInt64' { $reader.ReadUInt64() }
                    'Boolean' { $reader.ReadByte() }

                    'String' {
                        $characters = [Collections.Generic.List[byte]]::new()
                        while (($character = $reader.ReadByte()) -ne 0) { $characters.Add($character) }
                        [Text.UTF8Encoding]::new($false, $true).GetString($characters.ToArray())
                    }
                    default { throw "Unknown wire type: $type" }
                }
            }
            if ($stream.Position -eq $stream.Length) {
                $candidates.Add((Convert-SchedulerFields $schema.Name $fields))
            }
        } catch {
            # A different event layout may not fit; exactly one must fit completely.
        } finally { $reader.Dispose(); $stream.Dispose() }
    }
    if ($candidates.Count -ne 1) { throw "Unknown, ambiguous, or incomplete schema-23 payload ($($bytes.Length) bytes)" }
    return $candidates[0]
}

function Convert-SchedulerXml([xml]$Xml) {
    $payload = $Xml.SelectSingleNode("//*[local-name()='EventPayload']")
    if ($null -ne $payload) { $row = Convert-SchedulerPayload $payload.InnerText.Trim() }
    else {
        $task = $Xml.SelectSingleNode("//*[local-name()='RenderingInfo']/*[local-name()='Task']")
        $fields = [ordered]@{}
        foreach ($field in $Xml.SelectNodes("//*[local-name()='Data'][@Name]")) {
            if ($fields.Contains($field.GetAttribute('Name'))) { throw 'Duplicate event field' }
            $fields[$field.GetAttribute('Name')] = $field.InnerText.Trim()
        }
        if ($null -ne $task -and $null -ne $script:SchedulerSchema.PSObject.Properties[$task.InnerText]) {
            $row = Convert-SchedulerFields $task.InnerText $fields
        } else {
            # EventLogReader sometimes omits rendered TraceLogging task names.
            # The complete field set must still identify exactly one layout.
            $candidates = [Collections.Generic.List[object]]::new()
            foreach ($schema in $script:SchedulerSchema.PSObject.Properties) {
                $copy = [ordered]@{}
                foreach ($key in $fields.Keys) { $copy[$key] = $fields[$key] }
                try { $candidates.Add((Convert-SchedulerFields $schema.Name $copy)) } catch {}
            }
            if ($candidates.Count -ne 1) { throw 'Unknown or ambiguous named scheduler event' }
            $row = $candidates[0]
        }
    }
    $execution = $Xml.SelectSingleNode("//*[local-name()='System']/*[local-name()='Execution']")
    if ($null -eq $execution) { throw 'Missing ETW process ID' }
    $row | Add-Member -NotePropertyName ProcessId -NotePropertyValue ([uint32]$execution.GetAttribute('ProcessID'))
    return $row
}

function Assert-SchedulerRows([object[]]$Rows) {
    if ($Rows.Count -eq 0) { throw 'No scheduler events found' }
    $metadata = @{}; $sequences = @{}; $times = @{}
    foreach ($row in $Rows) {
        if ($row.SchemaVersion -ne 23 -or $row.ProcessCreationId -eq 0) { throw 'Invalid schema or process identity' }
        $identity = "$($row.ProcessId)/$($row.ProcessCreationId)"
        if ($row.Event -eq 'Metadata') {
            if ($row.SchemaMarker -ne 'MWPERFFIX_ETW_SCHEMA_23' -or $row.QpcFrequency -eq 0 -or $row.TargetFPS -lt 30 -or $row.TargetFPS -gt 1000) { throw 'Invalid scheduler metadata' }
            if ($metadata.ContainsKey($identity)) {
                $old = $metadata[$identity]
                if ($old.QpcFrequency -ne $row.QpcFrequency -or $old.TargetFPS -ne $row.TargetFPS) { throw 'Metadata changed within process' }
            }
            $metadata[$identity] = $row
            continue
        }
        $isFrame = $row.Event -eq 'AdaptiveReflectionFrame'
        $sequence = if ($isFrame) { $row.FrameSequence } else { $row.Sequence }
        $key = "$identity/$($row.Event)"
        if ($sequence -eq 0) { throw 'Zero scheduler sequence' }
        if ($sequences.ContainsKey($key)) {
            if ($sequence -le $sequences[$key]) { throw 'Non-increasing scheduler sequence' }
            if ($isFrame -and $sequence -ne $sequences[$key] + 1) { throw 'Missing scheduler frame event' }
        }
        $sequences[$key] = $sequence
        if (-not $isFrame) { continue }
        if ($times.ContainsKey($identity) -and $row.CompletionQpc -lt $times[$identity]) { throw 'Reversed frame time' }
        $times[$identity] = $row.CompletionQpc
        if ($row.SelectionValid -notin @(0,1) -or $row.TimingValid -notin @(0,1) -or
            $row.Eligible -notin @(0,1) -or $row.CostsValid -notin @(0,1) -or
            $row.CadenceHealthy -notin @(0,1) -or $row.BlendTimingValid -notin @(0,1)) {
            throw 'Invalid frame validity flags'
        }
        if ($row.TimingValid -and (-not $row.Eligible -or -not $row.PresentSucceeded -or
            $row.SelectedBudget -notin @(1,6) -or $row.CompletedFaceMask -ne $row.FaceMask)) {
            throw 'Invalid completed frame mask or eligibility'
        }
        if ($row.TimingValid) {
            [decimal]$sum = 0
            [decimal]$estimate = $row.ResidualEmaQpcTicks
            for ($face = 0; $face -lt 6; $face++) {
                $raw = $row."Face${face}QpcTicks"
                if (($row.CompletedFaceMask -band (1 -shl $face)) -eq 0 -and $raw -ne 0) { throw 'Unrendered face has a duration' }
                $sum += $raw
                $estimate += $row."Face${face}EmaQpcTicks"
            }
            if ($sum -ne $row.ObservedFaceQpcTicks) { throw 'Face-duration sum mismatch' }
            if ($row.CostsValid -and $row.ResidualEmaQpcTicks -ne 0 -and [Math]::Min($estimate, [decimal][UInt64]::MaxValue) -ne $row.PredictedFullQpcTicks) { throw 'Estimate-component mismatch' }
        } else {
            if ($row.CostsValid -or $row.ResidualEmaQpcTicks -ne 0 -or $row.FrameIntervalEmaQpcTicks -ne 0) { throw 'Invalid frame contains valid cost evidence' }
            for ($face = 0; $face -lt 6; $face++) {
                if ($row."Face${face}QpcTicks" -ne 0 -or $row."Face${face}EmaQpcTicks" -ne 0) { throw 'Invalid frame contains face costs' }
            }
        }
    }
    foreach ($row in $Rows) {
        if (-not $metadata.ContainsKey("$($row.ProcessId)/$($row.ProcessCreationId)")) { throw 'Missing process metadata' }
    }
}

function Get-Distribution([double[]]$Values) {
    if ($Values.Count -eq 0) { return [ordered]@{ Count = 0 } }
    [Array]::Sort($Values)
    return [ordered]@{
        Count = $Values.Count
        Mean = ($Values | Measure-Object -Average).Average
        P50 = $Values[[int][Math]::Floor(($Values.Count - 1) * 0.50)]
        P95 = $Values[[int][Math]::Floor(($Values.Count - 1) * 0.95)]
        P99 = $Values[[int][Math]::Floor(($Values.Count - 1) * 0.99)]
        Max = $Values[-1]
    }
}

function Get-SchedulerAnalysis([object[]]$Rows, [object[]]$PresentRows) {
    $metadata = @($Rows | Where-Object Event -eq 'Metadata')[0]
    $frames = @($Rows | Where-Object Event -eq 'AdaptiveReflectionFrame')
    $changes = @($Rows | Where-Object Event -eq 'AdaptiveReflectionStateChange')
    if ($frames.Count -eq 0) { throw 'No frame observations in process' }
    $frequency = [double]$metadata.QpcFrequency
    $ticksPerMs = $frequency / 1000.0
    $start = $frames[0].CompletionQpc
    # Invalid frames form hard barriers for local comparisons.
    $segment = 0; $previousFrame = $null
    $resetTimes = @($changes | Where-Object { $_.Reason -in @(5,6,7,9) } |
                    Sort-Object DecisionQpc | ForEach-Object DecisionQpc)
    $resetIndex = 0
    foreach ($frame in $frames) {
        $block = -not $frame.TimingValid -or -not $frame.Eligible
        $barrier = $false
        if ($null -ne $previousFrame) {
            # Gaps are recording segments, not assumed race boundaries.
            if (([double]$frame.CompletionQpc - $previousFrame.CompletionQpc) -gt $frequency)
                { $barrier = $true }
            while ($resetIndex -lt $resetTimes.Count -and
                   [uint64]$resetTimes[$resetIndex] -lt $frame.CompletionQpc) {
                if ([uint64]$resetTimes[$resetIndex] -ge $previousFrame.CompletionQpc)
                    { $barrier = $true }
                ++$resetIndex
            }
        }
        if ($barrier -or $block) { ++$segment }
        $frame | Add-Member -NotePropertyName Segment -NotePropertyValue $segment -Force
        $previousFrame = $frame
    }
    $valid = @($frames | Where-Object { $_.TimingValid -and $_.Eligible })
    $faces = @()
    foreach ($face in 0..5) {
        $entry = [ordered]@{ Face = $face }
        foreach ($budget in @(1, 6)) {
            $samples = @($valid | Where-Object { $_.SelectedBudget -eq $budget -and ($_.CompletedFaceMask -band (1 -shl $face)) })
            $entry["Budget$budget"] = Get-Distribution @($samples | ForEach-Object { $_."Face${face}QpcTicks" / $ticksPerMs })
        }
        $faces += $entry
    }
    $transitions = [Collections.Generic.List[object]]::new()
    foreach ($change in $changes) {
        $time = $change.DecisionQpc
        $prediction = $null
        if ($change.Reason -eq 4 -and $change.PreviousBudget -eq 1 -and $change.NewBudget -eq 6) {
            # Only compare the accepted weighted decision frame with the first
            # immediately subsequent full frame. Never cross a lifecycle barrier.
            $decisions = @($valid | Where-Object {
                $_.DecisionSequence -eq $change.Sequence -and $_.CompletionQpc -eq $time -and
                $_.SelectedBudget -eq 1 -and $_.CostsValid
            })
            if ($decisions.Count -eq 1) {
                $decision = $decisions[0]
                $next = @($frames | Where-Object {
                    $_.FrameSequence -eq $decision.FrameSequence + 1 -and
                    $_.Segment -eq $decision.Segment -and
                    $_.TimingValid -and $_.Eligible -and $_.PresentSucceeded -and
                    $_.SelectedBudget -eq 6
                })
                if ($next.Count -eq 1 -and $decision.PredictedFullQpcTicks -ne [UInt64]::MaxValue) {
                    $full = $next[0]
                    $errorMs = ([double]$decision.PredictedFullQpcTicks - $full.RendererWorkQpcTicks) / $ticksPerMs
                    $prediction = [ordered]@{
                        WeightedFrame = $decision.FrameSequence; FirstFullFrame = $full.FrameSequence
                        PredictedMs = $decision.PredictedFullQpcTicks / $ticksPerMs
                        ActualMs = $full.RendererWorkQpcTicks / $ticksPerMs
                        ErrorMs = $errorMs
                    }
                }
            }
        }
        $transitions.Add([ordered]@{
            Sequence = $change.Sequence; AtSeconds = ([double]$time - $start) / $frequency
            Reason = $change.Reason; PreviousBudget = $change.PreviousBudget
            NewBudget = $change.NewBudget; RecoveryPrediction = $prediction
        })
    }
    # Consecutive completed frames identify residence. Missing/corrupt samples,
    # reset barriers and capture boundaries are censored; never fill the gaps.
    $episodes = [Collections.Generic.List[object]]::new()
    $first = $null
    $prior = $null
    $count = 0
    $leftCensored = $true
    foreach ($frame in $frames) {
        $eligible = $frame.TimingValid -and $frame.Eligible -and $frame.PresentSucceeded -and
                    $frame.SelectedBudget -in @(1,6)
        $changed = $null -ne $prior -and ($frame.Segment -ne $prior.Segment -or
                   $frame.SelectedBudget -ne $prior.SelectedBudget -or -not $eligible)
        if ($null -ne $first -and $changed) {
            $episodes.Add([ordered]@{
                Budget = $first.SelectedBudget; Frames = $count
                DurationSeconds = ([double]$prior.CompletionQpc - $first.CompletionQpc) / $frequency
                LeftCensored = $leftCensored
                RightCensored = ($frame.Segment -ne $prior.Segment -or -not $eligible)
            })
            $first = $null; $count = 0
        }
        if ($eligible) {
            if ($null -eq $first) {
                $first = $frame; $count = 0
                $leftCensored = ($null -eq $prior -or $frame.Segment -ne $prior.Segment -or
                                 -not $prior.TimingValid -or -not $prior.Eligible -or
                                 -not $prior.PresentSucceeded)
            }
            ++$count
        }
        $prior = $frame
    }
    if ($null -ne $first) {
        $episodes.Add([ordered]@{
            Budget = $first.SelectedBudget; Frames = $count
            DurationSeconds = ([double]$frames[-1].CompletionQpc - $first.CompletionQpc) / $frequency
            LeftCensored = $leftCensored; RightCensored = $true
        })
    }
    $blockers = @{}; $blending = @()
    foreach ($frame in @($valid | Where-Object SelectedBudget -eq 1)) {
        $causes = @()
        if (-not $frame.CadenceHealthy) { $causes += 'cadence' }
        if ($frame.RendererWorkQpcTicks -gt $frame.CadenceBudgetQpcTicks) { $causes += 'renderer' }
        if (-not $frame.CostsValid) { $causes += 'costs-invalid' }
        $recoveryLimit = [decimal]::Floor([decimal]$frame.CadenceBudgetQpcTicks / 20) * 19
        if ($frame.PredictedFullQpcTicks -eq [UInt64]::MaxValue -or $frame.PredictedFullQpcTicks -gt $recoveryLimit) { $causes += 'estimate' }
        if ($frame.CompletionQpc -lt $frame.RetryAfterQpc) { $causes += 'cooldown' }
        $key = if ($causes.Count) { $causes -join '+' } else { 'none-streak-or-transition' }
        if (-not $blockers.ContainsKey($key)) { $blockers[$key] = 0 }
        $blockers[$key]++
    }
    foreach ($group in @($valid | Group-Object SelectedBudget, BlendMode, BlendSteps, BlendActive, BlendFailed)) {
        $sample = $group.Group[0]
        $entry = [ordered]@{ Budget = $sample.SelectedBudget; BlendMode = $sample.BlendMode; BlendSteps = $sample.BlendSteps; BlendActive = $sample.BlendActive; BlendFailed = $sample.BlendFailed; Frames = $group.Count; ValidBlendTimingFrames = 0 }
        $blendValid = @($group.Group | Where-Object BlendTimingValid -eq 1)
        $entry.ValidBlendTimingFrames = $blendValid.Count
        foreach ($field in @('BlendBeginFrameQpcTicks','BlendSelectScheduleQpcTicks','BlendBeginFaceQpcTicks','BlendEndFaceQpcTicks','ResidualWorkQpcTicks')) {
            $entry[$field + 'Ms'] = Get-Distribution @($blendValid | ForEach-Object { $_.$field / $ticksPerMs })
        }
        $blending += $entry
    }
    # Match QPC to the nearest PresentMon call, within 1ms, for this process only.
    $presents = @($PresentRows | Where-Object { [uint32]$_.ProcessID -eq $metadata.ProcessId } | Sort-Object { [UInt64]$_.QPCTime })
    $effectiveTargets = @($valid | ForEach-Object { [uint64]$_.CadenceBudgetQpcTicks } | Sort-Object -Unique)
    $mixedTargets = $effectiveTargets.Count -gt 1
    $matched = [Collections.Generic.List[object]]::new(); $cursor = 0
    $used = [Collections.Generic.HashSet[int]]::new()
    foreach ($frame in $valid) {
        if ($presents.Count -eq 0) { break }
        $qpc = [double]$frame.PrePresentQpc
        while ($cursor + 1 -lt $presents.Count -and [double]$presents[$cursor + 1].QPCTime -le $qpc) { $cursor++ }
        $index = $cursor
        if ($cursor + 1 -lt $presents.Count -and [Math]::Abs([double]$presents[$cursor + 1].QPCTime - $qpc) -lt [Math]::Abs([double]$presents[$cursor].QPCTime - $qpc)) { $index++ }
        $errorMs = [Math]::Abs([double]$presents[$index].QPCTime - $qpc) / $ticksPerMs
        if ($errorMs -ge 1 -or -not $used.Add([int]$index)) { continue }
        $pm = $presents[$index]
        $matched.Add([ordered]@{ Budget = $frame.SelectedBudget; FrameSequence = $frame.FrameSequence; PresentIndex = $index; TargetMs = ([double]$frame.CadenceBudgetQpcTicks / $ticksPerMs); ErrorMs = $errorMs; IntervalMs = [double]::Parse($pm.msBetweenPresents, [Globalization.CultureInfo]::InvariantCulture); GpuActiveMs = [double]::Parse($pm.msGPUActive, [Globalization.CultureInfo]::InvariantCulture); PresentMs = [double]::Parse($pm.msInPresentAPI, [Globalization.CultureInfo]::InvariantCulture) })
    }
    $presentSummary = @()
    foreach ($budget in @(0,1,6)) {
        $subset = @($matched | Where-Object { $budget -eq 0 -or $_.Budget -eq $budget })
        $presentSummary += [ordered]@{ Budget = $budget; Matched = $subset.Count; OverTarget = @($subset | Where-Object { $_.IntervalMs -gt $_.TargetMs }).Count; IntervalMs = (Get-Distribution @($subset | ForEach-Object IntervalMs)); GpuActiveMs = (Get-Distribution @($subset | ForEach-Object GpuActiveMs)); PresentMs = (Get-Distribution @($subset | ForEach-Object PresentMs)); MatchErrorMs = (Get-Distribution @($subset | ForEach-Object ErrorMs)) }
    }
    $coverage = @()
    foreach ($group in @($frames | Group-Object Segment, SelectedBudget)) {
        $sample = $group.Group[0]
        $coverage += [ordered]@{
            Segment = $sample.Segment; Budget = $sample.SelectedBudget
            Frames = $group.Count
            ControllerValid = @($group.Group | Where-Object { $_.Eligible -and $_.TimingValid }).Count
        }
    }
    $ageRows = [Collections.Generic.List[object]]::new()
    $lastTick = @($null,$null,$null,$null,$null,$null)
    $lastEligible = @(0,0,0,0,0,0)
    $previousSegment = -1
    $eligibleOrdinal = 0
    foreach ($frame in $frames) {
        if ($frame.Segment -ne $previousSegment) {
            $lastTick = @($null,$null,$null,$null,$null,$null)
            $lastEligible = @(0,0,0,0,0,0)
            $eligibleOrdinal = 0
            $previousSegment = $frame.Segment
        }
        if (-not $frame.Eligible -or -not $frame.TimingValid) { continue }
        ++$eligibleOrdinal
        for ($faceIndex = 0; $faceIndex -lt 6; ++$faceIndex) {
            $updated = ($frame.CompletedFaceMask -band (1 -shl $faceIndex)) -ne 0
            $refreshMs = $null; $refreshEligible = $null
            if ($updated) {
                if ($null -ne $lastTick[$faceIndex]) {
                    $refreshMs = ([double]$frame.CompletionQpc - $lastTick[$faceIndex]) / $ticksPerMs
                    $refreshEligible = $eligibleOrdinal - $lastEligible[$faceIndex]
                }
                $lastTick[$faceIndex] = $frame.CompletionQpc
                $lastEligible[$faceIndex] = $eligibleOrdinal
            }
            $ms = $null; $eligibleAge = $null
            if ($null -ne $lastTick[$faceIndex]) {
                $ms = ([double]$frame.CompletionQpc - $lastTick[$faceIndex]) / $ticksPerMs
                $eligibleAge = $eligibleOrdinal - $lastEligible[$faceIndex]
            }
            $ageRows.Add([ordered]@{ Segment = $frame.Segment; Budget = $frame.SelectedBudget; Face = $faceIndex; FrameSequence = $frame.FrameSequence; Updated = $updated; AgeMs = $ms; EligibleUpdates = $eligibleAge; RefreshIntervalMs = $refreshMs; RefreshIntervalEligibleUpdates = $refreshEligible; UnknownInitialAge = ($null -eq $ms) })
        }
    }
    $ageSummary = @()
    # Entries are ordered dictionaries; named-property grouping misses keys.
    # Use an explicit composite key to preserve each budget/physical face.
    foreach ($ageGroup in @($ageRows | Group-Object { "$($_.Budget)/$($_.Face)" })) {
        $sample = $ageGroup.Group[0]
        $known = @($ageGroup.Group | Where-Object { $null -ne $_.AgeMs })
        $intervals = @($ageGroup.Group | Where-Object { $null -ne $_.RefreshIntervalMs })
        $ageSummary += [ordered]@{
            Budget = $sample.Budget; Face = $sample.Face; Observations = $ageGroup.Count
            UnknownInitialAges = $ageGroup.Count - $known.Count
            StalenessMs = Get-Distribution @($known | ForEach-Object AgeMs)
            StalenessEligibleUpdates = Get-Distribution @($known | ForEach-Object EligibleUpdates)
            RefreshIntervalMs = Get-Distribution @($intervals | ForEach-Object RefreshIntervalMs)
            RefreshIntervalEligibleUpdates = Get-Distribution @($intervals | ForEach-Object RefreshIntervalEligibleUpdates)
        }
    }
    return [ordered]@{ Metadata = $metadata; MixedTargets = $mixedTargets; EffectiveTargetPeriodsQpc = $effectiveTargets; Frames = $frames.Count; ValidFrames = $valid.Count; FaceDistributionsMs = $faces; Transitions = $transitions.ToArray(); Episodes = $episodes.ToArray(); WeightedEpisodes = @($episodes | Where-Object { $_.Budget -eq 1 }); FullEpisodes = @($episodes | Where-Object { $_.Budget -eq 6 }); RecoveryBlockers = $blockers; Blending = $blending; PresentMon = $presentSummary; MatchedFrames = $matched.Count; UnmatchedFrames = $valid.Count - $matched.Count; Coverage = $coverage; EstimateAges = $ageSummary; Downshifts = @($changes | Where-Object Reason -eq 3).Count; Recoveries = @($changes | Where-Object Reason -eq 4).Count; Resets = @($changes | Where-Object { $_.Reason -in @(5,6,7,9) }).Count; RecoveryPredictionErrorsMs = (Get-Distribution @($transitions | Where-Object { $null -ne $_.RecoveryPrediction } | ForEach-Object { $_.RecoveryPrediction.ErrorMs })); ResidualWorkMs = (Get-Distribution @($valid | ForEach-Object { $_.ResidualWorkQpcTicks / $ticksPerMs })) }
}