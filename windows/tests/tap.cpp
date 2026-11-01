// In-process mock of XAML diagnostics: no Dia or WinUI installation required.
#include "../native/Tap.cpp"
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
void Check(bool condition, const char* text) { if (!condition) throw std::runtime_error(text); }
class FakeTree final : public IVisualTreeService {
    std::atomic<ULONG> refs{1};
    DWORD uiThread = GetCurrentThreadId();
    std::vector<std::wstring> created{L""};
public:
    struct Node { const wchar_t* id; std::wstring value; bool local; bool binding; };
    std::map<InstanceHandle, Node> nodes{
        {1, {L"ChatButton", L"Visible", false, false}},
        {2, {L"SidebarToggleButton", L"Visible", true, false}},
        {3, {L"CloseButton", L"Visible", false, false}}
    };
    unsigned clears = 0, writes = 0;
    bool ignoreWrites = false;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (iid != __uuidof(IUnknown) && iid != __uuidof(IVisualTreeService)) return E_NOINTERFACE;
        *out = static_cast<IVisualTreeService*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override { ULONG n = --refs; if (!n) delete this; return n; }
    HRESULT STDMETHODCALLTYPE AdviseVisualTreeChange(IVisualTreeServiceCallback* callback) override {
        for (auto handle : {1ull, 2ull, 3ull}) {
            VisualElement element{};
            element.Handle = handle;
            element.Type = SysAllocString(L"Microsoft.UI.Xaml.Controls.Button");
            ParentChildRelation relation{};
            HRESULT hr = callback->OnVisualTreeChange(relation, element, Add);
            SysFreeString(element.Type);
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
        auto node = nodes.find(handle);
        if (node == nodes.end()) return E_INVALIDARG;
        *sourceCount = 1; *count = 2;
        *sources = static_cast<PropertyChainSource*>(CoTaskMemAlloc(sizeof(PropertyChainSource)));
        *values = static_cast<PropertyChainValue*>(CoTaskMemAlloc(2 * sizeof(PropertyChainValue)));
        if (!*sources || !*values) return E_OUTOFMEMORY;
        ZeroMemory(*sources, sizeof(PropertyChainSource)); ZeroMemory(*values, 2 * sizeof(PropertyChainValue));
        (*sources)[0].Source = node->second.local ? BaseValueSourceLocal : BaseValueSourceDefault;
        (*values)[0].Index = 10;
        (*values)[0].PropertyName = SysAllocString(L"AutomationProperties.AutomationId");
        (*values)[0].Value = SysAllocString(node->second.id);
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
        if (index != 11 || !nodes.count(handle) || value >= created.size()) return E_INVALIDARG;
        if (!ignoreWrites) { nodes.at(handle).value = created[static_cast<size_t>(value)]; nodes.at(handle).local = true; ++writes; }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE ClearProperty(InstanceHandle handle, unsigned index) override {
        if (GetCurrentThreadId() != uiThread) return RPC_E_WRONG_THREAD;
        if (index != 11 || !nodes.count(handle)) return E_INVALIDARG;
        nodes.at(handle).value = L"Visible"; nodes.at(handle).local = false; ++clears;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetCollectionCount(InstanceHandle, unsigned*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetCollectionElements(InstanceHandle, unsigned, unsigned*, CollectionElementValue**) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE AddChild(InstanceHandle, InstanceHandle, unsigned) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE RemoveChild(InstanceHandle, unsigned) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE ClearChildren(InstanceHandle) override { return E_NOTIMPL; }
};
}

int main() {
    try {
        Check(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)), "COM");
        module = GetModuleHandleW(nullptr);
        auto fake = new FakeTree;
        auto tap = new Tap;
        Check(SUCCEEDED(tap->SetSite(fake)), "attach mock");
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
        Check(SendMessageW(hwnd, dt::Configure, dt::All, 0) == 0, "apply both buttons on UI thread");
        Check(fake->nodes.at(1).value == L"Collapsed" && fake->nodes.at(2).value == L"Collapsed", "both hidden");
        Check(fake->nodes.at(3).value == L"Visible", "window close button preserved");
        unsigned writes = fake->writes;
        Check(SendMessageW(hwnd, dt::Configure, dt::All, 0) == 0 && fake->writes == writes, "idempotent apply");
        Check(SendMessageW(hwnd, dt::Configure, 0, 0) == 0, "restore");
        Check(fake->clears == 1 && fake->nodes.at(1).value == L"Visible" && !fake->nodes.at(1).local, "default restored by clearing local value");
        Check(fake->nodes.at(2).value == L"Visible" && fake->nodes.at(2).local, "original local value restored");
        fake->nodes.at(1).binding = true;
        writes = fake->writes;
        Check(SendMessageW(hwnd, dt::Configure, dt::Chat, 0) == -1, "binding rejected");
        Check(fake->writes == writes && fake->nodes.at(1).value == L"Visible", "binding untouched");
        fake->nodes.at(1).binding = false;
        fake->ignoreWrites = true;
        Check(SendMessageW(hwnd, dt::Configure, dt::Chat, 0) == -1, "successful HRESULT without effective change is rejected");
        Check(fake->nodes.at(1).value == L"Visible", "failed verification rolls back");
        Check(SendMessageW(hwnd, dt::Active, 0, 0) == 0, "no owned overrides after rollback");
        std::cout << "Mock XAML apply, restore, binding and readback checks passed\n";
        // Like a real diagnostics session, the TAP remains resident until this process exits.
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n'; return 1;
    }
}
