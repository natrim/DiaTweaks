#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#ifdef GetCurrentTime
#undef GetCurrentTime
#endif
#include "Layout.h"
#include <winrt/Microsoft.UI.Content.h>
#include <winrt/Microsoft.UI.Composition.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Xaml.Hosting.h>
#include <winrt/Microsoft.UI.Interop.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <sstream>
#include <iomanip>
#include <vector>
namespace {
using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using Microsoft::UI::Xaml::Media::VisualTreeHelper;
using Microsoft::UI::Xaml::Hosting::ElementCompositionPreview;
void Insets(std::wostream& out, Thickness t) { out << t.Left << L',' << t.Top << L',' << t.Right << L',' << t.Bottom; }
void Corners(std::wostream& out, CornerRadius r) { out << r.TopLeft << L',' << r.TopRight << L',' << r.BottomRight << L',' << r.BottomLeft; }
bool Known(std::wstring_view name) {
    for (auto key : {L"VariantHost", L"NavigationBarHost", L"BookmarksBarHost", L"NavigationChromeHost", L"InfoBarHost",
                     L"SidebarPresenter", L"SpacesScrollViewer", L"SidebarBorder", L"RootGrid", L"WindowDragHeader"}) if (name == key) return true;
    return false;
}
void Composition(std::wostream& out, Microsoft::UI::Composition::Visual const& visual, unsigned depth = 0) {
    if (!visual || depth > 2) return;
    auto size = visual.Size(); auto offset = visual.Offset();
    out << L"      visual " << get_class_name(visual).c_str() << L" size=" << size.x << L',' << size.y
        << L" offset=" << offset.x << L',' << offset.y << L',' << offset.z;
    if (auto clip = visual.Clip()) {
        out << L" clip=" << get_class_name(clip).c_str();
        if (auto rect = clip.try_as<Microsoft::UI::Composition::RectangleClip>()) {
            auto tl = rect.TopLeftRadius(), tr = rect.TopRightRadius(), br = rect.BottomRightRadius(), bl = rect.BottomLeftRadius();
            out << L" radii=" << tl.x << L',' << tl.y << L';' << tr.x << L',' << tr.y << L';' << br.x << L',' << br.y << L';' << bl.x << L',' << bl.y;
        } else if (auto geometric = clip.try_as<Microsoft::UI::Composition::CompositionGeometricClip>()) {
            if (auto geometry = geometric.Geometry()) {
                out << L" geometry=" << get_class_name(geometry).c_str();
                if (auto rounded = geometry.try_as<Microsoft::UI::Composition::CompositionRoundedRectangleGeometry>()) {
                    auto radius = rounded.CornerRadius(), extent = rounded.Size();
                    out << L" radius=" << radius.x << L',' << radius.y << L" geometrySize=" << extent.x << L',' << extent.y;
                }
            }
        } else if (auto inset = clip.try_as<Microsoft::UI::Composition::InsetClip>()) {
            out << L" insets=" << inset.LeftInset() << L',' << inset.TopInset() << L',' << inset.RightInset() << L',' << inset.BottomInset();
        }
    }
    out << L'\n';
    if (auto container = visual.try_as<Microsoft::UI::Composition::ContainerVisual>()) {
        unsigned count = 0;
        for (auto const& child : container.Children()) { if (++count > 8) break; Composition(out, child, depth + 1); }
    }
}
void Element(std::wostream& out, DependencyObject const& node, UIElement const& root, wchar_t const* role, unsigned depth) {
    try {
        auto item = node.try_as<FrameworkElement>();
        if (!item) return;
        auto name = item.Name();
        auto bounds = item.TransformToVisual(root).TransformBounds({0,0,static_cast<float>(item.ActualWidth()),static_cast<float>(item.ActualHeight())});
        out << role << L'[' << depth << L"] " << get_class_name(item).c_str() << L" name="
            << (Known(name) ? name.c_str() : name.empty() ? L"(unnamed)" : L"(other)")
            << L" rect=" << bounds.X << L',' << bounds.Y << L',' << bounds.Width << L',' << bounds.Height
            << L" visibility=" << static_cast<int>(item.Visibility()) << L" alignment=" << static_cast<int>(item.HorizontalAlignment()) << L',' << static_cast<int>(item.VerticalAlignment())
            << L" size=" << item.Width() << L',' << item.Height() << L" min=" << item.MinWidth() << L',' << item.MinHeight() << L" margin=";
        Insets(out, item.Margin());
        out << L" localHeight=" << get_class_name(item.ReadLocalValue(FrameworkElement::HeightProperty())).c_str() << L'\n';
        DependencyProperty padding{nullptr}, corner{nullptr};
        if (item.try_as<Border>()) { padding=Border::PaddingProperty(); corner=Border::CornerRadiusProperty(); }
        else if (item.try_as<Grid>()) { padding=Grid::PaddingProperty(); corner=Grid::CornerRadiusProperty(); }
        else if (item.try_as<ContentPresenter>()) { padding=ContentPresenter::PaddingProperty(); corner=ContentPresenter::CornerRadiusProperty(); }
        if (padding) { out << L"    padding="; Insets(out, unbox_value<Thickness>(item.GetValue(padding))); }
        if (corner) { out << L" cornerRadius="; Corners(out, unbox_value<CornerRadius>(item.GetValue(corner))); }
        if (padding || corner) out << L'\n';
        if (auto grid = item.try_as<Grid>()) {
            unsigned index = 0;
            for (auto const& row : grid.RowDefinitions()) {
                if (index >= 16) break;
                auto height = row.Height();
                out << L"    row " << index++ << L" unit=" << static_cast<int>(height.GridUnitType) << L" height=" << height.Value
                    << L" min=" << row.MinHeight() << L" actual=" << row.ActualHeight() << L'\n';
            }
        }
        if (auto clip = item.Clip()) out << L"    xamlClip=" << get_class_name(clip).c_str() << L'\n';
        try { Composition(out, ElementCompositionPreview::GetElementVisual(item)); }
        catch (...) { out << L"    visual unavailable\n"; }
    } catch (...) { out << role << L'[' << depth << L"] unavailable hr=0x" << std::hex << static_cast<unsigned long>(to_hresult()) << std::dec << L'\n'; }
}
}
std::wstring dt::LayoutSnapshot(IInspectable* object) {
    Windows::Foundation::IInspectable instance{nullptr}; copy_from_abi(instance, object);
    auto host = instance.try_as<FrameworkElement>();
    if (!host || !host.XamlRoot()) return L"Layout root unavailable.\n";
    auto root = host.XamlRoot().Content();
    std::wostringstream out; out << std::fixed << std::setprecision(2);
    out << L"Scale=" << host.XamlRoot().RasterizationScale() << L" (logical coordinates; no page text or URLs)\n";
    DependencyObject ancestor = host;
    for (unsigned depth=0; ancestor && depth<20; ++depth, ancestor=VisualTreeHelper::GetParent(ancestor)) Element(out, ancestor, root, L"ancestor", depth);
    struct Visit { DependencyObject node; unsigned depth; };
    std::vector<Visit> queue{{host,0}};
    for (size_t i=0; i<queue.size() && i<96; ++i) {
        auto item=queue[i];
        Element(out,item.node,root,L"child",item.depth);
        if(item.depth>=4) continue;
        int count=VisualTreeHelper::GetChildrenCount(item.node);
        for(int j=0; j<count && queue.size()<96; ++j) queue.push_back({VisualTreeHelper::GetChild(item.node,j),item.depth+1});
    }
    auto hwnd=Microsoft::UI::GetWindowFromWindowId(host.XamlRoot().ContentIslandEnvironment().AppWindowId());
    struct ChildReport { std::wostream* out; HWND parent; unsigned count=0; } report{&out,hwnd};
    EnumChildWindows(hwnd, [](HWND child, LPARAM data)->BOOL {
        auto state=reinterpret_cast<ChildReport*>(data);
        if(++state->count>64) return FALSE;
        wchar_t cls[160]{}; GetClassNameW(child,cls,static_cast<int>(std::size(cls)));
        RECT rect{}; GetWindowRect(child,&rect);
        POINT origin{rect.left,rect.top}; ScreenToClient(state->parent,&origin);
        *state->out << L"HWND class=" << cls << L" physicalRect=" << origin.x << L',' << origin.y << L',' << rect.right-rect.left << L',' << rect.bottom-rect.top
                    << L" visible=" << IsWindowVisible(child) << L'\n';
        return TRUE;
    },reinterpret_cast<LPARAM>(&report));
    return out.str();
}
