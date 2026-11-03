#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../native/NativeViewport.h"
#include <stdexcept>
#include <iostream>
#include <thread>
#include <future>

namespace {
constexpr UINT Attempt=WM_APP+70;
WNDPROC nextExtra=nullptr;
LRESULT CALLBACK Extra(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) { return CallWindowProcW(nextExtra,hwnd,msg,wp,lp); }
void Check(bool yes,char const* text) { if (!yes) throw std::runtime_error(text); }
LRESULT CALLBACK Proc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
    if (msg==Attempt) {
        auto r=reinterpret_cast<RECT*>(lp);
        return SetWindowPos(hwnd,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);
    }
    if (msg==WM_CLOSE) { DestroyWindow(hwnd); PostQuitMessage(0); return 0; }
    return DefWindowProcW(hwnd,msg,wp,lp);
}
RECT Rect(HWND child,HWND owner) {
    RECT r{}; GetWindowRect(child,&r);
    POINT p{r.left,r.top}; ScreenToClient(owner,&p);
    return {p.x,p.y,p.x+r.right-r.left,p.y+r.bottom-r.top};
}
bool Same(RECT a,RECT b) { return a.left==b.left && a.top==b.top && a.right==b.right && a.bottom==b.bottom; }
HWND Owner() {
    auto hwnd=CreateWindowExW(0,L"ViewportTestOwner",L"",WS_POPUP|WS_VISIBLE,50,50,1100,850,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    Check(hwnd!=nullptr,"owner window"); return hwnd;
}
HWND Child(HWND owner,wchar_t const* type,int x,int y,int w,int h) {
    auto hwnd=CreateWindowExW(0,type,L"",WS_CHILD|WS_VISIBLE,x,y,w,h,owner,nullptr,GetModuleHandleW(nullptr),nullptr);
    Check(hwnd!=nullptr,"viewport window"); return hwnd;
}
bool Covered(HWND hwnd,int y) {
    auto region=CreateRectRgn(0,0,0,0);
    int type=GetWindowRgn(hwnd,region);
    bool covered=type!=ERROR && !PtInRegion(region,10,y);
    DeleteObject(region); return covered;
}
void AttemptSize(HWND hwnd,RECT bad) {
    DWORD_PTR result=0;
    Check(SendMessageTimeoutW(hwnd,Attempt,0,reinterpret_cast<LPARAM>(&bad),SMTO_ABORTIFHUNG,2000,&result)!=0 && result!=0,"native resize request completes");
}
}
int main() {
    try {
        std::thread([] { Sleep(15000); std::cerr << "Native viewport test timed out\n"; TerminateProcess(GetCurrentProcess(),99); }).detach();
        WNDCLASSW c{}; c.hInstance=GetModuleHandleW(nullptr); c.lpfnWndProc=Proc; c.lpszClassName=L"ViewportTestOwner";
        Check(RegisterClassW(&c)!=0,"register owner");
        c.lpszClassName=L"Chrome_WidgetWin_1"; Check(RegisterClassW(&c)!=0,"register native host");
        c.lpszClassName=L"Chrome_RenderWidgetHostHWND"; Check(RegisterClassW(&c)!=0,"register renderer");
        c.lpszClassName=L"UnrelatedViewport"; Check(RegisterClassW(&c)!=0,"register unrelated class");
        HWND owner=Owner();
        HWND view=Child(owner,L"Chrome_WidgetWin_1",200,44,796,752);
        HWND other=Child(owner,L"Chrome_WidgetWin_1",0,44,180,752);
        HWND unrelated=Child(owner,L"UnrelatedViewport",200,44,796,752);
        auto original=GetWindowLongPtrW(view,GWLP_WNDPROC);
        auto otherProc=GetWindowLongPtrW(other,GWLP_WNDPROC);
        auto unrelatedProc=GetWindowLongPtrW(unrelated,GWLP_WNDPROC);
        auto guard=dt::CreateNativeViewportGuard();
        RECT full{200,4,1000,800}, bad{200,44,996,796};
        Check(SUCCEEDED(dt::UpdateNativeViewportGuard(guard,owner,full)),"arm native guard");
        Check(Same(Rect(view,owner),full),"initial stale geometry synchronized");
        auto replacement=guard;
        guard.reset(); // Old toolbar retires; the shared target must stay active.
        AttemptSize(view,{200,8,996,796});
        Check(Same(Rect(view,owner),full),"small original inset rejected during shared guard handoff");
        guard=std::move(replacement);
        for (unsigned i=0;i<5;++i) {
            AttemptSize(view,bad);
            Check(Same(Rect(view,owner),full),"native tab resize cannot shrink or move viewport");
        }
        SetWindowPos(view,nullptr,200,44,796,752,SWP_NOZORDER|SWP_NOACTIVATE|SWP_NOSENDCHANGING);
        Check(Same(Rect(view,owner),full),"resize that bypasses changing notification is repaired on the native thread");
        Check(GetWindowLongPtrW(other,GWLP_WNDPROC)==otherProc && GetWindowLongPtrW(unrelated,GWLP_WNDPROC)==unrelatedProc,
              "other pane and unrelated class are not subclassed");
        RECT larger{200,4,1050,820};
        Check(SUCCEEDED(dt::UpdateNativeViewportGuard(guard,owner,larger,false)),"publish window resize");
        AttemptSize(view,bad);
        Check(Same(Rect(view,owner),larger),"new target follows real window resize");
        // A newly created renderer uses the guarded native parent's full client.
        RECT client{}; GetClientRect(view,&client);
        auto renderer=Child(view,L"Chrome_RenderWidgetHostHWND",0,0,client.right-4,client.bottom-40);
        Check(Rect(renderer,view).bottom==816,"new renderer starts at full native client height");
        AttemptSize(renderer,{0,0,client.right-4,client.bottom-40});
        Check(Rect(renderer,view).bottom==816,"replacement renderer cannot independently shrink inside its full-size host");
        Check(dt::NativeViewportGuardReport(guard).find(L"preventedResizes=0")==std::wstring::npos,"intercepted resize attempts are reported");
        RECT beforeOverlay{}; GetClientRect(view,&beforeOverlay);
        Check(SUCCEEDED(dt::UpdateNativeViewportGuard(guard,owner,larger,false,40)),"show floating toolbar");
        RECT duringOverlay{}; GetClientRect(view,&duringOverlay);
        Check(Same(beforeOverlay,duringOverlay) && Same(Rect(view,owner),larger),"floating reveal never resizes native client or moves its origin");
        Check(Covered(view,10) && !Covered(view,60),"only the toolbar band is excluded from native painting and input");
        AttemptSize(view,bad);
        Check(Same(Rect(view,owner),larger) && Covered(view,10),"native relayout preserves floating clip and full viewport");
        Check(SUCCEEDED(dt::UpdateNativeViewportGuard(guard,owner,larger,false,0)),"hide floating toolbar");
        auto read=CreateRectRgn(0,0,0,0);
        Check(GetWindowRgn(view,read)==ERROR,"hide restores originally unrestricted window region");
        auto originalRegion=CreateRectRgn(0,0,840,800);
        auto supplied=CreateRectRgn(0,0,840,800);
        Check(SetWindowRgn(view,supplied,TRUE)!=0,"install app region");
        Check(SUCCEEDED(dt::UpdateNativeViewportGuard(guard,owner,larger,false,40)),"clip an existing app region");
        Check(Covered(view,10) && !Covered(view,60),"floating clip intersects existing app region");
        Check(SUCCEEDED(dt::UpdateNativeViewportGuard(guard,owner,larger,false,0)),"restore existing app region");
        Check(GetWindowRgn(view,read)!=ERROR && EqualRgn(read,originalRegion),"original region restored exactly");
        Check(SUCCEEDED(dt::UpdateNativeViewportGuard(guard,owner,larger,false,40)),"reveal before detach");
        dt::SuspendNativeViewportGuard(guard);
        Check(GetWindowRgn(view,read)!=ERROR && EqualRgn(read,originalRegion),"unload restores clipping before removing native hook");
        // Dia changing the region ends our ownership; hiding must preserve it.
        Check(SUCCEEDED(dt::UpdateNativeViewportGuard(guard,owner,larger,false,40)),"reveal before app takeover");
        auto replacementRegion=CreateRectRgn(0,0,830,790);
        auto replacementCopy=CreateRectRgn(0,0,830,790);
        Check(SetWindowRgn(view,replacementRegion,TRUE)!=0,"app takes region ownership");
        Check(SUCCEEDED(dt::UpdateNativeViewportGuard(guard,owner,larger,false,0)),"hide after app takeover");
        Check(GetWindowRgn(view,read)!=ERROR && EqualRgn(read,replacementCopy),"hide preserves app-owned replacement region");
        DeleteObject(originalRegion); DeleteObject(replacementCopy); DeleteObject(read);
        dt::SuspendNativeViewportGuard(guard);
        Check(GetWindowLongPtrW(view,GWLP_WNDPROC)==original,"original window procedure restored");
        AttemptSize(view,bad);
        Check(Same(Rect(view,owner),bad),"toolbar reveal releases geometry constraint");
        Check(SUCCEEDED(dt::UpdateNativeViewportGuard(guard,owner,full)),"re-arm after reveal");
        nextExtra=reinterpret_cast<WNDPROC>(SetWindowLongPtrW(view,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(Extra)));
        dt::SuspendNativeViewportGuard(guard);
        Check(reinterpret_cast<WNDPROC>(GetWindowLongPtrW(view,GWLP_WNDPROC))==Extra,"preserve a later subclass");
        AttemptSize(view,bad);
        Check(Same(Rect(view,owner),bad),"dormant link passes native geometry through");
        Check(SUCCEEDED(dt::UpdateNativeViewportGuard(guard,owner,full)),"reuse dormant link without stacking it twice");
        AttemptSize(view,bad);
        Check(Same(Rect(view,owner),full),"reactivated middle link constrains native size");
        dt::SuspendNativeViewportGuard(guard);
        SetWindowLongPtrW(view,GWLP_WNDPROC,original);
        Check(SUCCEEDED(dt::UpdateNativeViewportGuard(guard,owner,full)),"re-arm after removing later subclass");
        guard.reset();
        Check(GetWindowLongPtrW(view,GWLP_WNDPROC)==original,"guard destruction removes active procedure");
        DestroyWindow(owner);

        // Chromium and XAML can have different UI threads. The callback reads
        // only a published RECT, and initial positioning never blocks XAML.
        std::promise<std::pair<HWND,HWND>> ready;
        std::thread native([&] {
            auto host=Owner(), child=Child(host,L"Chrome_WidgetWin_1",200,44,796,752);
            ready.set_value({host,child});
            MSG msg{}; while(GetMessageW(&msg,nullptr,0,0)>0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
        });
        auto windows=ready.get_future().get();
        auto threaded=dt::CreateNativeViewportGuard();
        bool armed=SUCCEEDED(dt::UpdateNativeViewportGuard(threaded,windows.first,full,true,40));
        if (armed) AttemptSize(windows.second,bad);
        bool correct=Same(Rect(windows.second,windows.first),full) && Covered(windows.second,10);
        dt::SuspendNativeViewportGuard(threaded);
        AttemptSize(windows.second,bad);
        for (unsigned i=0;i<100 && Covered(windows.second,10);++i) Sleep(5);
        bool released=Same(Rect(windows.second,windows.first),bad) && !Covered(windows.second,10);
        PostMessageW(windows.first,WM_CLOSE,0,0); native.join();
        Check(armed && correct && released,"cross-thread guard applies and detaches without UI-thread calls");
        std::cout << "Native viewport resize interception, pane isolation, resize, restore, floating overlay clip and cross-thread checks passed\n";
        return 0;
    } catch(std::exception const& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
