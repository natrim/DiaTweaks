#include "FakeTree.h"

namespace {
struct TestRegistry {
    HKEY key = nullptr;
    std::wstring path = L"Software\\DiaTweaksTests\\Persistence-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
    TestRegistry() {
        Check(RegCreateKeyExW(HKEY_CURRENT_USER, path.c_str(), 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &key, nullptr) == ERROR_SUCCESS, "create disposable registry root");
    }
    ~TestRegistry() { if (key) RegCloseKey(key); RegDeleteTreeW(HKEY_CURRENT_USER, path.c_str()); }
};
struct Session {
    HWND window = nullptr;
    explicit Session(dt::PreferencesStore store) {
        auto fake = new FakeTree;
        auto tap = new Tap(store);
        Check(SUCCEEDED(tap->SetSite(static_cast<IVisualTreeService*>(fake))), "start preference-test backend");
        HWND next = nullptr;
        while ((next = FindWindowExW(HWND_MESSAGE, next, dt::WindowClass, nullptr))) {
            if (GetWindowLongPtrW(next, GWLP_USERDATA) == reinterpret_cast<LONG_PTR>(tap)) { window = next; break; }
        }
        Check(window != nullptr, "locate new backend window");
        ULONGLONG deadline = GetTickCount64() + 5000;
        do {
            Pump();
            if (SendMessageW(window, dt::State, 0, 0) == 1 && (SendMessageW(window, dt::Query, dt::Sidebar, 0) & 0xffff)) return;
            Sleep(5);
        } while (GetTickCount64() < deadline);
        throw std::runtime_error("backend replay timeout");
    }
    LRESULT Send(UINT message, WPARAM value = 0) const { return SendMessageW(window, message, value, 0); }
};
unsigned Read(dt::PreferencesStore const& store) {
    unsigned value = 999;
    Check(SUCCEEDED(store.Read(value)), "read persisted selection");
    return value;
}
}

int main() {
    try {
        Check(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)), "COM");
        module = GetModuleHandleW(nullptr);
        TestRegistry registry;
        // Distinct stores model a package-isolated Dia view and a standalone
        // launcher view. All operations must use the backend's store over IPC.
        dt::PreferencesStore app{registry.key, L"PackagedDia"};
        dt::PreferencesStore launcher{registry.key, L"StandaloneLauncher"};
        Check(SUCCEEDED(launcher.Write(0)), "seed decoy launcher preference");
        Session first(app);
        Check(first.Send(dt::LoadSelection) == 0, "missing preference means off");
        HKEY missing = nullptr;
        Check(RegOpenKeyExW(registry.key, app.path, 0, KEY_READ, &missing) == ERROR_FILE_NOT_FOUND, "startup read must not create or save defaults");
        Check(first.Send(dt::ChangeAndSave, dt::All) == S_OK, "UI change applies and saves");
        Check(Read(app) == dt::All && Read(launcher) == 0, "UI saves only the app-owned store");
        Check(first.Send(dt::LoadSelection) == dt::All, "IPC reads saved switches from Dia");
        Check(first.Send(dt::Configure, 0) == 0, "clear in-memory state without saving");
        Check(Read(app) == dt::All, "refreshing runtime state does not change preferences");

        Session second(app);
        auto loaded = second.Send(dt::LoadSelection);
        Check(loaded == dt::All, "fresh backend sees previous run's saved choices");
        Check(second.Send(dt::Configure, static_cast<WPARAM>(loaded)) == 0, "reapply saved choice");
        Check(second.Send(dt::Query, dt::Chat) == 0x10001 && second.Send(dt::Query, dt::Sidebar) == 0x10001, "saved choice hides both controls");
        Check(Read(app) == dt::All && Read(launcher) == 0, "attachment preserves saved preferences and ignores launcher defaults");
        Check(second.Send(dt::ChangeAndSave, dt::Sidebar) == S_OK && Read(app) == dt::Sidebar, "latest UI change persists");
        Check(second.Send(dt::ChangeAndSave, dt::UnsupportedFlag) < 0 && Read(app) == dt::Sidebar, "rejected setting cannot overwrite saved choice");

        Session third(app);
        Check(third.Send(dt::LoadSelection) == dt::Sidebar, "second reattachment loads the latest selection");
        Check(third.Send(dt::Configure, dt::Sidebar) == 0, "apply latest saved choice");
        Check(third.Send(dt::Configure, 0) == 0 && third.Send(dt::SaveSelection) == S_OK, "explicit restore saves off");
        Check(Read(app) == 0, "restore persisted off");
        Check(SUCCEEDED(app.Write(dt::Chat)) && FAILED(app.Write(dt::UnsupportedFlag)) && Read(app) == dt::Chat, "invalid mask cannot overwrite a valid saved value");

        HKEY appKey = nullptr;
        Check(RegOpenKeyExW(registry.key, app.path, 0, KEY_SET_VALUE | KEY_QUERY_VALUE, &appKey) == ERROR_SUCCESS, "open test app key");
        DWORD invalidMask = dt::UnsupportedFlag;
        Check(RegSetValueExW(appKey, L"WindowsFlags", 0, REG_DWORD, reinterpret_cast<BYTE*>(&invalidMask), sizeof(invalidMask)) == ERROR_SUCCESS, "seed unsupported mask");
        unsigned untouched = 777;
        Check(FAILED(app.Read(untouched)) && untouched == 777, "invalid value is an error, not an off default");
        Check(third.Send(dt::LoadSelection) < 0, "IPC propagates read errors");
        DWORD raw = 0, size = sizeof(raw);
        Check(RegGetValueW(appKey, nullptr, L"WindowsFlags", RRF_RT_REG_DWORD, nullptr, &raw, &size) == ERROR_SUCCESS && raw == dt::UnsupportedFlag, "failed load leaves the stored value untouched");
        const wchar_t bad[] = L"invalid";
        Check(RegSetValueExW(appKey, L"WindowsFlags", 0, REG_SZ, reinterpret_cast<const BYTE*>(bad), sizeof(bad)) == ERROR_SUCCESS, "seed invalid preference type");
        Check(third.Send(dt::LoadSelection) < 0, "wrong registry type is not silently read as off");
        RegCloseKey(appKey);
        std::cout << "Registry persistence, isolated-view IPC, reattachment and read-error checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n'; return 1;
    }
}
