$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
foreach ($file in Get-ChildItem $root -Filter *.ps1 -Recurse) {
    $tokens = $null
    $parseErrors = $null
    [void][System.Management.Automation.Language.Parser]::ParseFile($file.FullName, [ref]$tokens, [ref]$parseErrors)
    if ($parseErrors.Count) { throw ($parseErrors | Out-String) }
}
$output = Join-Path $env:RUNNER_TEMP 'dia-inspection.json'
$rejected = $false
try { & (Join-Path $root 'Inspect-Dia.ps1') -OutputPath $output }
catch {
    if ($_.Exception.Message -notlike 'No Dia window found.*') { throw }
    $rejected = $true
}
if (-not $rejected) { throw 'Expected an explicit error when Dia is not running.' }
if (Test-Path $output) { throw 'A failed inspection must not create a report.' }
