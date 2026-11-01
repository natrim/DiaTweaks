#pragma once
#include <windows.h>
#ifdef DT_PRODUCTION_BUILD
#include <DiaTweaksBuild.h>
#else
#define DT_SESSION_SUFFIX L"tests"
#define DT_TAP_CLSID {0xd1a7eac5, 0x922b, 0x41f0, {0x89, 0x7b, 0xb3, 0x03, 0x51, 0x57, 0x8e, 0xa4}}
#endif

namespace dt {
inline constexpr wchar_t WindowPrefix[] = L"DiaTweaks.Windows.Tap.";
inline constexpr wchar_t WindowClass[] = L"DiaTweaks.Windows.Tap." DT_SESSION_SUFFIX;
inline constexpr UINT AddNode = WM_APP + 1, RemoveNode = WM_APP + 2;
inline constexpr UINT Configure = WM_APP + 3, State = WM_APP + 4;
inline constexpr UINT Query = WM_APP + 5, Flags = WM_APP + 6, Active = WM_APP + 7;
inline constexpr UINT Version = WM_APP + 8, Stats = WM_APP + 9;
inline constexpr UINT OwnedMask = WM_APP + 10, QueryVisible = WM_APP + 11;
inline constexpr UINT Failure = WM_APP + 12;
inline constexpr UINT SettingsNode = WM_APP + 13, SettingsState = WM_APP + 14, SaveSelection = WM_APP + 15;
inline constexpr UINT LoadSelection = WM_APP + 16, ChangeAndSave = WM_APP + 17;
inline constexpr LRESULT ProtocolVersion = 11;
// Keep detach messages and state values compatible across future protocol versions.
inline constexpr LRESULT DetachProtocolVersion = 11;
inline constexpr UINT Detach = WM_APP + 18, DetachComplete = WM_APP + 19;
enum FailureField : unsigned { FailureStage, FailureError, FailureMetadata, FailureSource };
enum Stage : unsigned {
    NoFailure, RegisterTarget, ReadVisibility, VisibilityType, VisibilityPolicy,
    HoldTarget, CreateCollapsed, SetCollapsed, VerifyCollapsed, CreateOriginal,
    SetOriginal, ClearOriginal, VerifyRestored, AdjustLayout
};
enum Stat : WPARAM {
    Added, Eligible, Inspected, FailedQueries, MissingIds, NamedMatches,
    Tracked, MissingVisibility, LastQueryError, DetachedOverrides
};
// This CLSID is private to the TAP DLL. It is never registered system-wide.
inline constexpr CLSID TapClsid = DT_TAP_CLSID;
}
