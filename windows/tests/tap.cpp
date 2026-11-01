#include "FakeTree.h"

int main() {
    try {
        Check(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)), "COM");
        module = GetModuleHandleW(nullptr);
        constexpr unsigned Legacy = dt::Chat | dt::Sidebar;
        auto fake = new FakeTree;
        auto tap = new Tap;
        Check(SUCCEEDED(tap->SetSite(static_cast<IVisualTreeService*>(fake))), "attach mock");
        HWND hwnd = FindWindowExW(HWND_MESSAGE, nullptr, dt::WindowClass, nullptr);
        Check(hwnd != nullptr, "UI dispatcher window");
        ULONGLONG deadline = GetTickCount64() + 5000;
        while (GetTickCount64() < deadline) {
            MSG msg{};
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
            if (SendMessageW(hwnd, dt::State, 0, 0) == 1 && (SendMessageW(hwnd, dt::Query, dt::Sidebar, 0) & 0xffff)) break;
            Sleep(5);
        }
        Check(SendMessageW(hwnd, dt::State, 0, 0) == 1, "worker replay completes");
        Check(SendMessageW(hwnd, dt::Version, 0, 0) == dt::ProtocolVersion, "protocol version");
        Check((SendMessageW(hwnd, dt::Query, dt::Chat, 0) & 0xffff) == 1, "x:Name fallback discovers exactly one ChatButton");
        Check((SendMessageW(hwnd, dt::Query, dt::Sidebar, 0) & 0xffff) == 1, "short Button type is accepted");
        Check(SendMessageW(hwnd, dt::Stats, dt::NamedMatches, 0) == 4, "fallback counted for diagnostics");
        Check(SendMessageW(hwnd, dt::Configure, Legacy, 0) == 0, "apply both buttons on UI thread");
        Check(fake->nodes.at(1).value == L"Collapsed" && fake->nodes.at(2).value == L"Collapsed", "both hidden");
        Check(fake->nodes.at(3).value == L"Visible", "window close button preserved");
        for (auto handle : {4ull, 5ull, 6ull})
            Check(fake->nodes.at(handle).value == L"Visible", "explicit ID precedence and label/type exclusions");
        unsigned writes = fake->writes;
        Check(SendMessageW(hwnd, dt::Configure, Legacy, 0) == 0 && fake->writes == writes, "idempotent apply");
        fake->Drop(1);
        Pump();
        Check(SendMessageW(hwnd, dt::Active, 0, 0) == 2, "removed visual retains its owned override");
        Check(SendMessageW(hwnd, dt::Query, dt::Chat, 0) == 0x10001, "owned detached child remains verifiable");
        Check(SendMessageW(hwnd, dt::Stats, dt::DetachedOverrides, 0) == 1, "detached override reported");
        Check(SendMessageW(hwnd, dt::OwnedMask, 0, 0) == Legacy, "restore includes detached chat");
        Check(SendMessageW(hwnd, dt::Configure, 0, 0) == 0, "restore");
        Pump();
        Check(fake->clears == 1 && fake->nodes.at(1).value == L"Visible" && !fake->nodes.at(1).local, "default restored by clearing local value");
        Check(fake->nodes.at(2).value == L"Visible" && fake->nodes.at(2).local, "original local value restored");
        Check(SendMessageW(hwnd, dt::QueryVisible, dt::Chat, 0) == 0x10001, "detached chat is live and visible after restore");
        // Retire boxed values when they leave a property. Sidebar keeps its
        // original visual handle, so a cached Collapsed handle fails cycle two.
        for (unsigned cycle = 0; cycle < 4; ++cycle) {
            for (unsigned selection : {dt::Sidebar, dt::Chat, static_cast<dt::Target>(dt::All)}) {
                Check(SendMessageW(hwnd, dt::Configure, selection, 0) == 0, "repeat hide after restore");
                for (unsigned target : {dt::Chat, dt::Sidebar}) if (selection & target)
                    Check(SendMessageW(hwnd, dt::Query, target, 0) == 0x10001, "repeated hide readback");
                Check(SendMessageW(hwnd, dt::Configure, 0, 0) == 0, "repeat restoration");
                Pump();
                Check(SendMessageW(hwnd, dt::QueryVisible, dt::Chat, 0) == 0x10001 &&
                      SendMessageW(hwnd, dt::QueryVisible, dt::Sidebar, 0) == 0x10001, "both visible after each cycle");
                Check(SendMessageW(hwnd, dt::Active, 0, 0) == 0, "cycle releases owned overrides");
            }
        }
        for (unsigned selection : {static_cast<unsigned>(dt::Downloads), static_cast<unsigned>(dt::Bookmark),
                                   dt::ExtensionParts, dt::ToolbarParts, static_cast<unsigned>(dt::RecentTabs), static_cast<unsigned>(dt::Spaces), dt::All}) {
            for (unsigned cycle = 0; cycle < 3; ++cycle) {
                Check(SendMessageW(hwnd, dt::Configure, selection, 0) == 0, "new tweak applies repeatedly");
                for (auto const& item : dt::NativeTargets) if (selection & item.flag)
                    Check(SendMessageW(hwnd, dt::Query, item.flag, 0) == 0x10001, "custom hosts match exact layout names, regardless of runtime type or UIA ID");
                Check(fake->nodes.at(3).value == L"Visible", "window caption untouched");
                Check(fake->nodes.at(17).value == L"Visible" && fake->nodes.at(19).value == L"Visible",
                      "overflow menus outside the sidebar header stay visible");
                Check(fake->nodes.at(13).value == L"Visible" && fake->nodes.at(13).binding,
                      "extensions host hides bound repeater without replacing its binding");
                Check(SendMessageW(hwnd, dt::Configure, 0, 0) == 0, "new tweaks restore");
                Pump();
                for (auto handle : {7ull, 8ull, 9ull, 10ull, 11ull, 16ull, 18ull})
                    Check(fake->nodes.at(handle).value == L"Visible", "new target baseline restored");
                Check(fake->nodes.at(12).value == L"Collapsed" && !fake->nodes.at(12).local,
                      "originally hidden bookmarks bar stays hidden and unowned");
            }
        }
        Check(SendMessageW(hwnd, dt::Query, dt::RecentTabs, 0) == 1, "only the scoped recent-tabs button is tracked");
        Check(SendMessageW(hwnd, dt::Configure, dt::Spaces, 0) == 0, "high-bit Spaces target applies");
        Check(fake->nodes.at(18).value == L"Collapsed", "message packing preserves target bits above 255");
        Check(SendMessageW(hwnd, dt::Configure, 0, 0) == 0, "Spaces restored");
        fake->nodes.at(18).binding = true;
        Check(SendMessageW(hwnd, dt::Configure, dt::Spaces, 0) == -1, "high-bit policy failure reported");
        Check(SendMessageW(hwnd, dt::Failure, dt::Spaces | (static_cast<WPARAM>(dt::FailureStage) << 32), 0) == dt::VisibilityPolicy,
              "failure protocol preserves high-bit target and field");
        fake->nodes.at(18).binding = false;
        Check(SendMessageW(hwnd, dt::Configure, 0, 0) == 0, "clear failed selection");
        Check(fake->staleValueWrites == 0, "no cached value handles reused across cycles");
        Check(SendMessageW(hwnd, dt::Configure, dt::Chat, 0) == 0, "hide before handle replacement");
        fake->Drop(1); Pump();
        fake->ReAdd(1); Pump();
        Check(SendMessageW(hwnd, dt::Active, dt::Chat, 0) == 1, "new handle for same object retains baseline");
        Check(SendMessageW(hwnd, dt::Configure, 0, 0) == 0, "restore after handle replacement");
        Check(fake->nodes.at(1).value == L"Visible", "new handle restores original visible value");
        Check(SendMessageW(hwnd, dt::Configure, dt::Chat, 0) == 0, "hide before failed restoration");
        fake->failRegister = true;
        Check(SendMessageW(hwnd, dt::Configure, 0, 0) == -1, "failed object registration cannot report restored");
        Check(SendMessageW(hwnd, dt::Active, dt::Chat, 0) == 1, "failed restore retains ownership for retry");
        fake->failRegister = false;
        Check(SendMessageW(hwnd, dt::Configure, 0, 0) == 0, "retry restore succeeds");
        Check(SendMessageW(hwnd, dt::Configure, dt::Chat, 0) == 0, "hide before app binding takes over");
        fake->nodes.at(1).binding = true;
        Check(SendMessageW(hwnd, dt::Configure, 0, 0) == 0, "do not overwrite app binding");
        Check(SendMessageW(hwnd, dt::Active, 0, 0) == 0 && SendMessageW(hwnd, dt::QueryVisible, dt::Chat, 0) == 1,
              "no owned overrides does not imply a visible Chat button");
        fake->nodes.at(1).value = L"Visible";
        fake->nodes.at(1).binding = true;
        writes = fake->writes;
        Check(SendMessageW(hwnd, dt::Configure, dt::Chat, 0) == -1, "binding rejected");
        Check(fake->writes == writes && fake->nodes.at(1).value == L"Visible", "binding untouched");
        fake->nodes.at(1).binding = false;
        fake->ignoreWrites = true;
        Check(SendMessageW(hwnd, dt::Configure, dt::Chat, 0) == -1, "successful HRESULT without effective change is rejected");
        Check(fake->nodes.at(1).value == L"Visible", "failed verification rolls back");
        Check(SendMessageW(hwnd, dt::Active, 0, 0) == 0, "no owned overrides after rollback");
        fake->nodes.at(1).omitVisibility = true;
        Check(SendMessageW(hwnd, dt::Query, dt::Chat, 0) == 1, "unreadable visibility does not masquerade as missing selector");
        Check(SendMessageW(hwnd, dt::Stats, dt::MissingVisibility, 0) == 1, "missing visibility is reported");
        fake->ignoreWrites = false;
        fake->nodes.at(1).omitVisibility = false;
        Check(SendMessageW(hwnd, dt::Configure, dt::Spaces, 0) == 0, "own a control before detach");
        fake->failRegister = true;
        Check(FAILED(static_cast<HRESULT>(SendMessageW(hwnd, dt::Detach, 0, 0))), "failed restoration blocks detach");
        Check(fake->unadvises == 0 && SendMessageW(hwnd, dt::State, 0, 0) == 1, "failed detach keeps subscription available for retry");
        fake->failRegister = false;
        fake->failUnadvise = true;
        Check(SendMessageW(hwnd, dt::Detach, 0, 0) == S_OK, "detach starts asynchronously after restoration");
        deadline = GetTickCount64() + 5000;
        while (IsWindow(hwnd) && SendMessageW(hwnd, dt::State, 0, 0) != -2 && GetTickCount64() < deadline) { Pump(); Sleep(5); }
        Check(IsWindow(hwnd) && SendMessageW(hwnd, dt::State, 0, 0) == -2, "failed unsubscribe retains retry endpoint");
        Check(fake->nodes.at(18).value == L"Visible", "detach restores owned control before unsubscribing");
        fake->failUnadvise = false;
        Check(SendMessageW(hwnd, dt::Detach, 0, 0) == S_OK, "retry asynchronous unsubscribe");
        deadline = GetTickCount64() + 5000;
        while (IsWindow(hwnd) && GetTickCount64() < deadline) { Pump(); Sleep(5); }
        Check(!IsWindow(hwnd) && fake->unadvises == 2, "successful detach destroys endpoint");
        VisualElement late{};
        late.Handle = 18;
        Check(tap->OnVisualTreeChange({}, late, Add) == S_OK, "late callbacks after detach are ignored");
        auto replacement = new Tap;
        Check(SUCCEEDED(replacement->SetSite(static_cast<IVisualTreeService*>(fake))), "new session attaches after detach without exiting process");
        HWND next = FindWindowExW(HWND_MESSAGE, nullptr, dt::WindowClass, nullptr);
        deadline = GetTickCount64() + 5000;
        while (GetTickCount64() < deadline) {
            Pump();
            if (SendMessageW(next, dt::State, 0, 0) == 1 && (SendMessageW(next, dt::Query, dt::Spaces, 0) & 0xffff)) break;
            Sleep(5);
        }
        Check(SendMessageW(next, dt::Configure, dt::Spaces, 0) == 0 && fake->nodes.at(18).value == L"Collapsed", "reattached session can patch again");
        Check(SendMessageW(next, dt::Configure, 0, 0) == 0, "reattached session restores baseline");
        std::cout << "Mock XAML apply, restore, binding and readback checks passed\n";
        // Like a real diagnostics session, the TAP remains resident until this process exits.
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n'; return 1;
    }
}
