#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <ocidl.h>
#include <xamlom.h>
#include <atomic>
#include <map>
#include <new>
#include "Properties.h"
#include "Protocol.h"

namespace {
HINSTANCE module;
struct Button {
    dt::Target target = dt::None;
    unsigned property = 0;
    InstanceHandle original = 0, collapsed = 0;
    bool local = false, active = false;
};

class Tap final : public IObjectWithSite, public IVisualTreeServiceCallback {
    std::atomic<ULONG> refs{1};
    std::atomic<int> state{0}; // 0 = replaying, 1 = ready, -1 = subscription failed
    std::atomic<unsigned> added{0}, eligible{0};
    IUnknown* site = nullptr;
    IVisualTreeService* service = nullptr;
    HWND window = nullptr;
    unsigned flags = 0;
    unsigned inspected = 0, failedQueries = 0, missingIds = 0, namedMatches = 0;
    HRESULT lastQueryError = S_OK;
    std::map<InstanceHandle, Button> buttons; // Accessed only on SetSite's UI thread.

    static DWORD WINAPI Subscribe(void* context) noexcept {
        auto self = static_cast<Tap*>(context);
        HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        HRESULT hr = self->service->AdviseVisualTreeChange(self);
        self->state = SUCCEEDED(hr) ? 1 : -1;
        if (SUCCEEDED(com)) CoUninitialize();
        self->Release();
        return 0;
    }
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) noexcept {
        auto self = reinterpret_cast<Tap*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (msg == WM_NCCREATE) {
            self = static_cast<Tap*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (self) {
            try { return self->Message(hwnd, msg, wp, lp); }
            catch (...) { return -1; }
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
    LRESULT Message(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        if (msg == dt::Version) return dt::ProtocolVersion;
        if (msg == dt::Stats) {
            switch (wp) {
            case dt::Added: return added.load();
            case dt::Eligible: return eligible.load();
            case dt::Inspected: return inspected;
            case dt::FailedQueries: return failedQueries;
            case dt::MissingIds: return missingIds;
            case dt::NamedMatches: return namedMatches;
            case dt::Tracked: return static_cast<LRESULT>(buttons.size());
            case dt::LastQueryError: return lastQueryError;
            case dt::MissingVisibility: {
                unsigned missing = 0;
                for (const auto& [node, button] : buttons) {
                    (void)button;
                    dt::Properties p(service, node);
                    if (!p.Find(L"Visibility")) ++missing;
                }
                return missing;
            }
            default: return -1;
            }
        }
        if (msg == dt::State) return state.load();
        if (msg == dt::Flags) return flags;
        if (msg == dt::AddNode) { Discover(static_cast<InstanceHandle>(wp), static_cast<dt::Target>(lp)); return 0; }
        if (msg == dt::RemoveNode) { buttons.erase(static_cast<InstanceHandle>(wp)); return 0; }
        if (msg == dt::Configure) {
            if (wp & ~dt::All) return -1;
            flags = static_cast<unsigned>(wp);
            int failures = 0;
            for (auto& [node, button] : buttons) {
                if (!Apply(node, button, (flags & button.target) != 0)) ++failures;
            }
            return failures ? -1 : 0;
        }
        if (msg == dt::Active) {
            unsigned count = 0;
            for (const auto& [node, button] : buttons) if (button.active) ++count;
            return count;
        }
        if (msg == dt::Query) {
            unsigned matched = 0, hidden = 0;
            for (const auto& [node, button] : buttons) if (button.target & wp) {
                ++matched; // A matched selector stays counted even if Visibility is unreadable.
                dt::Properties p(service, node);
                auto v = p.Find(L"Visibility");
                if (!v) continue;
                if (!v->MetadataBits && dt::IsCollapsed(dt::Text(v->Value))) ++hidden;
            }
            return static_cast<LRESULT>((hidden << 16) | matched);
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
    void Discover(InstanceHandle node, dt::Target namedTarget) {
        if (buttons.count(node) || buttons.size() >= 4096) return;
        ++inspected;
        dt::Properties p(service, node);
        if (FAILED(p.result)) { ++failedQueries; lastQueryError = p.result; return; }
        auto id = p.Find(L"AutomationId");
        // An unset string can be reported as null. No arbitrary property values
        // or x:Names are retained: the callback passes only our two known keys.
        if (!id || id->MetadataBits == IsValueNull || dt::Text(id->Value).empty()) ++missingIds;
        if (id && id->MetadataBits && id->MetadataBits != IsValueNull) return;
        Button button;
        button.target = dt::IdentifyButton(id && id->MetadataBits != IsValueNull ? dt::Text(id->Value) : L"", namedTarget);
        if (button.target == dt::None) return;
        if (button.target == namedTarget && (!id || id->MetadataBits == IsValueNull || dt::Text(id->Value).empty())) ++namedMatches;
        auto [it, inserted] = buttons.emplace(node, button);
        if (inserted && (flags & button.target)) Apply(node, it->second, true);
    }
    bool Apply(InstanceHandle node, Button& button, bool enable) {
        dt::Properties p(service, node);
        auto v = p.Find(L"Visibility");
        if (!v || dt::Text(v->Type) != L"Microsoft.UI.Xaml.Visibility") return false;
        if (!enable) {
            if (!button.active) return true;
            // If Dia has since changed the value itself, do not overwrite its change.
            if (v->MetadataBits || p.HasBinding(v->Index) || !dt::IsCollapsed(dt::Text(v->Value))) {
                button.active = false;
                return true;
            }
            HRESULT hr = Restore(node, button);
            if (FAILED(hr)) return false;
            dt::Properties restored(service, node);
            auto original = restored.Find(L"Visibility");
            if (!original || original->MetadataBits || !dt::IsVisible(dt::Text(original->Value))) return false;
            button.active = false;
            return true;
        }
        if (!v->MetadataBits && dt::IsCollapsed(dt::Text(v->Value))) return true;
        // An app-side visibility change ends our previous ownership of the value.
        button.active = false;
        auto source = p.Source(*v);
        bool simple = source == BaseValueSourceLocal || source == BaseValueSourceDefault ||
                      source == BaseValueSourceBuiltInStyle || source == BaseValueSourceStyle;
        if (!dt::CanOverride(v->MetadataBits == 0, simple, p.HasBinding(v->Index), dt::Text(v->Value))) return false;
        button.property = v->Index;
        button.local = source == BaseValueSourceLocal;
        std::wstring type(dt::Text(v->Type));
        if (button.local && FAILED(dt::CreateValue(service, type, std::wstring(dt::Text(v->Value)), &button.original))) return false;
        if (!button.collapsed && FAILED(dt::CreateValue(service, type, L"Collapsed", &button.collapsed))) return false;
        HRESULT hr = service->SetProperty(node, button.collapsed, button.property);
        if (FAILED(hr)) return false;
        button.active = true;
        dt::Properties after(service, node);
        auto changed = after.Find(L"Visibility");
        if (changed && !changed->MetadataBits && dt::IsCollapsed(dt::Text(changed->Value))) return true;
        if (SUCCEEDED(Restore(node, button))) button.active = false;
        return false;
    }
    HRESULT Restore(InstanceHandle node, const Button& button) {
        return button.local ? service->SetProperty(node, button.original, button.property)
                            : service->ClearProperty(node, button.property);
    }
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) noexcept override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (iid == __uuidof(IUnknown) || iid == __uuidof(IObjectWithSite)) *out = static_cast<IObjectWithSite*>(this);
        else if (iid == __uuidof(IVisualTreeServiceCallback)) *out = static_cast<IVisualTreeServiceCallback*>(this);
        else return E_NOINTERFACE;
        AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() noexcept override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() noexcept override {
        ULONG n = --refs;
        if (!n) delete this;
        return n;
    }
    HRESULT STDMETHODCALLTYPE SetSite(IUnknown* value) noexcept override {
        if (!value) return E_NOTIMPL; // Session and DLL remain resident until Dia exits.
        if (site) return HRESULT_FROM_WIN32(ERROR_ALREADY_INITIALIZED);
        HRESULT hr = value->QueryInterface(__uuidof(IVisualTreeService), reinterpret_cast<void**>(&service));
        if (FAILED(hr)) return hr;
        site = value; site->AddRef();
        // The diagnostics runtime can retain our COM vtable for the process lifetime.
        HMODULE pinned;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN | GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                          reinterpret_cast<LPCWSTR>(&WindowProc), &pinned);
        WNDCLASSW wc{};
        wc.lpfnWndProc = WindowProc; wc.hInstance = module; wc.lpszClassName = dt::WindowClass;
        if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return HRESULT_FROM_WIN32(GetLastError());
        window = CreateWindowExW(0, dt::WindowClass, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, module, this);
        if (!window) return HRESULT_FROM_WIN32(GetLastError());
        AddRef(); // Message window owns the object until the process exits.
        AddRef();
        HANDLE thread = CreateThread(nullptr, 0, Subscribe, this, 0, nullptr);
        if (!thread) { DWORD error = GetLastError(); Release(); state = -1; return HRESULT_FROM_WIN32(error); }
        CloseHandle(thread);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetSite(REFIID iid, void** out) noexcept override {
        if (!out) return E_POINTER;
        *out = nullptr;
        return site ? site->QueryInterface(iid, out) : E_FAIL;
    }
    HRESULT STDMETHODCALLTYPE OnVisualTreeChange(ParentChildRelation, VisualElement element, VisualMutationType mutation) noexcept override {
        // Only queue handles. All property access and layout changes happen later on the UI thread.
        UINT msg = mutation == Remove ? dt::RemoveNode : dt::AddNode;
        dt::Target namedTarget = dt::None;
        if (mutation == Add) {
            ++added;
            if (!dt::IsButtonType(dt::Text(element.Type))) return S_OK;
            ++eligible;
            namedTarget = dt::Identify(dt::Text(element.Name));
        }
        return PostMessageW(window, msg, static_cast<WPARAM>(element.Handle), namedTarget) ? S_OK : HRESULT_FROM_WIN32(GetLastError());
    }
    ~Tap() {
        if (service) service->Release();
        if (site) site->Release();
    }
};

class Factory final : public IClassFactory {
    std::atomic<ULONG> refs{1};
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) noexcept override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (iid != __uuidof(IUnknown) && iid != __uuidof(IClassFactory)) return E_NOINTERFACE;
        *out = static_cast<IClassFactory*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() noexcept override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() noexcept override { ULONG n = --refs; if (!n) delete this; return n; }
    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* outer, REFIID iid, void** out) noexcept override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (outer) return CLASS_E_NOAGGREGATION;
        auto tap = new(std::nothrow) Tap;
        if (!tap) return E_OUTOFMEMORY;
        HRESULT hr = tap->QueryInterface(iid, out); tap->Release(); return hr;
    }
    HRESULT STDMETHODCALLTYPE LockServer(BOOL) noexcept override { return S_OK; }
};
}

extern "C" HRESULT WINAPI DllGetClassObject(REFCLSID clsid, REFIID iid, void** out) {
    if (!out) return E_POINTER;
    *out = nullptr;
    if (clsid != dt::TapClsid) return CLASS_E_CLASSNOTAVAILABLE;
    auto factory = new(std::nothrow) Factory;
    if (!factory) return E_OUTOFMEMORY;
    HRESULT hr = factory->QueryInterface(iid, out); factory->Release(); return hr;
}
extern "C" HRESULT WINAPI DllCanUnloadNow() { return S_FALSE; }
BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) { module = instance; DisableThreadLibraryCalls(instance); }
    return TRUE;
}
