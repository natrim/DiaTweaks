#pragma once
#include <windows.h>

namespace dt {
inline constexpr wchar_t WindowClass[] = L"DiaTweaks.Windows.Tap.v1";
inline constexpr UINT AddNode = WM_APP + 1, RemoveNode = WM_APP + 2;
inline constexpr UINT Configure = WM_APP + 3, State = WM_APP + 4;
inline constexpr UINT Query = WM_APP + 5, Flags = WM_APP + 6, Active = WM_APP + 7;
// This CLSID is private to the TAP DLL. It is never registered system-wide.
inline constexpr CLSID TapClsid = {0xd1a7eac5, 0x922b, 0x41f0, {0x89, 0x7b, 0xb3, 0x03, 0x51, 0x57, 0x8e, 0xa4}};
}
