#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#include <xamlom.h>
#include <filesystem>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
#include "Protocol.h"
#include "Rules.h"
#include "SettingsHost.h"

namespace fs = std::filesystem;
namespace {
struct Handle {
    HANDLE value;
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
};
struct Library {
    HMODULE value;
    ~Library() { if (value) FreeLibrary(value); }
};
bool IsDia(DWORD pid) {
    Handle process{OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)};
    if (!process.value) return false;
    wchar_t path[32768]; DWORD length = static_cast<DWORD>(std::size(path));
    return QueryFullProcessImageNameW(process.value, 0, path, &length) &&
           _wcsicmp(fs::path(std::wstring(path, length)).filename().c_str(), L"Dia.exe") == 0;
}
std::set<DWORD> FindDia() {
    std::set<DWORD> result;
    EnumWindows([](HWND hwnd, LPARAM data) -> BOOL {
        wchar_t cls[128]{};
        GetClassNameW(hwnd, cls, static_cast<int>(std::size(cls)));
        if (wcscmp(cls, L"WinUIDesktopWin32WindowClass") != 0) return TRUE;
        DWORD pid = 0; GetWindowThreadProcessId(hwnd, &pid);
        if (IsDia(pid)) reinterpret_cast<std::set<DWORD>*>(data)->insert(pid);
        return TRUE;
    }, reinterpret_cast<LPARAM>(&result));
    return result;
}
HWND FindTap(DWORD pid) {
    HWND hwnd = nullptr;
    while ((hwnd = FindWindowExW(HWND_MESSAGE, hwnd, dt::WindowClass, nullptr))) {
        DWORD owner = 0; GetWindowThreadProcessId(hwnd, &owner);
        if (owner == pid) return hwnd;
    }
    return nullptr;
}
LRESULT Send(HWND hwnd, UINT message, WPARAM data = 0) {
    DWORD_PTR result = 0;
    if (!SendMessageTimeoutW(hwnd, message, data, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK, 5000, &result))
        throw std::runtime_error("Dia did not respond to the patch controller.");
    return static_cast<LRESULT>(result);
}
void Diagnostics(HWND hwnd) {
    std::wcerr << L"Discovery: added=" << Send(hwnd, dt::Stats, dt::Added)
               << L", buttons=" << Send(hwnd, dt::Stats, dt::Eligible)
               << L", inspected=" << Send(hwnd, dt::Stats, dt::Inspected)
               << L", queryErrors=" << Send(hwnd, dt::Stats, dt::FailedQueries)
               << L", emptyIds=" << Send(hwnd, dt::Stats, dt::MissingIds)
               << L", xNameMatches=" << Send(hwnd, dt::Stats, dt::NamedMatches)
               << L", tracked=" << Send(hwnd, dt::Stats, dt::Tracked)
               << L", missingVisibility=" << Send(hwnd, dt::Stats, dt::MissingVisibility)
               << L", detachedOverrides=" << Send(hwnd, dt::Stats, dt::DetachedOverrides)
               << L", lastQueryError=0x" << std::hex
               << static_cast<unsigned long>(Send(hwnd, dt::Stats, dt::LastQueryError)) << std::dec << L'\n';
    const wchar_t* stages[] = {
        L"none", L"register-target", L"read-visibility", L"visibility-type", L"visibility-policy",
        L"hold-target", L"create-collapsed", L"set-collapsed", L"verify-collapsed", L"create-original",
        L"set-original", L"clear-original", L"verify-restored"
    };
    for (unsigned target : {dt::Chat, dt::Sidebar}) {
        auto detail = [&](dt::FailureField field) { return Send(hwnd, dt::Failure, target | (static_cast<WPARAM>(field) << 8)); };
        auto stage = static_cast<unsigned>(detail(dt::FailureStage));
        if (!stage) continue;
        std::wcerr << (target == dt::Chat ? L"ChatButton" : L"SidebarToggleButton")
                   << L" failure: stage=" << (stage < std::size(stages) ? stages[stage] : L"unknown")
                   << L", hr=0x" << std::hex << static_cast<unsigned long>(detail(dt::FailureError))
                   << L", metadata=0x" << static_cast<unsigned long long>(detail(dt::FailureMetadata))
                   << std::dec << L", source=" << detail(dt::FailureSource) << L'\n';
    }
}
std::vector<fs::path> Modules(DWORD pid) {
    Handle process{OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid)};
    if (!process.value) throw std::runtime_error("Cannot inspect Dia. Run as the same user, without elevation.");
    USHORT machine = 0, native = 0;
    if (!IsWow64Process2(process.value, &machine, &native) ||
        (machine != IMAGE_FILE_MACHINE_AMD64 && !(machine == IMAGE_FILE_MACHINE_UNKNOWN && native == IMAGE_FILE_MACHINE_AMD64)))
        throw std::runtime_error("This build only supports x64 Dia.");
    std::vector<HMODULE> modules(512);
    DWORD bytes = 0;
    for (;;) {
        if (!EnumProcessModulesEx(process.value, modules.data(), static_cast<DWORD>(modules.size() * sizeof(HMODULE)), &bytes, LIST_MODULES_ALL))
            throw std::runtime_error("Cannot enumerate Dia's modules.");
        if (bytes <= modules.size() * sizeof(HMODULE)) break;
        if (bytes > 16384 * sizeof(HMODULE)) throw std::runtime_error("Unexpected module count.");
        modules.resize(bytes / sizeof(HMODULE) + 1);
    }
    modules.resize(bytes / sizeof(HMODULE));
    std::vector<fs::path> result;
    for (auto module : modules) {
        wchar_t path[32768]{};
        DWORD count = GetModuleFileNameExW(process.value, module, path, static_cast<DWORD>(std::size(path)));
        if (count && count < std::size(path)) result.emplace_back(std::wstring(path, count));
    }
    return result;
}
HRESULT Attach(DWORD pid, const fs::path& tap) {
    auto modules = Modules(pid);
    fs::path xaml;
    for (const auto& path : modules)
        if (_wcsicmp(path.filename().c_str(), L"Microsoft.UI.Xaml.dll") == 0) xaml = path;
    if (xaml.empty()) throw std::runtime_error("Dia has not loaded WinUI XAML. Open a browser window first.");
    // Use the runtime already loaded by this Dia, never a guessed SDK version.
    for (const wchar_t* name : {L"Microsoft.Internal.FrameworkUdk.dll", L"FrameworkUdk.dll", L"Microsoft.UI.Xaml.dll"}) {
        for (const auto& path : modules) {
            if (_wcsicmp(path.filename().c_str(), name) != 0) continue;
            Library library{LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS)};
            if (!library.value) continue;
            using Initialize = HRESULT(WINAPI*)(LPCWSTR, DWORD, LPCWSTR, LPCWSTR, CLSID, LPCWSTR);
            auto initialize = reinterpret_cast<Initialize>(GetProcAddress(library.value, "InitializeXamlDiagnosticsEx"));
            if (!initialize) continue;
            for (unsigned connection = 1; connection <= 128; ++connection) {
                std::wstring endpoint = L"WinUIVisualDiagConnection" + std::to_wstring(connection);
                HRESULT hr = initialize(endpoint.c_str(), pid, xaml.c_str(), tap.c_str(), dt::TapClsid, L"");
                if (hr != HRESULT_FROM_WIN32(ERROR_NOT_FOUND)) return hr;
            }
            return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
        }
    }
    throw std::runtime_error("Cannot load Dia's XAML diagnostics entry point. No patch was applied.");
}
void Help() {
    std::wcout << L"DiaTweaks for Windows (experimental, x64)\n"
               << L"  DiaTweaks.exe --settings [--pid NUMBER]\n"
               << L"  DiaTweaks.exe --hide-chat [--hide-sidebar] [--pid NUMBER]\n"
               << L"  DiaTweaks.exe --restore [--pid NUMBER]\n"
               << L"With no arguments, attach the Settings UI and apply saved preferences.\n"
               << L"Each command replaces the active selection. Restart Dia to unload the add-on.\n";
}
}

int wmain(int argc, wchar_t** argv) {
    try {
        unsigned flags = 0;
        DWORD pid = 0;
        bool restore = false;
        bool settingsUi = argc == 1;
        for (int i = 1; i < argc; ++i) {
            std::wstring arg(argv[i]);
            if (arg == L"--help" || arg == L"-h") { Help(); return 0; }
            if (arg == L"--hide-chat") flags |= dt::Chat;
            else if (arg == L"--hide-sidebar") flags |= dt::Sidebar;
            else if (arg == L"--restore") restore = true;
            else if (arg == L"--settings") settingsUi = true;
            else if (arg == L"--pid" && i + 1 < argc) {
                std::wstring text(argv[++i]);
                if (text.empty() || text.find_first_not_of(L"0123456789") != std::wstring::npos)
                    throw std::runtime_error("Invalid --pid.");
                auto number = std::stoull(text);
                if (!number || number > MAXDWORD) throw std::runtime_error("Invalid --pid.");
                pid = static_cast<DWORD>(number);
            } else throw std::runtime_error("Unknown or incomplete argument. Run with --help.");
        }
        if ((settingsUi && (flags || restore)) || (!settingsUi && ((!flags && !restore) || (flags && restore)))) { Help(); return 2; }
        auto processes = FindDia();
        if (pid && !processes.count(pid)) throw std::runtime_error("--pid must identify a Dia process with a WinUI browser window.");
        if (!pid) {
            if (processes.empty()) throw std::runtime_error("Start Dia and open a browser window first.");
            if (processes.size() != 1) throw std::runtime_error("Several Dia processes have browser windows. Select one with --pid.");
            pid = *processes.begin();
        }
        HWND window = FindTap(pid);
        if (window && Send(window, dt::Version) != dt::ProtocolVersion)
            throw std::runtime_error("An older DiaTweaks DLL is still loaded. Quit all Dia processes, replace both the EXE and DLL, then reopen Dia.");
        HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        if (FAILED(com)) throw std::runtime_error("Cannot initialize COM.");
        struct Com { ~Com() { CoUninitialize(); } } comScope;
        if (!window) {
            wchar_t executable[32768]{};
            DWORD count = GetModuleFileNameW(nullptr, executable, static_cast<DWORD>(std::size(executable)));
            if (!count || count >= std::size(executable)) throw std::runtime_error("Cannot locate this executable.");
            fs::path tap = fs::path(executable).parent_path() / L"DiaTweaks.Tap.dll";
            if (!fs::is_regular_file(tap)) throw std::runtime_error("Keep DiaTweaks.Tap.dll beside DiaTweaks.exe.");
            HRESULT hr = Attach(pid, tap);
            if (FAILED(hr)) {
                std::wcerr << L"XAML diagnostics attach failed: 0x" << std::hex << static_cast<unsigned long>(hr)
                           << L". No button override was requested.\n";
                return 1;
            }
            ULONGLONG deadline = GetTickCount64() + 15000;
            do {
                window = FindTap(pid);
                if (window && Send(window, dt::State) != 0) break;
                Sleep(50);
            } while (GetTickCount64() < deadline);
        }
        if (!window || Send(window, dt::State) != 1)
            throw std::runtime_error("The XAML tree subscription did not become ready. Restart Dia before retrying.");
        if (Send(window, dt::Version) != dt::ProtocolVersion)
            throw std::runtime_error("EXE and DLL versions differ. Quit Dia and replace both files together.");
        if (settingsUi) {
            // Read from the same process/package view that saves UI changes.
            // Never turn a failed read into defaults and overwrite the store.
            LRESULT saved = Send(window, dt::LoadSelection);
            if (saved < 0 || (static_cast<unsigned long long>(saved) & ~static_cast<unsigned long long>(dt::All)))
                throw std::runtime_error("Could not read Dia's saved preferences. No selection was changed or saved.");
            flags = static_cast<unsigned>(saved);
            std::wcout << L"Loaded saved selection: hide-chat=" << ((flags & dt::Chat) != 0)
                       << L", hide-sidebar=" << ((flags & dt::Sidebar) != 0) << L'\n';
        }
        // Posted replay notifications may still be pending when Advise returns.
        if (!restore) {
            ULONGLONG deadline = GetTickCount64() + 5000;
            do {
                bool found = true;
                for (unsigned target : {dt::Chat, dt::Sidebar}) if (flags & target)
                    found = found && ((Send(window, dt::Query, target) & 0xffff) != 0);
                if (found) break;
                Sleep(50);
            } while (GetTickCount64() < deadline);
        }
        unsigned previous = static_cast<unsigned>(Send(window, dt::Flags));
        unsigned restoring = restore ? static_cast<unsigned>(Send(window, dt::OwnedMask)) : 0;
        bool ok = Send(window, dt::Configure, flags) == 0;
        if (restore) {
            // Visibility restoration can post a visual-tree Add notification.
            // Give the UI thread time to publish it before checking live controls.
            ULONGLONG deadline = GetTickCount64() + 2000;
            do {
                bool verified = Send(window, dt::Active) == 0;
                for (unsigned target : {dt::Chat, dt::Sidebar}) if (restoring & target) {
                    auto counts = static_cast<unsigned>(Send(window, dt::QueryVisible, target));
                    verified = verified && (counts & 0xffff) != 0 && (counts >> 16) == (counts & 0xffff);
                }
                if (verified) break;
                Sleep(50);
            } while (GetTickCount64() < deadline);
            ok = ok && Send(window, dt::Active) == 0;
        }
        unsigned reporting = restore ? (restoring | previous) : flags;
        for (unsigned target : {dt::Chat, dt::Sidebar}) if (reporting & target) {
            auto counts = static_cast<unsigned>(Send(window, restore ? dt::QueryVisible : dt::Query, target));
            unsigned matched = counts & 0xffff, hidden = counts >> 16;
            std::wcout << (target == dt::Chat ? L"ChatButton" : L"SidebarToggleButton")
                       << L": " << hidden << L"/" << matched << (restore ? L" visible" : L" collapsed")
                       << L", owned overrides=" << Send(window, dt::Active, target) << L'\n';
            if (!restore || (restoring & target)) ok = ok && matched != 0 && hidden == matched;
        }
        if (!ok) {
            // Restore the previous selection if a new selection was only partly applied.
            bool rolledBack = !restore && Send(window, dt::Configure, previous) == 0;
            std::wcerr << L"Could not verify every requested change. "
                       << (rolledBack ? L"Previous selection restored.\n" : L"Restart Dia to restore its UI.\n");
            Diagnostics(window);
            return 1;
        }
        if (!settingsUi) {
            HRESULT saved = static_cast<HRESULT>(Send(window, dt::SaveSelection));
            if (FAILED(saved)) {
                std::wcerr << L"Changes applied for this session, but preferences could not be saved.\n";
                return 1;
            }
        }
        if (settingsUi) {
            std::wcout << L"Settings integration attached. Open Dia Settings and choose Patches.\n"
                       << L"Settings: candidates=" << Send(window, dt::SettingsState, dt::SettingsCandidates)
                       << L", installed=" << Send(window, dt::SettingsState, dt::SettingsInstalled)
                       << L", lastError=0x" << std::hex << static_cast<unsigned long>(Send(window, dt::SettingsState, dt::SettingsLastError)) << std::dec << L'\n';
            return 0;
        }
        std::wcout << (restore ? (restoring ? L"Owned overrides removed and restored button visibility verified.\n" : L"No owned visibility overrides remain.\n")
                              : L"Requested button visibility verified through XAML diagnostics.\n");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "DiaTweaks: " << error.what() << '\n';
        return 1;
    }
}
