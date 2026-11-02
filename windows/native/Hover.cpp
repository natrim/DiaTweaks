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
#include <cmath>
#include <limits>

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
struct ElementLease {
    weak_ref<FrameworkElement> element;
    Object height{nullptr}, minimum{nullptr};
    bool ownsHeight = false, ownsMinimum = false;
    HRESULT Restore() noexcept {
        try {
            auto item = element.get();
            if (!item) { ownsHeight = ownsMinimum = false; return S_OK; }
            if (ownsHeight) {
                auto local = item.ReadLocalValue(FrameworkElement::HeightProperty());
                if (local.try_as<Windows::Foundation::IReference<double>>() && std::isnan(item.Height())) item.SetValue(FrameworkElement::HeightProperty(), height);
                ownsHeight = false;
            }
            if (ownsMinimum) {
                auto local = item.ReadLocalValue(FrameworkElement::MinHeightProperty());
                if (local.try_as<Windows::Foundation::IReference<double>>() && item.MinHeight() == 0) item.SetValue(FrameworkElement::MinHeightProperty(), minimum);
                ownsMinimum = false;
            }
            return S_OK;
        } catch (...) { return to_hresult(); }
    }
    ~ElementLease() { Restore(); }
};
struct Region {
    weak_ref<FrameworkElement> pane, toolbar, bookmarks, page, owner;
    std::vector<std::unique_ptr<RowLease>> rows;
    std::vector<std::unique_ptr<ElementLease>> containers;
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

namespace {
template<class Leases, class Keep>
HRESULT RestoreExcept(Leases& leases, Keep keep) {
    HRESULT error = S_OK;
    for (auto it=leases.begin(); it!=leases.end();) {
        if (!keep(**it)) {
            auto hr=(*it)->Restore();
            if (SUCCEEDED(hr)) { it=leases.erase(it); continue; }
            error=hr;
        }
        ++it;
    }
    return error;
}
HRESULT CompactContainer(Region& region, FrameworkElement const& item) {
    for (auto it=region.containers.begin(); it!=region.containers.end(); ++it) if ((*it)->element.get()==item) {
        if ((!(*it)->ownsHeight || std::isnan(item.Height())) && (!(*it)->ownsMinimum || item.MinHeight()==0)) return S_OK;
        auto hr=(*it)->Restore(); if (FAILED(hr)) return hr;
        region.containers.erase(it); break;
    }
    auto height=item.ReadLocalValue(FrameworkElement::HeightProperty());
    auto minimum=item.ReadLocalValue(FrameworkElement::MinHeightProperty());
    bool setHeight=std::isfinite(item.Height()) && item.Height()>0 && item.Height()<=256 && height.try_as<Windows::Foundation::IReference<double>>();
    bool setMinimum=item.MinHeight()>0 && item.MinHeight()<=256 && minimum.try_as<Windows::Foundation::IReference<double>>();
    if (!setHeight && !setMinimum) return S_OK;
    auto lease=std::make_unique<ElementLease>(); lease->element=item; lease->height=height; lease->minimum=minimum;
    auto target=lease.get(); region.containers.push_back(std::move(lease));
    if (setHeight) { target->ownsHeight=true; item.Height((std::numeric_limits<double>::quiet_NaN)()); }
    if (setMinimum) { target->ownsMinimum=true; item.MinHeight(0); }
    return S_OK;
}
HRESULT CompactRow(Region& region, RowDefinition const& row) {
    for (auto it=region.rows.begin(); it!=region.rows.end(); ++it) if ((*it)->row.get()==row) {
        if ((!(*it)->ownsHeight || row.Height().GridUnitType==GridUnitType::Auto) && (!(*it)->ownsMinimum || row.MinHeight()==0)) return S_OK;
        auto hr=(*it)->Restore(); if (FAILED(hr)) return hr;
        region.rows.erase(it); break;
    }
    auto value=row.Height();
    if (value.GridUnitType!=GridUnitType::Pixel && value.GridUnitType!=GridUnitType::Auto) return S_OK;
    bool setHeight=value.GridUnitType==GridUnitType::Pixel && value.Value>0 && value.Value<=256;
    bool setMinimum=row.MinHeight()>0 && row.MinHeight()<=256;
    if (!setHeight && !setMinimum) return S_OK;
    auto height=row.ReadLocalValue(RowDefinition::HeightProperty()), minimum=row.ReadLocalValue(RowDefinition::MinHeightProperty());
    if ((setHeight && !height.try_as<Windows::Foundation::IReference<GridLength>>()) ||
        (setMinimum && !minimum.try_as<Windows::Foundation::IReference<double>>())) return S_OK;
    auto lease=std::make_unique<RowLease>(); lease->row=row; lease->height=height; lease->minimum=minimum;
    auto target=lease.get(); region.rows.push_back(std::move(lease));
    if (setHeight) { target->ownsHeight=true; row.Height({1,GridUnitType::Auto}); }
    if (setMinimum) { target->ownsMinimum=true; row.MinHeight(0); }
    return S_OK;
}
}
HRESULT dt::CompactToolbarRow(std::shared_ptr<void> const& value, bool compact) noexcept {
    if (!value) return S_OK;
    auto region=static_cast<Region*>(value.get());
    try {
        if (!compact) {
            auto row=RestoreExcept(region->rows,[](auto const&){return false;});
            auto wrapper=RestoreExcept(region->containers,[](auto const&){return false;});
            return FAILED(row)?row:wrapper;
        }
        auto host=region->owner.get(), pane=region->pane.get();
        if (!host || host.Visibility()!=Visibility::Collapsed) return S_OK;
        std::vector<RowDefinition> visitedRows;
        std::vector<FrameworkElement> visitedContainers;
        FrameworkElement branch=host;
        for (unsigned depth=0; depth<8; ++depth) {
            auto parent=VisualTreeHelper::GetParent(branch);
            if (!parent) break;
            auto element=parent.try_as<FrameworkElement>();
            if (!element) break;
            if (auto grid=parent.try_as<Grid>()) {
                int index=Grid::GetRow(branch);
                bool exclusive=Grid::GetRowSpan(branch)==1;
                for (auto const& child:grid.Children()) {
                    if (child==branch || child.Visibility()==Visibility::Collapsed) continue;
                    auto other=child.try_as<FrameworkElement>();
                    if (!other) { exclusive=false; break; }
                    if (other.DesiredSize().Height<=0 && other.ActualHeight()<=0) continue;
                    if (other.DesiredSize().Height<=0 && other.Name()==L"BackgroundBorder")
                        if (auto border=other.try_as<Border>(); border && !border.Child()) continue;
                    int first=Grid::GetRow(other), last=first+Grid::GetRowSpan(other);
                    if(first<=index && index<last) { exclusive=false; break; }
                }
                if (exclusive && index>=0 && static_cast<unsigned>(index)<grid.RowDefinitions().Size()) {
                    auto row=grid.RowDefinitions().GetAt(static_cast<unsigned>(index));
                    visitedRows.push_back(row);
                    auto hr=CompactRow(*region,row); if (FAILED(hr)) return hr;
                }
                // An implicit auto row is not the end of the layout chain.
            }
            if (element==pane) break;
            if (element.try_as<Grid>() || element.try_as<Border>() || element.try_as<StackPanel>() || element.try_as<ContentPresenter>()) {
                visitedContainers.push_back(element);
                auto hr=CompactContainer(*region,element); if (FAILED(hr)) return hr;
            }
            branch=element;
        }
        auto rows=RestoreExcept(region->rows,[&](auto const& lease){return std::find(visitedRows.begin(),visitedRows.end(),lease.row.get())!=visitedRows.end();});
        auto containers=RestoreExcept(region->containers,[&](auto const& lease){return std::find(visitedContainers.begin(),visitedContainers.end(),lease.element.get())!=visitedContainers.end();});
        return FAILED(rows)?rows:containers;
    } catch (...) { return to_hresult(); }
}

HRESULT dt::FlushToolbarLayout(std::shared_ptr<void> const& value) noexcept {
    if (!value) return S_OK;
    try {
        auto region = static_cast<Region*>(value.get());
        auto pane = region->pane.get();
        if (!pane || !pane.XamlRoot()) return S_OK;
        // A tab can replace VariantHost or its native-surface presenter while
        // keeping the toolbar alive. Never flush the previous tab's page.
        auto page = Named(pane, L"VariantHost");
        region->page = page;
        if (page) {
            struct Item { DependencyObject node; unsigned depth; };
            std::vector<Item> queue{{page, 0}};
            for (size_t i = 0; i < queue.size() && i < 96; ++i) {
                auto item = queue[i];
                if (auto element = item.node.try_as<FrameworkElement>()) {
                    element.InvalidateMeasure();
                    element.InvalidateArrange();
                }
                if (item.depth >= 5) continue;
                int count = VisualTreeHelper::GetChildrenCount(item.node);
                for (int j = 0; j < count && queue.size() < 96; ++j)
                    queue.push_back({VisualTreeHelper::GetChild(item.node, j), item.depth + 1});
            }
        }
        // Visibility and row policies have already been applied as a batch.
        // Arrange the current page now instead of waiting for the polling tick
        // to catch up with the newly inserted native viewport.
        pane.UpdateLayout();
        return S_OK;
    } catch (...) { return to_hresult(); }
}
