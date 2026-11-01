# DiaTweaks for Windows (experimental, x64)

This is an initial native Windows implementation, **not a validated port of all
DiaTweaks features**. Hiding, restoration and repeated hiding have been reported
working on Windows, including the Settings UI. Saved settings across runs have also been confirmed. The four new switches below
need on-device confirmation; whole-toolbar hiding is experimental.
The macOS installer and Rust CLI do not manage this backend.

## Implemented scope

| Windows switch | Upstream tweak | Selector |
| --- | --- | --- |
| `--settings` (or no arguments) | Settings UI and saved selection | Dia Settings → Patches |
| `--hide-chat` | `hideChatButton` | `ChatButton` |
| `--hide-sidebar` | `hideSidebarButton` | `SidebarToggleButton` |
| `--hide-downloads` | Windows addition | `DownloadsButton` |
| `--hide-bookmark` | Windows addition | `BookmarkButton` |
| `--hide-extensions` | Windows addition | `PinnedExtensionsHost` (contains repeater and overflow) |
| `--hide-toolbar` | `hideToolbar` (experimental) | `NavigationBarHost` and optional `BookmarksBarHost` |
| `--restore` | Restore this session's overrides | Saved original properties |

The button and extensions selectors were observed in a complete UI Automation
inspection of Dia `1.5.0.588`, x64, with sidebar tabs. Toolbar hosts were identified
in the official Windows package's compiled ContentStack layout. Only the navigation
and bookmarks hosts are targeted; the info-bar host and window caption are preserved.
Both tab layouts and page layout after collapsing toolbar hosts still need testing.
No claim is made about layout reflow or hover reveal.

Hover reveal, edge-to-edge content, corner shapes, recent tabs,
Chats library button, tab grouping, active-window styling
and automatic attachment after restart/update are not implemented. Dia's
built-in updater is unchanged. WSL cannot run this backend.

## Build

Use Visual Studio Build Tools with **Desktop development with C++** and the
Windows SDK, plus the NuGet CLI. In an **x64 Native Tools Command Prompt**, from the repository root:

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
Persistence tests use disposable registry keys to model different launcher and
Dia registry views, save through the real Settings IPC path, and load the saved
choice into fresh backend sessions. They also check invalid data and types,
explicit restoration to off, and that reading defaults never writes them back.

## Run

Keep the EXE and DLL in the same folder. Open Dia, then run without elevation:

```powershell
.\DiaTweaks.exe --settings
```

Open Dia's Settings and choose **Patches** at the bottom of its sidebar.
The page contains switches for the toolbar, Chat, sidebar, downloads, bookmark,
and extensions, availability notes, and a **Restore defaults** action.
Whole-toolbar hiding disables individual switches while preserving their saved
choices. Showing the toolbar again reapplies those choices. Optional bookmarks
controls may be absent; they are patched if they appear later.
Host discovery uses exact x:Names across custom and built-in XAML types. Extensions
are hidden at their wrapper, leaving the repeater’s app-owned bindings untouched.
Failed changes show target/stage/HRESULT details on the Patches page. Switches apply immediately
and follow CLI changes. The page uses WinUI controls and Dia's theme resources.
Choosing one of Dia's own categories returns to its original content.

Selections are saved under `HKCU\Software\DiaTweaks`, value `WindowsFlags`.
All preference I/O runs inside Dia, including reads requested by the launcher,
so the reader and writer use the same package/registry view. Startup reads the
selection after attaching and never saves defaults over it. A missing value
means off; other read errors stop attachment configuration without overwriting
the stored selection. A successful write is read back before reporting saved.
Run `--settings` again after restarting/updating Dia to attach the add-on and
apply that saved selection. No startup task, service or persistent DLL injection
is installed. A normal Dia restart clears in-memory UI changes until attaching
again. `--restore` also saves all switches as off.
Closing or leaving Patches is read-only: unload-triggered/programmatic switch
events do not save changes. Only active-page switch actions or explicit CLI
changes write preferences. After an upgrade, the launcher prints the loaded
choices for all six switches to help verify restart behavior.

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

The controller checks each requested control's effective Visibility after setting
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

When several Dia processes have browser
windows, select one with `--pid NUMBER`. The controller refuses arbitrary
processes and non-x64 targets. ARM64 Dia is not supported.

## Implementation

`native/Attach.cpp` discovers the running Dia's WinUI runtime and uses
`InitializeXamlDiagnosticsEx` to load our COM TAP DLL. The TAP uses the Windows
SDK's XAML diagnostics interfaces. The settings-page build generates C++/WinRT
projections from the pinned Microsoft.WindowsAppSDK.WinUI 1.8.250906003 package.
The running Dia supplies WinUI's runtime; its libraries are not shipped in the
artifact, and there are no hardcoded addresses in Dia.dll.

`native/Settings.cpp` recognizes the NavigationView in Dia's Windows settings
layout by `PaneTitleText` and `ManagedBannerBorder`. It adds a Patches button to
the pane footer without modifying Dia's item source or selecting an unknown
native route. The page temporarily overlays the content area and restores the
original branch visibility/binding when leaving Patches. Closing Settings
removes the injected UI and event handlers. Opening a new Settings window adds
the entry again. Layout detection failures leave Dia's settings intact.

`native/Tap.cpp` subscribes once, queues control handles, and reads/modifies XAML
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
eligible control types, inspected handles, property-query failures, empty IDs,
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
