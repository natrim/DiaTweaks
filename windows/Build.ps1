#Requires -Version 5.1
[CmdletBinding()]
param()
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT) { throw 'Build on Windows.' }
if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    throw 'Run from an x64 Native Tools command prompt for Visual Studio with the Windows SDK installed.'
}
if ($env:VSCMD_ARG_TGT_ARCH -ne 'x64') { throw 'Use the x64 Native Tools environment.' }
$output = Join-Path $PSScriptRoot 'build'
New-Item -ItemType Directory -Force -Path $output | Out-Null
Push-Location $output
try {
    $common = @('/nologo', '/std:c++20', '/EHsc', '/W4', '/WX', '/O2', '/MT', '/utf-8', '/bigobj', '/DUNICODE', '/D_UNICODE', '/D_WIN32_WINNT=0x0A00')
    & cl.exe @common (Join-Path $PSScriptRoot 'tests/rules.cpp') /Fe:rules.exe
    if ($LASTEXITCODE) { throw 'Policy test compilation failed.' }
    & .\rules.exe
    if ($LASTEXITCODE) { throw 'Policy tests failed.' }
    & cl.exe @common (Join-Path $PSScriptRoot 'tests/tap.cpp') /Fe:tap-tests.exe /link ole32.lib oleaut32.lib user32.lib advapi32.lib
    if ($LASTEXITCODE) { throw 'Mock TAP test compilation failed.' }
    & .\tap-tests.exe
    if ($LASTEXITCODE) { throw 'Mock TAP tests failed.' }
    $headers = & (Join-Path $PSScriptRoot 'Prepare-WinUI.ps1') -OutputDirectory $output
    # The preparation script writes tool output too; its last output is the path.
    $headers = @($headers)[-1]
    $exports = '/DEF:' + (Join-Path $PSScriptRoot 'native/Tap.def')
    & cl.exe @common /D DT_SETTINGS_UI "/I$headers" /LD (Join-Path $PSScriptRoot 'native/Tap.cpp') (Join-Path $PSScriptRoot 'native/Settings.cpp') /Fe:DiaTweaks.Tap.dll /link $exports ole32.lib oleaut32.lib user32.lib advapi32.lib windowsapp.lib
    if ($LASTEXITCODE) { throw 'TAP DLL compilation failed.' }
    & cl.exe @common (Join-Path $PSScriptRoot 'native/Attach.cpp') /Fe:DiaTweaks.exe /link ole32.lib oleaut32.lib user32.lib psapi.lib advapi32.lib
    if ($LASTEXITCODE) { throw 'Controller compilation failed.' }
} finally { Pop-Location }
Write-Host "Built DiaTweaks.exe and DiaTweaks.Tap.dll in $output"
