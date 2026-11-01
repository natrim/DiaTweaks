# DiaTweaks for Windows (experimental, x64)

This is an initial native Windows implementation, **not a validated port of all
DiaTweaks features**. Hiding, restoration and repeated hiding have been reported
working on Windows, including the Settings UI. Saved settings across runs, toolbar hiding, downloads, bookmark and extensions
have also been reported working. The new Recent Tabs and Spaces switches need
on-device confirmation. Whole-toolbar hiding remains experimental.
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
| `--hide-recent-tabs` | `hideRecentTabsButton` (sidebar only) | `OverflowButton` in the sidebar header (named header or App Menu/Spaces peers) |
| `--hide-spaces` | Windows addition | `SpaceSwitcherButton` |
| `--unload` / `--detach` | Disconnect the current add-on; keep saved choices | Restore UI, remove Settings handlers, unsubscribe |
| `--reload` | Disconnect and attach this build with saved choices | New cached DLL copy |
| `--restore` | Restore this session's overrides | Saved original properties |

The button and extensions selectors were observed in a complete UI Automation
inspection of Dia `1.5.0.588`, x64, with sidebar tabs. Toolbar hosts were identified
in the official Windows package's compiled ContentStack layout. Only the navigation
and bookmarks hosts are targeted; the info-bar host and window caption are preserved.
Both tab layouts and page layout after collapsing toolbar hosts still need testing.
The Recent Tabs selector requires a sidebar container (SpacesScrollViewer or
SidebarPresenter) and a header below it, identified by WindowDragHeader or the
App Menu and Spaces peer buttons. Structural names and attached AutomationIds are
both recognized. Tab-row/content branches and overflow menus elsewhere are excluded.
Candidates whose header is not connected yet are retried for up to five seconds;
availability refreshes when the header becomes recognizable.
Horizontal-tab Recent Tabs is not implemented; its switch is unavailable when no
matching sidebar header exists. Spaces uses the observed exact button ID.
No claim is made about layout reflow or hover reveal.

Hover reveal, edge-to-edge content, corner shapes, horizontal-tab recent tabs,
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
binding preservation and rollback. New checks cover sidebar-header ancestry,
unrelated overflow menus, high-bit protocol fields and sidebar preferences across sessions. It also removes hidden visuals, invalidates
their old handles, reintroduces the same object under a new handle, and tests
failed restoration and retry. It does not simulate the actual WinUI loader.
It retires replaced boxed values and runs repeated sidebar-only, Chat-only and
combined hide/restore cycles, checking both button visibility and ownership.
The Windows workflow runs these checks and uploads those two output files.
Persistence tests use disposable registry keys to model different launcher and
Dia registry views, save through the real Settings IPC path, and load the saved
choice into fresh backend sessions. They also check invalid data and types,
explicit restoration to off, and that reading defaults never writes them back.
Detach tests cover failed restoration, failed unsubscription and retry, late
callbacks, session recreation in the same process, and saved-choice preservation.

## Run

Keep the EXE and DLL in the same folder. Open Dia, then run without elevation:

```powershell
.\DiaTweaks.exe --settings
```

Open Dia's Settings and choose **Patches** at the bottom of its sidebar.
The page contains switches for the toolbar, Chat, sidebar, downloads, bookmark,
extensions, Recent Tabs and Spaces, availability notes, and a **Restore defaults** action.
Whole-toolbar hiding disables its child-control switches while preserving their saved
choices. The Recent Tabs and Spaces sidebar switches remain usable. Showing the toolbar again reapplies those choices. Optional bookmarks
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
Run `--settings` again after restarting Dia to attach the add-on and
apply that saved selection. No startup task, service or persistent DLL injection
is installed. A normal Dia restart clears in-memory UI changes until attaching
again. Use `--unload` to disconnect without changing saved choices, and `--reload`
to disconnect and attach the current build with those choices. `--restore` also saves all switches as off.
Closing or leaving Patches is read-only: unload-triggered/programmatic switch
events do not save changes. Only active-page switch actions or explicit CLI
changes write preferences. After an upgrade, the launcher prints the loaded
choices for all eight switches to help verify restart behavior.

### Disconnect and update without restarting

```powershell
.\DiaTweaks.exe --unload
# Replace the EXE and DLL with the new build, then:
.\DiaTweaks.exe --settings
# Or reconnect the current/new build and reapply saved choices:
.\DiaTweaks.exe --reload
```

**One final restart is required when upgrading from protocol 9 or older.** Those
versions do not implement detach and lock the original DLL. From protocol 10 on,
the launcher loads a uniquely named session copy under the user's temporary
DiaTweaks directory; the downloaded EXE and DLL stay replaceable. A new build's
launcher automatically detaches a compatible older session before attaching.
Both files must come from the same build. Each production build gets a distinct
COM class and control-window class to avoid reusing an old build's factory.

The `--unload` name means **logical detachment**, not physically unmapping the
DLL. It restores owned changes, removes the Patches page and event handlers,
unsubscribes on a worker thread, and removes the session's control window. It
preserves saved choices; `--restore` instead saves all switches as off. If
restoration, Settings cleanup or unsubscription fails, no replacement session is
attached. Retry `--unload` or restart Dia.

The inactive DLL remains pinned until Dia exits. Each reattachment creates a new
cached copy and can retain another diagnostics session/module in memory. Use
reload for updates and troubleshooting; it is not a polling/refresh operation.
Cached copies are not automatically deleted. Actual cross-build reattachment in
Dia still needs on-device testing; automated checks use the mock XAML service.

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
properties on the UI thread. Matching uses exact known AutomationIds or structural host names. The shared
OverflowButton ID also requires the sidebar-header ancestor scope. It changes
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

The DLL remains resident until Dia exits; its subscription ends on detachment.
Repeated commands reuse the existing session. A new subscription is created only
after explicit detachment/reload or a compatible build replacement. Tests and a successful build do not establish
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
