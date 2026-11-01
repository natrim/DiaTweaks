#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "SettingsHost.h"
#include "SettingsPage.h"
#include "Rules.h"
// The legacy Win32 GetCurrentTime macro collides with WinUI animation methods.
#ifdef GetCurrentTime
#undef GetCurrentTime
#endif
#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Data.h>
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
    bool installed = false, showing = false, syncing = false, disposed = false;
    event_token loaded{}, unloaded{}, invoked{}, selected{};
    UIElement originalFooter{nullptr};
    Object originalVisibility{nullptr};
    Microsoft::UI::Xaml::Data::Binding visibilityBinding{nullptr};
    StackPanel footer{nullptr};
    ToggleButton entry{nullptr};
    ScrollViewer page{nullptr};
    ToggleSwitch chat{nullptr}, sidebar{nullptr};
    TextBlock status{nullptr}, chatUnavailable{nullptr}, sidebarUnavailable{nullptr};
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
            entry.Click([weak](Object const&, RoutedEventArgs const&) {
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
        chat = page.FindName(L"HideChat").as<ToggleSwitch>();
        sidebar = page.FindName(L"HideSidebar").as<ToggleSwitch>();
        status = page.FindName(L"Status").as<TextBlock>();
        chatUnavailable = page.FindName(L"ChatUnavailable").as<TextBlock>();
        sidebarUnavailable = page.FindName(L"SidebarUnavailable").as<TextBlock>();
        auto weak = weak_from_this();
        chat.Toggled([weak](Object const&, RoutedEventArgs const&) {
            if (auto self = weak.lock(); self && self->showing && !self->disposed && !self->syncing) self->Change(dt::Chat, self->chat.IsOn());
        });
        sidebar.Toggled([weak](Object const&, RoutedEventArgs const&) {
            if (auto self = weak.lock(); self && self->showing && !self->disposed && !self->syncing) self->Change(dt::Sidebar, self->sidebar.IsOn());
        });
        page.FindName(L"RestoreButtons").as<Button>().Click([weak](Object const&, RoutedEventArgs const&) {
            if (auto self = weak.lock()) self->Write(0);
        });
    }
    void Show() noexcept {
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
            chat.Focus(FocusState::Programmatic);
            lastError = S_OK;
        } catch (...) {
            lastError = to_hresult();
            Hide();
        }
    }
    void Hide() noexcept {
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
        } catch (...) { lastError = to_hresult(); }
        syncing = wasSyncing;
        overlayParent = {}; hiddenBranch = {};
        originalVisibility = nullptr; visibilityBinding = nullptr;
    }
    void Refresh() noexcept {
        if (!page || disposed) return;
        syncing = true;
        try {
            unsigned flags = api.read(api.context);
            chat.IsOn((flags & dt::Chat) != 0); sidebar.IsOn((flags & dt::Sidebar) != 0);
            bool chatReady = api.available(api.context, dt::Chat);
            bool sidebarReady = api.available(api.context, dt::Sidebar);
            chat.IsEnabled(chatReady); sidebar.IsEnabled(sidebarReady);
            chatUnavailable.Visibility(chatReady ? Visibility::Collapsed : Visibility::Visible);
            sidebarUnavailable.Visibility(sidebarReady ? Visibility::Collapsed : Visibility::Visible);
        } catch (...) { lastError = to_hresult(); }
        syncing = false;
    }
    void Change(unsigned target, bool enabled) noexcept {
        unsigned flags = api.read(api.context);
        Write(enabled ? flags | target : flags & ~target);
    }
    void Write(unsigned flags) noexcept {
        if (disposed || !showing || syncing) return;
        try {
            HRESULT hr = api.write(api.context, flags);
            Refresh();
            status.Text(SUCCEEDED(hr) ? (flags ? L"Changes applied and saved." : L"All tweaks are off.")
                                     : L"Couldn’t apply or save that change. Previous settings restored.");
        } catch (...) { lastError = to_hresult(); Refresh(); }
    }
    void Dispose() noexcept {
        if (disposed) return;
        disposed = true; Hide();
        try {
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
        } catch (...) { lastError = to_hresult(); }
        installed = false;
        page = nullptr; chat = nullptr; sidebar = nullptr; entry = nullptr;
        footer = nullptr; originalFooter = nullptr; status = nullptr;
        chatUnavailable = nullptr; sidebarUnavailable = nullptr;
    }
};
std::vector<std::shared_ptr<Settings>> settings;
}

void dt::ObserveSettings(IInspectable* object, SettingsApi api) noexcept {
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
    for (auto const& item : settings) { item->TryInstall(); item->Refresh(); }
}
LRESULT dt::SettingsStatus(WPARAM field) noexcept {
    if (field == SettingsCandidates) return static_cast<LRESULT>(settings.size());
    if (field == SettingsLastError) return lastError;
    if (field == SettingsInstalled) return static_cast<LRESULT>(std::count_if(settings.begin(), settings.end(), [](auto const& item) { return item->installed; }));
    return 0;
}
