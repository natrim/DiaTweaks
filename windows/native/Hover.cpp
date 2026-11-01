#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#ifdef GetCurrentTime
#undef GetCurrentTime
#endif
#include "HoverHost.h"
#include <winrt/Microsoft.UI.Content.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Interop.h>
#include <winrt/Windows.Foundation.h>
#include <vector>
#include <algorithm>

namespace {
using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using Microsoft::UI::Xaml::Media::VisualTreeHelper;
using Windows::Foundation::Rect;
struct Region {
    weak_ref<FrameworkElement> pane, toolbar, bookmarks;
    double height = 56;
};
FrameworkElement Named(DependencyObject root, wchar_t const* name) {
    struct Visit { DependencyObject node; unsigned depth; };
    std::vector<Visit> queue{{root, 0}};
    for (size_t i = 0; i < queue.size() && i < 96; ++i) {
        auto item = queue[i];
        if (auto element = item.node.try_as<FrameworkElement>(); element && element.Name() == name) return element;
        if (item.depth >= 3) continue;
        int count = VisualTreeHelper::GetChildrenCount(item.node);
        for (int j = 0; j < count && queue.size() < 96; ++j)
            queue.push_back({VisualTreeHelper::GetChild(item.node, j), item.depth + 1});
    }
    return nullptr;
}
Rect Bounds(FrameworkElement const& element, UIElement const& root) {
    return element.TransformToVisual(root).TransformBounds({0, 0,
        static_cast<float>(element.ActualWidth()), static_cast<float>(element.ActualHeight())});
}
bool Inside(DependencyObject item, FrameworkElement const& ancestor) {
    for (unsigned depth = 0; item && depth < 64; ++depth, item = VisualTreeHelper::GetParent(item))
        if (item == ancestor) return true;
    return false;
}
}
std::shared_ptr<void> dt::TrackToolbar(IInspectable* object) noexcept {
    try {
        Windows::Foundation::IInspectable instance{nullptr};
        copy_from_abi(instance, object);
        auto host = instance.try_as<FrameworkElement>();
        if (!host || !host.XamlRoot()) return {};
        FrameworkElement pane{nullptr};
        DependencyObject current = host;
        for (unsigned depth = 0; current && depth < 12; ++depth, current = VisualTreeHelper::GetParent(current)) {
            auto element = current.try_as<FrameworkElement>();
            if (element && Named(current, L"VariantHost") && Named(current, L"NavigationBarHost")) { pane = element; break; }
        }
        if (!pane) return {};
        auto toolbar = Named(pane, L"NavigationBarHost");
        if (!toolbar) return {};
        // Validate the window mapping before offering the switch.
        auto hwnd = Microsoft::UI::GetWindowFromWindowId(host.XamlRoot().ContentIslandEnvironment().AppWindowId());
        if (!hwnd || !IsWindow(hwnd)) return {};
        auto region = std::make_shared<Region>();
        region->pane = pane; region->toolbar = toolbar;
        region->bookmarks = Named(pane, L"BookmarksBarHost");
        if (toolbar.ActualHeight() > 0) region->height = toolbar.ActualHeight();
        return region;
    } catch (...) { return {}; }
}
dt::HoverSample dt::ReadToolbarHover(std::shared_ptr<void> const& value, bool revealed) noexcept {
    try {
        if (!value) return {};
        auto region = static_cast<Region*>(value.get());
        auto pane = region->pane.get(), toolbar = region->toolbar.get();
        if (!pane || !toolbar || !pane.XamlRoot()) return {};
        auto root = pane.XamlRoot();
        auto content = root.Content();
        auto hwnd = Microsoft::UI::GetWindowFromWindowId(root.ContentIslandEnvironment().AppWindowId());
        if (!hwnd || !IsWindow(hwnd) || !content) return {};
        HoverSample sample{true};
        sample.active = IsWindowVisible(hwnd) && !IsIconic(hwnd) && GetAncestor(GetForegroundWindow(), GA_ROOT) == hwnd;
        if (!sample.active) return sample;
        auto bounds = Bounds(pane, content);
        if (bounds.Width <= 0 || bounds.Height <= 0) return {};
        if (revealed && toolbar.ActualHeight() > 0) {
            auto bar = Bounds(toolbar, content);
            region->height = bar.Y + bar.Height - bounds.Y;
            if (auto bookmarks = region->bookmarks.get(); bookmarks && bookmarks.Visibility() == Visibility::Visible && bookmarks.ActualHeight() > 0) {
                auto mark = Bounds(bookmarks, content);
                region->height = (std::max)(region->height, static_cast<double>(mark.Y + mark.Height - bounds.Y));
            }
        }
        POINT cursor{};
        if (GetCursorPos(&cursor)) {
            HWND under = GetAncestor(WindowFromPoint(cursor), GA_ROOT);
            if (under == hwnd && ScreenToClient(hwnd, &cursor)) {
                double scale = root.RasterizationScale();
                if (scale > 0) sample.pointer = InHoverBand(cursor.x / scale, cursor.y / scale,
                    bounds.X, bounds.Y, bounds.Width, bounds.Height, region->height, revealed);
            }
        }
        if (revealed) {
            auto focused = Input::FocusManager::GetFocusedElement(root).try_as<DependencyObject>();
            sample.focus = focused && (Inside(focused, toolbar) ||
                (region->bookmarks.get() && Inside(focused, region->bookmarks.get())));
        }
        return sample;
    } catch (...) { return {}; }
}
