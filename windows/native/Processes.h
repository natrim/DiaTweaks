#pragma once
#include <windows.h>
#include <filesystem>
#include <set>
#include <string>

namespace dt {
inline bool IsDiaProcess(DWORD pid) {
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) return false;
    wchar_t path[32768]; DWORD length = static_cast<DWORD>(std::size(path));
    bool found = QueryFullProcessImageNameW(process, 0, path, &length) &&
        _wcsicmp(std::filesystem::path(std::wstring(path, length)).filename().c_str(), L"Dia.exe") == 0;
    CloseHandle(process);
    return found;
}
inline std::set<DWORD> FindDia() {
    std::set<DWORD> result;
    EnumWindows([](HWND hwnd, LPARAM data) -> BOOL {
        wchar_t cls[128]{};
        GetClassNameW(hwnd, cls, static_cast<int>(std::size(cls)));
        if (wcscmp(cls, L"WinUIDesktopWin32WindowClass") != 0) return TRUE;
        DWORD pid = 0; GetWindowThreadProcessId(hwnd, &pid);
        if (IsDiaProcess(pid)) reinterpret_cast<std::set<DWORD>*>(data)->insert(pid);
        return TRUE;
    }, reinterpret_cast<LPARAM>(&result));
    return result;
}
}
