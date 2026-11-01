#pragma once
#include <windows.h>
#include <inspectable.h>
#include <string>

namespace dt {
struct SettingsApi {
    void* context = nullptr;
    unsigned (*read)(void*) = nullptr;
    bool (*available)(void*, unsigned) = nullptr;
    HRESULT (*write)(void*, unsigned) = nullptr;
    std::wstring (*failure)(void*) = nullptr;
};
bool IsSidebarHeader(IInspectable* object) noexcept;
enum SettingsField : WPARAM { SettingsCandidates, SettingsInstalled, SettingsLastError };
#ifdef DT_SETTINGS_UI
void ObserveSettings(IInspectable* navigationView, SettingsApi api) noexcept;
void RefreshSettings() noexcept;
bool CanDetachSettings() noexcept;
HRESULT DetachSettings() noexcept;
LRESULT SettingsStatus(WPARAM field) noexcept;
#else
inline void ObserveSettings(IInspectable*, SettingsApi) noexcept {}
inline void RefreshSettings() noexcept {}
inline bool CanDetachSettings() noexcept { return true; }
inline HRESULT DetachSettings() noexcept { return S_OK; }
inline LRESULT SettingsStatus(WPARAM) noexcept { return 0; }
#endif
}
