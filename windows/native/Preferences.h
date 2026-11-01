#pragma once
#include <windows.h>
#include "Rules.h"

namespace dt {
// Production callers use this only inside Dia. A packaged app and an external
// launcher need not see the same registry view. The root/path parameters allow
// persistence tests to use disposable keys without touching real preferences.
struct PreferencesStore {
    HKEY root = HKEY_CURRENT_USER;
    const wchar_t* path = L"Software\\DiaTweaks";

    HRESULT Read(unsigned& selection) const {
        DWORD flags = 0, bytes = sizeof(flags);
        LSTATUS status = RegGetValueW(root, path, L"WindowsFlags", RRF_RT_REG_DWORD,
                                      nullptr, &flags, &bytes);
        if (status == ERROR_FILE_NOT_FOUND || status == ERROR_PATH_NOT_FOUND) {
            selection = 0; return S_OK;
        }
        if (status != ERROR_SUCCESS) return HRESULT_FROM_WIN32(status);
        if (flags & ~All) return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        selection = flags;
        return S_OK;
    }
    HRESULT Write(unsigned flags) const {
        if (flags & ~All) return E_INVALIDARG;
        HKEY key = nullptr;
        LSTATUS status = RegCreateKeyExW(root, path, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr);
        if (status != ERROR_SUCCESS) return HRESULT_FROM_WIN32(status);
        DWORD value = flags;
        status = RegSetValueExW(key, L"WindowsFlags", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value));
        RegCloseKey(key);
        if (status != ERROR_SUCCESS) return HRESULT_FROM_WIN32(status);
        unsigned verified = 0;
        HRESULT hr = Read(verified);
        return FAILED(hr) ? hr : (verified == flags ? S_OK : E_FAIL);
    }
};
}
