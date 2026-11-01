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
binding preservation and rollback. It also removes hidden visuals, invalidates
their old handles, reintroduces the same object under a new handle, and tests
failed restoration and retry. It does not simulate the actual WinUI loader.
It retires replaced boxed values and runs repeated sidebar-only, Chat-only and
combined hide/restore cycles, checking both button visibility and ownership.
The Windows workflow runs these checks and uploads those two output files.

## Run

Keep the EXE and DLL in the same folder. Open Dia, then run without elevation:

```powershell
.\DiaTweaks.exe --hide-chat --hide-sidebar
```

When upgrading this experimental build, quit all Dia processes first, then
replace **both** the EXE and DLL before reopening Dia. The old DLL stays loaded
until the process exits. The controller checks the session's protocol version
and rejects an older loaded DLL.

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

Restore prints each affected button's current visible count and remaining owned
overrides. It waits briefly for visual-tree notifications and requires controls
whose visible baseline it changed to be live and Visible again before reporting
verified restoration. Removing our override is not by itself proof that Dia has
made a control visible; an app-side binding or a missing visual is reported
without claiming verified restoration. Controls hidden by Dia before attaching
are not forced visible.

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

A visual-tree Remove notification does not discard an active restore record.
The TAP retains the patched IInspectable and its COM identity while it owns an
override. It can register that object again if its old diagnostics handle is
invalid, or transfer the original baseline when the same object is added under
a new handle. References are released after restoring the owned value.
See [RegisterInstance](https://learn.microsoft.com/en-us/windows/win32/api/xamlom/nf-xamlom-ixamldiagnostics-registerinstance).

Original local Visibility is stored as its type and primitive enum text. Each
hide and each local-value restoration creates a fresh boxed value immediately
before SetProperty. A temporary boxed value's diagnostics handle is not reused
across hide/restore cycles.

UI Automation may expose a button's XAML `x:Name` as its AutomationId when the
attached AutomationId property is empty. Matching handles that fallback; a
nonempty explicit AutomationId takes precedence. This follows WinUI's
[`CAutomationPeer::GetAutomationIdHelper`](https://github.com/microsoft/microsoft-ui-xaml/blob/main/dxaml/xcp/core/core/elements/AutomationPeer.cpp).

On failure, a `Discovery:` line reports numeric counters (received additions,
eligible button types, inspected handles, property-query failures, empty IDs,
matches through x:Name, tracked controls, missing Visibility properties and
detached owned overrides).
It also includes the last failed discovery HRESULT. It does not print UI text,
page URLs or unknown element names. A matched control with unreadable Visibility
is included in the denominator, so that case is distinguishable from no match.
A per-button failure line identifies the failed stage and its HRESULT, plus
Visibility's metadata bits and source. This distinguishes a rejected policy,
expired value handle, failed write and failed readback from a discovery error.

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
