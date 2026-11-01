#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <ocidl.h>
#include <xamlom.h>
#include <atomic>
#include <map>
#include <memory>
#include <new>
#include "Properties.h"
#include "Protocol.h"
#include "SettingsHost.h"
#include "Preferences.h"

namespace {
HINSTANCE module;
struct Button {
    dt::Target target = dt::None;
    unsigned property = 0;
    // Keep semantic baseline values, never cache a temporary boxed enum handle
    // across property writes: diagnostics may evict that value after restore.
    std::wstring originalValue, valueType;
    dt::Stage failure = dt::NoFailure;
    HRESULT error = S_OK;
    LONGLONG metadata = 0;
    BaseValueSource source = BaseValueSourceUnknown;
    bool local = false, active = false, detached = false;
    std::shared_ptr<IInspectable> instance;
    std::shared_ptr<IUnknown> identity;
};
template<class T> std::shared_ptr<T> Adopt(T* value) {
    return std::shared_ptr<T>(value, [](T* p) { if (p) p->Release(); });
}
std::shared_ptr<IUnknown> Identity(IInspectable* value) {
    IUnknown* identity = nullptr;
    if (!value || FAILED(value->QueryInterface(__uuidof(IUnknown), reinterpret_cast<void**>(&identity)))) return {};
    return Adopt(identity);
}

class Tap final : public IObjectWithSite, public IVisualTreeServiceCallback {
    std::atomic<ULONG> refs{1};
    std::atomic<int> state{0}; // 0 = replaying, 1 = ready, -1 = subscription failed
    std::atomic<unsigned> added{0}, eligible{0};
    IUnknown* site = nullptr;
    IVisualTreeService* service = nullptr;
    IXamlDiagnostics* diagnostics = nullptr;
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
        if (msg == dt::SettingsState) return dt::SettingsStatus(wp);
        if (msg == dt::SaveSelection) return dt::SavePreferences(flags);
        if (msg == dt::SettingsNode) {
            auto object = Instance(static_cast<InstanceHandle>(wp));
            if (object) dt::ObserveSettings(object.get(), {
                this,
                [](void* context) { return static_cast<Tap*>(context)->flags; },
                [](void* context, unsigned target) {
                    auto self = static_cast<Tap*>(context);
                    return (self->Message(self->window, dt::Query, target, 0) & 0xffff) != 0 || self->Message(self->window, dt::Active, target, 0) != 0;
                },
                [](void* context, unsigned selection) { return static_cast<Tap*>(context)->ChangeFromSettings(selection); }
            });
            return 0;
        }
        if (msg == dt::Failure) {
            auto target = static_cast<unsigned>(wp & 0xff);
            auto field = static_cast<unsigned>(wp >> 8);
            for (const auto& [node, button] : buttons) {
                (void)node;
                if (button.target != target || button.failure == dt::NoFailure) continue;
                switch (field) {
                case dt::FailureStage: return button.failure;
                case dt::FailureError: return button.error;
                case dt::FailureMetadata: return static_cast<LRESULT>(button.metadata);
                case dt::FailureSource: return button.source;
                default: return -1;
                }
            }
            return 0;
        }
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
            case dt::DetachedOverrides: {
                unsigned count = 0;
                for (const auto& [node, button] : buttons) {
                    (void)node;
                    if (button.active && button.detached) ++count;
                }
                return count;
            }
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
        if (msg == dt::RemoveNode) {
            auto it = buttons.find(static_cast<InstanceHandle>(wp));
            if (it != buttons.end()) {
                // Removal from the visual tree is not proof of object destruction.
                if (it->second.active) it->second.detached = true;
                else buttons.erase(it);
            }
            return 0;
        }
        if (msg == dt::Configure) {
            if (wp & ~dt::All) return -1;
            flags = static_cast<unsigned>(wp);
            int failures = 0;
            for (auto& [node, button] : buttons) {
                if (!button.active && button.detached) continue;
                if (!Apply(node, button, (flags & button.target) != 0)) ++failures;
            }
            for (auto it = buttons.begin(); it != buttons.end();) {
                if (it->second.detached && !it->second.active) it = buttons.erase(it);
                else ++it;
            }
            dt::RefreshSettings();
            return failures ? -1 : 0;
        }
        if (msg == dt::Active) {
            unsigned count = 0;
            for (const auto& [node, button] : buttons) {
                (void)node;
                if (button.active && (!wp || (button.target & wp))) ++count;
            }
            return count;
        }
        if (msg == dt::OwnedMask) {
            unsigned mask = 0;
            for (const auto& [node, button] : buttons) {
                (void)node;
                if (button.active) mask |= button.target;
            }
            return mask;
        }
        if (msg == dt::Query || msg == dt::QueryVisible) {
            unsigned matched = 0, hidden = 0;
            for (const auto& [node, button] : buttons) if (!button.detached && (button.target & wp)) {
                ++matched; // A matched selector stays counted even if Visibility is unreadable.
                dt::Properties p(service, node);
                auto v = p.Find(L"Visibility");
                if (!v) continue;
                if (!v->MetadataBits && (msg == dt::Query ? dt::IsCollapsed(dt::Text(v->Value)) : dt::IsVisible(dt::Text(v->Value)))) ++hidden;
            }
            return static_cast<LRESULT>((hidden << 16) | matched);
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
    HRESULT ChangeFromSettings(unsigned selection) {
        unsigned previous = flags;
        bool applied = Message(window, dt::Configure, selection, 0) == 0;
        for (unsigned target : {dt::Chat, dt::Sidebar}) if (selection & target) {
            auto counts = static_cast<unsigned>(Message(window, dt::Query, target, 0));
            applied = applied && (counts & 0xffff) != 0 && (counts >> 16) == (counts & 0xffff);
        }
        HRESULT hr = applied ? dt::SavePreferences(selection) : E_FAIL;
        if (FAILED(hr)) Message(window, dt::Configure, previous, 0);
        return hr;
    }
    std::shared_ptr<IInspectable> Instance(InstanceHandle node) {
        IInspectable* instance = nullptr;
        HRESULT hr = diagnostics->GetIInspectableFromHandle(node, &instance);
        if (FAILED(hr)) { lastQueryError = hr; return {}; }
        return Adopt(instance);
    }
    void Discover(InstanceHandle node, dt::Target namedTarget) {
        auto existing = buttons.find(node);
        if (existing != buttons.end()) {
            existing->second.detached = false;
            Apply(node, existing->second, (flags & existing->second.target) != 0);
            return;
        }
        if (buttons.size() >= 4096) return;
        // The same object can reappear under a fresh diagnostics handle. Move
        // its saved baseline rather than mistaking our Collapsed value for Dia's.
        bool pending = false;
        for (const auto& [handle, button] : buttons) {
            (void)handle;
            if (button.active && button.detached) { pending = true; break; }
        }
        if (pending) {
            auto instance = Instance(node);
            auto identity = Identity(instance.get());
            for (auto it = buttons.begin(); identity && it != buttons.end(); ++it) {
                if (it->second.active && it->second.detached && it->second.identity.get() == identity.get()) {
                    Button saved = std::move(it->second);
                    buttons.erase(it);
                    saved.detached = false;
                    auto moved = buttons.emplace(node, std::move(saved)).first;
                    Apply(node, moved->second, (flags & moved->second.target) != 0);
                    return;
                }
            }
        }
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
        auto fail = [&](dt::Stage stage, HRESULT error) {
            button.failure = stage; button.error = error; return false;
        };
        if (!enable && !button.active) return true;
        // A removed visual's old handle can already be invalid. Keep the actual
        // object alive and register it again before attempting restoration.
        if (button.active && button.instance) {
            HRESULT hr = diagnostics->RegisterInstance(button.instance.get(), &node);
            if (FAILED(hr)) { lastQueryError = hr; return fail(dt::RegisterTarget, hr); }
        }
        dt::Properties p(service, node);
        auto v = p.Find(L"Visibility");
        if (!v) return fail(dt::ReadVisibility, FAILED(p.result) ? p.result : HRESULT_FROM_WIN32(ERROR_NOT_FOUND));
        button.metadata = v->MetadataBits; button.source = p.Source(*v);
        if (dt::Text(v->Type) != L"Microsoft.UI.Xaml.Visibility") return fail(dt::VisibilityType, E_NOTIMPL);
        if (!enable) {
            // If Dia has since changed the value itself, do not overwrite its change.
            if (v->MetadataBits || p.HasBinding(v->Index) || !dt::IsCollapsed(dt::Text(v->Value))) {
                button.active = false;
                button.identity.reset(); button.instance.reset();
                return true;
            }
            button.property = v->Index;
            HRESULT hr = Restore(node, button);
            if (FAILED(hr)) return false;
            dt::Properties restored(service, node);
            auto original = restored.Find(L"Visibility");
            if (!original || original->MetadataBits || !dt::IsVisible(dt::Text(original->Value))) return fail(dt::VerifyRestored, FAILED(restored.result) ? restored.result : E_FAIL);
            button.active = false;
            button.identity.reset(); button.instance.reset();
            return true;
        }
        if (!v->MetadataBits && dt::IsCollapsed(dt::Text(v->Value))) return true;
        // An app-side visibility change ends our previous ownership of the value.
        button.active = false;
        auto source = p.Source(*v);
        bool simple = source == BaseValueSourceLocal || source == BaseValueSourceDefault ||
                      source == BaseValueSourceBuiltInStyle || source == BaseValueSourceStyle;
        if (!dt::CanOverride(v->MetadataBits == 0, simple, p.HasBinding(v->Index), dt::Text(v->Value))) return fail(dt::VisibilityPolicy, S_FALSE);
        if (!button.instance) button.instance = Instance(node);
        if (!button.identity) button.identity = Identity(button.instance.get());
        if (!button.instance || !button.identity) return fail(dt::HoldTarget, FAILED(lastQueryError) ? lastQueryError : E_NOINTERFACE);
        button.property = v->Index;
        button.local = source == BaseValueSourceLocal;
        button.valueType = std::wstring(dt::Text(v->Type));
        button.originalValue = std::wstring(dt::Text(v->Value));
        InstanceHandle collapsed = 0;
        HRESULT hr = dt::CreateValue(service, button.valueType, L"Collapsed", &collapsed);
        if (FAILED(hr)) return fail(dt::CreateCollapsed, hr);
        hr = service->SetProperty(node, collapsed, button.property);
        if (FAILED(hr)) return fail(dt::SetCollapsed, hr);
        button.active = true;
        dt::Properties after(service, node);
        auto changed = after.Find(L"Visibility");
        if (changed && !changed->MetadataBits && dt::IsCollapsed(dt::Text(changed->Value))) {
            button.failure = dt::NoFailure; button.error = S_OK; return true;
        }
        if (SUCCEEDED(Restore(node, button))) button.active = false;
        return fail(dt::VerifyCollapsed, FAILED(after.result) ? after.result : E_FAIL);
    }
    HRESULT Restore(InstanceHandle node, Button& button) {
        HRESULT hr;
        if (button.local) {
            InstanceHandle original = 0;
            hr = dt::CreateValue(service, button.valueType, button.originalValue, &original);
            if (FAILED(hr)) { button.failure = dt::CreateOriginal; button.error = hr; return hr; }
            hr = service->SetProperty(node, original, button.property);
            if (FAILED(hr)) { button.failure = dt::SetOriginal; button.error = hr; }
        } else {
            hr = service->ClearProperty(node, button.property);
            if (FAILED(hr)) { button.failure = dt::ClearOriginal; button.error = hr; }
        }
        return hr;
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
        hr = value->QueryInterface(__uuidof(IXamlDiagnostics), reinterpret_cast<void**>(&diagnostics));
        if (FAILED(hr)) { service->Release(); service = nullptr; return hr; }
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
            auto type = dt::Text(element.Type);
            if (type == L"Microsoft.UI.Xaml.Controls.NavigationView" || type == L"NavigationView")
                return PostMessageW(window, dt::SettingsNode, static_cast<WPARAM>(element.Handle), 0) ? S_OK : HRESULT_FROM_WIN32(GetLastError());
            if (!dt::IsButtonType(dt::Text(element.Type))) return S_OK;
            ++eligible;
            namedTarget = dt::Identify(dt::Text(element.Name));
        }
        return PostMessageW(window, msg, static_cast<WPARAM>(element.Handle), namedTarget) ? S_OK : HRESULT_FROM_WIN32(GetLastError());
    }
    ~Tap() {
        if (service) service->Release();
        if (diagnostics) diagnostics->Release();
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
