#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>
#include <stdexcept>
#include <algorithm>
#include "Processes.h"

namespace {
constexpr wchar_t GuiClass[] = L"DiaTweaks.Control.v1";
constexpr UINT UpdateMessage = WM_APP + 70;
enum Control { Load = 101, Unload, Reload, Reset, Status, Help, CopyLog, ClearLog, Refresh, Process };
struct Action { int id; const wchar_t* title; const wchar_t* option; };
constexpr Action Actions[] = {
    {Load, L"Load", L"--settings"}, {Unload, L"Unload", L"--unload"},
    {Reload, L"Reload", L"--reload"}, {Reset, L"Reset settings", L"--reset-settings"},
    {Status, L"Status / diagnostics", L"--inspect-layout"}, {Help, L"CLI help", L"--help"}
};
struct Handle {
    HANDLE value = nullptr;
    ~Handle() { Close(); }
    void Close() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); value = nullptr; }
};
std::wstring Decode(std::string const& bytes) {
    if (bytes.empty()) return {};
    UINT codepage = CP_UTF8;
    int length = MultiByteToWideChar(codepage, MB_ERR_INVALID_CHARS, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
    if (!length) { codepage = CP_ACP; length = MultiByteToWideChar(codepage, 0, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0); }
    std::wstring text(static_cast<size_t>(length), L'\0');
    if (length) MultiByteToWideChar(codepage, 0, bytes.data(), static_cast<int>(bytes.size()), text.data(), length);
    return text;
}
std::wstring Text(HWND control) {
    int size = GetWindowTextLengthW(control);
    std::wstring text(static_cast<size_t>(size) + 1, L'\0');
    GetWindowTextW(control, text.data(), size + 1);
    text.resize(static_cast<size_t>(size));
    return text;
}
struct Update { enum Kind { Line, Done } kind; std::wstring text; DWORD code = 0; };
void Post(HWND window, Update update) {
    auto message = std::make_unique<Update>(std::move(update));
    if (PostMessageW(window, UpdateMessage, 0, reinterpret_cast<LPARAM>(message.get()))) message.release();
}
struct Work { HWND window; std::wstring executable, option; DWORD pid; };
DWORD WINAPI Worker(void* data) noexcept {
    std::unique_ptr<Work> work(static_cast<Work*>(data));
    DWORD result = 1;
    try {
        SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
        Handle read, write, input;
        if (!CreatePipe(&read.value, &write.value, &security, 0) || !SetHandleInformation(read.value, HANDLE_FLAG_INHERIT, 0))
            throw std::runtime_error("Cannot create the progress/output pipe.");
        input.value = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &security, OPEN_EXISTING, 0, nullptr);
        if (input.value == INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot initialize the controller input.");
        std::wstring command = L"\"" + work->executable + L"\" " + work->option;
        if (work->pid && work->option != L"--help") command += L" --pid " + std::to_wstring(work->pid);
        STARTUPINFOW startup{}; startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdOutput = startup.hStdError = write.value; startup.hStdInput = input.value;
        PROCESS_INFORMATION process{};
        if (!CreateProcessW(work->executable.c_str(), command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
                            nullptr, std::filesystem::path(work->executable).parent_path().c_str(), &startup, &process)) {
            Post(work->window, {Update::Line, L"Could not start DiaTweaks.exe. Keep it beside this control panel. Windows error " + std::to_wstring(GetLastError()) + L"."});
        } else {
            Handle child{process.hProcess}, thread{process.hThread};
            write.Close(); input.Close();
            std::string pending;
            char buffer[4096]; DWORD count = 0;
            bool outputError = false;
            for (;;) {
                if (!ReadFile(read.value, buffer, sizeof(buffer), &count, nullptr)) {
                    if (GetLastError() != ERROR_BROKEN_PIPE) outputError = true;
                    break;
                }
                if (!count) break;
                pending.append(buffer, count);
                size_t end;
                while ((end = pending.find('\n')) != std::string::npos) {
                    auto line = pending.substr(0, end);
                    if (!line.empty() && line.back() == '\r') line.pop_back();
                    Post(work->window, {Update::Line, Decode(line)});
                    pending.erase(0, end + 1);
                }
                if (pending.size() > 65536) { Post(work->window, {Update::Line, Decode(pending)}); pending.clear(); }
            }
            if (!pending.empty()) Post(work->window, {Update::Line, Decode(pending)});
            WaitForSingleObject(child.value, INFINITE);
            if (!GetExitCodeProcess(child.value, &result)) result = 1;
            if (outputError) { Post(work->window, {Update::Line, L"The output pipe failed; check Dia's current state before retrying."}); result = 1; }
        }
    } catch (std::exception const& error) { Post(work->window, {Update::Line, Decode(error.what())}); }
      catch (...) { Post(work->window, {Update::Line, L"Unexpected control-panel worker error."}); }
    Post(work->window, {Update::Done, {}, result});
    return 0;
}
struct Panel {
    HWND window = nullptr, title = nullptr, hint = nullptr, processLabel = nullptr, process = nullptr;
    HWND progress = nullptr, status = nullptr, log = nullptr, footer = nullptr;
    std::vector<HWND> buttons;
    HFONT normal = nullptr, heading = nullptr, mono = nullptr;
    HANDLE worker = nullptr;
    UINT dpi = 96;
    bool busy = false, closing = false, error = false;
    int action = 0;
    DWORD requestedPid = 0;
    std::wstring creationError;
    ~Panel() { if (worker) CloseHandle(worker); DeleteFonts(); }
    int Scale(int value) const { return MulDiv(value, static_cast<int>(dpi), 96); }
    void DeleteFonts() { if (normal) DeleteObject(normal); if (heading) DeleteObject(heading); if (mono) DeleteObject(mono); normal = heading = mono = nullptr; }
    HWND Add(wchar_t const* cls, wchar_t const* text, DWORD style, int id = 0, DWORD extended = 0) {
        HWND control = CreateWindowExW(extended, cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, Scale(100), Scale(30),
            window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
        if (!control) throw std::runtime_error("Cannot create control " + std::to_string(id) + "; Windows error " + std::to_string(GetLastError()) + ".");
        return control;
    }
    void Fonts() {
        DeleteFonts();
        normal = CreateFontW(-Scale(14), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
        heading = CreateFontW(-Scale(25), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
        mono = CreateFontW(-Scale(12), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Consolas");
        for (HWND child = GetWindow(window, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT))
            SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(child == title ? heading : child == log ? mono : normal), TRUE);
    }
    DWORD SelectedPid() const {
        auto selected = SendMessageW(process, CB_GETCURSEL, 0, 0);
        return selected == CB_ERR ? 0 : static_cast<DWORD>(SendMessageW(process, CB_GETITEMDATA, static_cast<WPARAM>(selected), 0));
    }
    void Processes(DWORD preserve = 0) {
        SendMessageW(process, CB_RESETCONTENT, 0, 0);
        SendMessageW(process, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Automatic (one running Dia process)"));
        SendMessageW(process, CB_SETITEMDATA, 0, 0);
        int selection = 0;
        bool found = false;
        for (DWORD pid : dt::FindDia()) {
            auto label = L"Dia - PID " + std::to_wstring(pid);
            auto index = SendMessageW(process, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
            SendMessageW(process, CB_SETITEMDATA, static_cast<WPARAM>(index), pid);
            if (pid == preserve) { selection = static_cast<int>(index); found = true; }
        }
        if (preserve && !found) {
            auto label = L"PID " + std::to_wstring(preserve) + L" (not running)";
            selection = static_cast<int>(SendMessageW(process, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str())));
            SendMessageW(process, CB_SETITEMDATA, selection, preserve);
        }
        SendMessageW(process, CB_SETCURSEL, selection, 0);
    }
    void Create() {
        dpi = GetDpiForWindow(window);
        title = Add(L"STATIC", L"DiaTweaks", 0);
        hint = Add(L"STATIC", L"Configure tweaks in Dia Settings > Patches. Keep Dia open while loading.", 0);
        processLabel = Add(L"STATIC", L"Dia process", 0);
        process = Add(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL, Process);
        buttons.push_back(Add(L"BUTTON", L"Refresh processes", BS_PUSHBUTTON | WS_TABSTOP, Refresh));
        for (auto const& item : Actions) buttons.push_back(Add(L"BUTTON", item.title, BS_PUSHBUTTON | WS_TABSTOP, item.id));
        buttons.push_back(Add(L"BUTTON", L"Copy log", BS_PUSHBUTTON | WS_TABSTOP, CopyLog));
        buttons.push_back(Add(L"BUTTON", L"Clear log", BS_PUSHBUTTON | WS_TABSTOP, ClearLog));
        status = Add(L"STATIC", L"Ready. Choose an action.", SS_LEFT);
        progress = Add(PROGRESS_CLASSW, L"", 0);
        SendMessageW(progress, PBM_SETRANGE32, 0, 100);
        log = Add(L"EDIT", L"", ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL | ES_READONLY | WS_VSCROLL | WS_HSCROLL | WS_TABSTOP, 0, WS_EX_CLIENTEDGE);
        SendMessageW(log, EM_SETLIMITTEXT, 4 * 1024 * 1024, 0);
        footer = Add(L"STATIC", L"Unload preserves saved choices. Reset turns all tweaks off. Inactive DLLs remain until Dia exits.", 0);
        Fonts(); Processes(requestedPid);
        Append(L"Ready. Load attaches Patches and applies saved choices. No changes happen until you choose an action.");
    }
    void Place(HWND control, int x, int y, int width, int height) const {
        MoveWindow(control, Scale(x), Scale(y), Scale((std::max)(1, width)), Scale((std::max)(1, height)), TRUE);
    }
    void Layout() {
        RECT rect{}; GetClientRect(window, &rect);
        int width = MulDiv(rect.right, 96, static_cast<int>(dpi)), height = MulDiv(rect.bottom, 96, static_cast<int>(dpi));
        int inner = width - 40;
        Place(title, 20, 16, inner, 34); Place(hint, 20, 55, inner, 36);
        Place(processLabel, 20, 101, 80, 25); Place(process, 104, 96, inner - 250, 250);
        Place(GetDlgItem(window, Refresh), width - 176, 96, 156, 30);
        int buttonWidth = (inner - 24) / 4;
        for (int i = 0; i < 4; ++i) Place(GetDlgItem(window, Actions[i].id), 20 + i * (buttonWidth + 8), 140, buttonWidth, 36);
        int ids[] = {Status, Help, CopyLog, ClearLog};
        for (int i = 0; i < 4; ++i) Place(GetDlgItem(window, ids[i]), 20 + i * (buttonWidth + 8), 184, buttonWidth, 32);
        Place(status, 20, 232, inner, 26); Place(progress, 20, 263, inner, 10);
        Place(log, 20, 289, inner, height - 353); Place(footer, 20, height - 52, inner, 40);
    }
    void Append(std::wstring const& line) {
        if (GetWindowTextLengthW(log) > 1024 * 1024) SetWindowTextW(log, L"[Older log output cleared]\r\n");
        std::wstring text = line + L"\r\n";
        SendMessageW(log, EM_SETSEL, static_cast<WPARAM>(-1), static_cast<LPARAM>(-1));
        SendMessageW(log, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(text.c_str()));
        SendMessageW(log, EM_SCROLLCARET, 0, 0);
    }
    void Enabled(bool enabled) {
        for (HWND button : buttons) {
            int id = GetDlgCtrlID(button);
            if (id != CopyLog && id != ClearLog) EnableWindow(button, enabled);
        }
        EnableWindow(process, enabled);
    }
    void Start(int id) {
        if (busy) return;
        auto actionItem = std::find_if(std::begin(Actions), std::end(Actions), [id](auto const& item) { return item.id == id; });
        if (actionItem == std::end(Actions)) return;
        wchar_t executable[32768]{};
        DWORD length = GetModuleFileNameW(nullptr, executable, static_cast<DWORD>(std::size(executable)));
        if (!length || length >= std::size(executable)) { Append(L"Cannot locate this control panel."); return; }
        auto cli = std::filesystem::path(executable).parent_path() / L"DiaTweaks.exe";
        auto work = std::make_unique<Work>(Work{window, cli.wstring(), actionItem->option, SelectedPid()});
        action = id; busy = true; error = false; Enabled(false);
        auto label = std::wstring(actionItem->title) + L"...";
        SetWindowTextW(status, label.c_str()); InvalidateRect(status, nullptr, TRUE);
        Append(L"\r\n> " + std::wstring(actionItem->title) + (work->pid ? L" (PID " + std::to_wstring(work->pid) + L")" : L" (automatic process)"));
        SetWindowLongPtrW(progress, GWL_STYLE, GetWindowLongPtrW(progress, GWL_STYLE) | PBS_MARQUEE);
        SendMessageW(progress, PBM_SETMARQUEE, TRUE, 50);
        worker = CreateThread(nullptr, 0, Worker, work.get(), 0, nullptr);
        if (!worker) { Complete(1); Append(L"Could not start the action worker."); return; }
        work.release();
    }
    void Complete(DWORD code) {
        busy = false; error = code != 0; Enabled(true);
        if (worker) { CloseHandle(worker); worker = nullptr; }
        SendMessageW(progress, PBM_SETMARQUEE, FALSE, 0);
        SetWindowLongPtrW(progress, GWL_STYLE, GetWindowLongPtrW(progress, GWL_STYLE) & ~static_cast<LONG_PTR>(PBS_MARQUEE));
        SendMessageW(progress, PBM_SETPOS, code ? 0 : 100, 0);
        std::wstring label = code ? L"Action failed (exit " + std::to_wstring(code) + L"). See log below." : L"Completed.";
        SetWindowTextW(status, label.c_str()); InvalidateRect(status, nullptr, TRUE);
        Append(code ? L"Action failed. Exit code: " + std::to_wstring(code) : L"Action completed successfully.");
        if (closing) DestroyWindow(window);
    }
    void Receive(Update const& update) {
        if (update.kind == Update::Done) { Complete(update.code); return; }
        Append(update.text);
        constexpr std::wstring_view prefix = L"[progress] ";
        if (update.text.starts_with(prefix) && !closing) SetWindowTextW(status, update.text.substr(prefix.size()).c_str());
    }
    void Copy() const {
        auto text = Text(log);
        if (!OpenClipboard(window)) return;
        size_t bytes = (text.size() + 1) * sizeof(wchar_t);
        HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
        if (memory) {
            if (void* target = GlobalLock(memory)) {
                memcpy(target, text.c_str(), bytes); GlobalUnlock(memory);
                EmptyClipboard();
                if (!SetClipboardData(CF_UNICODETEXT, memory)) GlobalFree(memory);
            } else GlobalFree(memory);
        }
        CloseClipboard();
    }
};
LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wp, LPARAM lp) noexcept {
    auto panel = reinterpret_cast<Panel*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        panel = static_cast<Panel*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        panel->window = window; SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(panel));
    }
    if (!panel) return DefWindowProcW(window, message, wp, lp);
    try {
        switch (message) {
        case WM_CREATE: panel->Create(); return 0;
        case WM_SIZE: panel->Layout(); return 0;
        case WM_DPICHANGED: {
            panel->dpi = HIWORD(wp); panel->Fonts();
            auto rect = reinterpret_cast<RECT*>(lp);
            SetWindowPos(window, nullptr, rect->left, rect->top, rect->right - rect->left, rect->bottom - rect->top, SWP_NOACTIVATE | SWP_NOZORDER);
            panel->Layout(); return 0;
        }
        case WM_GETMINMAXINFO: {
            auto info = reinterpret_cast<MINMAXINFO*>(lp);
            info->ptMinTrackSize = {panel->Scale(720), panel->Scale(520)}; return 0;
        }
        case WM_COMMAND: {
            int id = LOWORD(wp);
            if (id == Refresh && !panel->busy) panel->Processes(panel->SelectedPid());
            else if (id == CopyLog) panel->Copy();
            else if (id == ClearLog) SetWindowTextW(panel->log, L"");
            else panel->Start(id);
            return 0;
        }
        case UpdateMessage: {
            std::unique_ptr<Update> update(reinterpret_cast<Update*>(lp)); panel->Receive(*update); return 0;
        }
        case WM_CTLCOLORSTATIC:
            if (reinterpret_cast<HWND>(lp) == panel->status && panel->error) {
                SetTextColor(reinterpret_cast<HDC>(wp), RGB(180, 25, 25)); SetBkColor(reinterpret_cast<HDC>(wp), GetSysColor(COLOR_BTNFACE));
                return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_BTNFACE));
            }
            break;
        case WM_CLOSE:
            if (panel->busy) { panel->closing = true; SetWindowTextW(panel->status, L"Finishing the current action before closing..."); }
            else DestroyWindow(window);
            return 0;
        case WM_DESTROY: PostQuitMessage(0); return 0;
        }
    } catch (std::exception const& error) {
        if (message == WM_CREATE) { panel->creationError = Decode(error.what()); return -1; }
        if (panel->log) panel->Append(Decode(error.what()));
        panel->error = true;
        SetWindowTextW(panel->status, L"Control-panel error. See log below.");
    } catch (...) { if (message == WM_CREATE) { panel->creationError = L"Unexpected error creating controls."; return -1; } }
    return DefWindowProcW(window, message, wp, lp);
}
HWND CreatePanel(Panel& panel) {
    panel.dpi = GetDpiForSystem();
    INITCOMMONCONTROLSEX common{sizeof(common), ICC_PROGRESS_CLASS};
    if (!InitCommonControlsEx(&common)) { panel.creationError = L"InitCommonControlsEx failed; Windows error " + std::to_wstring(GetLastError()) + L"."; return nullptr; }
    WNDCLASSW cls{}; cls.lpfnWndProc = WindowProc; cls.hInstance = GetModuleHandleW(nullptr);
    cls.hCursor = LoadCursorW(nullptr, IDC_ARROW); cls.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    cls.hbrBackground = GetSysColorBrush(COLOR_BTNFACE); cls.lpszClassName = GuiClass;
    if (!RegisterClassW(&cls) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) { panel.creationError = L"RegisterClass failed; Windows error " + std::to_wstring(GetLastError()) + L"."; return nullptr; }
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    int width = panel.Scale(900), height = panel.Scale(680);
    if (work.right > work.left && work.bottom > work.top) {
        width = (std::min)(width, static_cast<int>(work.right - work.left - 32));
        height = (std::min)(height, static_cast<int>(work.bottom - work.top - 32));
    }
    HWND window = CreateWindowExW(WS_EX_CONTROLPARENT, GuiClass, L"DiaTweaks - Windows control panel", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, width, height, nullptr, nullptr, cls.hInstance, &panel);
    if (!window && panel.creationError.empty()) panel.creationError = L"CreateWindow failed; Windows error " + std::to_wstring(GetLastError()) + L".";
    return window;
}
}
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int show) {
    Panel panel;
    std::wstring argumentError;
    int count = 0;
    auto args = CommandLineToArgvW(GetCommandLineW(), &count);
    try {
        for (int i = 1; args && i < count; ++i) {
            std::wstring argument = args[i];
            if (argument != L"--pid" || i + 1 >= count) throw std::runtime_error("The control panel accepts only --pid NUMBER. Use DiaTweaks.exe for CLI commands.");
            std::wstring number = args[++i];
            if (number.empty() || number.find_first_not_of(L"0123456789") != std::wstring::npos) throw std::runtime_error("Invalid --pid.");
            auto pid = std::stoull(number);
            if (!pid || pid > MAXDWORD) throw std::runtime_error("Invalid --pid.");
            panel.requestedPid = static_cast<DWORD>(pid);
        }
    } catch (std::exception const& error) { argumentError = Decode(error.what()); }
    if (args) LocalFree(args);
    HWND window = CreatePanel(panel);
    if (!window) { MessageBoxW(nullptr, (L"Could not create the DiaTweaks control panel.\n" + panel.creationError).c_str(), L"DiaTweaks", MB_ICONERROR); return 1; }
    if (!argumentError.empty()) { panel.Append(argumentError); panel.error = true; SetWindowTextW(panel.status, L"Invalid launch arguments. See log below."); }
    ShowWindow(window, show); UpdateWindow(window);
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(window, &message)) { TranslateMessage(&message); DispatchMessageW(&message); }
    }
    return static_cast<int>(message.wParam);
}
