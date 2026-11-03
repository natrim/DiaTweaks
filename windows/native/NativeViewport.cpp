#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "NativeViewport.h"
#include "NativeViewportClip.h"
#include <map>
#include <mutex>
#include <vector>
#include <sstream>
#include <cmath>
#include <algorithm>
#include <string_view>

namespace {
struct Target;
struct Hook { WNDPROC original = nullptr; std::weak_ptr<Target> target; Target* identity = nullptr; bool installed = false; bool pendingDetach=false; std::shared_ptr<dt::NativeViewportClip> clip=std::make_shared<dt::NativeViewportClip>(); };
std::recursive_mutex gate;
std::map<HWND, Hook> hooks;
LRESULT CALLBACK WindowProc(HWND, UINT, WPARAM, LPARAM);
void Position(HWND, HWND, RECT);
void Clip(HWND,std::shared_ptr<Target> const&);
void FinishDetach(HWND);
HRESULT Attach(HWND, std::shared_ptr<Target> const&);
UINT SyncMessage() { static UINT message=RegisterWindowMessageW(L"DiaTweaks.NativeViewport.SyncBounds.v1"); return message; }
struct Target {
    HWND owner = nullptr;
    RECT client{};
    bool enabled = false;
    unsigned corrections = 0;
    int occludedTop = 0;
    HRESULT error = S_OK;
    ~Target();
};
bool Same(RECT a, RECT b) { return a.left==b.left && a.top==b.top && a.right==b.right && a.bottom==b.bottom; }
void Suspend(Target* target) {
    std::vector<HWND> pending;
    {
        std::lock_guard lock(gate);
        target->enabled=false;
        for (auto& [hwnd,hook]:hooks) if (hook.identity==target) {
            hook.pendingDetach=true; hook.target.reset(); hook.identity=nullptr; pending.push_back(hwnd);
        }
    }
    for (auto hwnd:pending) {
        if (GetWindowThreadProcessId(hwnd,nullptr)==GetCurrentThreadId()) FinishDetach(hwnd);
        else PostMessageW(hwnd,SyncMessage(),0,0);
    }
}
void FinishDetach(HWND hwnd) {
    std::shared_ptr<dt::NativeViewportClip> clip;
    { std::lock_guard lock(gate); auto it=hooks.find(hwnd); if (it==hooks.end() || !it->second.pendingDetach) return; clip=it->second.clip; }
    if (FAILED(clip->Set(hwnd,0))) return;
    std::lock_guard lock(gate);
    auto it=hooks.find(hwnd);
    if (it==hooks.end() || !it->second.pendingDetach) return;
    auto& hook=it->second;
    if (IsWindow(hwnd) && reinterpret_cast<WNDPROC>(GetWindowLongPtrW(hwnd,GWLP_WNDPROC))==WindowProc)
        if (SetWindowLongPtrW(hwnd,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(hook.original))) hook.installed=false;
    hook.pendingDetach=false;
}
void Clip(HWND hwnd,std::shared_ptr<Target> const& target) {
    std::shared_ptr<dt::NativeViewportClip> clip; int top=0;
    {
        std::lock_guard lock(gate);
        auto it=hooks.find(hwnd);
        if (it==hooks.end() || !target || !target->enabled) return;
        clip=it->second.clip; top=target->occludedTop;
    }
    wchar_t type[64]{}; GetClassNameW(hwnd,type,64);
    if (std::wstring_view(type)!=L"Chrome_WidgetWin_1") return;
    auto hr=clip->Set(hwnd,top);
    if (FAILED(hr)) { std::lock_guard lock(gate); target->error=hr; }
}
Target::~Target() { Suspend(this); }
LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    WNDPROC original = nullptr;
    std::shared_ptr<Target> target;
    {
        std::lock_guard lock(gate);
        auto it = hooks.find(hwnd);
        if (it != hooks.end()) { original = it->second.original; target = it->second.target.lock(); }
    }
    if (msg==WM_PARENTNOTIFY && LOWORD(wp)==WM_CREATE && target) {
        auto child=reinterpret_cast<HWND>(lp);
        wchar_t type[64]{}; GetClassNameW(child,type,64);
        DWORD process=0; GetWindowThreadProcessId(child,&process);
        HWND owner=nullptr; RECT rect{};
        { std::lock_guard lock(gate); if (target->enabled) { owner=target->owner; rect=target->client; } }
        // Enrol a replacement renderer before CreateWindow returns to Chromium.
        // Its first native resize must use the already full-size host client.
        RECT childRect{}; GetWindowRect(child,&childRect);
        LONG width=childRect.right-childRect.left, height=childRect.bottom-childRect.top;
        bool fullWidth=width==0 || std::abs(width-(rect.right-rect.left))<=16;
        bool fullHeight=height==0 || std::abs(height-(rect.bottom-rect.top))<=256;
        if (owner && process==GetCurrentProcessId() && std::wstring_view(type)==L"Chrome_RenderWidgetHostHWND" &&
            (GetWindowLongPtrW(child,GWL_STYLE)&WS_CHILD) && fullWidth && fullHeight &&
            GetParent(child)==hwnd && SUCCEEDED(Attach(child,target))) Position(child,owner,rect);
    }
    if (msg==SyncMessage()) {
        HWND owner=nullptr; RECT rect{};
        if (target) {
            std::lock_guard lock(gate);
            if (target->enabled) { owner=target->owner; rect=target->client; }
        }
        if (owner && IsWindow(owner) && IsChild(owner,hwnd)) { Position(hwnd,owner,rect); Clip(hwnd,target); }
        FinishDetach(hwnd);
        return 0;
    }
    LRESULT result = original ? CallWindowProcW(original, hwnd, msg, wp, lp) : DefWindowProcW(hwnd, msg, wp, lp);
    if (msg==WM_WINDOWPOSCHANGED && target && IsWindowVisible(hwnd)) {
        HWND owner=nullptr; RECT rect{};
        { std::lock_guard lock(gate); if (target->enabled) { owner=target->owner; rect=target->client; } }
        // SWP_NOSENDCHANGING bypasses the pre-change notification. Repair
        // that path before the owner thread returns to processing paints.
        if (owner && !IsIconic(owner) && IsChild(owner,hwnd)) { Position(hwnd,owner,rect); Clip(hwnd,target); }
    }
    if (msg == WM_NCDESTROY) {
        std::lock_guard lock(gate);
        if (auto it = hooks.find(hwnd); it != hooks.end()) { it->second.target.reset(); it->second.identity=nullptr; it->second.installed=false; }
    }
    if (msg != WM_WINDOWPOSCHANGING || !lp || !target) return result;
    auto pos = reinterpret_cast<WINDOWPOS*>(lp);
    if ((pos->flags & SWP_HIDEWINDOW) || ((pos->flags & (SWP_NOMOVE|SWP_NOSIZE)) == (SWP_NOMOVE|SWP_NOSIZE))) return result;
    HWND owner = nullptr;
    RECT rect{};
    {
        std::lock_guard lock(gate);
        if (!target->enabled) return result;
        owner = target->owner; rect = target->client;
    }
    if (!IsWindow(owner) || IsIconic(owner) || !IsChild(owner, hwnd)) return result;
    POINT origin{rect.left, rect.top};
    MapWindowPoints(owner, GetParent(hwnd), &origin, 1);
    int width=rect.right-rect.left, height=rect.bottom-rect.top;
    RECT actual{}; GetWindowRect(hwnd, &actual);
    POINT current{actual.left, actual.top}; ScreenToClient(GetParent(hwnd), &current);
    int x=(pos->flags&SWP_NOMOVE)?current.x:pos->x;
    int y=(pos->flags&SWP_NOMOVE)?current.y:pos->y;
    int cx=(pos->flags&SWP_NOSIZE)?actual.right-actual.left:pos->cx;
    int cy=(pos->flags&SWP_NOSIZE)?actual.bottom-actual.top:pos->cy;
    if (x!=origin.x || y!=origin.y || cx!=width || cy!=height) {
        pos->x=origin.x; pos->y=origin.y; pos->cx=width; pos->cy=height;
        pos->flags &= ~(SWP_NOMOVE|SWP_NOSIZE);
        std::lock_guard lock(gate); ++target->corrections;
    }
    return result;
}
HRESULT Attach(HWND hwnd, std::shared_ptr<Target> const& target) {
    std::lock_guard lock(gate);
    auto current = reinterpret_cast<WNDPROC>(GetWindowLongPtrW(hwnd, GWLP_WNDPROC));
    auto it = hooks.find(hwnd);
    if (it != hooks.end()) {
        auto previous = it->second.target.lock();
        if (previous && previous != target && previous->enabled) return HRESULT_FROM_WIN32(ERROR_BUSY);
        if (current == WindowProc || (it->second.installed && current != it->second.original)) {
            // Reuse a dormant link below another subclass. Installing our same
            // procedure above that chain again would recurse into itself.
            it->second.target = target; it->second.identity=target.get(); it->second.installed=true; it->second.pendingDetach=false; return S_OK;
        }
    }
    for (auto dead=hooks.begin(); dead!=hooks.end();) {
        if (dead->second.target.expired() && !IsWindow(dead->first)) dead=hooks.erase(dead); else ++dead;
    }
    if (hooks.size() >= 512 && !hooks.count(hwnd)) return E_OUTOFMEMORY;
    Hook fresh{}; fresh.original=current; fresh.target=target; fresh.identity=target.get(); fresh.installed=true;
    hooks[hwnd]=std::move(fresh);
    SetLastError(0);
    auto previous = SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(WindowProc));
    if (!previous && GetLastError()) { auto error=GetLastError(); hooks.erase(hwnd); return HRESULT_FROM_WIN32(error); }
    hooks[hwnd].original = reinterpret_cast<WNDPROC>(previous);
    return S_OK;
}
void Position(HWND hwnd, HWND owner, RECT rect) {
    if (!IsWindow(owner) || IsIconic(owner) || !IsChild(owner,hwnd)) return;
    POINT origin{rect.left,rect.top}; MapWindowPoints(owner,GetParent(hwnd),&origin,1);
    RECT actual{}; GetWindowRect(hwnd,&actual);
    POINT current{actual.left,actual.top}; ScreenToClient(GetParent(hwnd),&current);
    if (current.x==origin.x && current.y==origin.y && actual.right-actual.left==rect.right-rect.left && actual.bottom-actual.top==rect.bottom-rect.top) return;
    UINT flags=SWP_NOACTIVATE|SWP_NOZORDER|SWP_NOOWNERZORDER;
    // Never block XAML waiting for Chromium's window thread. Future resizes on
    // that thread are corrected synchronously by WM_WINDOWPOSCHANGING.
    if (GetWindowThreadProcessId(hwnd,nullptr)!=GetCurrentThreadId()) {
        // Resolve the latest target on the owner thread. An asynchronous
        // SetWindowPos would retain stale bounds after reveal or unload.
        PostMessageW(hwnd,SyncMessage(),0,0); return;
    }
    SetWindowPos(hwnd,nullptr,origin.x,origin.y,rect.right-rect.left,rect.bottom-rect.top,flags);
}
}
std::shared_ptr<void> dt::CreateNativeViewportGuard() { return std::make_shared<Target>(); }
void dt::SuspendNativeViewportGuard(std::shared_ptr<void> const& value) noexcept {
    if (value) Suspend(static_cast<Target*>(value.get()));
}
HRESULT dt::UpdateNativeViewportGuard(std::shared_ptr<void> const& value, HWND owner, RECT client, bool discover, int occludedTop) noexcept {
    if (!value || !owner || client.right-client.left<=32 || client.bottom-client.top<=32) return E_INVALIDARG;
    try {
        DWORD process=0; GetWindowThreadProcessId(owner,&process);
        if (process!=GetCurrentProcessId()) return E_ACCESSDENIED;
        auto target=std::static_pointer_cast<Target>(value);
        std::vector<HWND> windows;
        {
            std::lock_guard lock(gate);
            bool changed=!target->enabled || target->owner!=owner || !Same(target->client,client) || target->occludedTop!=occludedTop;
            target->owner=owner; target->client=client; target->enabled=true; target->occludedTop=(std::max)(0,occludedTop);
            if (!changed && !discover) return S_OK;
            for (auto const& [hwnd,hook] : hooks) if (hook.identity==target.get() && IsWindow(hwnd) && IsChild(owner,hwnd)) windows.push_back(hwnd);
        }
        if (discover) {
            struct Search { HWND owner; RECT rect; std::vector<HWND>* found; unsigned visited=0; } search{owner,client,&windows};
            EnumChildWindows(owner,[](HWND child,LPARAM data)->BOOL {
                auto s=reinterpret_cast<Search*>(data);
                if (++s->visited>128) return FALSE;
                wchar_t type[64]{}; GetClassNameW(child,type,64);
                if ((std::wstring_view(type)!=L"Chrome_WidgetWin_1" && std::wstring_view(type)!=L"Chrome_RenderWidgetHostHWND") ||
                    !(GetWindowLongPtrW(child,GWL_STYLE)&WS_CHILD) || !IsWindowVisible(child)) return TRUE;
                DWORD process=0; GetWindowThreadProcessId(child,&process);
                if (process!=GetCurrentProcessId()) return TRUE;
                RECT r{}; GetWindowRect(child,&r);
                POINT p{r.left,r.top}; ScreenToClient(s->owner,&p);
                if (std::abs(p.x-s->rect.left)>16 || std::abs(p.x+r.right-r.left-s->rect.right)>16 ||
                    std::abs(p.y-s->rect.top)>256 || r.bottom-r.top<32) return TRUE;
                if (std::find(s->found->begin(),s->found->end(),child)==s->found->end()) s->found->push_back(child);
                return TRUE;
            },reinterpret_cast<LPARAM>(&search));
        }
        HRESULT result=S_OK;
        // Hosts precede renderer children even when an existing handle list
        // happens to be sorted differently from EnumChildWindows traversal.
        for (unsigned pass=0; pass<2; ++pass) for (auto hwnd:windows) {
            wchar_t type[64]{}; GetClassNameW(hwnd,type,64);
            bool host=std::wstring_view(type)==L"Chrome_WidgetWin_1";
            if ((pass==0)!=host) continue;
            if (!host) {
                bool enrolledParent=false;
                std::lock_guard lock(gate);
                for (HWND parent=GetParent(hwnd); parent && parent!=owner; parent=GetParent(parent)) {
                    auto it=hooks.find(parent);
                    if (it!=hooks.end() && it->second.identity==target.get()) { enrolledParent=true; break; }
                }
                if (!enrolledParent) continue;
            }
            auto hr=Attach(hwnd,target);
            if (FAILED(hr)) { result=hr; continue; }
            Position(hwnd,owner,client);
            if (GetWindowThreadProcessId(hwnd,nullptr)==GetCurrentThreadId()) Clip(hwnd,target);
            else PostMessageW(hwnd,SyncMessage(),0,0);
        }
        { std::lock_guard lock(gate); target->error=result; }
        return result;
    } catch (...) { return E_FAIL; }
}
std::wstring dt::NativeViewportGuardReport(std::shared_ptr<void> const& value) {
    if (!value) return L"Native viewport guard unavailable\n";
    auto target=std::static_pointer_cast<Target>(value);
    std::lock_guard lock(gate);
    unsigned count=0;
    for (auto const& [hwnd,hook]:hooks) if (hook.identity==target.get() && IsWindow(hwnd)) ++count;
    std::wostringstream out;
    out << L"Native viewport guard: enabled=" << target->enabled << L" windows=" << count << L" preventedResizes=" << target->corrections
        << L" overlayOcclusion=" << target->occludedTop << L" target=" << target->client.left << L',' << target->client.top << L',' << target->client.right-target->client.left << L',' << target->client.bottom-target->client.top
        << L" hr=0x" << std::hex << static_cast<unsigned long>(target->error) << L'\n';
    return out.str();
}
