#pragma once
// In-process mock of XAML diagnostics: no Dia or WinUI installation required.
#include "../native/Tap.cpp"
#include "../native/Scope.h"
#include <iostream>
#include <stdexcept>
#include <vector>
#include <set>
#include <functional>

namespace {
void Check(bool condition, const char* text) { if (!condition) throw std::runtime_error(text); }
void Pump() {
    MSG msg{};
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
}
class FakeElement final : public IInspectable {
    std::atomic<ULONG> refs{1};
public:
    InstanceHandle canonical;
    FakeElement* parent = nullptr;
    bool headerPeers = false;
    dt::HoverSample hoverSample{true};
    bool compacted = false, failLayoutRestore = false, failLayoutFlush = false;
    unsigned layoutFlushes = 0;
    bool compactedAtFlush = false;
    std::function<void()> onLayoutFlush;
    bool failPageStyle = false, inlineSidebar = true;
    unsigned pageFlags = 0;
    const wchar_t* name = L"";
    explicit FakeElement(InstanceHandle handle) : canonical(handle) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (iid != __uuidof(IUnknown) && iid != __uuidof(IInspectable)) return E_NOINTERFACE;
        *out = static_cast<IInspectable*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override { ULONG n = --refs; if (!n) delete this; return n; }
    HRESULT STDMETHODCALLTYPE GetIids(ULONG*, IID**) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetRuntimeClassName(HSTRING*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetTrustLevel(TrustLevel*) override { return E_NOTIMPL; }
};
class FakeTree final : public IVisualTreeService, public IXamlDiagnostics {
    std::atomic<ULONG> refs{1};
    DWORD uiThread = GetCurrentThreadId();
    std::vector<std::wstring> created{L""};
    std::set<InstanceHandle> retiredValues;
    std::map<InstanceHandle, InstanceHandle> assignedValue;
    IVisualTreeServiceCallback* callback = nullptr;
    std::map<InstanceHandle, std::shared_ptr<FakeElement>> elements;
    std::map<InstanceHandle, InstanceHandle> aliases, current;
    std::set<InstanceHandle> invalid, detached;
    InstanceHandle nextHandle = 100;
    InstanceHandle Resolve(InstanceHandle handle) {
        if (invalid.count(handle)) return 0;
        return aliases.count(handle) ? aliases.at(handle) : handle;
    }
    HRESULT Notify(InstanceHandle handle, VisualMutationType mutation) {
        auto canonical = aliases.count(handle) ? aliases.at(handle) : handle;
        const auto& node = nodes.at(canonical);
        VisualElement element{};
        element.Handle = handle;
        element.Type = SysAllocString(node.type);
        element.Name = SysAllocString(node.name);
        ParentChildRelation relation{};
        HRESULT hr = callback->OnVisualTreeChange(relation, element, mutation);
        SysFreeString(element.Type); SysFreeString(element.Name);
        return hr;
    }
    void ReinsertIfVisible(InstanceHandle canonical) {
        if (nodes.at(canonical).value == L"Visible" && detached.erase(canonical)) {
            InstanceHandle handle = current.count(canonical) ? current.at(canonical) : canonical;
            Check(SUCCEEDED(Notify(handle, Add)), "publish reinserted visual");
        }
    }
public:
    struct Node {
        const wchar_t* id; std::wstring value; bool local; bool binding;
        const wchar_t* name = L"";
        const wchar_t* type = L"Microsoft.UI.Xaml.Controls.Button";
        bool omitVisibility = false;
        const wchar_t* defaultValue = L"Visible";
        InstanceHandle parent = 0;
        bool headerPeers = false;
    };
    std::map<InstanceHandle, Node> nodes{
        {1, {L"", L"Visible", false, false, L"ChatButton"}},
        {2, {L"SidebarToggleButton", L"Visible", true, false, L"", L"Button"}},
        {3, {L"CloseButton", L"Visible", false, false}},
        {4, {L"OtherButton", L"Visible", false, false, L"ChatButton"}},
        {5, {L"", L"Visible", false, false, L"ChatButtonLabel"}},
        {6, {L"ChatButton", L"Visible", false, false, L"ChatButton", L"Microsoft.UI.Xaml.Controls.TextBlock"}},
        {7, {L"DownloadsButton", L"Visible", false, false}},
        {8, {L"BookmarkButton", L"Visible", true, false}},
        {9, {L"", L"Visible", false, false, L"PinnedExtensionsHost", L"Dia.CustomHost"}},
        {10, {L"PinnedExtensionsOverflowButton", L"Visible", false, false}},
        {11, {L"CustomToolbarAutomationId", L"Visible", false, false, L"NavigationBarHost", L"ContentControl"}},
        {13, {L"PinnedExtensionsRepeater", L"Visible", false, true, L"PinnedExtensionsRepeater", L"ItemsRepeater"}},
        {12, {L"", L"Collapsed", false, false, L"BookmarksBarHost", L"Grid", false, L"Collapsed"}},
        {14, {L"SpacesScrollViewer", L"Visible", false, false, L"", L"ContentControl"}},
        {15, {L"", L"Visible", false, false, L"", L"Grid", false, L"Visible", 14, true}},
        {16, {L"OverflowButton", L"Visible", false, false, L"OverflowButton", L"Button", false, L"Visible", 15}},
        {17, {L"OverflowButton", L"Visible", false, false, L"OverflowButton", L"Button"}},
        {18, {L"SpaceSwitcherButton", L"Visible", true, false}},
        {19, {L"OverflowButton", L"Visible", false, false, L"OverflowButton", L"Button", false, L"Visible", 14}},
        {20, {L"", L"Visible", false, false, L"VariantHost", L"ContentPresenter"}}
    };
    unsigned clears = 0, writes = 0;
    unsigned staleValueWrites = 0;
    std::atomic<unsigned> unadvises{0};
    std::atomic<bool> failUnadvise{false};
    std::function<void()> onRead;
    bool ignoreWrites = false;
    bool failRegister = false;
    void Drop(InstanceHandle canonical) {
        InstanceHandle handle = current.count(canonical) ? current.at(canonical) : canonical;
        invalid.insert(handle); detached.insert(canonical);
        Check(SUCCEEDED(Notify(handle, Remove)), "publish removal");
    }
    void ReAdd(InstanceHandle canonical) {
        // Ignored element types need not have been inspected before replay.
        if (!elements.count(canonical)) {
            IInspectable* object = nullptr;
            Check(SUCCEEDED(GetIInspectableFromHandle(canonical, &object)), "materialize replayed visual");
            object->Release();
        }
        InstanceHandle handle = 0;
        Check(SUCCEEDED(RegisterInstance(elements.at(canonical).get(), &handle)), "new handle for same object");
        detached.erase(canonical);
        Check(SUCCEEDED(Notify(handle, Add)), "publish new handle");
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (iid == __uuidof(IUnknown) || iid == __uuidof(IVisualTreeService)) *out = static_cast<IVisualTreeService*>(this);
        else if (iid == __uuidof(IXamlDiagnostics)) *out = static_cast<IXamlDiagnostics*>(this);
        else return E_NOINTERFACE;
        AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override { ULONG n = --refs; if (!n) delete this; return n; }
    HRESULT STDMETHODCALLTYPE AdviseVisualTreeChange(IVisualTreeServiceCallback* value) override {
        callback = value;
        for (const auto& [handle, node] : nodes) {
            (void)node;
            HRESULT hr = Notify(handle, Add);
            if (FAILED(hr)) return hr;
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE UnadviseVisualTreeChange(IVisualTreeServiceCallback*) override { ++unadvises; return failUnadvise ? E_FAIL : S_OK; }
    HRESULT STDMETHODCALLTYPE GetEnums(unsigned*, EnumType**) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE CreateInstance(BSTR, BSTR value, InstanceHandle* result) override {
        if (GetCurrentThreadId() != uiThread) return RPC_E_WRONG_THREAD;
        created.emplace_back(value);
        *result = created.size() - 1;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetPropertyValuesChain(InstanceHandle handle, unsigned* sourceCount,
        PropertyChainSource** sources, unsigned* count, PropertyChainValue** values) override {
        if (GetCurrentThreadId() != uiThread) return RPC_E_WRONG_THREAD;
        if (onRead) { auto action = std::move(onRead); onRead = {}; action(); }
        auto node = nodes.find(Resolve(handle));
        if (node == nodes.end()) return E_INVALIDARG;
        *sourceCount = 1; *count = node->second.omitVisibility ? 1u : 2u;
        *sources = static_cast<PropertyChainSource*>(CoTaskMemAlloc(sizeof(PropertyChainSource)));
        *values = static_cast<PropertyChainValue*>(CoTaskMemAlloc(*count * sizeof(PropertyChainValue)));
        if (!*sources || !*values) return E_OUTOFMEMORY;
        ZeroMemory(*sources, sizeof(PropertyChainSource)); ZeroMemory(*values, *count * sizeof(PropertyChainValue));
        (*sources)[0].Source = node->second.local ? BaseValueSourceLocal : BaseValueSourceDefault;
        (*values)[0].Index = 10;
        (*values)[0].PropertyName = SysAllocString(L"AutomationProperties.AutomationId");
        (*values)[0].Value = SysAllocString(node->second.id);
        if (node->second.omitVisibility) return S_OK;
        auto& visibility = (*values)[1];
        visibility.Index = 11;
        visibility.PropertyName = SysAllocString(L"Visibility");
        visibility.Type = SysAllocString(L"Microsoft.UI.Xaml.Visibility");
        visibility.Value = SysAllocString(node->second.value.c_str());
        visibility.MetadataBits = node->second.binding ? IsValueBindingExpression : 0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetProperty(InstanceHandle handle, InstanceHandle value, unsigned index) override {
        if (GetCurrentThreadId() != uiThread) return RPC_E_WRONG_THREAD;
        auto canonical = Resolve(handle);
        if (index != 11 || !nodes.count(canonical) || value >= created.size()) return E_INVALIDARG;
        if (retiredValues.count(value)) { ++staleValueWrites; return E_INVALIDARG; }
        if (created[static_cast<size_t>(value)] == L"Visible" && elements.count(canonical))
            Check(!elements.at(canonical)->compacted, "row layout must be restored before showing toolbar");
        if (!ignoreWrites) {
            if (assignedValue[canonical] && assignedValue[canonical] != value) retiredValues.insert(assignedValue[canonical]);
            assignedValue[canonical] = value;
            nodes.at(canonical).value = created[static_cast<size_t>(value)]; nodes.at(canonical).local = true; ++writes;
            ReinsertIfVisible(canonical);
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE ClearProperty(InstanceHandle handle, unsigned index) override {
        if (GetCurrentThreadId() != uiThread) return RPC_E_WRONG_THREAD;
        auto canonical = Resolve(handle);
        if (index != 11 || !nodes.count(canonical)) return E_INVALIDARG;
        if (assignedValue[canonical]) retiredValues.insert(assignedValue[canonical]);
        assignedValue[canonical] = 0;
        if (nodes.at(canonical).defaultValue == std::wstring_view(L"Visible") && elements.count(canonical))
            Check(!elements.at(canonical)->compacted, "row layout must be restored before clearing Visibility");
        nodes.at(canonical).value = nodes.at(canonical).defaultValue; nodes.at(canonical).local = false; ++clears;
        ReinsertIfVisible(canonical);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetCollectionCount(InstanceHandle, unsigned*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetCollectionElements(InstanceHandle, unsigned, unsigned*, CollectionElementValue**) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE AddChild(InstanceHandle, InstanceHandle, unsigned) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE RemoveChild(InstanceHandle, unsigned) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE ClearChildren(InstanceHandle) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetDispatcher(IInspectable**) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetUiLayer(IInspectable**) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetApplication(IInspectable**) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetIInspectableFromHandle(InstanceHandle handle, IInspectable** out) override {
        if (GetCurrentThreadId() != uiThread) return RPC_E_WRONG_THREAD;
        auto canonical = Resolve(handle);
        if (!nodes.count(canonical)) return E_INVALIDARG;
        if (!elements.count(canonical)) elements.emplace(canonical, Adopt(new FakeElement(canonical)));
        auto element = elements.at(canonical).get();
        auto const& entry = nodes.at(canonical);
        element->name = dt::IsScopeMarker(entry.name) ? entry.name : entry.id;
        element->headerPeers = entry.headerPeers;
        auto parent = nodes.at(canonical).parent;
        element->parent = nullptr;
        if (parent) {
            IInspectable* ancestor = nullptr;
            HRESULT hr = GetIInspectableFromHandle(parent, &ancestor);
            if (FAILED(hr)) return hr;
            element->parent = static_cast<FakeElement*>(ancestor);
            ancestor->Release();
        }
        *out = element; (*out)->AddRef(); return S_OK;
    }
    FakeElement* ToolbarElement() { return elements.at(11).get(); }
    void ResetToolbarRow(InstanceHandle node) { elements.at(node)->compacted = false; }
    FakeElement* PageElement() {
        IInspectable* object = nullptr;
        Check(SUCCEEDED(GetIInspectableFromHandle(20, &object)), "fake page element");
        auto element = static_cast<FakeElement*>(object); object->Release(); return element;
    }
    bool Compacted(InstanceHandle canonical) const { return elements.at(canonical)->compacted; }
    void FailLayoutRestore(InstanceHandle canonical, bool fail) { elements.at(canonical)->failLayoutRestore = fail; }
    void Hover(InstanceHandle canonical, dt::HoverSample sample) {
        IInspectable* object = nullptr;
        Check(SUCCEEDED(GetIInspectableFromHandle(canonical, &object)), "hover object");
        static_cast<FakeElement*>(object)->hoverSample = sample;
        object->Release();
    }
    HRESULT STDMETHODCALLTYPE GetHandleFromIInspectable(IInspectable*, InstanceHandle*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE HitTest(RECT, unsigned*, InstanceHandle**) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE RegisterInstance(IInspectable* instance, InstanceHandle* out) override {
        if (GetCurrentThreadId() != uiThread) return RPC_E_WRONG_THREAD;
        if (failRegister) return E_FAIL;
        auto canonical = static_cast<FakeElement*>(instance)->canonical;
        InstanceHandle handle = current.count(canonical) ? current.at(canonical) : canonical;
        if (invalid.count(handle)) { handle = ++nextHandle; aliases[handle] = canonical; }
        current[canonical] = handle; *out = handle; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetInitializationData(BSTR*) override { return E_NOTIMPL; }
};
}


inline bool dt::IsSidebarHeader(IInspectable* object) noexcept {
    return InSidebarHeader(static_cast<FakeElement*>(object),
        [](FakeElement* item) { return item->parent; },
        [](FakeElement* item) { return item->name; },
        [](FakeElement* item) { return item->headerPeers; });
}

inline std::shared_ptr<void> dt::TrackToolbar(IInspectable* object) noexcept {
    object->AddRef();
    return std::shared_ptr<void>(static_cast<FakeElement*>(object), [](void* value) { static_cast<FakeElement*>(value)->Release(); });
}
inline dt::HoverSample dt::ReadToolbarHover(std::shared_ptr<void> const& region, bool) noexcept {
    return region ? static_cast<FakeElement*>(region.get())->hoverSample : HoverSample{};
}

inline HRESULT dt::FlushToolbarLayout(std::shared_ptr<void> const& region) noexcept {
    if (!region) return S_OK;
    auto element = static_cast<FakeElement*>(region.get());
    ++element->layoutFlushes;
    element->compactedAtFlush = element->compacted;
    if (element->onLayoutFlush) element->onLayoutFlush();
    return element->failLayoutFlush ? E_FAIL : S_OK;
}

inline HRESULT dt::CompactToolbarRow(std::shared_ptr<void> const& region, bool compact) noexcept {
    if (!region) return S_OK;
    auto element = static_cast<FakeElement*>(region.get());
    if (!compact && element->compacted && element->failLayoutRestore) return E_FAIL;
    element->compacted = compact;
    return S_OK;
}

namespace {
struct FakePages { std::shared_ptr<FakeElement> page; unsigned long long id = 0; unsigned flags = 0; HRESULT error = S_OK; };
}
inline std::shared_ptr<void> dt::CreatePages(PageApi) { return std::make_shared<FakePages>(); }
inline void dt::ObservePage(std::shared_ptr<void> const& value, unsigned long long id, IInspectable* object) noexcept {
    auto state = static_cast<FakePages*>(value.get());
    object->AddRef(); state->page = Adopt(static_cast<FakeElement*>(object)); state->id = id;
}
inline void dt::ForgetPage(std::shared_ptr<void> const& value, unsigned long long id) noexcept {
    auto state = static_cast<FakePages*>(value.get());
    if (state->id == id && state->page) { state->page->pageFlags = 0; state->page.reset(); }
}
inline HRESULT dt::ConfigurePages(std::shared_ptr<void> const& value, unsigned flags) noexcept {
    auto state = static_cast<FakePages*>(value.get());
    state->flags = flags; state->error = S_OK;
    if (state->page) {
        if ((flags & EdgeToEdge) && state->page->failPageStyle) return state->error = E_NOTIMPL;
        state->page->pageFlags = flags & EdgeToEdge ? flags & PageFlags : 0;
    }
    return S_OK;
}
inline void dt::TickPages(std::shared_ptr<void> const& value) noexcept { auto state = static_cast<FakePages*>(value.get()); ConfigurePages(value, state->flags); }
inline unsigned dt::QueryPages(std::shared_ptr<void> const& value, unsigned) noexcept {
    auto state = static_cast<FakePages*>(value.get()); return state->page ? (SUCCEEDED(state->error) ? 0x10001 : 1) : 0;
}
inline unsigned dt::OwnedPages(std::shared_ptr<void> const& value, unsigned mask) noexcept {
    auto state = static_cast<FakePages*>(value.get());
    if (!state->page) return 0;
    unsigned flags = state->page->pageFlags;
    unsigned count = (!mask || (mask & EdgeToEdge)) && (flags & EdgeToEdge) ? 1u : 0u;
    if ((!mask || (mask & SquareSidebar)) && (flags & SquareSidebar) && state->page->inlineSidebar) ++count;
    return count;
}
inline HRESULT dt::PagesError(std::shared_ptr<void> const& value) noexcept { return static_cast<FakePages*>(value.get())->error; }
inline std::wstring dt::PagesErrorText(std::shared_ptr<void> const& value) { return FAILED(PagesError(value)) ? L"Fake unsupported page binding." : L""; }

inline std::wstring dt::LayoutSnapshot(IInspectable*) { return L"fake geometry without page content\n"; }
inline std::wstring dt::PagesLayoutReport(std::shared_ptr<void> const&) { return L"fake page frame report\n"; }
