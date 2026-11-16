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
    check(percent(37.6, 1) == L"37.6%", "tooltip keeps fractional percentages");
    check(percent(37.6) == L"38%", "existing page percentage formatting stays unchanged");
    CLSID png;
    CLSIDFromString(L"{557cf406-1a04-11d3-9a73-0000f81ef32e}", &png);
    auto luminance = [](Color color) {
        auto linear = [](BYTE channel) {
            const double v = channel / 255.;
            return v <= .04045 ? v / 12.92 : std::pow((v + .055) / 1.055, 2.4);
        };
        return .2126 * linear(color.GetR()) + .7152 * linear(color.GetG()) + .0722 * linear(color.GetB());
    };
    for (bool lightTaskbar : {false, true}) {
        for (int size : {16, 20, 24, 32, 48, 64}) {
            int previousInk = size * size * 255 + 1, emptyInk = 0, unknownInk = 0;
            for (int used : {0, 25, 50, 52, 75, 80, 81, 90, 91, 99, 100, -1}) {
                Bitmap bitmap(size, size, PixelFormat32bppARGB);
                {
                    Graphics g(&bitmap);
                    drawTrayIcon(g, size, used, lightTaskbar);
                }
                int visible = 0, opaque = 0, alpha = 0, minx = size, maxx = -1, miny = size, maxy = -1;
                bool correctColor = true;
                const Color expected = used > 90   ? (lightTaskbar ? Color(196, 43, 28) : Color(255, 69, 58))
                                       : used > 80 ? (lightTaskbar ? Color(157, 88, 0) : Color(255, 159, 10))
                                                   : (lightTaskbar ? Color(32, 32, 32) : Color(255, 255, 255));
                const double foreground = luminance(expected);
                const double background = luminance(lightTaskbar ? Color(243, 243, 243) : Color(32, 32, 32));
                check((std::max(foreground, background) + .05) / (std::min(foreground, background) + .05) >= 3,
                      "normal, warning, critical and unknown colors contrast with the taskbar");
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
                check(maxy - miny + 1 >= size * .9 && maxx - minx + 1 >= size * .55 &&
                          maxx - minx + 1 <= size * .7,
                      "progress bar keeps full height and a readable vertical silhouette");
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
                auto name = std::wstring(lightTaskbar ? L"dist/tray-light-" : L"dist/tray-dark-") +
                            std::to_wstring(size) + L"-" + std::to_wstring(used) + L".png";
                check(bitmap.Save(name.c_str(), &png, nullptr) == Ok, "save actual tray raster");
            }
            check(unknownInk > emptyInk, "unavailable data differs from zero remaining quota");
            auto sample = [&](double used, bool topHalfOnly = false) {
                Bitmap bitmap(size, size, PixelFormat32bppARGB);
                {
                    Graphics g(&bitmap);
                    drawTrayIcon(g, size, used, lightTaskbar);
                }
                int alpha = 0;
                bool binaryAlpha = true;
                for (int y = 0; y < (topHalfOnly ? size / 2 : size); ++y)
                    for (int x = 0; x < size; ++x) {
                        Color color;
                        bitmap.GetPixel(x, y, &color);
                        alpha += color.GetA();
                        binaryAlpha &= color.GetA() == 0 || color.GetA() == 255;
                    }
                return std::make_pair(alpha, binaryAlpha);
            };
            const auto empty = sample(100), full = sample(0);
            check(empty.second && full.second, "outline and full fill have no blurry translucent edges");
            check(full.first > empty.first, "progress track has usable interior pixels");
            check(sample(75, true).first == sample(100, true).first,
                  "remaining quota fills from the bottom, leaving the upper half empty at 25 percent");
            for (double remaining : {.1, .5, 1., 9.9, 10., 19.9, 20., 25., 37.5, 50., 62.3, 75., 99.9}) {
                const double drawn = static_cast<double>(sample(100 - remaining).first - empty.first) /
                                     (full.first - empty.first);
                check(std::abs(drawn - remaining / 100) < .001,
                      "visible progress area matches exact remaining quota within 0.1 percentage point");
            }
        }
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
    // Measure the actual selected fonts: mixed scripts, dates and numbers must
    // fit their existing rows at normal and scaled DPI without truncation.
    struct TypeSample {
        const wchar_t *value;
        float size, width, height;
        bool emphasis;
    };
    const TypeSample samples[] = {
        {L"Codex Pulse", TypeSize::title, 200, 28, true},
        {L"账户额度", TypeSize::heading, 160, 24, true},
        {L"本机 Token", TypeSize::heading, 160, 24, true},
        {L"5 小时额度", TypeSize::body, 180, 20, false},
        {L"重置 11-16 18:30", TypeSize::caption, 192, 18, false},
        {L"99.9%", TypeSize::quota, 120, 32, false},
        {L"203.3 M", TypeSize::metric, 128, 30, false},
        {L"gpt-5.1-codex", TypeSize::body, 192, 32, false},
        {L"输入 / Input", TypeSize::body, 160, 28, false},
        {L"8.45 M", TypeSize::caption, 55, 20, false},
    };
    for (int dpi : {96, 120, 144, 192}) {
        Bitmap bitmap(800, 200, PixelFormat32bppARGB);
        Graphics g(&bitmap);
        g.ScaleTransform(dpi / 96.f, dpi / 96.f);
        for (const auto &sample : samples) {
            const auto face = textFace(sample.value, sample.emphasis);
            Font font(face.family, sample.size, face.style, UnitPixel);
            check(font.GetLastStatus() == Ok, "selected text face supports the requested weight");
            StringFormat format;
            format.SetFormatFlags(StringFormatFlagsNoWrap);
            RectF bounds;
            INT characters = 0, lines = 0;
            check(g.MeasureString(sample.value, -1, &font, RectF(0, 0, sample.width, sample.height),
                                  &format, &bounds, &characters, &lines) == Ok &&
                      characters == static_cast<INT>(wcslen(sample.value)) && lines == 1 &&
                      bounds.Width <= sample.width && bounds.Height <= sample.height,
                  "Chinese, English and numeric text fits without clipping at each DPI");
        }
    }
    settings = Settings{};
    settings.theme = 1;
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
    check(dirty.left <= px(quotaBounds(0, sizeDip().first).X) &&
              dirty.right >= px(quotaBounds(1, sizeDip().first).GetRight()),
          "countdown repaint covers the full width of the unboxed quota text");
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
        savePreview(L"dist/preview-overview-dpi-" + std::to_wstring(dpi) + L".png");
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
    // Empty and single-quota accounts must keep the summary and activity accessible.
    for (size_t quotas : {size_t(0), size_t(1)}) {
        command(ID_OVERVIEW);
        snap = demoSnapshot();
        snap.quota.windows.resize(quotas);
        snap.status = "当前账户尚未提供订阅额度，请登录后刷新；本机 Token 仍可查看。";
        RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
        layout();
        check(contentHeight <= sizeDip().second - FOOTER, "empty and single-quota overview fits");
        check((GetWindowLongPtrW(window, GWL_STYLE) & WS_VSCROLL) == 0,
              "empty and single-quota overview needs no scrollbar");
        savePreview(L"dist/preview-quota-count-" + std::to_wstring(quotas) + L".png");
    }
    snap = demoSnapshot();
    snap.quota.limitReached = true;
    RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
    layout();
    check(contentHeight <= sizeDip().second - FOOTER, "quota limit warning fits below local activity");
    snap = demoSnapshot();
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
    snap.quota.windows[1].used = 62.4;
    updateTray();
    check(std::abs(trayRenderUsage - 62.4) < .0001, "tray does not ceil fractional usage before drawing");
    check(trayTooltip.find(L"最少剩余 37.6%") != std::wstring::npos &&
              trayTooltip.find(L"对应额度：" + windowName(snap.quota.windows[1])) != std::wstring::npos,
          "tray tooltip names the limiting quota and its fractional remaining value");
    // Redirect HKCU to a disposable key; never change the user's actual theme.
    {
        const std::wstring sandboxPath = L"Software\\CodexPulseTests\\tray-" +
                                         std::to_wstring(GetCurrentProcessId());
        HKEY sandbox = nullptr, personalize = nullptr;
        check(RegCreateKeyExW(HKEY_CURRENT_USER, sandboxPath.c_str(), 0, nullptr, 0, KEY_ALL_ACCESS,
                              nullptr, &sandbox, nullptr) == ERROR_SUCCESS,
              "create isolated theme registry");
        check(RegOverridePredefKey(HKEY_CURRENT_USER, sandbox) == ERROR_SUCCESS, "isolate theme settings");
        check(!taskbarUsesLightTheme(), "missing system theme keeps legacy dark-taskbar behavior");
        check(RegCreateKeyExW(HKEY_CURRENT_USER,
                              L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                              0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &personalize, nullptr) == ERROR_SUCCESS,
              "create test personalization settings");
        auto setTheme = [&](const wchar_t *name, DWORD value) {
            check(RegSetValueExW(personalize, name, 0, REG_DWORD, reinterpret_cast<const BYTE *>(&value),
                                 sizeof value) == ERROR_SUCCESS,
                  "set isolated theme preference");
        };
        for (bool lightTaskbar : {true, false, true}) {
            setTheme(L"SystemUsesLightTheme", lightTaskbar ? 1 : 0);
            setTheme(L"AppsUseLightTheme", lightTaskbar ? 0 : 1);
            settings.theme = lightTaskbar ? 2 : 1;
            check(taskbarUsesLightTheme() == lightTaskbar, "taskbar theme is independent of app mode");
            const HICON before = trayIcon;
            const bool wasLight = trayRenderLight;
            ShowWindow(window, SW_HIDE);
            SendMessageW(window, WM_SETTINGCHANGE, 0, reinterpret_cast<LPARAM>(L"ImmersiveColorSet"));
            check(trayAdded && trayRenderLight == lightTaskbar, "hidden tray updates on theme change");
            if (wasLight != lightTaskbar)
                check(trayIcon != before, "theme changes invalidate the cached icon at unchanged usage");
            const HICON current = trayIcon;
            SendMessageW(window, WM_THEMECHANGED, 0, 0);
            check(trayIcon == current, "unchanged theme reuses the cached icon");
        }
        // The alternate theme notification must also redraw, without a usage refresh.
        setTheme(L"SystemUsesLightTheme", 0);
        SendMessageW(window, WM_THEMECHANGED, 0, 0);
        check(trayAdded && !trayRenderLight, "WM_THEMECHANGED refreshes the taskbar palette");
        RegCloseKey(personalize);
        check(RegOverridePredefKey(HKEY_CURRENT_USER, nullptr) == ERROR_SUCCESS, "restore real HKCU");
        RegCloseKey(sandbox);
        check(RegDeleteTreeW(HKEY_CURRENT_USER, sandboxPath.c_str()) == ERROR_SUCCESS,
              "remove isolated theme registry");
        showWindow();
        updateColors();
        updateTray();
    }
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
