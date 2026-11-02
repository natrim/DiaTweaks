#define wWinMain GuiEntryForTests
#include "../native/Gui.cpp"
#undef wWinMain
#include <iostream>

namespace {
void CheckGui(bool value, char const* text) { if (!value) throw std::runtime_error(text); }
void WaitAction(Panel& panel) {
    ULONGLONG end = GetTickCount64() + 15000;
    while (panel.busy && GetTickCount64() < end) {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message != WM_QUIT) { TranslateMessage(&message); DispatchMessageW(&message); }
        }
        Sleep(5);
    }
    CheckGui(!panel.busy, "GUI worker timeout");
}
}
int main() {
    try {
        Panel panel;
        HWND window = CreatePanel(panel);
        CheckGui(window != nullptr, "create real native controls");
        for (auto const& action : Actions) CheckGui(GetDlgItem(window, action.id) != nullptr, "action button exists");
        CheckGui(GetDlgItem(window, CopyLog) && GetDlgItem(window, ClearLog) && panel.process && panel.progress, "log and process controls exist");
        panel.Start(Help);
        CheckGui(panel.busy && !IsWindowEnabled(GetDlgItem(window, Load)) && !IsWindowEnabled(panel.process), "actions and process selection disabled during worker");
        WaitAction(panel);
        CheckGui(!panel.error && IsWindowEnabled(GetDlgItem(window, Load)), "help worker finishes successfully and restores controls");
        CheckGui(Text(panel.log).find(L"--reset-settings") != std::wstring::npos && Text(panel.log).find(L"--gui") != std::wstring::npos, "child CLI help is captured in GUI log");
        // Select this test process, which cannot be Dia. Exercise every action's
        // error routing without ever modifying a real running browser.
        panel.Processes(GetCurrentProcessId());
        for (int action : {Load, Unload, Reload, Reset, Status}) {
            SetWindowTextW(panel.log, L"");
            panel.Start(action);
            WaitAction(panel);
            CheckGui(panel.error && Text(panel.log).find(L"--pid must identify") != std::wstring::npos, "selected process and controller error reach the GUI");
            CheckGui(Text(panel.log).find(L"[progress]") != std::wstring::npos, "staged progress reaches the GUI log");
        }
        SendMessageW(window, WM_COMMAND, ClearLog, 0);
        CheckGui(Text(panel.log).empty(), "clear log action");
        panel.Start(Help);
        SendMessageW(window, WM_CLOSE, 0, 0);
        CheckGui(IsWindow(window) && panel.closing, "closing waits for in-flight operation");
        WaitAction(panel);
        CheckGui(!IsWindow(window), "window closes after worker completion");
        std::cout << "Native GUI, process routing, captured progress/errors, worker completion and close checks passed\n";
        return 0;
    } catch (std::exception const& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
