#Requires -Version 5.1
<#
.SYNOPSIS
Collect a bounded UI Automation snapshot for development of the Windows port.
.DESCRIPTION
Read-only: does not patch, inject into, restart or change Dia. Does not read
UI Automation Name or Value properties. Document and Chromium content subtrees
are excluded. Review the JSON before sharing; AutomationIds come from Dia.
#>
[CmdletBinding()]
param(
    [string]$OutputPath = (Join-Path (Get-Location) ('dia-inspection-{0}.json' -f (Get-Date -Format 'yyyyMMdd-HHmmss'))),
    [ValidateRange(1, 30)][int]$MaxDepth = 14,
    [ValidateRange(1, 10000)][int]$MaxNodes = 2000
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT) {
    throw 'This diagnostic requires Windows and a running Dia.'
}
if (Test-Path -LiteralPath $OutputPath) {
    throw "Output already exists: $OutputPath. Choose a new -OutputPath."
}

Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
$processes = @(Get-Process -Name Dia -ErrorAction SilentlyContinue |
    Where-Object { $_.MainWindowHandle -ne [IntPtr]::Zero })
if ($processes.Count -eq 0) {
    throw 'No Dia window found. Start Dia, open a normal browser window, and run this script again.'
}

$state = @{
    nodes = [Collections.Generic.List[object]]::new()
    unreadable = 0
    excluded = 0
    truncated = $false
}
$walker = [System.Windows.Automation.TreeWalker]::RawViewWalker

function Read-ChromeNode {
    param($Element, [int]$Parent, [int]$Depth, [int]$ProcessId)
    if ($state.nodes.Count -ge $MaxNodes) { $state.truncated = $true; return }
    try {
        $current = $Element.Current
        $type = $current.ControlType.ProgrammaticName
        $class = $current.ClassName
        # Never descend into a web document or Chromium's rendering host.
        # Also reject elements belonging to another process.
        if ($current.ProcessId -ne $ProcessId -or
            $type -eq 'ControlType.Document' -or
            $class -like 'Chrome_RenderWidgetHostHWND*') {
            $state.excluded++
            return
        }
        $bounds = $current.BoundingRectangle
        $index = $state.nodes.Count
        $state.nodes.Add([ordered]@{
            index = $index
            parent = $Parent
            depth = $Depth
            controlType = $type
            className = $class
            automationId = $current.AutomationId
            offscreen = $current.IsOffscreen
            width = $(if ([double]::IsInfinity($bounds.Width)) { $null } else { $bounds.Width })
            height = $(if ([double]::IsInfinity($bounds.Height)) { $null } else { $bounds.Height })
        })
        $child = $walker.GetFirstChild($Element)
        if ($Depth -ge $MaxDepth) {
            if ($null -ne $child) { $state.truncated = $true }
            return
        }
        while ($null -ne $child) {
            if ($state.nodes.Count -ge $MaxNodes) { $state.truncated = $true; break }
            Read-ChromeNode $child $index ($Depth + 1) $ProcessId
            $child = $walker.GetNextSibling($child)
        }
    } catch {
        # Provider errors may include page text. Count them without exporting it.
        $state.unreadable++
    }
}

$windows = [Collections.Generic.List[object]]::new()
foreach ($process in $processes) {
    $first = $state.nodes.Count
    $root = [System.Windows.Automation.AutomationElement]::FromHandle($process.MainWindowHandle)
    Read-ChromeNode $root -1 0 $process.Id
    $fileVersion = $null
    $modules = @()
    try {
        $fileVersion = $process.MainModule.FileVersionInfo.FileVersion
        $modules = @($process.Modules | ForEach-Object { $_.ModuleName } | Sort-Object -Unique)
    } catch { $state.unreadable++ }
    $windows.Add([ordered]@{
        processId = $process.Id
        firstNode = $first
        nodeCount = $state.nodes.Count - $first
        fileVersion = $fileVersion
        modules = $modules
    })
}
if ($state.nodes.Count -eq 0) {
    throw 'Dia did not expose a readable UI Automation tree. No report was written.'
}

$packages = @(Get-AppxPackage -Name TheBrowserCompany.Dia | ForEach-Object {
    [ordered]@{ version = [string]$_.Version; architecture = [string]$_.Architecture }
})
$report = [ordered]@{
    schemaVersion = 1
    capturedUtc = [DateTime]::UtcNow.ToString('o')
    windowsVersion = [Environment]::OSVersion.Version.ToString()
    powershellVersion = $PSVersionTable.PSVersion.ToString()
    packages = $packages
    windows = @($windows.ToArray())
    limits = @{ maxDepth = $MaxDepth; maxNodes = $MaxNodes }
    truncated = $state.truncated
    unreadable = $state.unreadable
    excluded = $state.excluded
    nodes = @($state.nodes.ToArray())
}
$report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $OutputPath -Encoding UTF8
Write-Host "Wrote $($state.nodes.Count) nodes to $OutputPath. Review the JSON before sharing."
if ($state.truncated) { Write-Warning 'The snapshot reached a depth or node limit.' }
if ($state.unreadable) { Write-Warning "$($state.unreadable) UI or process queries failed; the report may be incomplete." }
