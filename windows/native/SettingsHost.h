#pragma once
#include <windows.h>
#include <inspectable.h>

namespace dt {
struct SettingsApi {
    void* context = nullptr;
    unsigned (*read)(void*) = nullptr;
    bool (*available)(void*, unsigned) = nullptr;
    HRESULT (*write)(void*, unsigned) = nullptr;
};
enum SettingsField : WPARAM { SettingsCandidates, SettingsInstalled, SettingsLastError };
#ifdef DT_SETTINGS_UI
void ObserveSettings(IInspectable* navigationView, SettingsApi api) noexcept;
void RefreshSettings() noexcept;
LRESULT SettingsStatus(WPARAM field) noexcept;
#else
inline void ObserveSettings(IInspectable*, SettingsApi) noexcept {}
inline void RefreshSettings() noexcept {}
inline LRESULT SettingsStatus(WPARAM) noexcept { return 0; }
#endif
}
