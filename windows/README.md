# DiaTweaks for Windows (experimental, x64)

This is an initial native Windows implementation, **not a validated port of all
DiaTweaks features**. Hiding, restoration and repeated hiding have been reported
working on Windows, including the Settings UI. Saved settings across runs, toolbar hiding, downloads, bookmark and extensions
have also been reported working. Recent Tabs hiding has also been confirmed. Spaces and the new hover-reveal
option need on-device confirmation. Whole-toolbar hiding remains experimental.
The macOS installer and Rust CLI do not manage this backend.

## Implemented scope

| Windows switch | Upstream tweak | Selector |
| --- | --- | --- |
| `--edge-to-edge` | `edgeToEdge` (experimental) | Scoped VariantHost page frame and small padding |
| `--edge-to-edge --square-sidebar-corners` | `squareSidebarCorners` (experimental) | Sidebar-facing CornerRadius values on page chrome |
| `--gui` (or no arguments) | Windows control panel | `DiaTweaks.Gui.exe` |
| `--status` | Read-only session state and diagnostics | Existing session only |
| `--inspect-layout` | Live XAML/composition/child-window geometry | Existing session only |
| `--settings` | Settings UI and saved selection | Dia Settings → Patches |
| `--hide-toolbar --reveal-toolbar` | `revealToolbarOnHover` (experimental) | Pointer/focus policy on the hidden toolbar's own window and pane |
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
| `--restore` / `--reset-settings` | Restore this session's overrides | Saved original properties |

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
Hover reveal uses at least the top 24 logical pixels of the content pane. It also
covers the blank strip before VariantHost plus 16 logical pixels of the page,
with a 128-pixel cap. Dedicated fixed toolbar grid rows are made Auto while hidden
(and their explicit minimum height removed), then restored before reveal or when
disabling the option. Shared rows, star-sized rows and bound sizes are left alone.
The trigger therefore stays usable even where the surrounding native layout
still reserves space. Hover keeps the
bar open while the pointer or keyboard focus is in it, and hides after a 250 ms
pointer-leave delay. Losing the active window hides it immediately. It restores
only visibility values owned by DiaTweaks, so an already-hidden bookmarks bar
stays hidden. Native layout may reflow while the bar is shown; floating overlay
and animation parity with macOS are not implemented. Window/pane geometry, DPI,
multiple panes and menus need testing in real Dia.

Horizontal-tab recent tabs,
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
builds `windows/build/DiaTweaks.exe`, `windows/build/DiaTweaks.Gui.exe` and
`windows/build/DiaTweaks.Tap.dll`.
The mock exercises UI-thread dispatch, matching, property readback, restoration,
binding preservation and rollback. New checks cover sidebar-header ancestry,
unrelated overflow menus, high-bit protocol fields and sidebar preferences across sessions. It also removes hidden visuals, invalidates
their old handles, reintroduces the same object under a new handle, and tests
failed restoration and retry. It does not simulate the actual WinUI loader.
It retires replaced boxed values and runs repeated sidebar-only, Chat-only and
combined hide/restore cycles, checking both button visibility and ownership.
The Windows workflow runs these checks and uploads all three output files.
Persistence tests use disposable registry keys to model different launcher and
Dia registry views, save through the real Settings IPC path, and load the saved
choice into fresh backend sessions. They also check invalid data and types,
explicit restoration to off, and that reading defaults never writes them back.
Detach tests cover failed restoration, failed unsubscription and retry, late
callbacks, session recreation in the same process, and saved-choice preservation.

## Run

Keep **DiaTweaks.Gui.exe**, **DiaTweaks.exe** and **DiaTweaks.Tap.dll** in the same
folder. Double-click **DiaTweaks.Gui.exe** for the native Windows control panel.
Running DiaTweaks.exe with no arguments, or with --gui, opens that panel too.

The panel has **Load**, **Unload**, **Reload**, **Reset settings**,
**Status / diagnostics** and **CLI help** actions. It includes a Dia process
selector with **Refresh processes**, an animated progress bar, live stage text,
and a scrollable log with **Copy log** and **Clear log**. Controller errors and
exit codes appear in the window. Actions run in a separate worker process and
prevent overlapping requests; closing during an action waits for it to finish.
The panel itself makes no browser changes until an action is selected.

**Load** attaches and applies saved choices. **Unload** disconnects while keeping
those choices. **Reload** reconnects and applies them again. **Reset settings**
is the GUI action for --restore / --reset-settings: restore owned changes and
save all tweaks as off. Reset uses Dia's own registry view, so Dia must be open;
there is no external registry-view fallback. **Status** reads an existing
session without loading, replacing or changing it. CLI aliases share one button;
--pid is represented by the process selector. Tweak switches remain in Dia's
Settings rather than being duplicated in the control panel.

Native GUI tests create real controls and execute the CLI with captured pipes.
They test help, action-button/process routing, progress and error display,
control disabling and deferred close. Mutation tests deliberately target the
test executable's PID, so they cannot patch a running Dia during a local build.
Actual browser loading still needs on-device testing.

For scripts, open Dia and run without elevation:

```powershell
.\DiaTweaks.exe --settings
```

Open Dia's Settings and choose **Patches** at the bottom of its sidebar.
The page contains switches for the toolbar, Chat, sidebar, downloads, bookmark,
extensions, Recent Tabs, Spaces and hover reveal, availability notes, and a **Restore defaults** action.
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
choices for all eleven switches to help verify restart behavior.

### Disconnect and update without restarting

```powershell
.\DiaTweaks.exe --unload
# Replace all three files with the new build, then:
.\DiaTweaks.exe --settings
# Or reconnect the current/new build and reapply saved choices:
.\DiaTweaks.exe --reload
```

**One final restart is required when upgrading from protocol 9 or older.** Those
versions do not implement detach and lock the original DLL. From protocol 10 on,
the launcher loads a uniquely named session copy under the user's temporary
DiaTweaks directory; the downloaded EXE and DLL stay replaceable. A new build's
launcher automatically detaches a compatible older session before attaching.
All three files must come from the same build. Each production build gets a distinct
COM class and control-window class to avoid reusing an old build's factory.

Close the control panel before replacing DiaTweaks.Gui.exe; Dia itself can stay open.

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
times out. Hover is a dependent option: enable **Hide toolbar** first in Settings, or combine
`--hide-toolbar --reveal-toolbar` on the CLI. Disabling Hide toolbar preserves
the hover preference as inactive. The timer stops when the option is inactive or
the add-on detaches. No global input hook is installed.

All changes are in memory; a restart also unloads the DLL.

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

## Page frame tweaks (experimental)

**Edge-to-edge web content** removes small positive padding and stretch-layout
margins (up to 12 logical pixels) from the page chrome. The ancestor search
includes the rounded Grid, outer Border, inset Grid and further page-sized
wrappers above the toolbar/page pane. It stops before an ancestor widens into
the sidebar or window shell, rather than treating the first margin as a clip
boundary. The native stretch layout expands the page into these actual insets.
Large offsets, toolbar space, negative margins and settings/dialog content are
preserved. Chat/assistant page variants are excluded. Window captions and the
sidebar itself are not styled.

**Square corners by the sidebar** requires Edge-to-edge. It changes only the two
corners facing a visible inline sidebar, keeping the opposite side's original
radii. Hiding the sidebar restores those corners. It targets exposed XAML page
CornerRadius properties and existing RectangleClip masks on full-size page
visuals. Clip bounds, offsets and the opposite corners are preserved. Each
changed corner has its own snapshot and readback check; disable/reset/unload
restores it only if Dia has not changed that value. Unsupported clip shapes are
left alone. A layout without a supported rounded frame leaves the switch unavailable.
This is not a claim of macOS full-screen/animation/shadow parity.

Both options default off for existing and new Windows installations. The square
corner choice remains saved but inactive when Edge-to-edge is off. The modes
refresh after tab changes and resizing. Original local values or style/default
sources are restored when disabling, resetting or unloading. Bindings,
animations and unsupported XAML property sources are refused. Diagnostics include
supported frame counts, rounded-frame counts, owned page properties and the last
page-layout HRESULT. Actual results in Dia need on-device verification.

The hidden-toolbar watcher also reapplies compaction while the pointer is away,
so a native tab switch resetting/replacing the row does not require hover to
remove its gap again. A newly bound row is left to Dia instead of overwriting it.

### Live layout report

The control panel's **Status / diagnostics** action now includes --inspect-layout.
It records bounded page/toolbar ancestor and descendant geometry, fixed/auto grid
rows, explicit heights/minimum heights, XAML corner radii, composition clip types
and radii, and child-window class/rectangles. It does not read page text, URLs,
window titles, address values or arbitrary AutomationIds. Unknown XAML names are
redacted. Copy the log to diagnose layouts that do not match the supported frame
model. Inspection does not attach or modify a session.

Guessed negative margins on VariantHost have been removed: an independently
sized native web surface can be shifted instead of expanded by those margins.
The reported Windows layout exposes CornerRadius on both the inner Grid and
outer Border; both are styled on the sidebar-facing side. Their generated
composition clips are also checked directly for remaining corner radii. Rounded frame capability is
probed before enabling Edge-to-edge, so it does not depend on a prior apply.

Hide toolbar compacts only the toolbar rows and height wrappers. It preserves
all outer page margins, including the small top inset, when Edge-to-edge is off.
Only Edge-to-edge removes those insets. Disabling it restores the original
margin even while the toolbar remains hidden.

Compaction follows Hide toolbar itself, including when Reveal toolbar on hover is off.
Toolbar compaction now walks through implicit grid rows, supports nested
explicit fixed rows and plain fixed-height/min-height wrapper containers, and
retains each original value for restoration. Shared page rows and bindings are
preserved. Empty info-bar slots do not block compaction. The detach compatibility
floor is fixed at protocol 10 independently of the current report protocol.

Square corners follows same-size page ancestors beyond the first inset wrapper,
so an outer bottom-corner Border/Grid is included too. The layout report now
shows up to 20 ancestors to cover the same search depth.

Square corners also repairs a remaining bottom-left/bottom-right RectangleClip
radius after the XAML corner changes. Diagnostics report `ownedClipCorners` in
addition to XAML property ownership. The existing clip object and its geometry
remain in place, and replaced tab clips are restored before being released.

Hidden-toolbar viewport updates are triggered by coalesced XAML visual-tree
changes, including unnamed presenter replacements during tab switches. The UI
pass reapplies toolbar compaction before page styling, refreshes the current
VariantHost and completes pending layout once for the batch. It does not
invalidate the entire page subtree again, avoiding an extra native resize cycle. Hover reveal/hide transitions also flush layout. The
80/200ms watcher remains a fallback; outer margins still belong only to
Edge-to-edge. Native Chromium page painting is outside these XAML layout tests
and needs verification in Dia.

For a brief bottom flash, Status / diagnostics includes **VIEWPORT TIMING**.
After hidden-toolbar layout events it samples XAML page bounds and matching
visible Chromium/D3D child-window rectangles for up to two seconds. Only changed
geometry plus before/after layout samples is retained (96 entries maximum).
`bottomShortBy` is the physical-pixel difference between the expected page bottom
and native surface bottom. The render callback only observes geometry and is
revoked on expiry, toolbar reveal or region destruction. It reads no page text,
URLs or window titles. Switch a few tabs and copy Status / diagnostics afterwards
to distinguish a delayed native resize from repainting inside an already full-size surface.

### Native viewport resize guard

The timing log showed a full-height XAML page while the existing Chromium host
was reset to the toolbar-visible rectangle on tab switches. While the toolbar
is hidden, the viewport guard corrects `WM_WINDOWPOSCHANGING` on matching
same-process `Chrome_WidgetWin_1` child windows before that stale move/resize
is applied. It publishes current XAML bounds to the native window procedure;
that callback does not access WinUI or wait on the XAML thread. Layout changes
refresh the target, including window resize and DPI changes. A temporary old
toolbar offset is excluded while real info/bookmark bars retain their space.

Reveal/disable/unload suspends the constraint before restoring the toolbar and
restores the original window procedure when it is still ours. Other subclass
chains remain intact. Other panes, classes and processes are excluded. Timing
diagnostics include the guarded-window count and prevented resize count.
Win32 integration tests cover actual child windows, repeated stale native
resizes, real target resizing, release/re-arm, destruction and another window
thread. These tests do not reproduce Chromium's rendering or Dia itself.
