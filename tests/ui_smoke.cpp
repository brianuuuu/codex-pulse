#define UNICODE
#define _UNICODE
#include <windows.h>
#include <shellapi.h>
#include <iostream>
#include <string>
int main(int argc, char **argv) {
    int mode = argc > 1 ? atoi(argv[1]) : 0;
    HWND h = FindWindowW(L"CodexPulse.Window.1", mode == 20 ? L"Codex Pulse" : L"Codex Pulse · 演示");
    if (!h) {
        std::cerr << "No demo window\n";
        return 1;
    }
    if (mode == 20) {
        SendMessageW(h, WM_COMMAND, 103, 0);
        for (int i = 0; i < 100; ++i) {
            wchar_t label[100]{};
            GetWindowTextW(GetDlgItem(h, 103), label, 100);
            if (std::wstring(label) == L"刷新   F5")
                break;
            Sleep(100);
        }
        SendMessageW(h, WM_COMMAND, 104, 0);
        if (!OpenClipboard(nullptr))
            return 1;
        auto mem = GetClipboardData(CF_UNICODETEXT);
        auto p = mem ? static_cast<wchar_t *>(GlobalLock(mem)) : nullptr;
        std::wstring report = p ? p : L"";
        if (p)
            GlobalUnlock(mem);
        CloseClipboard();
        if (report.find(L"今日总计：300") == std::wstring::npos ||
            report.find(L"输入：240，其中缓存：120") == std::wstring::npos ||
            report.find(L"API Key 模式") == std::wstring::npos ||
            report.find(L"演示数据") != std::wstring::npos) {
            std::cerr << "Fixture report mismatch\n";
            return 1;
        }
        wchar_t self[32768]{};
        GetModuleFileNameW(nullptr, self, 32768);
        std::wstring exe = self;
        exe = exe.substr(0, exe.find_last_of(L"\\/")) + L"\\CodexPulse.exe";
        std::wstring command = L"\"" + exe + L"\"";
        STARTUPINFOW si{};
        si.cb = sizeof si;
        PROCESS_INFORMATION pi{};
        if (!CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si,
                            &pi))
            return 1;
        DWORD wait = WaitForSingleObject(pi.hProcess, 3000), code = 1;
        GetExitCodeProcess(pi.hProcess, &code);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        if (wait != WAIT_OBJECT_0 || code != 0 || !IsWindowVisible(h))
            return 1;
        std::cout << "PASS: 6 fixture / single-instance integration checks\n";
        return 0;
    }
    if (mode == 1) {
        SendMessageW(h, WM_COMMAND, 102, 0);
        SendMessageW(h, WM_COMMAND, 110, 0);
        SendMessageW(h, WM_COMMAND, 110, 0);
        SendMessageW(h, WM_COMMAND, 100, 0);
        return 0;
    }
    if (mode == 2) {
        SendMessageW(h, WM_COMMAND, 101, 0);
        return 0;
    }
    if (mode == 3) {
        SendMessageW(h, WM_COMMAND, 102, 0);
        return 0;
    }
    if (mode == 4) {
        SendMessageW(h, WM_COMMAND, 110, 0);
        SendMessageW(h, WM_COMMAND, 100, 0);
        return 0;
    }
    if (mode == 6) {
        SendMessageW(h, WM_COMMAND, 100, 0);
        SetWindowPos(h, nullptr, 100, 60, 700, 950, SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    if (mode == 5) {
        SendMessageW(h, WM_COMMAND, 102, 0);
        SetWindowPos(h, nullptr, 100, 40, 840, 1140, SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    if (mode == 9) {
        SendMessageW(h, WM_DESTROY, 0, 0);
        return 0;
    }
    int checks = 0;
    auto check = [&](bool ok, const char *label) {
        ++checks;
        if (!ok) {
            std::cerr << "FAIL: " << label << "\n";
            ExitProcess(1);
        }
    };
    check(IsWindowVisible(h), "window visible");
    SendMessageW(h, WM_COMMAND, 102, 0);
    check(IsWindowVisible(GetDlgItem(h, 110)), "settings controls shown");
    SendMessageW(h, WM_COMMAND, 100, 0);
    check(!IsWindowVisible(GetDlgItem(h, 110)), "settings controls hidden on overview");
    SendMessageW(h, WM_COMMAND, 104, 0);
    check(OpenClipboard(nullptr), "clipboard opens");
    auto mem = GetClipboardData(CF_UNICODETEXT);
    check(mem != nullptr, "Unicode report copied");
    auto p = static_cast<wchar_t *>(GlobalLock(mem));
    std::wstring report = p ? p : L"";
    if (p)
        GlobalUnlock(mem);
    CloseClipboard();
    check(report.find(L"230,200") != std::wstring::npos, "report token total");
    check(report.find(L"演示数据") != std::wstring::npos, "demo label in report");
    check(report.find(L"access_token") == std::wstring::npos, "report contains no credentials");
    SendMessageW(h, WM_CLOSE, 0, 0);
    check(!IsWindowVisible(h), "close hides to tray");
    SendMessageW(h, WM_APP + 1, 0, MAKELONG(WM_LBUTTONDBLCLK, 1));
    check(IsWindowVisible(h), "tray double click restores");
    SendMessageW(h, WM_SYSCOMMAND, SC_MINIMIZE, 0);
    check(!IsWindowVisible(h), "minimize hides to tray");
    SendMessageW(h, WM_APP + 1, 0, MAKELONG(NIN_KEYSELECT, 1));
    check(IsWindowVisible(h), "tray keyboard restores");
    SendMessageW(h, WM_COMMAND, 101, 0);
    SendMessageW(h, WM_VSCROLL, SB_PAGEDOWN, 0);
    SendMessageW(h, WM_COMMAND, 100, 0);
    check(IsWindowVisible(h), "tabs and scroll remain responsive");
    std::cout << "PASS: " << checks << " UI smoke checks\n";
    return 0;
}
