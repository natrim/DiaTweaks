#pragma once

namespace dt {
inline constexpr wchar_t SettingsPageXaml[] = LR"XAML(
<ScrollViewer xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
 xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
 x:Name="DiaTweaksPage" VerticalScrollBarVisibility="Auto" HorizontalScrollBarVisibility="Disabled"
 Background="{ThemeResource ApplicationPageBackgroundThemeBrush}" Padding="32,24">
 <StackPanel MaxWidth="600" HorizontalAlignment="Stretch" Spacing="20">
  <StackPanel Spacing="6">
   <TextBlock Text="Patches" FontSize="28" FontWeight="SemiBold"/>
   <TextBlock Text="Small changes to Dia’s toolbar." TextWrapping="Wrap" Opacity="0.7"/>
  </StackPanel>
  <StackPanel x:Name="TweakRows" Spacing="8"/>
  <StackPanel Spacing="10">
   <Button x:Name="RestoreButtons" Content="Restore defaults" HorizontalAlignment="Left"
    AutomationProperties.AutomationId="DiaTweaks.Restore"/>
   <TextBlock x:Name="Status" Text="Changes apply immediately." TextWrapping="Wrap" Opacity="0.7"
    AutomationProperties.LiveSetting="Polite" AutomationProperties.AutomationId="DiaTweaks.Status"/>
  </StackPanel>
  <TextBlock Text="DiaTweaks is unofficial and is not affiliated with The Browser Company."
   FontSize="12" Opacity="0.6" TextWrapping="Wrap" Margin="0,12,0,0"/>
 </StackPanel>
</ScrollViewer>)XAML";

inline constexpr wchar_t SettingsRowXaml[] = LR"XAML(
<Border xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
 xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
 CornerRadius="8" Padding="16" BorderThickness="1"
 Background="{ThemeResource CardBackgroundFillColorDefaultBrush}" BorderBrush="{ThemeResource CardStrokeColorDefaultBrush}">
 <Grid ColumnSpacing="20">
  <Grid.ColumnDefinitions><ColumnDefinition Width="*"/><ColumnDefinition Width="Auto"/></Grid.ColumnDefinitions>
  <StackPanel VerticalAlignment="Center" Spacing="4">
   <TextBlock x:Name="Title" FontWeight="SemiBold"/>
   <TextBlock x:Name="Description" TextWrapping="Wrap" Opacity="0.7"/>
   <TextBlock x:Name="Unavailable" TextWrapping="Wrap" Opacity="0.7" Visibility="Collapsed"/>
  </StackPanel>
  <ToggleSwitch x:Name="Switch" Grid.Column="1" VerticalAlignment="Center"/>
 </Grid>
</Border>)XAML";

inline constexpr wchar_t SettingsEntryXaml[] = LR"XAML(
<ToggleButton xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
 xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
 x:Name="DiaTweaksEntry" HorizontalAlignment="Stretch" HorizontalContentAlignment="Left"
 BorderThickness="0" Background="Transparent" Padding="12,10" Margin="8,4"
 AutomationProperties.AutomationId="DiaTweaks.Patches" AutomationProperties.Name="Patches">
 <StackPanel Orientation="Horizontal" Spacing="12">
  <FontIcon Glyph="&#xE90F;" FontSize="16"/>
  <TextBlock Text="Patches"/>
 </StackPanel>
</ToggleButton>)XAML";
}
