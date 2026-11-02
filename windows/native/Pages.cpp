#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#ifdef GetCurrentTime
#undef GetCurrentTime
#endif
#include "PageHost.h"
#include "PagePolicy.h"
#include "Rules.h"
#include "Layout.h"
#include <sstream>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <vector>
#include <algorithm>
#include <functional>

namespace {
using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using Microsoft::UI::Xaml::Media::VisualTreeHelper;
using Object = Windows::Foundation::IInspectable;
using Rect = Windows::Foundation::Rect;
bool Same(Thickness a, Thickness b) { return a.Left == b.Left && a.Top == b.Top && a.Right == b.Right && a.Bottom == b.Bottom; }
bool Same(CornerRadius a, CornerRadius b) { return a.TopLeft == b.TopLeft && a.TopRight == b.TopRight && a.BottomRight == b.BottomRight && a.BottomLeft == b.BottomLeft; }
Thickness Native(dt::page::Insets a) { return {a.left, a.top, a.right, a.bottom}; }
dt::page::Insets Plain(Thickness a) { return {a.Left, a.Top, a.Right, a.Bottom}; }
dt::page::Corners Plain(CornerRadius a) { return {a.TopLeft, a.TopRight, a.BottomRight, a.BottomLeft}; }
CornerRadius Native(dt::page::Corners a) { return {a.tl, a.tr, a.br, a.bl}; }
struct Lease {
    DependencyObject object{nullptr}; DependencyProperty property{nullptr};
    Object original{nullptr}, baseline{nullptr}, applied{nullptr};
    unsigned feature = 0;
    std::function<bool(Object const&, Object const&)> equal;
    bool Ours() const { return equal(object.ReadLocalValue(property), applied); }
    HRESULT Restore() noexcept {
        try {
            if (Ours()) {
                if (original == DependencyProperty::UnsetValue()) object.ClearValue(property);
                else object.SetValue(property, original);
                auto current = object.ReadLocalValue(property);
                if (original == DependencyProperty::UnsetValue() ? current != original : !equal(current, original)) return E_FAIL;
            }
            return S_OK;
        } catch (...) { return to_hresult(); }
    }
};
struct PageFrame {
    unsigned long long id;
    weak_ref<FrameworkElement> host, pane;
    std::vector<Lease> leases;
    std::vector<weak_ref<FrameworkElement>> sidebars;
    ULONGLONG scanned = 0;
    bool ready = false, corners = false, detached = false;
    HRESULT error = S_OK;
};
struct State {
    dt::PageApi api;
    std::vector<PageFrame> pages;
    unsigned flags = 0;
    HRESULT error = S_OK;
    std::wstring detail;
    bool updating = false;
    unsigned writes = 0;
};
Rect Bounds(FrameworkElement const& item, UIElement const& root) {
    return item.TransformToVisual(root).TransformBounds({0, 0, static_cast<float>(item.ActualWidth()), static_cast<float>(item.ActualHeight())});
}
std::vector<FrameworkElement> Elements(DependencyObject root, unsigned limit, unsigned depthLimit) {
    struct Item { DependencyObject node; unsigned depth; };
    std::vector<Item> queue{{root, 0}}; std::vector<FrameworkElement> found;
    for (size_t i = 0; i < queue.size() && i < limit; ++i) {
        auto item = queue[i];
        if (auto element = item.node.try_as<FrameworkElement>()) found.push_back(element);
        if (item.depth >= depthLimit) continue;
        int count = VisualTreeHelper::GetChildrenCount(item.node);
        for (int j = 0; j < count && queue.size() < limit; ++j) queue.push_back({VisualTreeHelper::GetChild(item.node, j), item.depth + 1});
    }
    return found;
}
FrameworkElement Named(DependencyObject const& root, wchar_t const* name) {
    for (auto const& element : Elements(root, 96, 3)) if (element.Name() == name) return element;
    return nullptr;
}
bool Visible(FrameworkElement const& item) {
    DependencyObject node = item;
    for (unsigned i = 0; node && i < 64; ++i, node = VisualTreeHelper::GetParent(node))
        if (auto ui = node.try_as<UIElement>(); ui && ui.Visibility() != Visibility::Visible) return false;
    return item.ActualWidth() > 32 && item.ActualHeight() > 0;
}
Lease* Find(PageFrame& page, DependencyObject const& object, DependencyProperty const& property) {
    for (auto& lease : page.leases) if (lease.object == object && lease.property == property) return &lease;
    return nullptr;
}
template<class T> T Baseline(PageFrame& page, DependencyObject const& object, DependencyProperty const& property) {
    if (auto lease = Find(page, object, property); lease && lease->Ours()) return unbox_value<T>(lease->baseline);
    return unbox_value<T>(object.GetValue(property));
}
template<class T> bool Equal(Object const& a, Object const& b) {
    auto left = a.try_as<Windows::Foundation::IReference<T>>(), right = b.try_as<Windows::Foundation::IReference<T>>();
    return left && right && Same(left.Value(), right.Value());
}
template<class T> HRESULT Set(State& state, PageFrame& page, DependencyObject const& object,
                             DependencyProperty const& property, wchar_t const* name, T desired, unsigned feature) {
    auto lease = Find(page, object, property);
    if (lease && !lease->Ours()) {
        page.leases.erase(std::remove_if(page.leases.begin(), page.leases.end(), [&](auto const& entry) { return &entry == lease; }), page.leases.end());
        lease = nullptr;
    }
    T baseline = Baseline<T>(page, object, property);
    if (Same(baseline, desired)) {
        if (lease) {
            HRESULT hr = lease->Restore();
            if (FAILED(hr)) return hr;
            page.leases.erase(std::remove_if(page.leases.begin(), page.leases.end(), [&](auto const& entry) { return &entry == lease; }), page.leases.end());
        }
        return S_OK;
    }
    if (!state.api.plain(state.api.context, reinterpret_cast<IInspectable*>(get_abi(object)), name)) {
        state.detail = std::wstring(L"Page ") + name + L" uses an unsupported source or binding.";
        return E_NOTIMPL;
    }
    if (!lease) {
        auto local = object.ReadLocalValue(property);
        if (local != DependencyProperty::UnsetValue() && !local.try_as<Windows::Foundation::IReference<T>>()) return E_NOTIMPL;
        if (page.leases.size() >= 256) return E_OUTOFMEMORY;
        page.leases.push_back({object, property, local, object.GetValue(property), box_value(desired), feature, Equal<T>});
        lease = &page.leases.back();
    }
    lease->feature = feature;
    auto desiredBox = box_value(desired);
    if (Equal<T>(object.GetValue(property), desiredBox) && lease->Ours() && Equal<T>(lease->applied, desiredBox)) return S_OK;
    auto previous = lease->applied;
    lease->applied = desiredBox;
    try { object.SetValue(property, desiredBox); }
    catch (...) {
        if (!Equal<T>(object.ReadLocalValue(property), desiredBox)) lease->applied = previous;
        throw;
    }
    ++state.writes;
    if (!Equal<T>(object.GetValue(property), lease->applied)) return E_FAIL;
    return S_OK;
}
HRESULT Restore(PageFrame& page) {
    HRESULT error = S_OK;
    for (auto it = page.leases.rbegin(); it != page.leases.rend(); ++it) {
        auto hr = it->Restore(); if (FAILED(hr)) error = hr;
    }
    if (SUCCEEDED(error)) page.leases.clear();
    return error;
}
dt::page::Box Plain(Rect a) { return {a.X, a.Y, a.Width, a.Height}; }
DependencyProperty RadiusProperty(FrameworkElement const& element) {
    if (element.try_as<Border>()) return Border::CornerRadiusProperty();
    if (element.try_as<Grid>()) return Grid::CornerRadiusProperty();
    if (element.try_as<ContentPresenter>()) return ContentPresenter::CornerRadiusProperty();
    return nullptr;
}
std::vector<FrameworkElement> Frames(PageFrame& page, FrameworkElement const& host, UIElement const& root) {
    auto frames = Elements(host, 96, 5);
    std::vector<FrameworkElement> ancestors;
    std::vector<dt::page::FrameCandidate> candidates;
    DependencyObject parent = VisualTreeHelper::GetParent(host);
    // The toolbar/page pane is inside the rounded Border and inset Grid.
    // Include those ancestors, stopping at the outer inset or a wider app shell.
    for (unsigned i = 0; parent && parent != root && i < 20; ++i, parent = VisualTreeHelper::GetParent(parent)) {
        auto element = parent.try_as<FrameworkElement>();
        if (!element) continue;
        auto margin = Baseline<Thickness>(page, element, FrameworkElement::MarginProperty());
        ancestors.push_back(element);
        candidates.push_back({Plain(Bounds(element, root)), Plain(margin)});
    }
    auto count = dt::page::FrameCount(Plain(Bounds(host, root)), candidates);
    frames.insert(frames.end(), ancestors.begin(), ancestors.begin() + count);
    return frames;
}
bool Initialize(PageFrame& page) {
    auto host = page.host.get();
    if (!host || !host.XamlRoot() || host.ActualWidth() <= 0 || host.ActualHeight() <= 0) return false;
    if (!page.pane.get()) {
        DependencyObject node = host;
        for (unsigned i = 0; node && i < 12; ++i, node = VisualTreeHelper::GetParent(node))
            if (auto element = node.try_as<FrameworkElement>(); element && Named(node, L"NavigationBarHost") && Named(node, L"VariantHost") == host) { page.pane = element; break; }
    }
    if (!page.pane.get()) return false;
    page.corners = false;
    for (auto const& element : Frames(page, host, host.XamlRoot().Content()))
        if (auto property = RadiusProperty(element); property &&
            dt::page::HasRadius(Plain(Baseline<CornerRadius>(page, element, property)))) page.corners = true;
    return true;
}
HRESULT Apply(State& state, PageFrame& page) {
    if (page.detached) return Restore(page);
    bool edge = (state.flags & dt::EdgeToEdge) != 0;
    bool hiddenToolbar = (state.flags & dt::Toolbar) != 0;
    if (!edge && !hiddenToolbar) {
        auto hr = Restore(page);
        page.ready = Initialize(page);
        return hr;
    }
    page.ready = Initialize(page);
    if (!page.ready) return S_OK;
    auto host = page.host.get();
    auto root = host.XamlRoot().Content();
    auto rootElement = root.try_as<FrameworkElement>();
    if (!rootElement) { page.ready = false; return S_OK; }
    auto frame = Bounds(host, root);
    if (frame.Width <= 0 || frame.Height <= 0) { page.ready = false; return S_OK; }
    auto frames = Frames(page, host, root);
    for (auto const& item : frames) {
        auto type = get_class_name(item);
        auto rect = Bounds(item, root);
        if ((std::wstring_view(type).find(L"Chat") != std::wstring_view::npos || std::wstring_view(type).find(L"Assistant") != std::wstring_view::npos) &&
            rect.Width >= frame.Width * .9 && rect.Height >= frame.Height * .9) { page.ready = false; return Restore(page); }
    }
    HRESULT result = S_OK;
    // Replaced tab content must not keep stale frame objects or their overrides.
    for (auto it = page.leases.begin(); it != page.leases.end();) {
        bool present = it->object == host || std::any_of(frames.begin(), frames.end(), [&](auto const& item) {
            auto rect = Bounds(item, root);
            return it->object == item && rect.Width >= frame.Width * .9 && rect.Height >= frame.Height * .9;
        });
        if (!present) {
            auto hr = it->Restore();
            if (SUCCEEDED(hr)) { it = page.leases.erase(it); continue; }
            result = hr;
        }
        ++it;
    }
    unsigned before = state.writes;
    for (auto const& element : frames) {
        auto rect = Bounds(element, root);
        if (rect.Width < frame.Width * .9 || rect.Height < frame.Height * .9) continue;
        // Let the existing stretch layout resize the native surface. Never
        // compensate with negative VariantHost margins or move child HWNDs.
        if (element.HorizontalAlignment() == HorizontalAlignment::Stretch &&
            element.VerticalAlignment() == VerticalAlignment::Stretch &&
            std::isnan(element.Width()) && std::isnan(element.Height())) {
            auto property = FrameworkElement::MarginProperty();
            auto value = Baseline<Thickness>(page, element, property);
            auto desired = dt::page::FrameMargin(Plain(value), edge, hiddenToolbar);
            unsigned owners = (edge ? dt::EdgeToEdge : 0u) | (hiddenToolbar ? dt::Toolbar : 0u);
            auto hr = Set(state, page, element, property, L"Margin", Native(desired), owners);
            if (FAILED(hr) && edge) result = hr;
        }
        DependencyProperty padding{nullptr};
        if (element.try_as<Border>()) padding = Border::PaddingProperty();
        else if (element.try_as<Grid>()) padding = Grid::PaddingProperty();
        else if (element.try_as<ContentPresenter>()) padding = ContentPresenter::PaddingProperty();
        if (padding) {
            auto value = Baseline<Thickness>(page, element, padding);
            auto hr = Set(state, page, element, padding, L"Padding", edge ? Native(dt::page::Strip(Plain(value))) : value, dt::EdgeToEdge);
            if (FAILED(hr)) result = hr;
        }
    }
    if (state.writes != before) rootElement.UpdateLayout();
    frame = Bounds(host, root);
    auto base = Baseline<Thickness>(page, host, FrameworkElement::MarginProperty());
    auto current = host.Margin();
    Rect original = frame;
    original.X -= static_cast<float>(current.Left - base.Left);
    original.Y -= static_cast<float>(current.Top - base.Top);
    original.Width += static_cast<float>((current.Left - base.Left) + (current.Right - base.Right));
    original.Height += static_cast<float>((current.Top - base.Top) + (current.Bottom - base.Bottom));
    bool cachedVisible = std::any_of(page.sidebars.begin(), page.sidebars.end(), [](auto const& weak) { auto item = weak.get(); return item && Visible(item); });
    if (page.scanned == 0 || (!cachedVisible && GetTickCount64() - page.scanned > 1000)) {
        page.scanned = GetTickCount64(); page.sidebars.clear();
        for (auto const& item : Elements(root, 4096, 24)) {
            auto name = item.Name();
            auto id = Automation::AutomationProperties::GetAutomationId(item);
            if (name == L"SpacesScrollViewer" || name == L"SidebarPresenter" || id == L"SpacesScrollViewer") page.sidebars.emplace_back(item);
        }
    }
    bool left = false, right = false;
    for (auto const& weak : page.sidebars) if (auto sidebar = weak.get(); sidebar && Visible(sidebar)) {
        auto rect = Bounds(sidebar, root);
        double gapLeft = original.X - (rect.X + rect.Width), gapRight = rect.X - (original.X + original.Width);
        if (gapLeft >= -1 && gapLeft <= 24) { left = true; }
        if (gapRight >= -1 && gapRight <= 24) { right = true; }
    }
    frame = Bounds(host, root);
    page.corners = false;
    for (auto const& element : frames) {
        auto rect = Bounds(element, root);
        if (rect.Width < frame.Width * .9 || rect.Height < frame.Height * .9) continue;
        auto corners = RadiusProperty(element);
        if (corners) {
            auto value = Baseline<CornerRadius>(page, element, corners);
            if (dt::page::HasRadius(Plain(value))) page.corners = true;
            bool square = edge && (state.flags & dt::SquareSidebar) != 0;
            auto hr = Set(state, page, element, corners, L"CornerRadius", Native(dt::page::Square(Plain(value), square && left, square && right)), dt::SquareSidebar);
            if (FAILED(hr)) result = hr;
        }
    }
    return result;
}

void Update(State& state) {
    state.error = S_OK; state.detail.clear();
    for (auto& page : state.pages) {
        try { page.error = Apply(state, page); } catch (...) { page.error = to_hresult(); }
        if (FAILED(page.error)) state.error = page.error;
    }
    state.pages.erase(std::remove_if(state.pages.begin(), state.pages.end(), [](auto const& page) { return (page.detached || !page.host.get()) && page.leases.empty(); }), state.pages.end());
}
}
std::shared_ptr<void> dt::CreatePages(PageApi api) { auto state = std::make_shared<State>(); state->api = api; return state; }
void dt::ObservePage(std::shared_ptr<void> const& value, unsigned long long id, IInspectable* object) noexcept {
    if (!value) return;
    try {
        auto& state = *static_cast<State*>(value.get());
        Object instance{nullptr}; copy_from_abi(instance, object);
        auto host = instance.try_as<FrameworkElement>();
        if (!host) return;
        for (auto& page : state.pages) if (page.id == id) {
            if (page.host.get() == host) { page.detached = false; return; }
            auto hr = Restore(page);
            if (FAILED(hr)) { state.error = hr; return; }
            page = PageFrame{}; page.id = id; page.host = host; page.ready = Initialize(page); return;
        }
        if (state.pages.size() >= 128) return;
        {
            PageFrame page{}; page.id = id; page.host = host;
            page.ready = Initialize(page); state.pages.push_back(std::move(page));
        }
    } catch (...) { static_cast<State*>(value.get())->error = to_hresult(); }
}
void dt::ForgetPage(std::shared_ptr<void> const& value, unsigned long long id) noexcept {
    if (!value) return;
    auto& state = *static_cast<State*>(value.get());
    for (auto& page : state.pages) if (page.id == id) page.detached = true;
    if (state.updating) return;
    try { Update(state); } catch (...) { state.error = to_hresult(); }
}
HRESULT dt::ConfigurePages(std::shared_ptr<void> const& value, unsigned flags) noexcept {
    if (!value) return flags & (EdgeToEdge | SquareSidebar) ? E_FAIL : S_OK;
    auto& state = *static_cast<State*>(value.get());
    if (state.updating) return HRESULT_FROM_WIN32(ERROR_BUSY);
    state.updating = true; state.flags = flags;
    try { Update(state); } catch (...) { state.error = to_hresult(); }
    state.updating = false;
    return state.error;
}
void dt::TickPages(std::shared_ptr<void> const& value) noexcept {
    if (!value) return;
    auto& state = *static_cast<State*>(value.get());
    ConfigurePages(value, state.flags);
    // Discovery while disabled must still refresh capability checks.
    if (!(state.flags & EdgeToEdge)) for (auto& page : state.pages) {
        try { page.ready = Initialize(page); } catch (...) { page.ready = false; }
    }
}
unsigned dt::QueryPages(std::shared_ptr<void> const& value, unsigned feature) noexcept {
    if (!value) return 0;
    auto& state = *static_cast<State*>(value.get());
    if (!state.updating) for (auto& page : state.pages) if ((!page.ready || !page.corners) && !page.detached) {
        try { page.ready = Initialize(page); } catch (...) { page.ready = false; }
    }
    unsigned matched = 0, verified = 0;
    for (auto const& page : state.pages) if (!page.detached && page.ready && (feature != SquareSidebar || page.corners)) {
        ++matched; if (SUCCEEDED(page.error)) ++verified;
    }
    return (verified << 16) | matched;
}
unsigned dt::OwnedPages(std::shared_ptr<void> const& value, unsigned feature) noexcept {
    if (!value) return 0;
    unsigned count = 0;
    for (auto const& page : static_cast<State*>(value.get())->pages) for (auto const& lease : page.leases)
        if (!feature || (feature & lease.feature)) ++count;
    return count;
}
HRESULT dt::PagesError(std::shared_ptr<void> const& value) noexcept { return value ? static_cast<State*>(value.get())->error : S_OK; }
std::wstring dt::PagesErrorText(std::shared_ptr<void> const& value) {
    if (!value) return L"PageFrame styling is unavailable.";
    auto& state = *static_cast<State*>(value.get());
    if (!state.detail.empty()) return state.detail;
    return FAILED(state.error) ? L"Could not apply or restore the page frame properties." : L"";
}

std::wstring dt::PagesLayoutReport(std::shared_ptr<void> const& value) {
    if (!value) return L"No page style state.\n";
    auto& state = *static_cast<State*>(value.get());
    std::wostringstream out;
    out << L"PAGE LAYOUT (read-only)\n";
    for (auto const& page : state.pages) {
        out << L"Page frame: ready=" << page.ready << L" rounded=" << page.corners << L" detached=" << page.detached
            << L" ownedProperties=" << page.leases.size() << L" hr=0x" << std::hex << static_cast<unsigned long>(page.error) << std::dec << L'\n';
        if (auto host = page.host.get()) out << LayoutSnapshot(reinterpret_cast<IInspectable*>(get_abi(host)));
        if (out.tellp() > 140000) { out << L"[page report limit]\n"; break; }
    }
    return out.str();
}
