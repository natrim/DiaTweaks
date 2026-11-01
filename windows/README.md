# DiaTweaks for Windows (experimental, x64)

This is an initial native Windows implementation, **not a validated port of all
DiaTweaks features**. It has been cross-compiled for Windows; runtime testing in
Dia is still required. The macOS installer and Rust CLI do not manage this backend.

## Implemented scope

| Windows switch | Upstream tweak | Selector |
| --- | --- | --- |
| `--hide-chat` | `hideChatButton` | `ChatButton` |
| `--hide-sidebar` | `hideSidebarButton` | `SidebarToggleButton` |
| `--restore` | Restore this session's button overrides | Saved original properties |

Selectors were observed in a complete UI Automation inspection of Dia
`1.5.0.588`, x64, with sidebar tabs. That inspection establishes the controls'
identifiers, not a successful patch test. XAML property enumeration, diagnostics
attachment, both tab layouts and page layout after collapsing a button still
need testing on Windows. No claim is made about layout reflow.

The toolbar, hover reveal, edge-to-edge content, corner shapes, recent tabs,
Chats library button, tab grouping, active-window styling, Settings integration
and automatic reapplication after restart/update are not implemented. Dia's
built-in updater is unchanged. WSL cannot run this backend.

## Build

Use Visual Studio Build Tools with **Desktop development with C++** and the
Windows SDK. In an **x64 Native Tools Command Prompt**, from the repository root:

```bat
powershell -NoProfile -File windows\Build.ps1
```

This compiles and runs selector-policy tests and a mock XAML service test, then
builds `windows/build/DiaTweaks.exe` and `windows/build/DiaTweaks.Tap.dll`.
The mock exercises UI-thread dispatch, matching, property readback, restoration,
binding preservation and rollback. It does not simulate the actual WinUI loader.
The Windows workflow runs these checks and uploads those two output files.

## Run

Keep the EXE and DLL in the same folder. Open Dia, then run without elevation:

```powershell
.\DiaTweaks.exe --hide-chat --hide-sidebar
```

Each command replaces the active selection. To hide only Chat:

```powershell
.\DiaTweaks.exe --hide-chat
```

To restore the previous visibility values:

```powershell
.\DiaTweaks.exe --restore
```

The controller checks each requested button's effective Visibility after setting
it. A missing selector, unsupported property source, binding, failed readback or
attachment error returns non-zero. If a new selection fails, it attempts to
restore the previous selection. Restart Dia if restoration fails or attachment
times out. All changes are in memory; a restart also unloads the DLL.

Nothing is installed persistently or started at login. Run the command again
after restarting or updating Dia. When several Dia processes have browser
windows, select one with `--pid NUMBER`. The controller refuses arbitrary
processes and non-x64 targets. ARM64 Dia is not supported.

## Implementation

`native/Attach.cpp` discovers the running Dia's WinUI runtime and uses
`InitializeXamlDiagnosticsEx` to load our COM TAP DLL. The TAP uses the Windows
SDK's XAML diagnostics interfaces, so the build does not need a guessed WinUI
NuGet version or hardcoded addresses in Dia.dll.

`native/Tap.cpp` subscribes once, queues button handles, and reads/modifies XAML
properties on the UI thread. Only exact observed AutomationIds match. It changes
plain Visibility values and refuses bindings, animations and visual-state
sources. For restoration it retains a plain local value or clears its override
to let the original style/default take effect. App-side changes are left alone.

The diagnostics DLL and subscription remain resident until Dia exits. Repeated
commands use the existing message-only control window rather than opening a
fresh diagnostics session. Tests and a successful build do not establish
runtime compatibility with Dia.

References: [XAML diagnostics entry point](https://learn.microsoft.com/en-us/windows/win32/api/xamlom/nf-xamlom-initializexamldiagnosticsex),
[property value chains](https://learn.microsoft.com/en-us/windows/win32/api/xamlom/ns-xamlom-propertychainvalue),
[creating typed values](https://learn.microsoft.com/en-us/windows/win32/api/xamlom/nf-xamlom-ivisualtreeservice-createinstance).

## Collect a UI Automation report

`Inspect-Dia.ps1` is a separate read-only diagnostic. In Windows PowerShell 5.1
or PowerShell 7, with a blank tab open, run from the repository root:

```powershell
.\windows\Inspect-Dia.ps1
```

Review `dia-inspection-*.json` before sharing. It contains control types, class
names, AutomationIds, sizes, module names and app versions. It does not request
Name or Value properties and skips document/Chromium renderer subtrees.
AutomationIds are app-provided and may still contain information you do not
want to share. Existing output files are not overwritten. Optional parameters:
`-OutputPath`, `-MaxDepth` (default 14), `-MaxNodes` (default 2000).
