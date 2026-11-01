#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "SettingsHost.h"
#include "SettingsPage.h"
#include "Rules.h"
#include "Scope.h"
// The legacy Win32 GetCurrentTime macro collides with WinUI animation methods.
#ifdef GetCurrentTime
#undef GetCurrentTime
#endif
#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Data.h>
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Microsoft.UI.Xaml.Markup.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <algorithm>
#include <memory>
#include <vector>

namespace {
using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Microsoft::UI::Xaml::Controls::Primitives;
using Microsoft::UI::Xaml::Media::VisualTreeHelper;
using Microsoft::UI::Xaml::Markup::XamlReader;
using Object = Windows::Foundation::IInspectable;
HRESULT lastError = S_OK;
unsigned settingsDepth = 0;
struct SettingsCall {
    SettingsCall() { ++settingsDepth; }
    ~SettingsCall() { --settingsDepth; }
};

FrameworkElement Named(NavigationView const& nav, wchar_t const* name) {
    if (auto item = nav.FindName(name).try_as<FrameworkElement>()) return item;
    std::vector<DependencyObject> queue{nav};
    for (size_t i = 0; i < queue.size() && i < 1024; ++i) {
        auto current = queue[i];
        if (auto element = current.try_as<FrameworkElement>(); element && element.Name() == name) return element;
        int count = VisualTreeHelper::GetChildrenCount(current);
        for (int j = 0; j < count && queue.size() < 1024; ++j) queue.push_back(VisualTreeHelper::GetChild(current, j));
    }
    return nullptr;
}

struct Settings : std::enable_shared_from_this<Settings> {
    weak_ref<NavigationView> navigation;
    dt::SettingsApi api;
    bool installed = false, showing = false, syncing = false, disposed = false, disposalFailed = false;
    event_token loaded{}, unloaded{}, invoked{}, selected{}, entryClick{}, restoreClick{};
    Button restore{nullptr};
    UIElement originalFooter{nullptr};
    Object originalVisibility{nullptr};
    Microsoft::UI::Xaml::Data::Binding visibilityBinding{nullptr};
    StackPanel footer{nullptr};
    ToggleButton entry{nullptr};
    ScrollViewer page{nullptr};
    struct Row { unsigned mask, required; ToggleSwitch toggle; TextBlock note; event_token toggled; };
    std::vector<Row> rows;
    TextBlock status{nullptr};
    weak_ref<Panel> overlayParent;
    weak_ref<FrameworkElement> hiddenBranch;

    Settings(NavigationView const& nav, dt::SettingsApi callbacks) : navigation(nav), api(callbacks) {}
    void Start() {
        auto nav = navigation.get();
        auto weak = weak_from_this();
        loaded = nav.Loaded([weak](Object const&, RoutedEventArgs const&) {
            if (auto self = weak.lock()) self->TryInstall();
        });
        unloaded = nav.Unloaded([weak](Object const&, RoutedEventArgs const&) {
            if (auto self = weak.lock()) self->Dispose();
        });
        TryInstall();
    }
    void TryInstall() noexcept {
        SettingsCall call;
        if (installed || disposed) return;
        auto nav = navigation.get();
        if (!nav) return;
        try {
            // These named elements belong to Dia's PlatformPreferencesWindowView
            // layout. Do not add controls to unrelated NavigationViews.
            if (!Named(nav, L"PaneTitleText") || !Named(nav, L"ManagedBannerBorder")) return;
            if (!nav.Content().try_as<FrameworkElement>()) return;
            entry = XamlReader::Load(dt::SettingsEntryXaml).as<ToggleButton>();
            auto weak = weak_from_this();
            entryClick = entry.Click([weak](Object const&, RoutedEventArgs const&) {
                if (auto self = weak.lock()) self->Show();
            });
            originalFooter = nav.PaneFooter();
            footer = StackPanel();
            nav.PaneFooter(nullptr);
            if (originalFooter) {
                ContentPresenter original;
                original.Content(originalFooter);
                footer.Children().Append(original);
            }
            footer.Children().Append(entry);
            nav.PaneFooter(footer);
            invoked = nav.ItemInvoked([weak](NavigationView const&, NavigationViewItemInvokedEventArgs const&) {
                if (auto self = weak.lock()) self->Hide();
            });
            selected = nav.SelectionChanged([weak](NavigationView const&, NavigationViewSelectionChangedEventArgs const&) {
                if (auto self = weak.lock()) self->Hide();
            });
            installed = true;
            lastError = S_OK;
        } catch (...) {
            lastError = to_hresult();
            try {
                if (footer) {
                    nav.PaneFooter(nullptr);
                    footer.Children().Clear();
                    nav.PaneFooter(originalFooter);
                }
            } catch (...) {}
            entry = nullptr; footer = nullptr;
        }
    }
    void BuildPage() {
        if (page) return;
        page = XamlReader::Load(dt::SettingsPageXaml).as<ScrollViewer>();
        status = page.FindName(L"Status").as<TextBlock>();
        auto container = page.FindName(L"TweakRows").as<StackPanel>();
        auto weak = weak_from_this();
        for (auto const& tweak : dt::Tweaks) {
            auto card = XamlReader::Load(dt::SettingsRowXaml).as<Border>();
            card.FindName(L"Title").as<TextBlock>().Text(tweak.title);
            card.FindName(L"Description").as<TextBlock>().Text(tweak.detail);
            auto toggle = card.FindName(L"Switch").as<ToggleSwitch>();
            auto note = card.FindName(L"Unavailable").as<TextBlock>();
            Automation::AutomationProperties::SetName(toggle, tweak.title);
            Automation::AutomationProperties::SetAutomationId(toggle, hstring(L"DiaTweaks.") + tweak.key);
            unsigned mask = tweak.mask;
            auto toggled = toggle.Toggled([weak, mask](Object const& sender, RoutedEventArgs const&) {
                if (auto self = weak.lock(); self && self->showing && !self->disposed && !self->syncing)
                    self->Change(mask, sender.as<ToggleSwitch>().IsOn());
            });
            rows.push_back({tweak.mask, tweak.required, toggle, note, toggled});
            container.Children().Append(card);
        }
        restore = page.FindName(L"RestoreButtons").as<Button>();
        restoreClick = restore.Click([weak](Object const&, RoutedEventArgs const&) {
            if (auto self = weak.lock()) self->Write(0);
        });
    }
    void Show() noexcept {
        SettingsCall call;
        try {
            if (showing) { entry.IsChecked(true); return; }
            auto nav = navigation.get();
            if (!nav) return;
            BuildPage();
            auto branch = nav.Content().as<FrameworkElement>();
            Panel parent{nullptr};
            for (unsigned depth = 0; depth < 12; ++depth) {
                auto next = VisualTreeHelper::GetParent(branch);
                if (!next || next == nav) break;
                parent = next.try_as<Panel>();
                if (parent) break;
                branch = next.as<FrameworkElement>();
            }
            if (!parent) throw hresult_error(E_NOTIMPL);
            originalVisibility = branch.ReadLocalValue(UIElement::VisibilityProperty());
            if (auto expression = branch.GetBindingExpression(UIElement::VisibilityProperty())) visibilityBinding = expression.ParentBinding();
            else visibilityBinding = nullptr;
            Grid::SetRow(page, Grid::GetRow(branch));
            Grid::SetColumn(page, Grid::GetColumn(branch));
            Grid::SetRowSpan(page, Grid::GetRowSpan(branch));
            Grid::SetColumnSpan(page, Grid::GetColumnSpan(branch));
            Canvas::SetZIndex(page, 100);
            page.Margin(branch.Margin());
            overlayParent = parent; hiddenBranch = branch;
            parent.Children().Append(page);
            showing = true;
            branch.Visibility(Visibility::Collapsed);
            entry.IsChecked(true);
            Refresh();
            for (auto const& row : rows) if (row.toggle.IsEnabled()) { row.toggle.Focus(FocusState::Programmatic); break; }
            lastError = S_OK;
        } catch (...) {
            lastError = to_hresult();
            Hide();
        }
    }
    void Hide() noexcept {
        SettingsCall call;
        if (!showing) return;
        showing = false;
        bool wasSyncing = syncing;
        syncing = true;
        try {
            if (auto parent = overlayParent.get()) {
                uint32_t index = 0;
                if (parent.Children().IndexOf(page, index)) parent.Children().RemoveAt(index);
            }
            if (auto branch = hiddenBranch.get()) {
                if (visibilityBinding) branch.SetBinding(UIElement::VisibilityProperty(), visibilityBinding);
                else if (originalVisibility == DependencyProperty::UnsetValue()) branch.ClearValue(UIElement::VisibilityProperty());
                else branch.SetValue(UIElement::VisibilityProperty(), originalVisibility);
            }
            if (entry) entry.IsChecked(false);
        } catch (...) { lastError = to_hresult(); disposalFailed = true; }
        syncing = wasSyncing;
        overlayParent = {}; hiddenBranch = {};
        originalVisibility = nullptr; visibilityBinding = nullptr;
    }
    void Refresh() noexcept {
        SettingsCall call;
        if (!page || disposed) return;
        syncing = true;
        try {
            unsigned flags = api.read(api.context);
            for (auto const& row : rows) {
                row.toggle.IsOn((flags & row.mask) != 0);
                bool toolbarHidden = dt::HiddenWithToolbar(row.mask, flags);
                bool ready = api.available(api.context, row.required);
                row.toggle.IsEnabled(ready && !toolbarHidden);
                row.note.Text(toolbarHidden ? L"Hidden with the toolbar. Your preference is preserved."
                                            : L"Not available in the current browser window.");
                row.note.Visibility(ready && !toolbarHidden ? Visibility::Collapsed : Visibility::Visible);
            }
        } catch (...) { lastError = to_hresult(); }
        syncing = false;
    }
    void Change(unsigned target, bool enabled) noexcept {
        unsigned flags = api.read(api.context);
        Write(enabled ? flags | target : flags & ~target);
    }
    void Write(unsigned flags) noexcept {
        SettingsCall call;
        if (disposed || !showing || syncing) return;
        try {
            HRESULT hr = api.write(api.context, flags);
            Refresh();
            if (SUCCEEDED(hr)) status.Text(flags ? L"Changes applied and saved." : L"All tweaks are off.");
            else {
                std::wstring message = L"Couldn’t apply or save that change. Previous settings restored. ";
                if (api.failure) message += api.failure(api.context);
                status.Text(message);
            }
        } catch (...) { lastError = to_hresult(); Refresh(); }
    }
    void Dispose() noexcept {
        SettingsCall call;
        if (disposed) return;
        disposed = true; Hide();
        try {
            if (entry) entry.Click(entryClick);
            if (restore) restore.Click(restoreClick);
            for (auto const& row : rows) row.toggle.Toggled(row.toggled);
            if (auto nav = navigation.get()) {
                nav.Loaded(loaded); nav.Unloaded(unloaded);
                if (installed) {
                    nav.ItemInvoked(invoked);
                    nav.SelectionChanged(selected);
                    // Detach the wrapper before returning its content to Dia.
                    nav.PaneFooter(nullptr);
                    if (footer) footer.Children().Clear();
                    nav.PaneFooter(originalFooter);
                }
            }
        } catch (...) { lastError = to_hresult(); disposalFailed = true; }
        installed = false;
        page = nullptr; rows.clear(); entry = nullptr; restore = nullptr;
        footer = nullptr; originalFooter = nullptr; status = nullptr;
    }
};
std::vector<std::shared_ptr<Settings>> settings;
}

void dt::ObserveSettings(IInspectable* object, SettingsApi api) noexcept {
    SettingsCall call;
    try {
        Object instance{nullptr};
        copy_from_abi(instance, object);
        auto nav = instance.try_as<NavigationView>();
        if (!nav) return;
        settings.erase(std::remove_if(settings.begin(), settings.end(), [](auto const& item) {
            return item->disposed || !item->navigation.get();
        }), settings.end());
        for (auto const& item : settings) if (item->navigation.get() == nav) { item->TryInstall(); return; }
        auto item = std::make_shared<Settings>(nav, api);
        settings.push_back(item);
        item->Start();
    } catch (...) { lastError = to_hresult(); }
}
void dt::RefreshSettings() noexcept {
    SettingsCall call;
    for (auto const& item : settings) { item->TryInstall(); item->Refresh(); }
}
LRESULT dt::SettingsStatus(WPARAM field) noexcept {
    if (field == SettingsCandidates) return static_cast<LRESULT>(settings.size());
    if (field == SettingsLastError) return lastError;
    if (field == SettingsInstalled) return static_cast<LRESULT>(std::count_if(settings.begin(), settings.end(), [](auto const& item) { return item->installed; }));
    return 0;
}

namespace {
hstring ScopeLabel(DependencyObject const& item) {
    auto element = item.try_as<FrameworkElement>();
    if (!element) return {};
    auto name = element.Name();
    if (dt::IsScopeMarker(name)) return name;
    auto id = Automation::AutomationProperties::GetAutomationId(element);
    return dt::IsScopeMarker(id) ? id : hstring{};
}
bool HeaderPeers(DependencyObject const& root) {
    struct Visit { DependencyObject node; unsigned depth; };
    std::vector<Visit> queue{{root, 0}};
    bool menu = false, spaces = false;
    for (size_t i = 0; i < queue.size() && i < 64; ++i) {
        auto item = queue[i];
        auto label = ScopeLabel(item.node);
        if (auto button = item.node.try_as<Button>()) {
            menu = menu || label == L"AppMenuButton";
            spaces = spaces || label == L"SpaceSwitcherButton";
            if (menu && spaces) return true;
            continue;
        }
        if (item.depth >= 4 || item.node.try_as<ScrollViewer>() || label == L"TabListScrollViewer" || label == L"RowsPanel") continue;
        int count = VisualTreeHelper::GetChildrenCount(item.node);
        for (int j = 0; j < count && queue.size() < 64; ++j)
            queue.push_back({VisualTreeHelper::GetChild(item.node, j), item.depth + 1});
    }
    return false;
}
}
bool dt::IsSidebarHeader(IInspectable* object) noexcept {
    try {
        Object instance{nullptr};
        copy_from_abi(instance, object);
        auto node = instance.try_as<DependencyObject>();
        return InSidebarHeader(node,
            [](DependencyObject const& item) { return VisualTreeHelper::GetParent(item); },
            [](DependencyObject const& item) { return ScopeLabel(item); },
            [](DependencyObject const& item) { return HeaderPeers(item); });
    } catch (...) { return false; }
}

bool dt::CanDetachSettings() noexcept { return settingsDepth == 0; }
HRESULT dt::DetachSettings() noexcept {
    if (!CanDetachSettings()) return HRESULT_FROM_WIN32(ERROR_BUSY);
    HRESULT result = S_OK;
    for (auto const& item : settings) {
        item->Dispose();
        if (item->disposalFailed) result = E_FAIL;
    }
    if (SUCCEEDED(result)) settings.clear();
    return result;
}
