#pragma once
#include <windows.h>

namespace dt {
inline constexpr wchar_t WindowClass[] = L"DiaTweaks.Windows.Tap.v1";
inline constexpr UINT AddNode = WM_APP + 1, RemoveNode = WM_APP + 2;
inline constexpr UINT Configure = WM_APP + 3, State = WM_APP + 4;
inline constexpr UINT Query = WM_APP + 5, Flags = WM_APP + 6, Active = WM_APP + 7;
inline constexpr UINT Version = WM_APP + 8, Stats = WM_APP + 9;
inline constexpr UINT OwnedMask = WM_APP + 10, QueryVisible = WM_APP + 11;
inline constexpr UINT Failure = WM_APP + 12;
inline constexpr LRESULT ProtocolVersion = 4;
enum FailureField : unsigned { FailureStage, FailureError, FailureMetadata, FailureSource };
enum Stage : unsigned {
    NoFailure, RegisterTarget, ReadVisibility, VisibilityType, VisibilityPolicy,
    HoldTarget, CreateCollapsed, SetCollapsed, VerifyCollapsed, CreateOriginal,
    SetOriginal, ClearOriginal, VerifyRestored
};
enum Stat : WPARAM {
    Added, Eligible, Inspected, FailedQueries, MissingIds, NamedMatches,
    Tracked, MissingVisibility, LastQueryError, DetachedOverrides
};
// This CLSID is private to the TAP DLL. It is never registered system-wide.
inline constexpr CLSID TapClsid = {0xd1a7eac5, 0x922b, 0x41f0, {0x89, 0x7b, 0xb3, 0x03, 0x51, 0x57, 0x8e, 0xa4}};
}
