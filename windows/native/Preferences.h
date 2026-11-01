#pragma once
#include <windows.h>
#include "Rules.h"

namespace dt {
inline unsigned LoadPreferences() {
    DWORD flags = 0, bytes = sizeof(flags);
    LSTATUS status = RegGetValueW(HKEY_CURRENT_USER, L"Software\\DiaTweaks", L"WindowsFlags",
                                  RRF_RT_REG_DWORD, nullptr, &flags, &bytes);
    return status == ERROR_SUCCESS && !(flags & ~All) ? flags : 0;
}
inline HRESULT SavePreferences(unsigned flags) {
    if (flags & ~All) return E_INVALIDARG;
    HKEY key = nullptr;
    LSTATUS status = RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\DiaTweaks", 0, nullptr, 0,
                                    KEY_SET_VALUE, nullptr, &key, nullptr);
    if (status != ERROR_SUCCESS) return HRESULT_FROM_WIN32(status);
    DWORD value = flags;
    status = RegSetValueExW(key, L"WindowsFlags", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value));
    RegCloseKey(key);
    return HRESULT_FROM_WIN32(status);
}
}
