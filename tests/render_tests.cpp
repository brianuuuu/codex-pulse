// Reuse the production drawing functions; this console test never starts the application entry point.
#include "../src/main.cpp"
#include <iostream>
int main() {
    ULONG_PTR token;
    GdiplusStartupInput init;
    if (GdiplusStartup(&token, &init, nullptr) != Ok)
        return 2;
    int checks = 0;
    auto check = [&](bool v, const char *name) {
        ++checks;
        if (!v) {
            std::cerr << "FAIL: " << name << '\n';
            ExitProcess(1);
        }
    };
    check(shortCount(203300000) == L"203.3 M", "large totals use compact precision");
    check(shortCount(65120000) == L"65.12 M", "retain useful precision below 100M");
    check(shortCount(1234000000) == L"1.23 B", "billions avoid overflowing card");
    check(shortCount(999999999) == L"1.00 B", "rounding promotes suffix");
    CLSID png;
    CLSIDFromString(L"{557cf406-1a04-11d3-9a73-0000f81ef32e}", &png);
    for (int size : {16, 20, 24, 32, 48, 64}) {
        int previousInk = size * size * 255 + 1, emptyInk = 0, unknownInk = 0;
        for (int used : {0, 25, 50, 52, 75, 80, 81, 90, 91, 99, 100, -1}) {
            Bitmap bitmap(size, size, PixelFormat32bppARGB);
            {
                Graphics g(&bitmap);
                drawTrayIcon(g, size, used);
            }
            int visible = 0, opaque = 0, alpha = 0, minx = size, maxx = -1, miny = size, maxy = -1;
            bool correctColor = true;
            const Color expected = used > 90   ? Color(255, 69, 58)
                                   : used > 80 ? Color(255, 159, 10)
                                               : Color(255, 255, 255);
            for (int y = 0; y < size; ++y)
                for (int x = 0; x < size; ++x) {
                    Color c;
                    bitmap.GetPixel(x, y, &c);
                    alpha += c.GetA();
                    if (c.GetA() > 240)
                        correctColor &= std::abs(int(c.GetR()) - int(expected.GetR())) <= 2 &&
                                        std::abs(int(c.GetG()) - int(expected.GetG())) <= 2 &&
                                        std::abs(int(c.GetB()) - int(expected.GetB())) <= 2;
                    if (c.GetA() > 240)
                        ++opaque;
                    if (c.GetA() > 127) {
                        ++visible;
                        minx = std::min(minx, x);
                        maxx = std::max(maxx, x);
                        miny = std::min(miny, y);
                        maxy = std::max(maxy, y);
                    }
                }
            check(correctColor && opaque > 0, "outline and fill use the remaining-quota threshold color");
            check(visible >= size * 1.5, "empty rounded progress bar outline remains readable");
            check(opaque < size * size * .7, "rounded progress bar has no solid colored tile");
            check(maxx - minx + 1 >= size * .9 && maxy - miny + 1 >= size * .5 - 1,
                  "rounded progress bar uses the native slot with a horizontal silhouette");
            check(minx >= 0 && maxx < size && miny >= 0 && maxy < size,
                  "rounded progress bar stays inside icon");
            if (used >= 0) {
                // Subpixel rounded arcs can shift up to one opaque pixel of coverage.
                check(alpha <= previousInk + 255, "remaining charge decreases within raster tolerance");
                previousInk = alpha;
                if (used == 100)
                    emptyInk = alpha;
            } else
                unknownInk = alpha;
            auto name = L"dist/tray-" + std::to_wstring(size) + L"-" + std::to_wstring(used) + L".png";
            check(bitmap.Save(name.c_str(), &png, nullptr) == Ok, "save actual tray raster");
        }
        check(unknownInk > emptyInk, "unavailable data differs from zero remaining quota");
    }
    // Exercise the minute-only refresh policy without waiting for real wall-clock boundaries.
    HINSTANCE inst = GetModuleHandleW(nullptr);
    WNDCLASSW cls{};
    cls.lpfnWndProc = proc;
    cls.style = CS_DBLCLKS;
    cls.hInstance = inst;
    cls.lpszClassName = L"PulseRenderProbe";
    RegisterClassW(&cls);
    instance = inst;
    demo = true;
    chooseFont();
    settings = Settings{};
    snap = demoSnapshot();
    window = CreateWindowW(cls.lpszClassName, L"probe", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, 20, 20,
                           DEFAULT_WIDTH, DEFAULT_HEIGHT, nullptr, nullptr, inst, nullptr);
    KillTimer(window, 1);
    ShowWindow(window, SW_SHOW);
    UpdateWindow(window);
    demo = true;
    tab = 0;
    scrollY = 0;
    dpiScale = 1;
    snap = demoSnapshot();
    countdownMinute = 0;
    ValidateRect(window, nullptr);
    updateCountdowns(1200);
    RECT dirty{};
    check(GetUpdateRect(window, &dirty, FALSE) && dirty.top >= QUOTA_TOP + RESET_ROW &&
              dirty.bottom <= QUOTA_TOP + RESET_ROW + 36,
          "countdown invalidates only reset rows");
    ValidateRect(window, nullptr);
    for (time_t now = 1201; now < 1260; ++now)
        updateCountdowns(now);
    check(!GetUpdateRect(window, nullptr, FALSE), "59 idle timer ticks do not repaint the window");
    updateCountdowns(1260);
    check(GetUpdateRect(window, nullptr, FALSE), "next minute updates countdown");
    ValidateRect(window, nullptr);
    tab = 1;
    updateCountdowns(1320);
    check(!GetUpdateRect(window, nullptr, FALSE), "details page is not repainted by countdown");
    tab = 0;
    snap = demoSnapshot();
    auto savePreview = [&](const std::wstring &path) {
        RECT client{};
        GetClientRect(window, &client);
        HDC screen = GetDC(window), memory = CreateCompatibleDC(screen);
        HBITMAP dib = CreateCompatibleBitmap(screen, client.right, client.bottom);
        auto old = SelectObject(memory, dib);
        BitBlt(memory, 0, 0, client.right, client.bottom, screen, 0, 0, SRCCOPY);
        {
            Bitmap capture(dib, nullptr);
            check(capture.Save(path.c_str(), &png, nullptr) == Ok, "save production window preview");
        }
        SelectObject(memory, old);
        DeleteObject(dib);
        DeleteDC(memory);
        ReleaseDC(window, screen);
    };
    for (int dpi : {96, 120, 144}) {
        float scale = dpi / 96.f;
        RECT area{30, 30, 30 + static_cast<int>(DEFAULT_WIDTH * scale),
                  30 + static_cast<int>(DEFAULT_HEIGHT * scale)};
        // In-process pointer is valid; do not send this pointer-bearing message across processes.
        SendMessageW(window, WM_DPICHANGED, MAKELONG(dpi, dpi), reinterpret_cast<LPARAM>(&area));
        RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
        check(std::abs(dpiScale - scale) < .001f, "DPI change updates logical scaling");
        savePreview(L"dist/preview-blue-dpi-" + std::to_wstring(dpi) + L".png");
    }
    check((GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_LAYERED) == 0, "window stays fully opaque");
    check(tooltip && SendMessageW(tooltip, TTM_GETTOOLCOUNT, 0, 0) == static_cast<LRESULT>(controls.size()),
          "every icon has a tooltip");
    check(tooltipFont && SendMessageW(tooltip, WM_GETFONT, 0, 0) == reinterpret_cast<LRESULT>(tooltipFont),
          "tooltip uses the selected Unicode UI font");
    check(!controlTip(ID_THEME).empty() && !controlTip(ID_COPY).empty(), "icon labels remain accessible");
    const bool priorStartup = autostartRegistered();
    initializeAutostart();
    check(!settings.autostartInitialized && autostartRegistered() == priorStartup,
          "demo launch never registers startup");
    check(autostart(), "demo reflects the default enabled startup option");
    command(ID_AUTOSTART);
    check(!autostart() && autostartRegistered() == priorStartup,
          "demo startup toggle changes only the preview");
    command(ID_AUTOSTART);
    command(ID_ACTIVITY);
    command(ID_SESSIONS);
    check(pageCount() > 1, "long session history uses pages");
    command(ID_NEXT);
    check(activityPage == 1, "next page changes session page");
    command(ID_PREV);
    check(activityPage == 0, "previous page returns to first page");
    command(ID_STATS);
    const float pageWidth = sizeDip().first;
    for (int page : {ID_OVERVIEW, ID_ACTIVITY, ID_SETTINGS}) {
        command(page);
        RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
        layout();
        auto [width, height] = sizeDip();
        check(std::abs(width - pageWidth) < .01f, "tabs keep the same content width");
        check(contentHeight <= height - FOOTER, "primary view fits without scrolling");
        check((GetWindowLongPtrW(window, GWL_STYLE) & WS_VSCROLL) == 0, "primary view hides the scrollbar");
        savePreview(L"dist/preview-page-" + std::to_wstring(page) + L".png");
    }
    settings.theme = 2;
    updateColors();
    command(ID_OVERVIEW);
    RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
    savePreview(L"dist/preview-dark.png");
    settings.theme = 1;
    updateColors();
    command(ID_OVERVIEW);
    snap.quota.windows.resize(8, snap.quota.windows.front());
    RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
    layout();
    check((GetWindowLongPtrW(window, GWL_STYLE) & WS_VSCROLL) != 0,
          "extra quota windows remain reachable by overflow scrolling");
    check(std::abs(sizeDip().first - pageWidth) < .01f,
          "overflow scrollbar does not shrink cards or navigation");
    snap = demoSnapshot();
    command(ID_OVERVIEW);
    RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
    layout();
    check((GetWindowLongPtrW(window, GWL_STYLE) & WS_VSCROLL) == 0,
          "scrollbar disappears when normal content returns");
    check(std::abs(sizeDip().first - pageWidth) < .01f,
          "content width stays stable after overflow clears");
    // Integrated chrome must keep native window operations and hit targets.
    RECT outer{}, client{};
    GetWindowRect(window, &outer);
    GetClientRect(window, &client);
    POINT origin{0, 0};
    ClientToScreen(window, &origin);
    check(origin.x == outer.left && origin.y == outer.top,
          "client area extends through the former native title bar");
    auto hit = [&](int x, int y) {
        POINT point{px(x), px(y)};
        ClientToScreen(window, &point);
        return SendMessageW(window, WM_NCHITTEST, 0, MAKELPARAM(point.x, point.y));
    };
    check(hit(100, 36) == HTCAPTION, "brand area supports native title dragging");
    check(hit(100, 100) == HTCLIENT, "data area does not drag the window");
    check(hit(1, 1) == HTTOPLEFT, "corner supports diagonal resizing");
    check(hit(200, 1) == HTTOP, "top edge supports resizing");
    RECT maximize{};
    GetWindowRect(controls[ID_MAXIMIZE], &maximize);
    check(SendMessageW(window, WM_NCHITTEST, 0,
                       MAKELPARAM((maximize.left + maximize.right) / 2,
                                  (maximize.top + maximize.bottom) / 2)) == HTCLIENT,
          "caption buttons do not accidentally drag the window");
    command(ID_MAXIMIZE);
    check(IsZoomed(window), "integrated maximize button maximizes");
    command(ID_MAXIMIZE);
    check(!IsZoomed(window), "integrated restore button restores");
    SendMessageW(window, WM_NCLBUTTONDBLCLK, HTCAPTION, 0);
    check(IsZoomed(window), "double-click title maximizes");
    SendMessageW(window, WM_NCLBUTTONDBLCLK, HTCAPTION, 0);
    check(!IsZoomed(window), "double-click title restores");
    updateTray();
    command(ID_MINIMIZE);
    check(!IsWindowVisible(window), "integrated minimize respects tray preference");
    showWindow();
    command(ID_CLOSE);
    check(!IsWindowVisible(window), "integrated close collects into tray");
    showWindow();
    check(IsWindowVisible(window), "window restores after integrated close");
    DestroyWindow(window);
    window = nullptr;
    std::cout << "PASS: " << checks << " icon, number and timer rendering checks\n";
    GdiplusShutdown(token);
    return 0;
}
