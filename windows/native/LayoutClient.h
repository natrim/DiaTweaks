#pragma once
#include <windows.h>
#include <string>
#include <stdexcept>
#include "Protocol.h"
namespace dt::layout {
struct Reply { HWND expected; bool received=false; std::wstring text; };
inline LRESULT CALLBACK Receive(HWND window, UINT message, WPARAM wp, LPARAM lp) {
    auto state=reinterpret_cast<Reply*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if(message==WM_NCCREATE) {
        state=static_cast<Reply*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(state));
    }
    if(message==WM_COPYDATA && state && reinterpret_cast<HWND>(wp)==state->expected) {
        auto copy=reinterpret_cast<COPYDATASTRUCT*>(lp);
        if(!copy || copy->dwData!=LayoutMagic || !copy->lpData || copy->cbData<sizeof(wchar_t) || copy->cbData>512000 || copy->cbData%sizeof(wchar_t)) return FALSE;
        auto text=static_cast<wchar_t const*>(copy->lpData);
        size_t size=copy->cbData/sizeof(wchar_t);
        if(text[size-1]!=L'\0') return FALSE;
        state->text.assign(text,size-1); state->received=true; return TRUE;
    }
    return DefWindowProcW(window,message,wp,lp);
}
inline std::wstring Capture(HWND target, DWORD timeout=10000) {
    if(!IsWindow(target)) throw std::runtime_error("No live DiaTweaks session for layout inspection.");
    WNDCLASSW cls{}; cls.lpfnWndProc=Receive; cls.hInstance=GetModuleHandleW(nullptr); cls.lpszClassName=L"DiaTweaks.LayoutReply.v1";
    if(!RegisterClassW(&cls) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS) throw std::runtime_error("Cannot initialize the layout reply window.");
    Reply reply{target};
    HWND window=CreateWindowExW(0,cls.lpszClassName,L"",0,0,0,0,0,HWND_MESSAGE,nullptr,cls.hInstance,&reply);
    if(!window) throw std::runtime_error("Cannot create the layout reply window.");
    struct Cleanup { HWND hwnd; ~Cleanup(){DestroyWindow(hwnd);} } cleanup{window};
    if(!PostMessageW(target,LayoutReport,reinterpret_cast<WPARAM>(window),0)) throw std::runtime_error("Cannot request layout diagnostics.");
    auto deadline=GetTickCount64()+timeout;
    while(!reply.received && GetTickCount64()<deadline) {
        MSG message{};
        while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
        if(!reply.received) Sleep(10);
    }
    if(!reply.received) throw std::runtime_error("Layout inspection timed out. No page properties were changed by inspection.");
    return reply.text;
}
}
