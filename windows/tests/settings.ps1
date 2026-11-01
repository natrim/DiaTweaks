$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$source = [IO.File]::ReadAllText((Join-Path $root 'native/SettingsPage.h'))
$fragments = [regex]::Matches($source, '(?s)LR"XAML\((.*?)\)XAML"')
if ($fragments.Count -ne 3) { throw 'Expected a page, a row and a sidebar-entry XAML fragment.' }
$names = @{}
foreach ($fragment in $fragments) {
    [xml]$xml = $fragment.Groups[1].Value
    $ns = [Xml.XmlNamespaceManager]::new($xml.NameTable)
    $ns.AddNamespace('x', 'http://schemas.microsoft.com/winfx/2006/xaml')
    foreach ($node in $xml.SelectNodes('//*[@x:Name]', $ns)) {
        $name = $node.GetAttribute('Name', 'http://schemas.microsoft.com/winfx/2006/xaml')
        if ($names.ContainsKey($name)) { throw "Duplicate XAML name: $name" }
        $names[$name] = $node
    }
}
foreach ($name in @('TweakRows', 'Switch', 'Title', 'Description', 'Unavailable', 'RestoreButtons', 'Status', 'DiaTweaksEntry')) {
    if (-not $names.ContainsKey($name)) { throw "Missing settings control: $name" }
}
if ($names['Switch'].LocalName -ne 'ToggleSwitch') { throw 'Expected a ToggleSwitch row.' }
$implementation = [IO.File]::ReadAllText((Join-Path $root 'native/Settings.cpp'))
if (-not $implementation.Contains('AutomationProperties::SetName(toggle, tweak.title)') -or
    -not $implementation.Contains('for (auto const& tweak : dt::Tweaks)')) { throw 'Catalog rows must receive accessible names.' }
Write-Host 'Settings XAML parses and all wired controls have unique names.'
