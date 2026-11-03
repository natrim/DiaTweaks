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
#include "HoverHost.h"
#include "PageHost.h"
#include "Layout.h"

namespace {
HINSTANCE module;
struct Button {
    dt::Target target = dt::None;
    std::shared_ptr<void> hoverRegion;
    dt::HoverState hover;
    bool hoverRestored = false;
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
    std::atomic<bool> stopping{false};
    std::atomic<bool> toolbarLayoutEnabled{false}, toolbarLayoutPending{false};
    IUnknown* site = nullptr;
    IVisualTreeService* service = nullptr;
    IXamlDiagnostics* diagnostics = nullptr;
    HWND window = nullptr;
    dt::PreferencesStore preferences;
    unsigned flags = 0, messageDepth = 0;
    DWORD dispatcherThread = 0;
    unsigned earlyToolbarAttempts = 0, visibilityRepairs = 0;
    std::atomic<unsigned> deferredToolbarAdds{0};
    bool applyingPolicy = false;
    std::wstring changeError;
    unsigned inspected = 0, failedQueries = 0, missingIds = 0, namedMatches = 0;
    HRESULT lastQueryError = S_OK;
    struct PendingScope { dt::Target named; unsigned attempts = 0; };
    std::map<InstanceHandle, PendingScope> pendingRecent;
    static constexpr UINT_PTR ScopeTimer = 0x4454, HoverTimer = 0x4455;
    unsigned hoverAttempts = 0, pageAttempts = 0;
    static constexpr UINT_PTR PageTimer = 0x4456;
    std::shared_ptr<void> pages;
    std::map<InstanceHandle, Button> buttons; // Accessed only on SetSite's UI thread.

    bool HoverEnabled() const { return (flags & dt::Toolbar) && (flags & dt::RevealToolbar); }
    bool ShouldHide(Button const& button) const {
        return (flags & button.target) != 0 && !(HoverEnabled() && (button.target & dt::ToolbarParts) && button.hover.revealed);
    }
    void InheritViewport(std::shared_ptr<void> const& region) {
        // Prefer the last live host, then retained restore snapshots.
        for (unsigned pass=0; pass<2; ++pass) for (auto const& [node,button]:buttons) {
            (void)node;
            if (button.target==dt::Toolbar && button.hoverRegion && button.hoverRegion!=region &&
                static_cast<unsigned>(button.detached)==pass && dt::InheritToolbarViewport(region,button.hoverRegion)) return;
        }
    }
    std::shared_ptr<void> TrackToolbar(IInspectable* object) {
        auto region=dt::TrackToolbar(object);
        if (region) InheritViewport(region);
        if (region) dt::WatchToolbarPolicy(region,{this,[](void* context) {
            auto self=static_cast<Tap*>(context);
            if (self->stopping || !(self->flags & dt::Toolbar)) return;
            if (self->messageDepth || self->applyingPolicy) { self->QueueToolbarLayout(); return; }
            struct Depth { unsigned& value; Depth(unsigned& n):value(n) { ++value; } ~Depth() { --value; } } depth{self->messageDepth};
            ++self->visibilityRepairs;
            self->UpdateHover(false);
            self->QueueToolbarLayout();
        }});
        return region;
    }
    bool TryLiveToolbar(UINT msg, InstanceHandle node, dt::Target target=dt::None) noexcept {
        if (!toolbarLayoutEnabled || GetCurrentThreadId()!=dispatcherThread || messageDepth) return false;
        try {
            if (msg==dt::RemoveNode) {
                auto it=buttons.find(node);
                if (it==buttons.end() || !(it->second.target & dt::ToolbarParts)) return false;
            }
            struct Depth { unsigned& value; Depth(unsigned& n):value(n) { ++value; } ~Depth() { --value; } } depth{messageDepth};
            if (msg==dt::RemoveNode) Message(window,msg,static_cast<WPARAM>(node),0);
            else { ++earlyToolbarAttempts; Discover(node,target,dt::Family::Host); }
            return true;
        } catch (...) { return false; }
    }
    bool EnsureHoverRegions() {
        bool available = false;
        for (auto& [node, button] : buttons) if ((button.target & dt::ToolbarParts) && !button.detached) {
            if (!button.hoverRegion) {
                auto object = button.instance ? button.instance : Instance(node);
                if (object) button.hoverRegion = TrackToolbar(object.get());
            }
            if (button.target == dt::Toolbar && button.hoverRegion) available = true;
        }
        return available;
    }
    void QueueToolbarLayout() noexcept {
        if (stopping || !toolbarLayoutEnabled) return;
        if (toolbarLayoutPending.exchange(true)) return;
        if (!PostMessageW(window, dt::ToolbarLayout, 0, 0)) toolbarLayoutPending = false;
    }
    HRESULT FlushToolbarLayouts() noexcept {
        HRESULT result = S_OK;
        for (auto& [node, button] : buttons) if (button.target == dt::Toolbar && !button.detached) {
            (void)node;
            InheritViewport(button.hoverRegion);
            auto hr = dt::FlushToolbarLayout(button.hoverRegion);
            if (FAILED(hr)) { button.failure = dt::AdjustLayout; button.error = hr; result = hr; }
        }
        return result;
    }
    void UpdateHover(bool finishLayout = true) {
        bool ready = EnsureHoverRegions();
        if (hoverAttempts) --hoverAttempts;
        if (!HoverEnabled()) {
            if (flags & dt::Toolbar) {
                for (auto& [node, button] : buttons) if ((button.target & dt::ToolbarParts) && !button.detached) Apply(node, button, ShouldHide(button));
            } else if (ready || !hoverAttempts) KillTimer(window, HoverTimer);
            if (ready) dt::RefreshSettings();
            return;
        }
        ULONGLONG now = GetTickCount64();
        bool layoutChanged = false;
        for (auto& [node, button] : buttons) if ((button.target & dt::ToolbarParts) && !button.detached && button.hoverRegion) {
            auto previous = button.hover;
            auto sample = dt::ReadToolbarHover(button.hoverRegion, button.hover.revealed);
            bool reveal = button.hover.Update(sample, now);
            if (reveal == previous.revealed) {
                if (!reveal && (flags & button.target)) Apply(node, button, true);
                continue;
            }
            bool owned = button.active;
            if (Apply(node, button, (flags & button.target) && !reveal)) {
                button.hoverRestored = reveal && owned;
                layoutChanged = true;
            } else button.hover = previous;
        }
        if (layoutChanged && finishLayout) FlushToolbarLayouts();
    }
    static DWORD WINAPI Subscribe(void* context) noexcept {
        auto self = static_cast<Tap*>(context);
        HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        HRESULT hr = self->service->AdviseVisualTreeChange(self);
        self->state = SUCCEEDED(hr) ? 1 : -1;
        if (SUCCEEDED(com)) CoUninitialize();
        self->Release();
        return 0;
    }
    static DWORD WINAPI Unsubscribe(void* context) noexcept {
        auto self = static_cast<Tap*>(context);
        HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        HRESULT hr = FAILED(com) ? com : self->service->UnadviseVisualTreeChange(self);
        if (SUCCEEDED(com)) CoUninitialize();
        if (!PostMessageW(self->window, dt::DetachComplete, static_cast<WPARAM>(hr), 0)) self->state = -2;
        self->Release();
        return 0;
    }
    HRESULT BeginDetach() {
        if (messageDepth > 1 || !dt::CanDetachSettings() || (state != 1 && state != -2))
            return HRESULT_FROM_WIN32(ERROR_BUSY);
        if (!stopping) {
            if (Message(window, dt::Configure, 0, 0) != 0 || Message(window, dt::Active, 0, 0) != 0) return E_FAIL;
            HRESULT hr = dt::DetachSettings();
            if (FAILED(hr)) return hr;
        }
        stopping = true;
        toolbarLayoutEnabled = false;
        KillTimer(window, ScopeTimer);
        KillTimer(window, HoverTimer);
        KillTimer(window, PageTimer);
        pendingRecent.clear();
        state = 2;
        AddRef();
        HANDLE thread = CreateThread(nullptr, 0, Unsubscribe, this, 0, nullptr);
        if (!thread) { DWORD error = GetLastError(); Release(); state = -2; return HRESULT_FROM_WIN32(error); }
        CloseHandle(thread);
        return S_OK;
    }
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) noexcept {
        auto self = reinterpret_cast<Tap*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (msg == WM_NCCREATE) {
            self = static_cast<Tap*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (self) {
            self->AddRef();
            ++self->messageDepth;
            LRESULT result;
            try { result = self->Message(hwnd, msg, wp, lp); }
            catch (...) { result = -1; }
            --self->messageDepth;
            self->Release();
            return result;
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
    LRESULT Message(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        if (msg == dt::ToolbarLayout) {
            // Keep the pending bit set during the pass: changes caused by our
            // own measure/arrange must not enqueue an endless refresh loop.
            struct Complete { std::atomic<bool>& pending; ~Complete() { pending = false; } } complete{toolbarLayoutPending};
            if (stopping || !(flags & dt::Toolbar)) return 0;
            UpdateHover(false);
            if (flags & dt::PageFlags) dt::TickPages(pages);
            return FlushToolbarLayouts();
        }
        if (msg == WM_TIMER && wp == PageTimer) {
            if (stopping) return 0;
            dt::TickPages(pages);
            if (pageAttempts) --pageAttempts;
            if (!(flags & dt::PageFlags) && !pageAttempts) KillTimer(hwnd, PageTimer);
            dt::RefreshSettings();
            return 0;
        }
        if (msg == WM_TIMER && wp == HoverTimer) {
            if (!stopping) UpdateHover();
            return 0;
        }
        if (msg == WM_TIMER && wp == ScopeTimer) {
            if (stopping) return 0;
            auto candidates = pendingRecent;
            for (auto const& [node, pending] : candidates) {
                Discover(node, pending.named, dt::Family::Button);
                auto it = pendingRecent.find(node);
                if (it != pendingRecent.end() && ++it->second.attempts >= 20) pendingRecent.erase(it);
            }
            if (pendingRecent.empty()) KillTimer(hwnd, ScopeTimer);
            return 0;
        }
        if (msg == dt::Version) return dt::ProtocolVersion;
        if (msg == dt::State) return state.load();
        if (msg == dt::Detach) return BeginDetach();
        if (msg == dt::DetachComplete) {
            if (FAILED(static_cast<HRESULT>(wp))) { state = -2; return 0; }
            buttons.clear();
            DestroyWindow(hwnd);
            UnregisterClassW(dt::WindowClass, module);
            window = nullptr;
            Release(); // Release the control window's ownership after teardown.
            return 0;
        }
        if (stopping && msg >= WM_APP) return E_ABORT;
        if (msg == dt::LayoutReport) {
            std::wstring report = dt::PagesLayoutReport(pages);
            report += L"\nTOOLBAR LAYOUT (read-only)\n";
            report += L"Toolbar early policy: inlineAttempts="+std::to_wstring(earlyToolbarAttempts)+
                L" visibilityRepairs="+std::to_wstring(visibilityRepairs)+L" deferredAdds="+std::to_wstring(deferredToolbarAdds.load())+L"\n";
            for (auto const& [node, button] : buttons) if (button.target == dt::Toolbar && !button.detached) {
                auto object = button.instance ? button.instance : Instance(node);
                if (object) report += dt::LayoutSnapshot(object.get());
                report += dt::ToolbarLayoutTrace(button.hoverRegion);
                if (report.size() > 220000) break;
            }
            if (report.size() > 240000) { report.resize(240000); report += L"\n[report truncated]\n"; }
            COPYDATASTRUCT copy{dt::LayoutMagic, static_cast<DWORD>((report.size()+1)*sizeof(wchar_t)), report.data()};
            DWORD_PTR result = 0;
            return SendMessageTimeoutW(reinterpret_cast<HWND>(wp), WM_COPYDATA, reinterpret_cast<WPARAM>(hwnd), reinterpret_cast<LPARAM>(&copy), SMTO_ABORTIFHUNG, 2000, &result) ? static_cast<LRESULT>(result) : 0;
        }
        if (msg == dt::PageQuery) return dt::QueryPages(pages, static_cast<unsigned>(wp));
        if (msg == dt::PageOwned) return dt::OwnedPages(pages, static_cast<unsigned>(wp));
        if (msg == dt::PageError) return dt::PagesError(pages);
        if (msg == dt::PageNode) {
            if (flags & dt::Toolbar) UpdateHover(false);
            auto object = Instance(static_cast<InstanceHandle>(wp));
            if (object) dt::ObservePage(pages, static_cast<InstanceHandle>(wp), object.get());
            dt::TickPages(pages);
            pageAttempts = 25; SetTimer(window, PageTimer, 200, nullptr);
            QueueToolbarLayout();
            dt::RefreshSettings();
            return 0;
        }
        if (msg == dt::SettingsState) return dt::SettingsStatus(wp);
        if (msg == dt::SaveSelection) return preferences.Write(flags);
        if (msg == dt::LoadSelection) {
            unsigned saved = 0;
            HRESULT hr = preferences.Read(saved);
            return FAILED(hr) ? static_cast<LRESULT>(hr) : static_cast<LRESULT>(saved);
        }
        if (msg == dt::ChangeAndSave) return ChangeFromSettings(static_cast<unsigned>(wp));
        if (msg == dt::SettingsNode) {
            auto object = Instance(static_cast<InstanceHandle>(wp));
            if (object) dt::ObserveSettings(object.get(), {
                this,
                [](void* context) { return static_cast<Tap*>(context)->flags; },
                [](void* context, unsigned target) {
                    auto self = static_cast<Tap*>(context);
                    if (target == dt::RevealToolbar) return self->EnsureHoverRegions();
                    if (target & dt::PageFlags) return (dt::QueryPages(self->pages, target) & 0xffff) != 0;
                    return (self->Message(self->window, dt::Query, target, 0) & 0xffff) != 0 || self->Message(self->window, dt::Active, target, 0) != 0;
                },
                [](void* context, unsigned selection) {
                    auto self = static_cast<Tap*>(context);
                    return static_cast<HRESULT>(self->Message(self->window, dt::ChangeAndSave, selection, 0));
                },
                [](void* context) { return static_cast<Tap*>(context)->changeError; }
            });
            return 0;
        }
        if (msg == dt::Failure) {
            auto target = static_cast<unsigned>(wp & 0xffffffffULL);
            auto field = static_cast<unsigned>(wp >> 32);
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
        if (msg == dt::AddNode) { Discover(static_cast<InstanceHandle>(wp), static_cast<dt::Target>(static_cast<ULONG_PTR>(lp) & 0xffffffffULL), static_cast<dt::Family>((static_cast<ULONG_PTR>(lp) >> 32) & 0xff)); return 0; }
        if (msg == dt::RemoveNode) {
            dt::ForgetPage(pages, static_cast<InstanceHandle>(wp));
            pendingRecent.erase(static_cast<InstanceHandle>(wp));
            auto it = buttons.find(static_cast<InstanceHandle>(wp));
            if (it != buttons.end()) {
                if (it->second.target & dt::ToolbarParts) dt::RetireToolbarViewport(it->second.hoverRegion);
                // Removal from the visual tree is not proof of object destruction.
                if (it->second.active) it->second.detached = true;
                else buttons.erase(it);
            }
            return 0;
        }
        if (msg == dt::Configure) {
            if (messageDepth > 1) return -1;
            if (wp & ~dt::All) return -1;
            unsigned previous = flags;
            flags = static_cast<unsigned>(wp);
            toolbarLayoutEnabled = (flags & dt::Toolbar) != 0;
            if (!HoverEnabled()) {
                for (auto& [node, button] : buttons) {
                    (void)node;
                    button.hover.Reset(); button.hoverRestored = false;
                }
            }
            if (flags & dt::Toolbar) {
                bool ready = EnsureHoverRegions();
                if ((HoverEnabled() && !ready) || !SetTimer(window, HoverTimer, HoverEnabled() ? 80 : 200, nullptr)) return -1;
            } else KillTimer(window, HoverTimer);
            int failures = 0;
            // Reveal parent hosts before restoring children; hide hosts only
            // after child choices have been applied while their handles are live.
            for (unsigned pass = 0; pass < 3; ++pass) {
                for (auto& [node, button] : buttons) {
                    bool host = (button.target & dt::ToolbarParts) != 0;
                    bool enable = ShouldHide(button);
                    unsigned order = host ? (enable ? 2u : 0u) : 1u;
                    if (pass != order || (!button.active && button.detached)) continue;
                    if (!Apply(node, button, enable)) ++failures;
                }
            }
            for (auto it = buttons.begin(); it != buttons.end();) {
                if (it->second.detached && !it->second.active) it = buttons.erase(it);
                else ++it;
            }
            if (FAILED(dt::ConfigurePages(pages, flags))) ++failures;
            if (((previous | flags) & dt::Toolbar) && FAILED(FlushToolbarLayouts())) ++failures;
            if ((flags & dt::PageFlags) && !SetTimer(window, PageTimer, 200, nullptr)) ++failures;
            else if (!(flags & dt::PageFlags) && !pageAttempts) KillTimer(window, PageTimer);
            dt::RefreshSettings();
            return failures ? -1 : 0;
        }
        if (msg == dt::Active) {
            unsigned count = dt::OwnedPages(pages, static_cast<unsigned>(wp));
            for (const auto& [node, button] : buttons) {
                (void)node;
                if (button.active && (!wp || (button.target & wp))) ++count;
            }
            return count;
        }
        if (msg == dt::OwnedMask) {
            unsigned mask = (dt::OwnedPages(pages, dt::EdgeToEdge) ? dt::EdgeToEdge : 0) | (dt::OwnedPages(pages, dt::SquareSidebar) ? dt::SquareSidebar : 0);
            for (const auto& [node, button] : buttons) {
                (void)node;
                if (button.active) mask |= button.target;
            }
            return mask;
        }
        if (msg == dt::Query || msg == dt::QueryVisible) {
            unsigned matched = 0, hidden = 0;
            for (const auto& [node, button] : buttons) if ((button.target & wp) &&
                (!button.detached || (msg == dt::Query && button.active))) {
                ++matched; // A matched selector stays counted even if Visibility is unreadable.
                InstanceHandle handle = node;
                if (button.active && button.instance &&
                    FAILED(diagnostics->RegisterInstance(button.instance.get(), &handle))) continue;
                dt::Properties p(service, handle);
                auto v = p.Find(L"Visibility");
                if (!v) continue;
                bool hoverVisible = msg == dt::Query && HoverEnabled() && button.hover.revealed &&
                    button.hoverRestored && dt::IsVisible(dt::Text(v->Value));
                if (!v->MetadataBits && (hoverVisible || (msg == dt::Query ? dt::IsCollapsed(dt::Text(v->Value)) : dt::IsVisible(dt::Text(v->Value))))) ++hidden;
            }
            return static_cast<LRESULT>((hidden << 16) | matched);
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
    HRESULT ChangeFromSettings(unsigned selection) {
        changeError.clear();
        unsigned previous = flags;
        bool applied = Message(window, dt::Configure, selection, 0) == 0;
        if (!applied) changeError = dt::PagesErrorText(pages);
        for (unsigned feature : {dt::EdgeToEdge, dt::SquareSidebar}) if ((selection & feature) && (selection & dt::EdgeToEdge)) {
            auto counts = dt::QueryPages(pages, feature);
            if (!(counts & 0xffff) || (counts >> 16) != (counts & 0xffff)) { applied = false; changeError += L" No supported page frame verified. "; }
        }
        for (auto const& item : dt::NativeTargets) if (selection & item.flag) {
            unsigned target = item.flag;
            auto counts = static_cast<unsigned>(Message(window, dt::Query, target, 0));
            if ((counts & 0xffff) == 0 && !item.optional)
                changeError += std::wstring(L"Target not found: ") + item.id + L". ";
            for (const auto& [node, button] : buttons) {
                (void)node;
                if (button.target != target || button.failure == dt::NoFailure) continue;
                wchar_t detail[256]{};
                swprintf_s(detail, L"%s: stage=%u, hr=0x%08lX, metadata=0x%llX, source=%u. ",
                    item.id, static_cast<unsigned>(button.failure), static_cast<unsigned long>(button.error),
                    static_cast<unsigned long long>(button.metadata), static_cast<unsigned>(button.source));
                changeError += detail;
            }
            applied = applied && ((item.optional && counts == 0) ||
                ((counts & 0xffff) != 0 && (counts >> 16) == (counts & 0xffff)));
        }
        HRESULT hr = applied ? preferences.Write(selection) : E_FAIL;
        if (FAILED(hr)) {
            if (changeError.empty()) {
                wchar_t detail[64]{};
                swprintf_s(detail, L"Apply/save error: 0x%08lX.", static_cast<unsigned long>(hr));
                changeError = detail;
            }
            Message(window, dt::Configure, previous, 0);
        }
        return hr;
    }
    std::shared_ptr<IInspectable> Instance(InstanceHandle node) {
        IInspectable* instance = nullptr;
        HRESULT hr = diagnostics->GetIInspectableFromHandle(node, &instance);
        if (FAILED(hr)) { lastQueryError = hr; return {}; }
        return Adopt(instance);
    }
    void Discover(InstanceHandle node, dt::Target namedTarget, dt::Family family) {
        auto existing = buttons.find(node);
        if (existing != buttons.end()) {
            if (existing->second.detached) dt::ReattachToolbarViewport(existing->second.hoverRegion);
            existing->second.detached = false;
            Apply(node, existing->second, ShouldHide(existing->second));
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
                    dt::ReattachToolbarViewport(saved.hoverRegion);
                    saved.detached = false;
                    auto moved = buttons.emplace(node, std::move(saved)).first;
                    Apply(node, moved->second, ShouldHide(moved->second));
                    return;
                }
            }
        }
        ++inspected;
        dt::Properties p(service, node);
        if (FAILED(p.result)) { ++failedQueries; lastQueryError = p.result; return; }
        auto id = p.Find(L"AutomationId");
        // An unset string can be reported as null. No arbitrary property values
        // or x:Names are retained: the callback passes only known target keys.
        if (!id || id->MetadataBits == IsValueNull || dt::Text(id->Value).empty()) ++missingIds;
        if (family != dt::Family::Host && id && id->MetadataBits && id->MetadataBits != IsValueNull) return;
        Button button;
        // Structural hosts are identified by their exact layout x:Name;
        // their separate UIA ID may be customized by Dia.
        button.target = family == dt::Family::Host ? namedTarget :
            dt::IdentifyButton(id && id->MetadataBits != IsValueNull ? dt::Text(id->Value) : L"", namedTarget, family, true);
        if (button.target == dt::None) return;
        if (button.target == dt::RecentTabs) {
            auto object = Instance(node);
            if (!object || !dt::IsSidebarHeader(object.get())) {
                if (pendingRecent.size() < 32 && pendingRecent.try_emplace(node, PendingScope{namedTarget}).second)
                    SetTimer(window, ScopeTimer, 250, nullptr);
                return;
            }
            pendingRecent.erase(node);
        }
        if (button.target == namedTarget && (family == dt::Family::Host || !id || id->MetadataBits == IsValueNull || dt::Text(id->Value).empty())) ++namedMatches;
        if (button.target & dt::ToolbarParts) {
            auto object = Instance(node);
            if (object) button.hoverRegion = TrackToolbar(object.get());
            if (!button.hoverRegion) { hoverAttempts = 50; SetTimer(window, HoverTimer, 80, nullptr); }
        }
        auto [it, inserted] = buttons.emplace(node, button);
        if (inserted && HoverEnabled() && (button.target & dt::ToolbarParts) && button.hoverRegion)
            it->second.hover.Update(dt::ReadToolbarHover(button.hoverRegion,false),GetTickCount64());
        if (inserted && (flags & button.target)) Apply(node, it->second, ShouldHide(it->second));
        if (inserted) dt::RefreshSettings();
    }
    bool Apply(InstanceHandle node, Button& button, bool enable) {
        struct Applying { bool& value; bool previous; Applying(bool& v):value(v),previous(v) { value=true; } ~Applying() { value=previous; } } applying{applyingPolicy};
        auto fail = [&](dt::Stage stage, HRESULT error) {
            button.failure = stage; button.error = error; return false;
        };
        if (button.target==dt::Toolbar) {
            HRESULT hr=dt::SetToolbarCaption(button.hoverRegion,(flags & dt::Toolbar) && enable);
            if (FAILED(hr)) return fail(dt::AdjustLayout,hr);
        }
        if (button.target & dt::ToolbarParts) {
            HRESULT hr=dt::SetToolbarOverlay(button.hoverRegion,HoverEnabled());
            if (FAILED(hr)) return fail(dt::AdjustLayout,hr);
        }
        if ((button.target & dt::ToolbarParts) && !enable) {
            HRESULT hr = dt::CompactToolbarRow(button.hoverRegion, HoverEnabled());
            if (FAILED(hr)) return fail(dt::AdjustLayout, hr);
        }
        if (!enable && !button.active) return true;
        // A removed visual's old handle can already be invalid. Keep the actual
        // object alive and register it again before attempting restoration.
        if (button.active && button.instance) {
            HRESULT hr = diagnostics->RegisterInstance(button.instance.get(), &node);
            if (FAILED(hr)) { lastQueryError = hr; return fail(dt::RegisterTarget, hr); }
        }
        if (enable && button.target == dt::RecentTabs && !button.detached) {
            auto object = button.instance ? button.instance : Instance(node);
            if (!object || !dt::IsSidebarHeader(object.get())) return fail(dt::VisibilityPolicy, E_INVALIDARG);
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
        if (!v->MetadataBits && dt::IsCollapsed(dt::Text(v->Value))) {
            HRESULT hr = (flags & dt::Toolbar) && (button.target & dt::ToolbarParts) ? dt::CompactToolbarRow(button.hoverRegion, true) : S_OK;
            return SUCCEEDED(hr) ? true : fail(dt::AdjustLayout, hr);
        }
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
            HRESULT layout = (flags & dt::Toolbar) && (button.target & dt::ToolbarParts) ? dt::CompactToolbarRow(button.hoverRegion, true) : S_OK;
            if (FAILED(layout)) return fail(dt::AdjustLayout, layout);
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
    explicit Tap(dt::PreferencesStore store = {}) : preferences(store) {}
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
        dispatcherThread=GetCurrentThreadId();
        pages = dt::CreatePages({this, [](void* context, IInspectable* object, wchar_t const* property) {
            auto self = static_cast<Tap*>(context);
            InstanceHandle node = 0;
            if (FAILED(self->diagnostics->RegisterInstance(object, &node))) return false;
            dt::Properties chain(self->service, node);
            auto value = chain.Find(property);
            if (!value || chain.HasBinding(value->Index)) return false;
            LONGLONG allowed = IsValueHandle | IsValueHandleAndEvaluatedValue;
            if (value->MetadataBits & ~allowed) return false;
            auto source = chain.Source(*value);
            return source == BaseValueSourceLocal || source == BaseValueSourceDefault || source == BaseValueSourceStyle || source == BaseValueSourceBuiltInStyle;
        }});
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
        if (stopping) return S_OK;
        // Even unnamed page presenters can be replaced during a tab switch.
        // Coalesce their notifications and marshal all layout work to the UI.
        struct LayoutNotice { Tap* self; ~LayoutNotice() { self->QueueToolbarLayout(); } } notice{this};
        // Live host callbacks on this UI thread can collapse a replacement
        // before its first measure. Replay/worker callbacks stay marshalled.
        if (mutation==Remove && TryLiveToolbar(dt::RemoveNode,element.Handle)) return S_OK;
        UINT msg = mutation == Remove ? dt::RemoveNode : dt::AddNode;
        dt::Target namedTarget = dt::None;
        dt::Family family = dt::Family::Button;
        if (mutation == Add) {
            ++added;
            auto type = dt::Text(element.Type);
            if (dt::Text(element.Name) == L"VariantHost")
                return PostMessageW(window, dt::PageNode, static_cast<WPARAM>(element.Handle), 0) ? S_OK : HRESULT_FROM_WIN32(GetLastError());
            if (type == L"Microsoft.UI.Xaml.Controls.NavigationView" || type == L"NavigationView")
                return PostMessageW(window, dt::SettingsNode, static_cast<WPARAM>(element.Handle), 0) ? S_OK : HRESULT_FROM_WIN32(GetLastError());
            // Dia uses custom host types as well as built-in panels/presenters.
            // Exact known host x:Names identify the wrapper independently of type.
            namedTarget = dt::IdentifyHost(dt::Text(element.Name));
            if (namedTarget != dt::None) family = dt::Family::Host;
            else {
                if (!dt::IsButtonType(type)) return S_OK;
                namedTarget = dt::Identify(dt::Text(element.Name), dt::Family::Button, true);
            }
            ++eligible;
            if ((namedTarget & dt::ToolbarParts) && toolbarLayoutEnabled && !TryLiveToolbar(dt::AddNode,element.Handle,namedTarget)) ++deferredToolbarAdds;
        }
        return PostMessageW(window, msg, static_cast<WPARAM>(element.Handle), namedTarget | (static_cast<LPARAM>(family) << 32)) ? S_OK : HRESULT_FROM_WIN32(GetLastError());
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
