#pragma once
#include <windows.h>
#include <algorithm>
namespace dt {
// Copies are ours; the handle passed successfully to SetWindowRgn is Windows'.
struct NativeViewportClip {
    HRGN original=nullptr, applied=nullptr;
    bool originalPresent=false, owned=false, busy=false;
    ~NativeViewportClip() { Clear(); }
    void Clear() noexcept {
        if (original) DeleteObject(original);
        if (applied) DeleteObject(applied);
        original=applied=nullptr; owned=originalPresent=false;
    }
    static HRGN Copy(HRGN source) {
        auto copy=CreateRectRgn(0,0,0,0);
        if (copy && CombineRgn(copy,source,nullptr,RGN_COPY)!=ERROR) return copy;
        if (copy) DeleteObject(copy);
        return nullptr;
    }
    bool Ours(HWND window) const {
        if (!owned) return false;
        auto current=CreateRectRgn(0,0,0,0);
        if (!current) return false;
        bool same=GetWindowRgn(window,current)!=ERROR && EqualRgn(current,applied);
        DeleteObject(current); return same;
    }
    HRESULT Set(HWND window,int top) noexcept {
        if (busy) return S_OK;
        busy=true;
        struct Done { bool& value; ~Done() { value=false; } } done{busy};
        if (!IsWindow(window)) { Clear(); return S_OK; }
        if (owned && !Ours(window)) Clear(); // Dia took ownership of its region.
        if (top<=0) {
            if (!owned) return S_OK;
            auto restore=originalPresent ? Copy(original) : nullptr;
            if (originalPresent && !restore) return E_OUTOFMEMORY;
            if (!SetWindowRgn(window,restore,TRUE)) { if (restore) DeleteObject(restore); return E_FAIL; }
            Clear(); return S_OK;
        }
        RECT bounds{}; if (!GetWindowRect(window,&bounds)) return E_FAIL;
        int width=bounds.right-bounds.left, height=bounds.bottom-bounds.top;
        if (!owned) {
            Clear();
            original=CreateRectRgn(0,0,0,0);
            if (!original) return E_OUTOFMEMORY;
            originalPresent=GetWindowRgn(window,original)!=ERROR;
        }
        auto desired=CreateRectRgn(0,(std::min)(top,height),width,height);
        if (!desired) return E_OUTOFMEMORY;
        if (originalPresent && CombineRgn(desired,desired,original,RGN_AND)==ERROR) { DeleteObject(desired); return E_FAIL; }
        if (owned && EqualRgn(desired,applied)) { DeleteObject(desired); return S_OK; }
        auto snapshot=Copy(desired);
        if (!snapshot) { DeleteObject(desired); return E_OUTOFMEMORY; }
        if (!SetWindowRgn(window,desired,TRUE)) { DeleteObject(desired); DeleteObject(snapshot); return E_FAIL; }
        if (applied) DeleteObject(applied);
        applied=snapshot; owned=true;
        return S_OK;
    }
};
}
