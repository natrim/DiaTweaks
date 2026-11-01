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
#include <winrt/Microsoft.UI.Xaml.Data.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Interop.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <vector>
#include <algorithm>

namespace {
using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using Microsoft::UI::Xaml::Media::VisualTreeHelper;
using Windows::Foundation::Rect;
using Object = Windows::Foundation::IInspectable;
struct RowLease {
    weak_ref<RowDefinition> row;
    Object height{nullptr}, minimum{nullptr};
    bool ownsHeight = false, ownsMinimum = false;
    ~RowLease() { Restore(); }
    HRESULT Restore() noexcept {
        try {
            auto definition = row.get();
            if (!definition) { ownsHeight = ownsMinimum = false; return S_OK; }
            if (ownsHeight) {
                auto value = definition.ReadLocalValue(RowDefinition::HeightProperty());
                if (value.try_as<Windows::Foundation::IReference<GridLength>>() && definition.Height().GridUnitType == GridUnitType::Auto)
                    definition.SetValue(RowDefinition::HeightProperty(), height);
                ownsHeight = false;
            }
            if (ownsMinimum) {
                auto value = definition.ReadLocalValue(RowDefinition::MinHeightProperty());
                if (value.try_as<Windows::Foundation::IReference<double>>() && definition.MinHeight() == 0)
                    definition.SetValue(RowDefinition::MinHeightProperty(), minimum);
                ownsMinimum = false;
            }
            return S_OK;
        } catch (...) { return to_hresult(); }
    }
};
struct Region {
    weak_ref<FrameworkElement> pane, toolbar, bookmarks, page, owner;
    std::unique_ptr<RowLease> layout;
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
        region->pane = pane; region->toolbar = toolbar; region->owner = host;
        region->page = Named(pane, L"VariantHost");
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
        double pageOffset = 0;
        if (auto page = region->page.get(); page && page.ActualHeight() > 0)
            pageOffset = (std::max)(0.0, static_cast<double>(Bounds(page, content).Y - bounds.Y));
        POINT cursor{};
        if (GetCursorPos(&cursor)) {
            HWND under = GetAncestor(WindowFromPoint(cursor), GA_ROOT);
            if (under == hwnd && ScreenToClient(hwnd, &cursor)) {
                double scale = root.RasterizationScale();
                if (scale > 0) sample.pointer = InHoverBand(cursor.x / scale, cursor.y / scale,
                    bounds.X, bounds.Y, bounds.Width, bounds.Height, region->height, revealed, pageOffset);
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

HRESULT dt::CompactToolbarRow(std::shared_ptr<void> const& value, bool compact) noexcept {
    if (!value) return S_OK;
    auto region = static_cast<Region*>(value.get());
    if (!compact) {
        if (!region->layout) return S_OK;
        HRESULT hr = region->layout->Restore();
        if (SUCCEEDED(hr)) region->layout.reset();
        return hr;
    }
    if (region->layout) return S_OK;
    try {
        auto host = region->owner.get();
        if (!host || host.Visibility() != Visibility::Collapsed) return S_OK;
        FrameworkElement branch = host;
        for (unsigned depth = 0; depth < 4; ++depth) {
            auto parent = VisualTreeHelper::GetParent(branch);
            if (!parent) return S_OK;
            if (auto grid = parent.try_as<Grid>()) {
                int index = Grid::GetRow(branch);
                if (index < 0 || Grid::GetRowSpan(branch) != 1 || static_cast<unsigned>(index) >= grid.RowDefinitions().Size()) return S_OK;
                for (auto const& child : grid.Children()) {
                    if (child.Visibility() == Visibility::Collapsed) continue;
                    auto element = child.try_as<FrameworkElement>();
                    if (!element) return S_OK;
                    if (element == branch) continue;
                    int first = Grid::GetRow(element), last = first + Grid::GetRowSpan(element);
                    if (first <= index && index < last) return S_OK;
                }
                auto row = grid.RowDefinitions().GetAt(static_cast<unsigned>(index));
                auto height = row.Height();
                if (height.GridUnitType != GridUnitType::Pixel && height.GridUnitType != GridUnitType::Auto) return S_OK;
                bool changeHeight = height.GridUnitType == GridUnitType::Pixel && height.Value > 0;
                bool changeMinimum = row.MinHeight() > 0;
                if (!changeHeight && !changeMinimum) return S_OK;
                auto originalHeight = row.ReadLocalValue(RowDefinition::HeightProperty());
                auto originalMinimum = row.ReadLocalValue(RowDefinition::MinHeightProperty());
                // Primitive local values only. Never replace an app binding or
                // infer a baseline from an evaluated value.
                if (changeHeight && !originalHeight.try_as<Windows::Foundation::IReference<GridLength>>()) return S_OK;
                if (changeMinimum && !originalMinimum.try_as<Windows::Foundation::IReference<double>>()) return S_OK;
                region->layout = std::make_unique<RowLease>();
                region->layout->row = row;
                region->layout->height = originalHeight;
                region->layout->minimum = originalMinimum;
                if (changeHeight) {
                    region->layout->ownsHeight = true;
                    row.Height({1, GridUnitType::Auto});
                }
                if (changeMinimum) {
                    region->layout->ownsMinimum = true;
                    row.MinHeight(0);
                }
                return S_OK;
            }
            auto element = parent.try_as<FrameworkElement>();
            if (!element) return S_OK;
            branch = element;
        }
        return S_OK;
    } catch (...) { return to_hresult(); }
}
