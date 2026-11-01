#Requires -Version 5.1
[CmdletBinding()]
param([Parameter(Mandatory = $true)][string]$OutputDirectory)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if (-not (Get-Command nuget.exe -ErrorAction SilentlyContinue)) {
    throw 'NuGet CLI is required to restore the pinned WinUI build metadata.'
}
$packages = Join-Path $OutputDirectory 'packages'
& nuget.exe install Microsoft.WindowsAppSDK.WinUI -Version 1.8.250906003 -OutputDirectory $packages -NonInteractive -DirectDownload
if ($LASTEXITCODE) { throw 'WinUI metadata restore failed.' }
$metadata = Join-Path $OutputDirectory 'metadata'
$headers = Join-Path $OutputDirectory 'generated'
New-Item -ItemType Directory -Force -Path $metadata, $headers | Out-Null
$seen = @{}
foreach ($file in Get-ChildItem $packages -Recurse -Filter *.winmd | Sort-Object FullName) {
    if (-not $seen.ContainsKey($file.Name)) {
        Copy-Item -LiteralPath $file.FullName -Destination (Join-Path $metadata $file.Name) -Force
        $seen[$file.Name] = $true
    }
}
if (-not $seen.ContainsKey('Microsoft.UI.Xaml.winmd')) { throw 'WinUI XAML metadata is missing.' }
$sdkVersion = $env:WindowsSDKVersion.TrimEnd('\')
$compiler = Join-Path $env:WindowsSdkDir "bin\$sdkVersion\x64\cppwinrt.exe"
$reference = Join-Path $env:WindowsSdkDir "UnionMetadata\$sdkVersion\Windows.winmd"
if (-not (Test-Path $compiler) -or -not (Test-Path $reference)) { throw 'Windows SDK C++/WinRT tools or metadata are missing.' }
& $compiler -input $metadata -reference $reference -output $headers
if ($LASTEXITCODE) { throw 'C++/WinRT header generation failed.' }
$interop = Get-ChildItem $packages -Recurse -Filter Microsoft.UI.Interop.h | Where-Object { $_.Directory.Name -eq 'winrt' } | Select-Object -First 1
if (-not $interop) { throw 'Windows App SDK window interop header is missing.' }
Copy-Item -LiteralPath $interop.FullName -Destination (Join-Path $headers 'winrt/Microsoft.UI.Interop.h') -Force
return $headers
