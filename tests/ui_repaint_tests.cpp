#define UNICODE
#define _UNICODE
#include <windows.h>
#include <iostream>
#include <vector>
#include <cstdint>

std::vector<uint32_t> pixels(HWND h) {
    RECT r{};
    GetClientRect(h, &r);
    int w = r.right, ht = r.bottom;
    HDC screen = GetDC(h), mem = CreateCompatibleDC(screen);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = w;
    info.bmiHeader.biHeight = -ht;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void *bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    auto old = SelectObject(mem, bitmap);
    BitBlt(mem, 0, 0, w, ht, screen, 0, 0, SRCCOPY);
    std::vector<uint32_t> data(static_cast<uint32_t *>(bits), static_cast<uint32_t *>(bits) + w * ht);
    SelectObject(mem, old);
    DeleteObject(bitmap);
    DeleteDC(mem);
    ReleaseDC(h, screen);
    return data;
}
int main() {
    HWND h = FindWindowW(L"CodexPulse.Window.1", L"Codex Pulse · 演示");
    if (!h)
        return 2;
    SendMessageW(h, WM_APP + 1, 0, MAKELONG(WM_LBUTTONDBLCLK, 1));
    int checks = 0;
    auto check = [&](bool value, const char *label) {
        ++checks;
        if (!value) {
            std::cerr << "FAIL: " << label << '\n';
            ExitProcess(1);
        }
    };
    check((GetWindowLongPtrW(h, GWL_STYLE) & WS_CLIPCHILDREN) != 0, "parent excludes native child controls");
    SetWindowPos(h, nullptr, 100, 60, 480, 460, SWP_NOZORDER | SWP_NOACTIVATE);
    for (int theme = 0; theme < 2; ++theme) {
        if (theme) {
            SendMessageW(h, WM_COMMAND, 110, 0);
            SendMessageW(h, WM_COMMAND, 110, 0);
        }
        SendMessageW(h, WM_COMMAND, 100, 0);
        RedrawWindow(h, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
        // Allow posted layout/hover messages and the Windows compositor to settle after resizing.
        Sleep(150);
        HWND button = GetDlgItem(h, 101);
        auto before = pixels(button);
        for (int i = 0; i < 100; ++i) {
            // Paint only the parent. In 1.0.0 this erased the already-painted child button.
            RedrawWindow(h, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_NOCHILDREN);
            check(before == pixels(button), "parent repaint preserves visible button pixels");
            SendMessageW(h, WM_VSCROLL, i % 2 ? SB_PAGEUP : SB_PAGEDOWN, 0);
            UpdateWindow(h);
            check(before == pixels(button), "scroll leaves header button pixels unchanged");
        }
    }
    // Restore the default light overview for the remaining smoke test and screenshots.
    SendMessageW(h, WM_COMMAND, 110, 0);
    SendMessageW(h, WM_COMMAND, 100, 0);
    SetWindowPos(h, nullptr, 100, 60, 480, 520, SWP_NOZORDER | SWP_NOACTIVATE);
    std::cout << "PASS: " << checks << " repaint checks (200 parent redraws + 200 scrolls, light/dark)\n";
}
