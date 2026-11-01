$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$source = [IO.File]::ReadAllText((Join-Path $root 'native/SettingsPage.h'))
$fragments = [regex]::Matches($source, '(?s)LR"XAML\((.*?)\)XAML"')
if ($fragments.Count -ne 2) { throw 'Expected a page and a sidebar-entry XAML fragment.' }
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
foreach ($name in @('HideChat', 'HideSidebar')) {
    if (-not $names.ContainsKey($name) -or $names[$name].LocalName -ne 'ToggleSwitch') { throw "Missing toggle $name" }
    if (-not $names[$name].GetAttribute('AutomationProperties.Name')) { throw "Missing accessible name: $name" }
}
foreach ($name in @('RestoreButtons', 'Status', 'ChatUnavailable', 'SidebarUnavailable', 'DiaTweaksEntry')) {
    if (-not $names.ContainsKey($name)) { throw "Missing settings control: $name" }
}
Write-Host 'Settings XAML parses and all wired controls have unique names.'
