// In-process mock of XAML diagnostics: no Dia or WinUI installation required.
#include "../native/Tap.cpp"
#include <iostream>
#include <stdexcept>
#include <vector>
#include <set>

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
    };
    std::map<InstanceHandle, Node> nodes{
        {1, {L"", L"Visible", false, false, L"ChatButton"}},
        {2, {L"SidebarToggleButton", L"Visible", true, false, L"", L"Button"}},
        {3, {L"CloseButton", L"Visible", false, false}},
        {4, {L"OtherButton", L"Visible", false, false, L"ChatButton"}},
        {5, {L"", L"Visible", false, false, L"ChatButtonLabel"}},
        {6, {L"ChatButton", L"Visible", false, false, L"ChatButton", L"Microsoft.UI.Xaml.Controls.TextBlock"}}
    };
    unsigned clears = 0, writes = 0;
    unsigned staleValueWrites = 0;
    bool ignoreWrites = false;
    bool failRegister = false;
    void Drop(InstanceHandle canonical) {
        InstanceHandle handle = current.count(canonical) ? current.at(canonical) : canonical;
        invalid.insert(handle); detached.insert(canonical);
        Check(SUCCEEDED(Notify(handle, Remove)), "publish removal");
    }
    void ReAdd(InstanceHandle canonical) {
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
    HRESULT STDMETHODCALLTYPE UnadviseVisualTreeChange(IVisualTreeServiceCallback*) override { return S_OK; }
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
        nodes.at(canonical).value = L"Visible"; nodes.at(canonical).local = false; ++clears;
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
        *out = elements.at(canonical).get(); (*out)->AddRef(); return S_OK;
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

int main() {
    try {
        Check(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)), "COM");
        module = GetModuleHandleW(nullptr);
        auto fake = new FakeTree;
        auto tap = new Tap;
        Check(SUCCEEDED(tap->SetSite(static_cast<IVisualTreeService*>(fake))), "attach mock");
        HWND hwnd = FindWindowExW(HWND_MESSAGE, nullptr, dt::WindowClass, nullptr);
        Check(hwnd != nullptr, "UI dispatcher window");
        ULONGLONG deadline = GetTickCount64() + 5000;
        while (GetTickCount64() < deadline) {
            MSG msg{};
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
            if (SendMessageW(hwnd, dt::State, 0, 0) == 1 && (SendMessageW(hwnd, dt::Query, dt::Sidebar, 0) & 0xffff)) break;
            Sleep(5);
        }
        Check(SendMessageW(hwnd, dt::State, 0, 0) == 1, "worker replay completes");
        Check(SendMessageW(hwnd, dt::Version, 0, 0) == dt::ProtocolVersion, "protocol version");
        Check((SendMessageW(hwnd, dt::Query, dt::Chat, 0) & 0xffff) == 1, "x:Name fallback discovers exactly one ChatButton");
        Check((SendMessageW(hwnd, dt::Query, dt::Sidebar, 0) & 0xffff) == 1, "short Button type is accepted");
        Check(SendMessageW(hwnd, dt::Stats, dt::NamedMatches, 0) == 1, "fallback counted for diagnostics");
        Check(SendMessageW(hwnd, dt::Configure, dt::All, 0) == 0, "apply both buttons on UI thread");
        Check(fake->nodes.at(1).value == L"Collapsed" && fake->nodes.at(2).value == L"Collapsed", "both hidden");
        Check(fake->nodes.at(3).value == L"Visible", "window close button preserved");
        for (auto handle : {4ull, 5ull, 6ull})
            Check(fake->nodes.at(handle).value == L"Visible", "explicit ID precedence and label/type exclusions");
        unsigned writes = fake->writes;
        Check(SendMessageW(hwnd, dt::Configure, dt::All, 0) == 0 && fake->writes == writes, "idempotent apply");
        fake->Drop(1);
        Pump();
        Check(SendMessageW(hwnd, dt::Active, 0, 0) == 2, "removed visual retains its owned override");
        Check(SendMessageW(hwnd, dt::Stats, dt::DetachedOverrides, 0) == 1, "detached override reported");
        Check(SendMessageW(hwnd, dt::OwnedMask, 0, 0) == dt::All, "restore includes detached chat");
        Check(SendMessageW(hwnd, dt::Configure, 0, 0) == 0, "restore");
        Pump();
        Check(fake->clears == 1 && fake->nodes.at(1).value == L"Visible" && !fake->nodes.at(1).local, "default restored by clearing local value");
        Check(fake->nodes.at(2).value == L"Visible" && fake->nodes.at(2).local, "original local value restored");
        Check(SendMessageW(hwnd, dt::QueryVisible, dt::Chat, 0) == 0x10001, "detached chat is live and visible after restore");
        // Retire boxed values when they leave a property. Sidebar keeps its
        // original visual handle, so a cached Collapsed handle fails cycle two.
        for (unsigned cycle = 0; cycle < 4; ++cycle) {
            for (unsigned selection : {dt::Sidebar, dt::Chat, static_cast<dt::Target>(dt::All)}) {
                Check(SendMessageW(hwnd, dt::Configure, selection, 0) == 0, "repeat hide after restore");
                for (unsigned target : {dt::Chat, dt::Sidebar}) if (selection & target)
                    Check(SendMessageW(hwnd, dt::Query, target, 0) == 0x10001, "repeated hide readback");
                Check(SendMessageW(hwnd, dt::Configure, 0, 0) == 0, "repeat restoration");
                Pump();
                Check(SendMessageW(hwnd, dt::QueryVisible, dt::Chat, 0) == 0x10001 &&
                      SendMessageW(hwnd, dt::QueryVisible, dt::Sidebar, 0) == 0x10001, "both visible after each cycle");
                Check(SendMessageW(hwnd, dt::Active, 0, 0) == 0, "cycle releases owned overrides");
            }
        }
        Check(fake->staleValueWrites == 0, "no cached value handles reused across cycles");
        Check(SendMessageW(hwnd, dt::Configure, dt::Chat, 0) == 0, "hide before handle replacement");
        fake->Drop(1); Pump();
        fake->ReAdd(1); Pump();
        Check(SendMessageW(hwnd, dt::Active, dt::Chat, 0) == 1, "new handle for same object retains baseline");
        Check(SendMessageW(hwnd, dt::Configure, 0, 0) == 0, "restore after handle replacement");
        Check(fake->nodes.at(1).value == L"Visible", "new handle restores original visible value");
        Check(SendMessageW(hwnd, dt::Configure, dt::Chat, 0) == 0, "hide before failed restoration");
        fake->failRegister = true;
        Check(SendMessageW(hwnd, dt::Configure, 0, 0) == -1, "failed object registration cannot report restored");
        Check(SendMessageW(hwnd, dt::Active, dt::Chat, 0) == 1, "failed restore retains ownership for retry");
        fake->failRegister = false;
        Check(SendMessageW(hwnd, dt::Configure, 0, 0) == 0, "retry restore succeeds");
        Check(SendMessageW(hwnd, dt::Configure, dt::Chat, 0) == 0, "hide before app binding takes over");
        fake->nodes.at(1).binding = true;
        Check(SendMessageW(hwnd, dt::Configure, 0, 0) == 0, "do not overwrite app binding");
        Check(SendMessageW(hwnd, dt::Active, 0, 0) == 0 && SendMessageW(hwnd, dt::QueryVisible, dt::Chat, 0) == 1,
              "no owned overrides does not imply a visible Chat button");
        fake->nodes.at(1).value = L"Visible";
        fake->nodes.at(1).binding = true;
        writes = fake->writes;
        Check(SendMessageW(hwnd, dt::Configure, dt::Chat, 0) == -1, "binding rejected");
        Check(fake->writes == writes && fake->nodes.at(1).value == L"Visible", "binding untouched");
        fake->nodes.at(1).binding = false;
        fake->ignoreWrites = true;
        Check(SendMessageW(hwnd, dt::Configure, dt::Chat, 0) == -1, "successful HRESULT without effective change is rejected");
        Check(fake->nodes.at(1).value == L"Visible", "failed verification rolls back");
        Check(SendMessageW(hwnd, dt::Active, 0, 0) == 0, "no owned overrides after rollback");
        fake->nodes.at(1).omitVisibility = true;
        Check(SendMessageW(hwnd, dt::Query, dt::Chat, 0) == 1, "unreadable visibility does not masquerade as missing selector");
        Check(SendMessageW(hwnd, dt::Stats, dt::MissingVisibility, 0) == 1, "missing visibility is reported");
        std::cout << "Mock XAML apply, restore, binding and readback checks passed\n";
        // Like a real diagnostics session, the TAP remains resident until this process exits.
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n'; return 1;
    }
}
