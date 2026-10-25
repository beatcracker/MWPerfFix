param([Parameter(Mandatory = $true)][string]$BuildDirectory)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Invoke-Checked([string]$Program, [string[]]$Arguments) {
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Program exited with code $LASTEXITCODE" }
}

$build = (Resolve-Path -LiteralPath $BuildDirectory).Path
$run = Join-Path $build ('etw-capture-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $run | Out-Null
$session = 'MWPerfFixSmoke-' + [Guid]::NewGuid().ToString('N')
$etl = Join-Path $run 'smoke.etl'
$xml = Join-Path $run 'smoke.xml'
$wprProfile = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\MWPerfFix.wprp'))
$emitter = Join-Path $build 'diagnostics\mwperffix_etw_emitter.exe'
$validator = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\tools\validate-etw.ps1'))
# WPR session control requires an elevated token. Fail with a clear message
# instead of a raw 0x80070005 from wpr.exe.
$principal = [Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'smoke-etw requires an elevated terminal. Run mise run smoke-etw from an administrator PowerShell.'
}
Invoke-Checked wpr.exe @('-start', ($wprProfile + '!Diagnostics'), '-filemode', '-instancename', $session)
try {
    Invoke-Checked $emitter @()
} finally {
    Invoke-Checked wpr.exe @('-stop', $etl, '-instancename', $session)
}
Invoke-Checked tracerpt.exe @($etl, '-o', $xml, '-of', 'XML', '-lr', '-y')
Invoke-Checked powershell.exe @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $validator,
    '-XmlPath', $xml, '-RequireSynthetic')

[xml]$decoded = Get-Content -LiteralPath $xml -Raw
$events = @($decoded.SelectNodes("//*[local-name()='Event'][*[local-name()='System']/*[local-name()='Provider'][@Name='MWPerfFix.Diagnostics']]"))
if ($events.Count -ne 3) { throw "Expected three provider events, found $($events.Count)" }
$wide = $decoded.SelectSingleNode("//*[local-name()='Data'][@Name='RendererWorkQpcTicks']")
if ($null -eq $wide -or $wide.InnerText.Trim() -ne '17179869188') { throw '64-bit timing did not round-trip' }
foreach ($field in @('Face0QpcTicks', 'Face0EmaQpcTicks', 'BlendBeginFrameQpcTicks')) {
    $node = $decoded.SelectSingleNode("//*[local-name()='Data'][@Name='$field']")
    $expected = if ($field -eq 'BlendBeginFrameQpcTicks') { '4294967298' } else { '4294967297' }
    if ($null -eq $node -or $node.InnerText.Trim() -ne $expected) { throw "$field did not round-trip" }
}
# Exercise the same filtered decoder used for real captures, independently of
# tracerpt's field rendering and the synthetic test fixture's wire encoder.
. (Join-Path $PSScriptRoot '..\tools\analysis-common.ps1')
$filteredRecords = Join-Path $run 'provider.raw'
$extracted = [MwPerfEtwReader]::Extract($etl, $filteredRecords)
$filtered = @(& {
    foreach ($line in [IO.File]::ReadLines($filteredRecords)) {
        $split = $line.IndexOf([char]9)
        if ($split -lt 1) { throw 'Malformed filtered ETW record' }
        $eventProcessId = [uint32]::Parse($line.Substring(0, $split))
        $payload = $line.Substring($split + 1)
        if ($payload.StartsWith('R')) {
            $row = Convert-SchedulerPayload $payload.Substring(1)
            $row | Add-Member -NotePropertyName ProcessId -NotePropertyValue $eventProcessId
        } elseif ($payload.StartsWith('N')) {
            $xmlText = [Text.Encoding]::UTF8.GetString([Convert]::FromBase64String($payload.Substring(1)))
            $row = Convert-SchedulerXml ([xml]$xmlText)
            if ($row.ProcessId -ne $eventProcessId) { throw 'Named smoke ETW process mismatch' }
        } else { throw 'Unsupported smoke record encoding' }
        $row
    }
})
if ($extracted -ne $filtered.Count) { throw 'Filtered smoke count mismatch' }
Assert-SchedulerRows $filtered
if ($filtered.Count -ne 3) { throw 'Filtered decoder did not recover all three events' }
$frame = @($filtered | Where-Object Event -eq 'AdaptiveReflectionFrame')[0]
if ($frame.Face0QpcTicks -ne 4294967297 -or $frame.BlendBeginFrameQpcTicks -ne 4294967298) { throw 'Filtered decoder lost wide timing fields' }
$namedFrame = @($events | Where-Object { $_.SelectSingleNode(".//*[local-name()='Data'][@Name='Face0QpcTicks']") })[0]
$named = Convert-SchedulerXml $namedFrame.OuterXml
if ($named.Face0QpcTicks -ne $frame.Face0QpcTicks) { throw 'Named timing decode failed' }
Write-Output "PASS: schema 23 round-tripped through ETW and filtered decoder: $etl"