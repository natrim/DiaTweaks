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
  <StackPanel Spacing="8">
   <TextBlock Text="Toolbar" FontWeight="SemiBold" Margin="0,0,0,4"/>
   <Border CornerRadius="8" Padding="16" BorderThickness="1"
    Background="{ThemeResource CardBackgroundFillColorDefaultBrush}" BorderBrush="{ThemeResource CardStrokeColorDefaultBrush}">
    <Grid ColumnSpacing="20">
     <Grid.ColumnDefinitions><ColumnDefinition Width="*"/><ColumnDefinition Width="Auto"/></Grid.ColumnDefinitions>
     <StackPanel VerticalAlignment="Center" Spacing="4">
      <TextBlock Text="Hide Chat button" FontWeight="SemiBold"/>
      <TextBlock Text="Remove the Chat button from the toolbar." TextWrapping="Wrap" Opacity="0.7"/>
      <TextBlock x:Name="ChatUnavailable" Text="Not available in the current browser window." TextWrapping="Wrap" Opacity="0.7" Visibility="Collapsed"/>
     </StackPanel>
     <ToggleSwitch x:Name="HideChat" Grid.Column="1" VerticalAlignment="Center"
      AutomationProperties.AutomationId="DiaTweaks.HideChat" AutomationProperties.Name="Hide Chat button"/>
    </Grid>
   </Border>
   <Border CornerRadius="8" Padding="16" BorderThickness="1"
    Background="{ThemeResource CardBackgroundFillColorDefaultBrush}" BorderBrush="{ThemeResource CardStrokeColorDefaultBrush}">
    <Grid ColumnSpacing="20">
     <Grid.ColumnDefinitions><ColumnDefinition Width="*"/><ColumnDefinition Width="Auto"/></Grid.ColumnDefinitions>
     <StackPanel VerticalAlignment="Center" Spacing="4">
      <TextBlock Text="Hide sidebar button" FontWeight="SemiBold"/>
      <TextBlock Text="Remove the button that opens and closes the sidebar." TextWrapping="Wrap" Opacity="0.7"/>
      <TextBlock x:Name="SidebarUnavailable" Text="Not available in the current browser window." TextWrapping="Wrap" Opacity="0.7" Visibility="Collapsed"/>
     </StackPanel>
     <ToggleSwitch x:Name="HideSidebar" Grid.Column="1" VerticalAlignment="Center"
      AutomationProperties.AutomationId="DiaTweaks.HideSidebar" AutomationProperties.Name="Hide sidebar button"/>
    </Grid>
   </Border>
  </StackPanel>
  <StackPanel Spacing="10">
   <Button x:Name="RestoreButtons" Content="Restore buttons" HorizontalAlignment="Left"
    AutomationProperties.AutomationId="DiaTweaks.Restore"/>
   <TextBlock x:Name="Status" Text="Changes apply immediately." TextWrapping="Wrap" Opacity="0.7"
    AutomationProperties.LiveSetting="Polite" AutomationProperties.AutomationId="DiaTweaks.Status"/>
  </StackPanel>
  <TextBlock Text="DiaTweaks is unofficial and is not affiliated with The Browser Company."
   FontSize="12" Opacity="0.6" TextWrapping="Wrap" Margin="0,12,0,0"/>
 </StackPanel>
</ScrollViewer>)XAML";

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
