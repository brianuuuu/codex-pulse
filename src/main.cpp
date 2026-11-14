#include "platform.hpp"
#include "tray_icon.hpp"
#include "ui_icons.hpp"
#include <commctrl.h>
#include <shobjidl.h>
using namespace pulse;
using namespace Gdiplus;
namespace {
constexpr wchar_t CLASS_NAME[] = L"CodexPulse.Window.1";
constexpr UINT MSG_TRAY = WM_APP + 1, MSG_READY = WM_APP + 2;
constexpr int ID_OVERVIEW = 100, ID_ACTIVITY = 101, ID_SETTINGS = 102, ID_REFRESH = 103, ID_COPY = 104,
              ID_MORE = 105, ID_THEME = 110, ID_INTERVAL = 111, ID_ONLINE = 112, ID_TRAY = 113,
              ID_NOTIFY = 114, ID_HOME = 115, ID_AUTOSTART = 116, ID_RESET_HOME = 117, ID_STATUS = 118,
              ID_ABOUT = 119, ID_STATS = 120, ID_MODELS = 121, ID_SESSIONS = 122, ID_PREV = 123,
              ID_NEXT = 124, ID_MINIMIZE = 125, ID_MAXIMIZE = 126, ID_CLOSE = 127;
constexpr int DEFAULT_WIDTH = 480, DEFAULT_HEIGHT = 520;
constexpr int TITLE_HEIGHT = 44, BODY_TOP = 78, FOOTER = 44;
constexpr int QUOTA_TOP = BODY_TOP + 144, QUOTA_STEP = 126, RESET_ROW = 76;
int activityView = 0, activityPage = 0;
HWND tooltip = nullptr;
HFONT tooltipFont = nullptr;
std::wstring tooltipBuffer;
bool copied = false;
int hoveredControl = 0;
HWND window = nullptr;
HINSTANCE instance = nullptr;
float dpiScale = 1;
int tab = 0, scrollY = 0, contentHeight = 0;
Settings settings;
bool demo = false, busy = false, trayAdded = false, closing = false;
unsigned generation = 0;
std::thread worker;
std::atomic<bool> stopping{false};
std::mutex resultMutex;
UINT taskbarCreated = 0;
HICON windowIconLarge = nullptr, windowIconSmall = nullptr;
int trayRenderSize = 0;
double trayRenderUsage = -2;
std::wstring trayTooltip;
time_t countdownMinute = 0;
HICON trayIcon = nullptr;
ULONG_PTR gdip = 0;
time_t nextRefresh = 0;
int failures = 0;
std::string notifiedWindow;
struct Snapshot {
    Quota quota;
    Activity activity;
    std::string status = "正在读取用量…", account, identity, authSource;
    fs::path home;
    time_t refreshed = 0;
    bool cached = false, online = false;
    int retry = 0;
};
Snapshot snap, pending;
unsigned pendingGeneration = 0;
struct Palette {
    Color bg, card, text, muted, line, accent, soft, warning, inset;
};
Palette colors;
std::map<int, HWND> controls;
std::wstring uiFont = L"Segoe UI";
void chooseFont() {
    for (auto name : {L"Microsoft YaHei UI", L"Microsoft YaHei", L"Noto Sans CJK SC", L"Segoe UI", L"Arial",
                      L"DejaVu Sans"}) {
        FontFamily family(name);
        if (family.IsAvailable()) {
            uiFont = name;
            return;
        }
    }
}
int px(float x) {
    return static_cast<int>(std::lround(x * dpiScale));
}
std::pair<float, float> sizeDip() {
    RECT r{};
    GetClientRect(window, &r);
    // Keep card and navigation widths stable when a page needs the native scrollbar.
    // The bar occupies the existing right gutter, outside the cards and controls.
    if (GetWindowLongPtrW(window, GWL_STYLE) & WS_VSCROLL)
        r.right += GetSystemMetricsForDpi(SM_CXVSCROLL, GetDpiForWindow(window));
    return {r.right / dpiScale, r.bottom / dpiScale};
}
bool systemDark() {
    DWORD value = 1, n = sizeof value;
    RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                 L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &n);
    return value == 0;
}
void updateColors() {
    bool dark = settings.theme == 2 || (settings.theme == 0 && systemDark());
    colors =
        dark ? Palette{Color(23, 27, 36), Color(35, 41, 54), Color(237, 240, 247), Color(164, 173, 190),
                       Color(52, 61, 78), Color(124, 168, 255), Color(39, 55, 83), Color(241, 182, 91),
                       Color(28, 33, 46)}
             : Palette{Color(245, 245, 249), Color(255, 255, 255), Color(24, 28, 38), Color(119, 122, 133),
                       Color(229, 232, 240), Color(48, 103, 216), Color(234, 241, 253), Color(180, 108, 25),
                       Color(248, 248, 252)};
    BOOL value = dark;
    DwmSetWindowAttribute(window, 20, &value, sizeof value);
    COLORREF border = RGB(colors.line.GetR(), colors.line.GetG(), colors.line.GetB());
    DwmSetWindowAttribute(window, 34, &border, sizeof border);
    InvalidateRect(window, nullptr, FALSE);
    for (auto [id, h] : controls)
        InvalidateRect(h, nullptr, FALSE);
}
void rounded(Graphics &g, RectF r, float radius, Color color, const Color *gradientEnd = nullptr) {
    if (r.Width <= 0 || r.Height <= 0)
        return;
    float d = std::min({radius * 2, r.Width, r.Height});
    GraphicsPath p;
    p.AddArc(r.X, r.Y, d, d, 180, 90);
    p.AddArc(r.GetRight() - d, r.Y, d, d, 270, 90);
    p.AddArc(r.GetRight() - d, r.GetBottom() - d, d, d, 0, 90);
    p.AddArc(r.X, r.GetBottom() - d, d, d, 90, 90);
    p.CloseFigure();
    if (gradientEnd) {
        LinearGradientBrush gradient(PointF(r.X, r.Y), PointF(r.GetRight(), r.GetBottom()), color,
                                     *gradientEnd);
        g.FillPath(&gradient, &p);
    } else {
        SolidBrush b(color);
        g.FillPath(&b, &p);
    }
}
void text(Graphics &g, const std::wstring &s, float x, float y, float w, float h, float fontSize, Color c,
          bool bold = false, StringAlignment align = StringAlignmentNear, bool wrap = false) {
    Font f(uiFont.c_str(), fontSize, bold ? FontStyleBold : FontStyleRegular, UnitPixel);
    StringFormat fmt;
    fmt.SetAlignment(align);
    fmt.SetLineAlignment(StringAlignmentCenter);
    fmt.SetTrimming(wrap ? StringTrimmingEllipsisWord : StringTrimmingEllipsisCharacter);
    if (!wrap)
        fmt.SetFormatFlags(StringFormatFlagsNoWrap);
    SolidBrush b(c);
    g.DrawString(s.c_str(), -1, &f, RectF(x, y, w, h), &fmt, &b);
}
void fittedNumber(Graphics &g, const std::wstring &value, float x, float y, float w, float h) {
    StringFormat format(StringFormat::GenericTypographic());
    format.SetFormatFlags(StringFormatFlagsNoWrap);
    format.SetLineAlignment(StringAlignmentCenter);
    format.SetTrimming(StringTrimmingNone);
    float fontSize = 22.f;
    Font initial(uiFont.c_str(), fontSize, FontStyleBold, UnitPixel);
    RectF measured;
    g.MeasureString(value.c_str(), -1, &initial, PointF(0, 0), &format, &measured);
    if (measured.Width > w - 2)
        fontSize *= std::max(0.4f, (w - 2) / measured.Width);
    Font fitted(uiFont.c_str(), fontSize, FontStyleBold, UnitPixel);
    SolidBrush brush(colors.text);
    g.DrawString(value.c_str(), -1, &fitted, RectF(x, y, w, h), &format, &brush);
}
void label(Graphics &g, const std::wstring &name, const std::wstring &value, float y, float width,
           Color valueColor) {
    text(g, name, 40, y, width * .50f, 28, 12, colors.muted);
    text(g, value, width * .50f, y, width * .50f - 40, 28, 13, valueColor, false, StringAlignmentFar);
}
void section(Graphics &g, const std::wstring &s, float y, float w) {
    text(g, s, 24, y, w - 48, 24, 14, colors.text, true);
}
std::wstring sourceLabel() {
    if (demo)
        return L"演示数据 · 未连接账户";
    if (snap.quota.source == "online")
        return snap.cached ? L"在线缓存 · 刷新未成功" : L"在线额度 · ChatGPT";
    if (snap.quota.source == "local")
        return L"本地快照 · 可能已过期";
    return L"等待可用额度数据";
}
std::wstring statusLabel() {
    if (busy)
        return L"正在后台刷新…";
    return wide(snap.status);
}
RectF quotaBounds(size_t index, float width) {
    const float cardWidth = (width - 92) / 2;
    return RectF(40 + static_cast<float>(index % 2) * (cardWidth + 12),
                 QUOTA_TOP + static_cast<float>(index / 2) * QUOTA_STEP, cardWidth, 116);
}
void quotaCard(Graphics &g, const Window &q, const RectF &r) {
    const float x = r.X + 12, y = r.Y, width = r.Width - 24;
    rounded(g, r, 10, colors.inset);
    text(g, windowName(q), x, y + 6, width, 20, 12, colors.text, true);
    Color tone = q.used >= 90 ? colors.warning : colors.accent;
    text(g, percent(q.used), x, y + 26, width - 30, 32, 26, tone, true);
    text(g, L"已用", r.GetRight() - 42, y + 36, 30, 18, 9, colors.muted, false, StringAlignmentFar);
    rounded(g, RectF(x, y + 64, width, 5), 2.5f, colors.line);
    if (q.valid() && q.used > 0)
        rounded(g, RectF(x, y + 64, width * static_cast<float>(q.used / 100), 5), 2.5f, tone);
    text(g, q.reset ? L"重置 " + timestamp(q.reset) : L"重置时间未提供", x, y + RESET_ROW, width,
         18, 10, colors.muted);
    text(g, q.reset ? duration(q.reset - std::time(nullptr)) : L"—", x, y + RESET_ROW + 18,
         width, 18, 10, colors.muted);
}
float drawOverview(Graphics &g, float w) {
    float y = BODY_TOP + 12;
    const Color heroEnd = colors.bg.GetR() < 80 ? Color(39, 58, 91) : Color(222, 235, 255);
    rounded(g, RectF(24, y, w - 48, 86), 16, colors.card, &heroEnd);
    text(g, L"用量工作台", 40, y + 6, 140, 20, 12, colors.accent, true);
    const auto state = demo                           ? L"演示数据"
                       : busy                         ? L"正在刷新"
                       : snap.cached                  ? L"在线缓存"
                       : snap.quota.source == "local" ? L"本地快照"
                       : snap.online                  ? L"已连接"
                                                      : L"未连接";
    text(g, L"●  " + std::wstring(state), w - 160, y + 6, 120, 20, 10,
         snap.cached ? colors.warning : colors.muted, false, StringAlignmentFar);
    const auto today = localTime(std::time(nullptr));
    const auto date = std::to_wstring(today.tm_mon + 1) + L"月" + std::to_wstring(today.tm_mday) + L"日";
    const wchar_t *weekdays[] = {L"星期日", L"星期一", L"星期二", L"星期三", L"星期四", L"星期五", L"星期六"};
    text(g, date, 40, y + 26, 140, 30, 22, colors.text, true);
    text(g, weekdays[today.tm_wday], 188, y + 30, 80, 26, 13, colors.muted);
    text(g, wide(snap.account.empty() ? "未连接账户" : snap.account), 40, y + 62, (w - 80) * .52f,
         18, 10.5f, colors.muted);
    const auto credits = snap.quota.unlimited ? L"∞" : snap.quota.creditBalance.empty()
                                                        ? L"—" : wide(snap.quota.creditBalance);
    const auto plan = wide(snap.quota.plan.empty() ? "—" : snap.quota.plan);
    text(g, plan + L"  ·  " + credits + L" Credits", w * .53f, y + 62, w * .47f - 40, 18,
         10.5f, colors.muted, false, StringAlignmentFar);
    y += 98;
    const int rows = std::max(1, static_cast<int>((snap.quota.windows.size() + 1) / 2));
    const float quotaHeight = snap.quota.windows.empty() ? 126.f : 34.f + rows * QUOTA_STEP;
    rounded(g, RectF(24, y, w - 48, quotaHeight), 16, colors.card);
    text(g, L"账户额度", 40, y + 6, 120, 22, 14, colors.text, true);
    text(g, L"采样 " + timestamp(snap.quota.observed), w - 210, y + 7, 170, 20, 9, colors.muted,
         false, StringAlignmentFar);
    if (snap.quota.windows.empty()) {
        rounded(g, RectF(40, y + 34, w - 80, 80), 10, colors.inset);
        drawGlyph(g, Glyph::Cloud, 52, y + 46, 18, colors.muted);
        text(g, L"暂无额度数据", 80, y + 40, w - 132, 26, 12, colors.text, true);
        text(g, wide(snap.status), 52, y + 72, w - 104, 34, 11, colors.muted, false,
             StringAlignmentNear, true);
    } else {
        for (size_t i = 0; i < snap.quota.windows.size(); ++i)
            quotaCard(g, snap.quota.windows[i], quotaBounds(i, w));
    }
    y += quotaHeight + 12;
    rounded(g, RectF(24, y, w - 48, 100), 16, colors.card);
    text(g, L"本机 Token", 40, y + 6, w - 80, 22, 14, colors.text, true);
    float gap = 10, cw = (w - 100) / 3;
    const std::array<std::pair<std::wstring, int64_t>, 3> cards{{{L"今天", snap.activity.today.total},
                                                                 {L"7 天", snap.activity.week.total},
                                                                 {L"30 天", snap.activity.month.total}}};
    for (size_t i = 0; i < cards.size(); ++i) {
        float x = 40 + static_cast<float>(i) * (cw + gap);
        rounded(g, RectF(x, y + 34, cw, 54), 10, colors.inset);
        text(g, cards[i].first, x + 10, y + 38, cw - 20, 18, 10, colors.muted);
        fittedNumber(g, shortCount(cards[i].second), x + 10, y + 57, cw - 20, 27);
    }
    y += 112;
    if (snap.quota.limitReached) {
        text(g, L"额度已达上限", 28, y, w - 56, 24, 11, colors.warning);
        y += 32;
    }
    return y;
}
int pageCapacity() {
    auto [w, h] = sizeDip();
    (void)w;
    return std::max(1, static_cast<int>((h - FOOTER - BODY_TOP - 84) / 56));
}
int activityItemCount() {
    return activityView == 1 ? static_cast<int>(snap.activity.models.size())
                             : static_cast<int>(snap.activity.recent.size());
}
int pageCount() {
    return std::max(1, (activityItemCount() + pageCapacity() - 1) / pageCapacity());
}
float drawActivity(Graphics &g, float w) {
    const auto [unused, h] = sizeDip();
    (void)unused;
    float y = BODY_TOP + 8;
    section(g, activityView == 0 ? L"用量 · 本机" : activityView == 1 ? L"模型 · 30 天" : L"最近会话", y, w);
    y += 38;
    if (activityView != 0) {
        activityPage = std::clamp(activityPage, 0, pageCount() - 1);
        int first = activityPage * pageCapacity(),
            last = std::min(activityItemCount(), first + pageCapacity());
        std::vector<std::pair<std::string, Tokens>> models(snap.activity.models.begin(),
                                                           snap.activity.models.end());
        std::sort(models.begin(), models.end(),
                  [](auto &a, auto &b) { return a.second.total > b.second.total; });
        if (first == last) {
            text(g, L"暂无记录", 40, y, w - 80, 44, 12, colors.muted);
        }
        for (int i = first; i < last; ++i) {
            rounded(g, RectF(24, y, w - 48, 48), 11, colors.card);
            if (activityView == 1) {
                drawGlyph(g, Glyph::Models, 40, y + 14, 20, colors.muted);
                text(g, wide(models[i].first), 72, y + 8, w - 242, 32, 12, colors.text, true);
                text(g, shortCount(models[i].second.total), w - 176, y + 8, 134, 32, 15, colors.accent, true,
                     StringAlignmentFar);
            } else {
                const auto &item = snap.activity.recent[i];
                text(g, wide(item.model), 40, y + 3, w - 218, 23, 12, colors.text, true);
                text(g, timestamp(item.last) + L"  ·  " + wide(item.id.substr(0, 8)), 40, y + 24, w - 218, 18,
                     10, colors.muted);
                text(g, shortCount(item.total.total), w - 172, y + 8, 130, 32, 15, colors.accent, true,
                     StringAlignmentFar);
            }
            y += 56;
        }
        if (pageCount() > 1)
            text(g, std::to_wstring(activityPage + 1) + L" / " + std::to_wstring(pageCount()), w / 2 - 40,
                 h - FOOTER - 31, 80, 26, 11, colors.muted, false, StringAlignmentCenter);
        return h - FOOTER - 4;
    }
    float chartHeight = std::clamp(h - FOOTER - y - 246, 84.f, 138.f);
    rounded(g, RectF(24, y, w - 48, chartHeight), 16, colors.card);
    std::array<int64_t, 7> totals{};
    std::array<std::string, 7> days{};
    int64_t peak = 1;
    for (int i = 0; i < 7; ++i) {
        auto tm = localTime(std::time(nullptr));
        tm.tm_mday -= 6 - i;
        tm.tm_isdst = -1;
        days[i] = dateKey(std::mktime(&tm));
        auto it = snap.activity.daily.find(days[i]);
        totals[i] = it == snap.activity.daily.end() ? 0 : it->second.total;
        peak = std::max(peak, totals[i]);
    }
    float col = (w - 84) / 7, baseline = y + chartHeight - 29, barMax = chartHeight - 61;
    for (int i = 0; i < 7; ++i) {
        float x = 42 + i * col, bh = static_cast<float>(totals[i]) * barMax / static_cast<float>(peak);
        text(g, shortCount(totals[i]), x, y + 7, col, 20, 9, colors.muted, false, StringAlignmentCenter);
        rounded(g, RectF(x + col * .28f, baseline - std::max(3.f, bh), col * .44f, std::max(3.f, bh)), 3,
                i == 6 ? colors.accent : colors.soft);
        text(g, wide(days[i].substr(5)), x, baseline + 6, col, 19, 10, colors.muted, false,
             StringAlignmentCenter);
    }
    y += chartHeight + 14;
    section(g, L"今日 Token", y, w);
    y += 28;
    rounded(g, RectF(24, y, w - 48, 158), 16, colors.card);
    label(g, L"输入", count(snap.activity.today.input), y + 8, w, colors.text);
    label(g, L"    缓存输入", count(snap.activity.today.cached), y + 34, w, colors.muted);
    label(g, L"输出", count(snap.activity.today.output), y + 60, w, colors.text);
    label(g, L"    推理输出", count(snap.activity.today.reasoning), y + 86, w, colors.muted);
    label(g, L"合计", count(snap.activity.today.total), y + 124, w, colors.accent);
    y += 172;
    drawGlyph(g, Glyph::Folder, 30, y + 3, 16, colors.muted);
    text(g, std::to_wstring(snap.activity.files), 53, y, w / 4, 22, 11, colors.muted);
    drawGlyph(g, Glyph::Session, w / 3, y + 3, 16, colors.muted);
    text(g, std::to_wstring(snap.activity.sessions), w / 3 + 23, y, w / 4, 22, 11, colors.muted);
    if (snap.activity.partial || snap.activity.malformed) {
        drawGlyph(g, Glyph::Warning, w - 144, y + 3, 16, colors.warning);
        text(g, snap.activity.partial ? L"索引中" : std::to_wstring(snap.activity.malformed), w - 121, y, 96,
             22, 11, colors.warning, false, StringAlignmentFar);
    }
    return y + 30;
}
bool autostart() {
    return demo ? settings.autostart : autostartRegistered();
}

std::wstring themeLabel() {
    return settings.theme == 0 ? L"跟随系统" : settings.theme == 1 ? L"浅色" : L"深色";
}
void setText(int id, const std::wstring &s) {
    auto it = controls.find(id);
    if (it == controls.end())
        return;
    wchar_t current[256]{};
    GetWindowTextW(it->second, current, 256);
    if (s != current)
        SetWindowTextW(it->second, s.c_str());
}
void updateControlText() {
    setText(ID_THEME, L"外观：" + themeLabel());
    setText(ID_INTERVAL, L"刷新：" + std::to_wstring(settings.interval / 60) + L" 分钟");
    setText(ID_ONLINE, settings.online ? L"在线额度：已开启" : L"在线额度：已关闭");
    setText(ID_TRAY, settings.tray ? L"最小化到托盘：开" : L"最小化到托盘：关");
    setText(ID_NOTIFY, settings.notify ? L"额度提醒：已开启" : L"额度提醒：已关闭");
    setText(ID_AUTOSTART, autostart() ? L"开机启动：已开启" : L"开机启动：已关闭");
    setText(ID_REFRESH, busy ? L"正在刷新…" : L"刷新   F5");
}
float drawSettings(Graphics &g, float w) {
    float y = BODY_TOP + 8;
    section(g, L"偏好", y, w);
    y += 28;
    rounded(g, RectF(24, y, w - 48, 128), 16, colors.card);
    y += 142;
    section(g, L"数据目录", y, w);
    y += 28;
    rounded(g, RectF(24, y, w - 48, 84), 16, colors.card);
    auto home = settings.home.empty() ? defaultHome() : fs::path(settings.home);
    text(g, home.wstring(), 40, y + 8, w - 80, 30, 11, colors.text, false, StringAlignmentNear, true);
    y += 96;
    rounded(g, RectF(24, y, w - 48, 44), 16, colors.card);
    drawGlyph(g, Glyph::Cloud, 40, y + 12, 20, colors.muted);
    text(g,
         demo                             ? L"演示"
         : snap.authSource == "auth.json" ? L"文件登录"
         : snap.authSource == "Windows 凭据管理器"
             ? L"系统凭据"
             : wide(snap.authSource.empty() ? "未连接" : snap.authSource),
         72, y + 8, w - 112, 28, 12, colors.text);
    return y + 56;
}
std::wstring controlTip(int id) {
    if (id == ID_STATUS)
        return statusLabel() + L"\n" + sourceLabel() + L"\n" + timestamp(snap.quota.observed) +
               L"\n点击查看详情";
    if (id == ID_ABOUT)
        return L"关于与说明 · 1.0.7";
    wchar_t label[256]{};
    auto it = controls.find(id);
    if (it != controls.end())
        GetWindowTextW(it->second, label, 256);
    std::wstring result = label;
    if (id == ID_OVERVIEW)
        result += L" · Ctrl+1";
    if (id == ID_ACTIVITY)
        result += L" · Ctrl+2";
    if (id == ID_SETTINGS)
        result += L" · Ctrl+3";
    if (id == ID_COPY)
        result += L" · Ctrl+C";
    return result;
}
LRESULT CALLBACK buttonProc(HWND h, UINT message, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR) {
    const int id = GetDlgCtrlID(h);
    if (message == WM_MOUSEMOVE && hoveredControl != id) {
        hoveredControl = id;
        TRACKMOUSEEVENT track{sizeof track, TME_LEAVE, h, 0};
        TrackMouseEvent(&track);
        InvalidateRect(h, nullptr, FALSE);
    }
    if (message == WM_MOUSELEAVE) {
        if (hoveredControl == id)
            hoveredControl = 0;
        InvalidateRect(h, nullptr, FALSE);
    }
    if (message == WM_SETCURSOR && IsWindowEnabled(h)) {
        SetCursor(LoadCursorW(nullptr, IDC_HAND));
        return TRUE;
    }
    if (message == WM_NCDESTROY)
        RemoveWindowSubclass(h, buttonProc, 1);
    return DefSubclassProc(h, message, wp, lp);
}
void updateTooltipFont() {
    if (!tooltip)
        return;
    HFONT next =
        CreateFontW(-px(12), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                    CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, uiFont.c_str());
    if (next) {
        SendMessageW(tooltip, WM_SETFONT, reinterpret_cast<WPARAM>(next), TRUE);
        if (tooltipFont)
            DeleteObject(tooltipFont);
        tooltipFont = next;
    }
    SendMessageW(tooltip, TTM_SETMAXTIPWIDTH, 0, px(340));
}
void initTooltips() {
    INITCOMMONCONTROLSEX init{sizeof init, ICC_WIN95_CLASSES};
    InitCommonControlsEx(&init);
    tooltip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr,
                              WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX, CW_USEDEFAULT, CW_USEDEFAULT,
                              CW_USEDEFAULT, CW_USEDEFAULT, window, nullptr, instance, nullptr);
    if (!tooltip)
        return;
    updateTooltipFont();
    SendMessageW(tooltip, TTM_SETDELAYTIME, TTDT_INITIAL, 450);
    SendMessageW(tooltip, TTM_SETDELAYTIME, TTDT_AUTOPOP, 10000);
    for (auto [id, button] : controls) {
        TOOLINFOW tool{};
        tool.cbSize = sizeof tool;
        tool.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
        tool.hwnd = window;
        tool.uId = reinterpret_cast<UINT_PTR>(button);
        tool.lpszText = LPSTR_TEXTCALLBACKW;
        SendMessageW(tooltip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&tool));
        SetWindowSubclass(button, buttonProc, 1, 0);
    }
}
void layout() {
    if (!window)
        return;
    auto [w, h] = sizeDip();
    if (tab == 1)
        activityPage = std::clamp(activityPage, 0, pageCount() - 1);
    scrollY = std::clamp(scrollY, 0, std::max(0, contentHeight - static_cast<int>(h) + FOOTER));
    HDWP positions = BeginDeferWindowPos(static_cast<int>(controls.size()));
    auto move = [&](int id, float x, float y, float width, float height, bool visible = true) {
        auto ctl = controls.at(id);
        RECT before{};
        GetWindowRect(ctl, &before);
        MapWindowPoints(nullptr, window, reinterpret_cast<POINT *>(&before), 2);
        const int nx = px(x), ny = px(y), nw = px(width), nh = px(height);
        const bool moved = before.left != nx || before.top != ny || before.right - before.left != nw ||
                           before.bottom - before.top != nh;
        const bool shown = (GetWindowLongPtrW(ctl, GWL_STYLE) & WS_VISIBLE) != 0;
        if (!moved && shown == visible)
            return;
        UINT flags = SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOCOPYBITS;
        if (shown != visible)
            flags |= visible ? SWP_SHOWWINDOW : SWP_HIDEWINDOW;
        if (!moved)
            flags |= SWP_NOMOVE | SWP_NOSIZE;
        if (positions)
            positions = DeferWindowPos(positions, ctl, nullptr, nx, ny, nw, nh, flags);
        else
            SetWindowPos(ctl, nullptr, nx, ny, nw, nh, flags);
    };
    const float navWidth = (w - 48) / 3;
    move(ID_OVERVIEW, 24, TITLE_HEIGHT, navWidth, BODY_TOP - TITLE_HEIGHT);
    move(ID_ACTIVITY, 24 + navWidth, TITLE_HEIGHT, navWidth, BODY_TOP - TITLE_HEIGHT);
    move(ID_SETTINGS, 24 + navWidth * 2, TITLE_HEIGHT, navWidth, BODY_TOP - TITLE_HEIGHT);
    move(ID_MINIMIZE, w - 124, 8, 28, 28);
    move(ID_MAXIMIZE, w - 88, 8, 28, 28);
    move(ID_CLOSE, w - 52, 8, 28, 28);
    move(ID_STATUS, 104, h - 38, 32, 32);
    move(ID_REFRESH, 24, h - 38, 32, 32);
    move(ID_COPY, 64, h - 38, 32, 32);
    move(ID_ABOUT, w - 100, h - 38, 32, 32);
    move(ID_MORE, w - 60, h - 38, 32, 32);
    const float tile = (w - 96) / 3;
    auto setting = [&](int id, int col, int row) {
        float y = BODY_TOP + 44 + row * 56 - scrollY;
        move(id, 40 + col * (tile + 8), y, tile, 50, tab == 2 && y >= BODY_TOP && y + 50 <= h - FOOTER);
    };
    setting(ID_THEME, 0, 0);
    setting(ID_INTERVAL, 1, 0);
    setting(ID_ONLINE, 2, 0);
    setting(ID_TRAY, 0, 1);
    setting(ID_NOTIFY, 1, 1);
    setting(ID_AUTOSTART, 2, 1);
    const float folderY = BODY_TOP + 248 - scrollY;
    move(ID_HOME, 40, folderY, 36, 32, tab == 2 && folderY >= BODY_TOP && folderY + 32 <= h - FOOTER);
    move(ID_RESET_HOME, 84, folderY, 36, 32, tab == 2 && folderY >= BODY_TOP && folderY + 32 <= h - FOOTER);
    move(ID_STATS, w - 148, BODY_TOP + 4, 32, 30, tab == 1);
    move(ID_MODELS, w - 108, BODY_TOP + 4, 32, 30, tab == 1);
    move(ID_SESSIONS, w - 68, BODY_TOP + 4, 32, 30, tab == 1);
    bool paging = tab == 1 && activityView != 0 && pageCount() > 1;
    move(ID_PREV, w / 2 - 82, h - FOOTER - 32, 28, 26, paging);
    move(ID_NEXT, w / 2 + 54, h - FOOTER - 32, 28, 26, paging);
    EnableWindow(controls[ID_PREV], activityPage > 0);
    EnableWindow(controls[ID_NEXT], activityPage + 1 < pageCount());
    if (positions)
        EndDeferWindowPos(positions);
    int viewport = static_cast<int>(h) - BODY_TOP - FOOTER;
    SCROLLINFO si{sizeof si,
                  SIF_RANGE | SIF_PAGE | SIF_POS,
                  0,
                  std::max(0, contentHeight - BODY_TOP),
                  static_cast<UINT>(std::max(1, viewport)),
                  scrollY,
                  0};
    SCROLLINFO before{};
    before.cbSize = sizeof before;
    before.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    GetScrollInfo(window, SB_VERT, &before);
    if (before.nMax != si.nMax || before.nPage != si.nPage || before.nPos != si.nPos)
        SetScrollInfo(window, SB_VERT, &si, TRUE);
}
void invalidateBody() {
    auto [w, h] = sizeDip();
    RECT r{0, px(BODY_TOP), px(w), px(h - FOOTER)};
    if (r.bottom > r.top)
        InvalidateRect(window, &r, FALSE);
}
void invalidateStatus() {
    auto [w, h] = sizeDip();
    RECT r{px(176), px(10), px(w - 134), px(32)};
    InvalidateRect(window, &r, FALSE);
    if (tab == 0) {
        RECT hero{px(w - 160), px(std::max(float(BODY_TOP), BODY_TOP + 18.f - scrollY)),
                  px(w - 40), px(std::min(h - FOOTER, BODY_TOP + 38.f - scrollY))};
        if (hero.bottom > hero.top)
            InvalidateRect(window, &hero, FALSE);
    }
    auto it = controls.find(ID_STATUS);
    if (it != controls.end())
        InvalidateRect(it->second, nullptr, FALSE);
}
void updateCountdowns(time_t now) {
    const time_t minute = now / 60;
    if (countdownMinute == minute)
        return;
    countdownMinute = minute;
    if (tab != 0 || !IsWindowVisible(window) || IsIconic(window))
        return;
    auto [w, h] = sizeDip();
    for (size_t i = 0; i < snap.quota.windows.size(); ++i) {
        if (!snap.quota.windows[i].reset)
            continue;
        const auto bounds = quotaBounds(i, w);
        float top = bounds.Y + RESET_ROW - scrollY;
        RECT r{px(bounds.X + 12), px(std::max(static_cast<float>(BODY_TOP), top)), px(bounds.GetRight() - 12),
               px(std::min(h - FOOTER, top + 36))};
        if (r.bottom > r.top)
            InvalidateRect(window, &r, FALSE);
    }
}
void paint() {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(window, &ps);
    RECT cr{};
    GetClientRect(window, &cr);
    if (cr.right <= 0 || cr.bottom <= 0) {
        EndPaint(window, &ps);
        return;
    }
    Bitmap image(cr.right, cr.bottom, PixelFormat32bppPARGB);
    Graphics g(&image);
    g.Clear(colors.bg);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
    g.ScaleTransform(dpiScale, dpiScale);
    auto [w, h] = sizeDip();
    SolidBrush chrome(colors.card);
    g.FillRectangle(&chrome, 0.f, 0.f, w, float(BODY_TOP));
    g.FillRectangle(&chrome, 0.f, h - FOOTER, w, float(FOOTER));
    rounded(g, RectF(24, 10, 24, 24), 8, colors.accent);
    text(g, L">_", 24, 10, 24, 23, 13, Color(255, 255, 255), true, StringAlignmentCenter);
    text(g, L"Codex Pulse", 58, 8, w - 196, 28, 14, colors.text, true);
    const auto state = demo                           ? L""
                       : snap.cached                  ? L"缓存"
                       : snap.quota.source == "local" ? L"本地"
                       : snap.quota.windows.empty()   ? L"未连接"
                                                      : L"";
    if (*state)
        text(g, state, 176, 10, w - 310, 22, 9, demo || snap.cached ? colors.warning : colors.muted);
    auto saved = g.Save();
    g.SetClip(RectF(0, BODY_TOP, w, std::max(1.f, h - BODY_TOP - FOOTER)));
    g.TranslateTransform(0, static_cast<float>(-scrollY));
    float end = tab == 0 ? drawOverview(g, w) : tab == 1 ? drawActivity(g, w) : drawSettings(g, w);
    g.Restore(saved);
    int newHeight = static_cast<int>(end);
    if (newHeight != contentHeight) {
        contentHeight = newHeight;
        PostMessageW(window, WM_APP + 4, 0, 0);
    }
    Graphics screen(dc);
    screen.DrawImage(&image, 0, 0);
    EndPaint(window, &ps);
}
void drawButton(const DRAWITEMSTRUCT *d) {
    const int pixelWidth = d->rcItem.right - d->rcItem.left, pixelHeight = d->rcItem.bottom - d->rcItem.top;
    if (pixelWidth <= 0 || pixelHeight <= 0)
        return;
    Bitmap buffer(pixelWidth, pixelHeight, PixelFormat32bppPARGB);
    Graphics g(&buffer);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
    g.ScaleTransform(dpiScale, dpiScale);
    float w = (d->rcItem.right - d->rcItem.left) / dpiScale,
          h = (d->rcItem.bottom - d->rcItem.top) / dpiScale;
    int id = static_cast<int>(d->CtlID);
    bool active = (id >= ID_OVERVIEW && id <= ID_SETTINGS && id - ID_OVERVIEW == tab) ||
                  (id >= ID_STATS && id <= ID_SESSIONS && id - ID_STATS == activityView) || id == ID_REFRESH;
    bool pressed = (d->itemState & ODS_SELECTED) != 0, disabled = (d->itemState & ODS_DISABLED) != 0;
    bool focusVisible = (d->itemState & ODS_FOCUS) && !(d->itemState & ODS_NOFOCUSRECT);
    bool settingsButton = id >= ID_THEME && id <= ID_RESET_HOME;
    bool navigation = id >= ID_OVERVIEW && id <= ID_SETTINGS;
    // Match the opaque surface behind this native child.
    // Parent painting excludes children, so the button must paint its own corners.
    RECT position{};
    GetWindowRect(d->hwndItem, &position);
    MapWindowPoints(nullptr, window, reinterpret_cast<POINT *>(&position), 2);
    float originY = position.top / dpiScale;
    const bool onChrome = originY < BODY_TOP || originY >= sizeDip().second - FOOTER;
    // Clear every pixel before antialiasing; a filled rectangle leaves translucent edge pixels.
    g.Clear(settingsButton || onChrome ? colors.card : colors.bg);
    if (!navigation && (pressed || active || hoveredControl == id || focusVisible || settingsButton))
        rounded(g, RectF(1, 1, w - 2, h - 3), 9,
                id == ID_CLOSE && (pressed || hoveredControl == id) ? Color(225, 58, 73)
                : pressed                                           ? colors.line
                : active || hoveredControl == id || focusVisible    ? colors.soft
                                                                    : colors.inset);
    Glyph glyph = Glyph::Info;
    switch (id) {
    case ID_MINIMIZE:
        glyph = Glyph::Minimize;
        break;
    case ID_MAXIMIZE:
        glyph = IsZoomed(window) ? Glyph::Restore : Glyph::Maximize;
        break;
    case ID_CLOSE:
        glyph = Glyph::Close;
        break;
    case ID_OVERVIEW:
        glyph = Glyph::Grid;
        break;
    case ID_ACTIVITY:
    case ID_STATS:
        glyph = Glyph::Chart;
        break;
    case ID_SETTINGS:
        glyph = Glyph::Sliders;
        break;
    case ID_REFRESH:
        glyph = Glyph::Refresh;
        break;
    case ID_COPY:
        glyph = copied ? Glyph::Check : Glyph::Copy;
        break;
    case ID_MORE:
        glyph = Glyph::More;
        break;
    case ID_THEME:
        glyph = settings.theme == 0 ? Glyph::Auto : settings.theme == 1 ? Glyph::Sun : Glyph::Moon;
        break;
    case ID_INTERVAL:
        glyph = Glyph::Clock;
        break;
    case ID_ONLINE:
        glyph = Glyph::Cloud;
        break;
    case ID_TRAY:
        glyph = Glyph::Tray;
        break;
    case ID_NOTIFY:
        glyph = Glyph::Bell;
        break;
    case ID_AUTOSTART:
        glyph = Glyph::Power;
        break;
    case ID_HOME:
        glyph = Glyph::Folder;
        break;
    case ID_RESET_HOME:
        glyph = Glyph::Reset;
        break;
    case ID_MODELS:
        glyph = Glyph::Models;
        break;
    case ID_SESSIONS:
        glyph = Glyph::Session;
        break;
    case ID_PREV:
        glyph = Glyph::Left;
        break;
    case ID_NEXT:
        glyph = Glyph::Right;
        break;
    case ID_STATUS:
        glyph = busy                          ? Glyph::Refresh
                : snap.online && !snap.cached ? Glyph::Check
                : snap.cached                 ? Glyph::History
                                              : Glyph::Warning;
        break;
    }
    Color tone = disabled ? colors.line : active ? colors.accent : colors.text;
    if (id == ID_CLOSE && (pressed || hoveredControl == id))
        tone = Color(255, 255, 255);
    if (id == ID_STATUS)
        tone = snap.online && !snap.cached ? colors.accent : colors.warning;
    bool tile = id >= ID_THEME && id <= ID_AUTOSTART && id != ID_HOME;
    float iconSize = tile ? 20.f : id >= ID_MINIMIZE ? 14.f : 19.f;
    if (navigation) {
        const wchar_t *labels[] = {L"概览", L"明细", L"设置"};
        text(g, labels[id - ID_OVERVIEW], 0, 0, w, h - 8, 13,
             active ? colors.text : hoveredControl == id || focusVisible ? colors.accent : colors.muted,
             active, StringAlignmentCenter);
        if (active || focusVisible)
            rounded(g, RectF(w / 2 - 11, h - 6, 22, 3), 1.5f, active ? colors.accent : colors.muted);
    } else {
        drawGlyph(g, glyph, (w - iconSize) / 2, tile ? 6.f : (h - iconSize) / 2, iconSize, tone);
    }
    if (id == ID_THEME || id == ID_INTERVAL)
        text(g, id == ID_THEME ? themeLabel() : std::to_wstring(settings.interval / 60) + L" min", 4, h - 22,
             w - 8, 20, 10, colors.muted, false, StringAlignmentCenter);
    else if (tile) {
        bool enabled = id == ID_ONLINE   ? settings.online
                       : id == ID_TRAY   ? settings.tray
                       : id == ID_NOTIFY ? settings.notify
                                         : autostart();
        rounded(g, RectF(w / 2 - 10, h - 20, 20, 10), 5, enabled ? colors.accent : colors.line);
        SolidBrush knob(Color(255, 255, 255));
        g.FillEllipse(&knob, w / 2 + (enabled ? 2.f : -8.f), h - 18, 6.f, 6.f);
    }
    Graphics screen(d->hDC);
    screen.DrawImage(&buffer, static_cast<int>(d->rcItem.left), static_cast<int>(d->rcItem.top));
}
void showWindow() {
    ShowWindow(window, SW_RESTORE);
    SetForegroundWindow(window);
    InvalidateRect(window, nullptr, FALSE);
}
void updateWindowIcons() {
    const UINT dpi = static_cast<UINT>(std::lround(dpiScale * 96));
    auto load = [&](int metric) {
        int pixels = GetSystemMetricsForDpi(metric, dpi);
        return reinterpret_cast<HICON>(
            LoadImageW(instance, MAKEINTRESOURCEW(1), IMAGE_ICON, pixels, pixels, LR_DEFAULTCOLOR));
    };
    HICON small = load(SM_CXSMICON), large = load(SM_CXICON);
    if (small) {
        SendMessageW(window, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(small));
        if (windowIconSmall)
            DestroyIcon(windowIconSmall);
        windowIconSmall = small;
    }
    if (large) {
        SendMessageW(window, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(large));
        if (windowIconLarge)
            DestroyIcon(windowIconLarge);
        windowIconLarge = large;
    }
}
void updateTray() {
    if (closing)
        return;
    HWND taskbar = FindWindowW(L"Shell_TrayWnd", nullptr);
    const UINT dpi = taskbar ? GetDpiForWindow(taskbar) : GetDpiForSystem();
    const int iconSize = std::clamp(GetSystemMetricsForDpi(SM_CXSMICON, dpi ? dpi : 96), 16, 64);
    double used = -1;
    const Window *limitingWindow = nullptr;
    for (const auto &q : snap.quota.windows)
        if (q.valid() && q.used > used) {
            used = q.used;
            limitingWindow = &q;
        }
    const double usage = used;
    const bool iconChanged = !trayIcon || iconSize != trayRenderSize || usage != trayRenderUsage;
    HICON next = iconChanged ? createTrayIcon(iconSize, usage) : trayIcon;
    if (!next)
        return;
    NOTIFYICONDATAW n{};
    n.cbSize = sizeof n;
    n.hWnd = window;
    n.uID = 1;
    n.uCallbackMessage = MSG_TRAY;
    n.hIcon = next;
    std::wstring tip =
        used < 0 ? L"Codex Pulse · 暂无额度数据" : L"Codex Pulse · 最少剩余 " + percent(100 - used, 1);
    if (limitingWindow)
        tip += L"\n对应额度：" + windowName(*limitingWindow);
    for (size_t i = 0; i < std::min<size_t>(2, snap.quota.windows.size()); ++i)
        tip += L"\n" + windowName(snap.quota.windows[i]) + L"：已用 " + percent(snap.quota.windows[i].used, 1);
    tip += L"\n" + sourceLabel();
    tip.resize(std::min<size_t>(tip.size(), 127));
    wcsncpy_s(n.szTip, tip.c_str(), _TRUNCATE);
    const bool adding = !trayAdded;
    n.uFlags = adding ? (NIF_MESSAGE | NIF_ICON | NIF_TIP)
                      : ((iconChanged ? NIF_ICON : 0) | (tip != trayTooltip ? NIF_TIP : 0));
    if (n.uFlags & NIF_TIP)
        n.uFlags |= NIF_SHOWTIP;
    bool ok = trayAdded;
    bool added = adding;
    if (n.uFlags)
        ok = Shell_NotifyIconW(adding ? NIM_ADD : NIM_MODIFY, &n) != FALSE;
    if (!ok && !adding) {
        n.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
        ok = Shell_NotifyIconW(NIM_ADD, &n) != FALSE;
        added = ok;
    }
    trayAdded = ok;
    if (ok) {
        if (added) {
            n.uVersion = NOTIFYICON_VERSION_4;
            Shell_NotifyIconW(NIM_SETVERSION, &n);
        }
        if (iconChanged) {
            if (trayIcon)
                DestroyIcon(trayIcon);
            trayIcon = next;
            trayRenderSize = iconSize;
            trayRenderUsage = usage;
        }
        trayTooltip = std::move(tip);
    } else if (iconChanged)
        DestroyIcon(next);
}
void balloon(const wchar_t *title, const std::wstring &body) {
    if (!trayAdded)
        return;
    NOTIFYICONDATAW n{};
    n.cbSize = sizeof n;
    n.hWnd = window;
    n.uID = 1;
    n.uFlags = NIF_INFO;
    wcsncpy_s(n.szInfoTitle, title, _TRUNCATE);
    wcsncpy_s(n.szInfo, body.c_str(), _TRUNCATE);
    n.dwInfoFlags = NIIF_INFO;
    Shell_NotifyIconW(NIM_MODIFY, &n);
}
void hideWindow() {
    if (!trayAdded) {
        ShowWindow(window, SW_MINIMIZE);
        return;
    }
    ShowWindow(window, SW_HIDE);
}
void save() {
    if (demo)
        return;
    if (!saveSettings(settings))
        MessageBoxW(window, L"无法保存设置。请检查本地应用数据目录的写入权限。", L"Codex Pulse",
                    MB_ICONWARNING);
}
Snapshot demoSnapshot() {
    Snapshot s;
    s.status = "演示模式：所有数字均为示例";
    s.account = "de***@example.com";
    s.authSource = "演示模式（不读取真实账户）";
    s.refreshed = std::time(nullptr);
    s.home = defaultHome();
    s.online = true;
    s.quota = parseQuota(parse(
        R"({"plan_type":"plus","rate_limit":{"primary_window":{"used_percent":34,"limit_window_seconds":18000},"secondary_window":{"used_percent":62,"limit_window_seconds":604800}},"credits":{"has_credits":true,"balance":"24.50"}})"));
    s.quota.windows[0].reset = s.refreshed + 7320;
    s.quota.windows[1].reset = s.refreshed + 246600;
    s.activity.today = {186400, 142800, 43800, 17600, 230200};
    s.activity.week = {58000000, 35000000, 7120000, 2000000, 65120000};
    s.activity.month = {182970000, 130000000, 20330000, 8000000, 203300000};
    s.activity.sessions = 24;
    s.activity.files = 31;
    std::array<int64_t, 7> values{8450000, 15780000, 6900000, 18150000, 7450000, 8159800, 230200};
    for (int i = 0; i < 7; i++) {
        auto tm = localTime(s.refreshed);
        tm.tm_mday -= 6 - i;
        tm.tm_isdst = -1;
        Tokens t;
        t.total = values[i];
        s.activity.daily[dateKey(std::mktime(&tm))] = t;
    }
    s.activity.models["gpt-5.3-codex"].total = 150000000;
    s.activity.models["gpt-5.2-codex"].total = 53300000;
    for (int i = 0; i < 12; i++) {
        Session x;
        x.id = "019a91f2-13b8-4c10-92f2-abcdef123456";
        x.model = i % 2 ? "gpt-5.2-codex" : "gpt-5.3-codex";
        x.last = s.refreshed - i * 2400;
        x.total.total = 230200 + (i % 6) * 37900;
        s.activity.recent.push_back(x);
    }
    return s;
}
void refresh() {
    if (busy || closing)
        return;
    if (demo) {
        snap = demoSnapshot();
        nextRefresh = std::time(nullptr) + settings.interval;
        updateTray();
        invalidateStatus();
        InvalidateRect(window, nullptr, FALSE);
        return;
    }
    if (worker.joinable())
        worker.join();
    busy = true;
    updateControlText();
    invalidateStatus();
    auto config = settings;
    auto prior = snap;
    unsigned gen = generation;
    HWND target = window;
    worker = std::thread([config, prior, gen, target] {
        Snapshot out;
        out.home = config.home.empty() ? defaultHome() : fs::path(config.home);
        out.refreshed = std::time(nullptr);
        try {
            static Scanner scanner;
            static fs::path scannedHome;
            if (out.home != scannedHome) {
                scanner.clear();
                scannedHome = out.home;
            }
            out.activity = scanner.scan(out.home);
            Auth auth = readAuth(out.home);
            out.identity = auth.identity;
            out.account = auth.label;
            out.authSource = auth.kind == "chatgpt"   ? auth.location
                             : auth.kind == "api-key" ? "API Key 模式"
                                                      : "未发现支持的凭据";
            if (config.online && auth.kind == "chatgpt" && !stopping.load()) {
                HttpResult result;
                if (prior.activity.partial && prior.online && !prior.cached &&
                    prior.identity == auth.identity &&
                    std::time(nullptr) - prior.quota.observed < config.interval) {
                    result.ok = true;
                    result.quota = prior.quota;
                } else
                    result = fetchUsage(auth);
                out.retry = result.retryAfter;
                if (result.ok) {
                    out.quota = std::move(result.quota);
                    out.online = true;
                    out.status = "额度已同步 · " + utf8(timestamp(out.refreshed, false));
                } else {
                    out.status = result.error;
                    if (prior.identity == auth.identity && !auth.identity.empty() &&
                        prior.quota.source == "online") {
                        out.quota = prior.quota;
                        out.cached = true;
                        out.online = true;
                    }
                }
            } else if (!config.online)
                out.status = "本地模式 · 不发送网络请求";
            else if (auth.kind == "api-key")
                out.status = "API Key 模式 · 仅显示本机 Token，订阅额度不适用";
            else if (auth.kind == "invalid")
                out.status = "auth.json 无法读取或格式有误，请在 Codex 中重新登录";
            else if (auth.kind == "unsupported")
                out.status = "此登录存储暂不支持直接读取 · 可使用本地统计";
            else
                out.status = "未发现登录信息 · 请在 Codex 登录，或到设置选择目录";
            if (out.quota.windows.empty() && out.activity.localQuota.usable()) {
                out.quota = out.activity.localQuota;
                out.cached = true;
            }
            if (out.activity.partial)
                out.status += " · 日志分批扫描中";
            if (out.activity.inaccessible)
                out.status += " · 部分目录或文件不可读";
            if (!auth.token.empty())
                SecureZeroMemory(auth.token.data(), auth.token.size());
        } catch (...) {
            out.status = "本轮读取未完成，请检查目录权限后重试";
            out.cached = true;
        }
        if (stopping.load())
            return;
        {
            std::lock_guard<std::mutex> lock(resultMutex);
            pending = std::move(out);
            pendingGeneration = gen;
        }
        PostMessageW(target, MSG_READY, 0, 0);
    });
}
std::wstring report() {
    std::wostringstream out;
    out << L"Codex Pulse 1.0.7\r\n"
        << (demo ? L"演示数据\r\n" : L"") << sourceLabel() << L"\r\n"
        << L"状态：" << wide(snap.status) << L"\r\n"
        << L"额度采样：" << timestamp(snap.quota.observed) << L"\r\n";
    for (const auto &q : snap.quota.windows)
        out << windowName(q) << L"：已用 " << percent(q.used) << L"，剩余 "
            << percent(q.valid() ? 100 - q.used : -1) << L"，重置 " << timestamp(q.reset) << L"\r\n";
    out << L"\r\n本机 Token（当前目录，按本地日历日）\r\n今日总计：" << count(snap.activity.today.total)
        << L"\r\n输入：" << count(snap.activity.today.input) << L"，其中缓存："
        << count(snap.activity.today.cached) << L"\r\n输出：" << count(snap.activity.today.output)
        << L"，其中推理：" << count(snap.activity.today.reasoning) << L"\r\n近 7 天："
        << count(snap.activity.week.total) << L"\r\n近 30 天：" << count(snap.activity.month.total)
        << L"\r\n会话数：" << snap.activity.sessions << L"\r\n文件数：" << snap.activity.files
        << L"\r\n异常记录：" << snap.activity.malformed << L"\r\n扫描完整性："
        << (snap.activity.partial ? L"分批扫描中" : L"本轮完成") << L"\r\n\r\n模型分布（近 30 天）：\r\n";
    for (auto &[m, t] : snap.activity.models)
        out << wide(m) << L"：" << count(t.total) << L"\r\n";
    out << L"\r\n缓存输入及推理输出已包含在父项内，不重复相加。\r\n本机日志不代表全账户用量，Token "
           L"不等于费用。\r\n";
    return out.str();
}
void copyReport() {
    auto value = report();
    if (!OpenClipboard(window)) {
        MessageBoxW(window, L"剪贴板正被占用，请稍后重试。", L"Codex Pulse", MB_ICONINFORMATION);
        return;
    }
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, (value.size() + 1) * sizeof(wchar_t));
    if (mem) {
        void *ptr = GlobalLock(mem);
        if (ptr) {
            memcpy(ptr, value.c_str(), (value.size() + 1) * sizeof(wchar_t));
            GlobalUnlock(mem);
            EmptyClipboard();
            if (!SetClipboardData(CF_UNICODETEXT, mem))
                GlobalFree(mem);
        } else
            GlobalFree(mem);
    }
    CloseClipboard();
    copied = true;
    setText(ID_COPY, L"已复制");
    InvalidateRect(controls[ID_COPY], nullptr, FALSE);
    SetTimer(window, 2, 1800, nullptr);
}
void chooseHome() {
    IFileOpenDialog *dialog = nullptr;
    if (SUCCEEDED(
            CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) {
        DWORD options;
        dialog->GetOptions(&options);
        dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
        dialog->SetTitle(L"选择 Codex 数据目录（包含 auth.json 或 sessions 的目录）");
        if (SUCCEEDED(dialog->Show(window))) {
            IShellItem *item = nullptr;
            if (SUCCEEDED(dialog->GetResult(&item))) {
                PWSTR path = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                    settings.home = path;
                    CoTaskMemFree(path);
                    ++generation;
                    snap = Snapshot{};
                    save();
                    refresh();
                }
                item->Release();
            }
        }
        dialog->Release();
    }
}
void toggleAutostart() {
    const bool enabled = !autostart();
    const auto result = setAutostartEnabled(enabled);
    if (result == ERROR_SUCCESS) {
        settings.autostart = enabled;
        settings.autostartInitialized = true;
    } else
        MessageBoxW(window, L"无法保存开机启动设置。", L"Codex Pulse", MB_ICONWARNING);
}
void initializeAutostart() {
    if (demo || settings.autostartInitialized)
        return;
    const auto result = applyDefaultAutostart(settings);
    save();
    updateControlText();
    if (result != ERROR_SUCCESS)
        MessageBoxW(window, L"无法启用开机启动。可在设置中重新开启。", L"Codex Pulse", MB_ICONWARNING);
}

void menu(bool tray) {
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, 1, L"打开用量窗口");
    AppendMenuW(m, MF_STRING | (busy ? MF_GRAYED : 0), 2, L"立即刷新");
    AppendMenuW(m, MF_STRING, 3, L"复制用量报告");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, 4, L"打开 Codex 用量网页");
    AppendMenuW(m, MF_STRING, 5, L"设置");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, 6, L"退出 Codex Pulse");
    POINT p;
    GetCursorPos(&p);
    SetForegroundWindow(window);
    int result = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, p.x, p.y, 0, window, nullptr);
    DestroyMenu(m);
    PostMessageW(window, WM_NULL, 0, 0);
    switch (result) {
    case 1:
        showWindow();
        break;
    case 2:
        refresh();
        break;
    case 3:
        copyReport();
        break;
    case 4:
        ShellExecuteW(window, L"open", L"https://chatgpt.com/codex/settings/usage", nullptr, nullptr,
                      SW_SHOWNORMAL);
        break;
    case 5:
        tab = 2;
        scrollY = 0;
        showWindow();
        layout();
        break;
    case 6:
        DestroyWindow(window);
        break;
    }
    (void)tray;
}
void command(int id) {
    if (id == ID_MINIMIZE || id == ID_MAXIMIZE || id == ID_CLOSE) {
        if (id == ID_CLOSE)
            SendMessageW(window, WM_CLOSE, 0, 0);
        else
            SendMessageW(window, WM_SYSCOMMAND,
                         id == ID_MINIMIZE  ? SC_MINIMIZE
                         : IsZoomed(window) ? SC_RESTORE
                                            : SC_MAXIMIZE,
                         0);
        return;
    }
    if (id >= ID_OVERVIEW && id <= ID_SETTINGS) {
        tab = id - ID_OVERVIEW;
        scrollY = 0;
        contentHeight = 0;
        layout();
        updateControlText();
        for (int n = ID_OVERVIEW; n <= ID_SETTINGS; n++)
            InvalidateRect(controls[n], nullptr, FALSE);
        InvalidateRect(window, nullptr, FALSE);
        return;
    }
    if (id >= ID_STATS && id <= ID_SESSIONS) {
        activityView = id - ID_STATS;
        activityPage = 0;
        scrollY = 0;
        contentHeight = 0;
        layout();
        for (int n = ID_STATS; n <= ID_SESSIONS; ++n)
            InvalidateRect(controls[n], nullptr, FALSE);
        invalidateBody();
        return;
    }
    if (id == ID_PREV || id == ID_NEXT) {
        activityPage = std::clamp(activityPage + (id == ID_NEXT ? 1 : -1), 0, pageCount() - 1);
        layout();
        invalidateBody();
        return;
    }
    if (id == ID_STATUS) {
        auto message = statusLabel() + L"\n\n" + sourceLabel() + L"\n采样：" +
                       timestamp(snap.quota.observed) + L"\n本地日志：" +
                       std::to_wstring(snap.activity.files) + L"\n异常记录：" +
                       std::to_wstring(snap.activity.malformed) + L"\n凭据：" + wide(snap.authSource);
        MessageBoxW(window, message.c_str(), L"连接详情", MB_OK | MB_ICONINFORMATION);
        return;
    }
    if (id == ID_ABOUT) {
        MessageBoxW(window,
                    L"Codex Pulse 1.0.7\n\n只读本机登录与用量，仅向 chatgpt.com "
                    L"查询额度。\n无遥测，不上传会话内容。\n\n账户额度与本机 Token "
                    L"统计口径不同。\n缓存输入已包含在输入中，推理输出已包含在输出中。\n独立工具，与 OpenAI "
                    L"无隶属关系。",
                    L"关于", MB_OK | MB_ICONINFORMATION);
        return;
    }
    switch (id) {
    case ID_REFRESH:
        refresh();
        return;
    case ID_COPY:
        copyReport();
        return;
    case ID_MORE:
        menu(false);
        return;
    case ID_THEME:
        settings.theme = (settings.theme + 1) % 3;
        updateColors();
        break;
    case ID_INTERVAL: {
        const int choices[] = {60, 120, 300, 900};
        for (int i = 0; i < 4; i++)
            if (settings.interval == choices[i]) {
                settings.interval = choices[(i + 1) % 4];
                break;
            }
        nextRefresh = std::time(nullptr) + settings.interval;
        break;
    }
    case ID_ONLINE:
        settings.online = !settings.online;
        ++generation;
        snap = Snapshot{};
        refresh();
        break;
    case ID_TRAY:
        settings.tray = !settings.tray;
        break;
    case ID_NOTIFY:
        settings.notify = !settings.notify;
        break;
    case ID_HOME:
        chooseHome();
        break;
    case ID_RESET_HOME:
        settings.home.clear();
        ++generation;
        snap = Snapshot{};
        refresh();
        break;
    case ID_AUTOSTART:
        if (demo)
            settings.autostart = !settings.autostart;
        else
            toggleAutostart();
        break;
    }
    save();
    updateControlText();
    layout();
    InvalidateRect(window, nullptr, FALSE);
}
LRESULT CALLBACK proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == taskbarCreated && taskbarCreated) {
        trayAdded = false;
        updateTray();
        return 0;
    }
    switch (msg) {
    case WM_NCCALCSIZE:
        if (wp) {
            auto params = reinterpret_cast<NCCALCSIZE_PARAMS *>(lp);
            if (IsZoomed(h)) {
                MONITORINFO monitor{};
                monitor.cbSize = sizeof monitor;
                if (GetMonitorInfoW(MonitorFromRect(&params->rgrc[0], MONITOR_DEFAULTTONEAREST), &monitor))
                    params->rgrc[0] = monitor.rcWork;
            }
            // Preserve a genuine scrollbar only for overflowing content.
            if (GetWindowLongPtrW(h, GWL_STYLE) & WS_VSCROLL)
                params->rgrc[0].right -= GetSystemMetricsForDpi(SM_CXVSCROLL, GetDpiForWindow(h));
            return 0;
        }
        break;
    case WM_NCLBUTTONDBLCLK:
        if (wp == HTCAPTION) {
            command(ID_MAXIMIZE);
            return 0;
        }
        break;
    case WM_NCHITTEST: {
        POINT screen{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        RECT bounds{};
        GetWindowRect(h, &bounds);
        const int x = screen.x - bounds.left, y = screen.y - bounds.top;
        const int width = bounds.right - bounds.left, height = bounds.bottom - bounds.top;
        if (!IsZoomed(h)) {
            const UINT dpi = GetDpiForWindow(h);
            const int edge = std::max(px(5), GetSystemMetricsForDpi(SM_CXSIZEFRAME, dpi) +
                                                 GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi));
            bool left = x < edge, right = x >= width - edge;
            bool top = y < edge, bottom = y >= height - edge;
            if (top && left)
                return HTTOPLEFT;
            if (top && right)
                return HTTOPRIGHT;
            if (bottom && left)
                return HTBOTTOMLEFT;
            if (bottom && right)
                return HTBOTTOMRIGHT;
            if (left)
                return HTLEFT;
            if (right)
                return HTRIGHT;
            if (top)
                return HTTOP;
            if (bottom)
                return HTBOTTOM;
        }
        for (auto [id, control] : controls) {
            (void)id;
            RECT area{};
            GetWindowRect(control, &area);
            if (IsWindowVisible(control) && PtInRect(&area, screen))
                return HTCLIENT;
        }
        POINT client = screen;
        ScreenToClient(h, &client);
        RECT area{};
        GetClientRect(h, &area);
        if ((GetWindowLongPtrW(h, GWL_STYLE) & WS_VSCROLL) && client.x >= area.right)
            return HTVSCROLL;
        return client.y < px(TITLE_HEIGHT) ? HTCAPTION : HTCLIENT;
    }
    case WM_CREATE: {
        window = h;
        taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
        const std::pair<int, const wchar_t *> defs[] = {{ID_OVERVIEW, L"概览"},
                                                        {ID_ACTIVITY, L"明细"},
                                                        {ID_SETTINGS, L"设置"},
                                                        {ID_REFRESH, L"刷新   F5"},
                                                        {ID_COPY, L"复制报告"},
                                                        {ID_MORE, L"更多操作"},
                                                        {ID_THEME, L""},
                                                        {ID_INTERVAL, L""},
                                                        {ID_ONLINE, L""},
                                                        {ID_TRAY, L""},
                                                        {ID_NOTIFY, L""},
                                                        {ID_HOME, L"选择目录…"},
                                                        {ID_RESET_HOME, L"恢复自动检测"},
                                                        {ID_AUTOSTART, L""},
                                                        {ID_STATUS, L"连接状态"},
                                                        {ID_ABOUT, L"关于"},
                                                        {ID_STATS, L"趋势与今日用量"},
                                                        {ID_MODELS, L"模型用量"},
                                                        {ID_SESSIONS, L"最近会话"},
                                                        {ID_PREV, L"上一页"},
                                                        {ID_NEXT, L"下一页"},
                                                        {ID_MINIMIZE, L"最小化"},
                                                        {ID_MAXIMIZE, L"最大化"},
                                                        {ID_CLOSE, L"收起到托盘"}};
        for (auto [id, name] : defs)
            controls[id] = CreateWindowExW(
                0, L"BUTTON", name, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 1, 1, h,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance, nullptr);
        initTooltips();
        updateColors();
        updateControlText();
        SetTimer(h, 1, 1000, nullptr);
        return 0;
    }
    case WM_NOTIFY: {
        auto hdr = reinterpret_cast<NMHDR *>(lp);
        if (hdr && hdr->hwndFrom == tooltip && hdr->code == TTN_GETDISPINFOW) {
            auto info = reinterpret_cast<NMTTDISPINFOW *>(lp);
            tooltipBuffer = controlTip(GetDlgCtrlID(reinterpret_cast<HWND>(hdr->idFrom)));
            info->lpszText = tooltipBuffer.data();
            return 0;
        }
        break;
    }
    case WM_PAINT:
        paint();
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_DRAWITEM:
        drawButton(reinterpret_cast<DRAWITEMSTRUCT *>(lp));
        return TRUE;
    case WM_COMMAND:
        command(LOWORD(wp));
        return 0;
    case WM_SIZE:
        setText(ID_MAXIMIZE, IsZoomed(h) ? L"还原" : L"最大化");
        if (controls.count(ID_MAXIMIZE))
            InvalidateRect(controls[ID_MAXIMIZE], nullptr, FALSE);
        if (wp == SIZE_MINIMIZED && settings.tray)
            hideWindow();
        else {
            layout();
            InvalidateRect(h, nullptr, FALSE);
        }
        return 0;
    case WM_GETMINMAXINFO: {
        auto mm = reinterpret_cast<MINMAXINFO *>(lp);
        mm->ptMinTrackSize = {px(440), px(460)};
        MONITORINFO monitor{};
        monitor.cbSize = sizeof monitor;
        if (GetMonitorInfoW(MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST), &monitor)) {
            mm->ptMaxPosition = {monitor.rcWork.left - monitor.rcMonitor.left,
                                 monitor.rcWork.top - monitor.rcMonitor.top};
            mm->ptMaxSize = {monitor.rcWork.right - monitor.rcWork.left,
                             monitor.rcWork.bottom - monitor.rcWork.top};
        }
        return 0;
    }
    case WM_DPICHANGED: {
        dpiScale = HIWORD(wp) / 96.f;
        auto r = reinterpret_cast<RECT *>(lp);
        SetWindowPos(h, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        updateWindowIcons();
        updateTray();
        updateTooltipFont();
        layout();
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    }
    case WM_DISPLAYCHANGE:
        updateTray();
        return 0;
    case WM_SETTINGCHANGE:
        updateColors();
        updateWindowIcons();
        updateTray();
        return 0;
    case WM_MOUSEWHEEL: {
        auto [w, height] = sizeDip();
        (void)w;
        int maxScroll = std::max(0, contentHeight - static_cast<int>(height) + FOOTER);
        scrollY = std::clamp(scrollY - GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA * 54, 0, maxScroll);
        layout();
        invalidateBody();
        return 0;
    }
    case WM_VSCROLL: {
        SCROLLINFO si{};
        si.cbSize = sizeof si;
        si.fMask = SIF_ALL;
        GetScrollInfo(h, SB_VERT, &si);
        int pos = scrollY;
        switch (LOWORD(wp)) {
        case SB_LINEUP:
            pos -= 36;
            break;
        case SB_LINEDOWN:
            pos += 36;
            break;
        case SB_PAGEUP:
            pos -= static_cast<int>(si.nPage);
            break;
        case SB_PAGEDOWN:
            pos += static_cast<int>(si.nPage);
            break;
        case SB_THUMBTRACK:
            pos = si.nTrackPos;
            break;
        case SB_TOP:
            pos = 0;
            break;
        case SB_BOTTOM:
            pos = si.nMax;
            break;
        }
        scrollY = std::clamp(pos, 0, std::max(0, si.nMax - static_cast<int>(si.nPage) + 1));
        layout();
        invalidateBody();
        return 0;
    }
    case WM_KEYDOWN:
        if (wp == VK_F5)
            refresh();
        if (wp == VK_ESCAPE)
            hideWindow();
        return 0;
    case MSG_TRAY: {
        UINT event = LOWORD(lp);
        if (event == WM_LBUTTONDBLCLK || event == NIN_SELECT || event == NIN_KEYSELECT)
            showWindow();
        else if (event == WM_CONTEXTMENU || event == WM_RBUTTONUP)
            menu(true);
        return 0;
    }
    case MSG_READY: {
        busy = false;
        if (worker.joinable())
            worker.join();
        {
            std::lock_guard<std::mutex> lock(resultMutex);
            if (pendingGeneration == generation)
                snap = std::move(pending);
            else {
                refresh();
                return 0;
            }
        }
        failures = (!snap.online || snap.cached) && settings.online ? std::min(failures + 1, 4) : 0;
        int wait = std::max(settings.interval, snap.retry);
        if (failures)
            wait = std::max(wait, std::min(900, settings.interval * (1 << failures)));
        if (snap.activity.partial)
            wait = std::min(wait, 10);
        nextRefresh = std::time(nullptr) + wait;
        updateControlText();
        updateTray();
        invalidateStatus();
        layout();
        InvalidateRect(h, nullptr, FALSE);
        if (settings.notify && !snap.cached && snap.online) {
            for (auto &q : snap.quota.windows)
                if (q.valid() && q.used >= 90) {
                    auto key = snap.identity + q.name + std::to_string(q.reset);
                    if (key != notifiedWindow) {
                        notifiedWindow = key;
                        balloon(L"Codex 额度接近上限", windowName(q) + L"已使用 " + percent(q.used) +
                                                           L"，重置于 " + timestamp(q.reset));
                    }
                    break;
                }
        }
        return 0;
    }
    case WM_APP + 4:
        layout();
        return 0;
    case WM_TIMER:
        if (wp == 2) {
            copied = false;
            setText(ID_COPY, L"复制报告");
            InvalidateRect(controls[ID_COPY], nullptr, FALSE);
            KillTimer(h, 2);
        } else {
            if (!busy && std::time(nullptr) >= nextRefresh)
                refresh();
            updateCountdowns(std::time(nullptr));
        }
        return 0;
    case WM_POWERBROADCAST:
        if (wp == PBT_APMRESUMEAUTOMATIC)
            nextRefresh = std::time(nullptr) + 2;
        return TRUE;
    case WM_CLOSE:
        hideWindow();
        return 0;
    case WM_ENDSESSION:
        if (wp)
            DestroyWindow(h);
        return 0;
    case WM_QUERYENDSESSION:
        return TRUE;
    case WM_DESTROY: {
        closing = true;
        stopping = true;
        KillTimer(h, 1);
        KillTimer(h, 2);
        NOTIFYICONDATAW n{};
        n.cbSize = sizeof n;
        n.hWnd = h;
        n.uID = 1;
        Shell_NotifyIconW(NIM_DELETE, &n);
        if (tooltip) {
            DestroyWindow(tooltip);
            tooltip = nullptr;
        }
        if (tooltipFont) {
            DeleteObject(tooltipFont);
            tooltipFont = nullptr;
        }
        PostQuitMessage(0);
        return 0;
    }
    }
    return DefWindowProcW(h, msg, wp, lp);
}
} // namespace
int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int) {
    instance = inst;
    int argc = 0;
    LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    bool startTray = false;
    for (int i = 1; i < argc; i++) {
        if (std::wstring(argv[i]) == L"--demo")
            demo = true;
        if (std::wstring(argv[i]) == L"--tray")
            startTray = true;
    }
    LocalFree(argv);
    auto mutexName = demo ? L"Local\\CodexPulse.Demo.1" : L"Local\\CodexPulse.1";
    HANDLE mutex = CreateMutexW(nullptr, FALSE, mutexName);
    if (mutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND existing = FindWindowW(CLASS_NAME, demo ? L"Codex Pulse · 演示" : L"Codex Pulse");
        if (existing) {
            ShowWindow(existing, SW_RESTORE);
            SetForegroundWindow(existing);
        }
        CloseHandle(mutex);
        return 0;
    }
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    dpiScale = GetDpiForSystem() / 96.f;
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    GdiplusStartupInput startup;
    if (GdiplusStartup(&gdip, &startup, nullptr) != Ok)
        return 2;
    chooseFont();
    settings = demo ? Settings{} : loadSettings();
    WNDCLASSEXW cls{};
    cls.cbSize = sizeof cls;
    cls.hInstance = inst;
    cls.lpfnWndProc = proc;
    cls.lpszClassName = CLASS_NAME;
    cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    cls.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    cls.hIconSm = cls.hIcon;
    cls.style = CS_DBLCLKS;
    if (!RegisterClassExW(&cls))
        return 3;
    RECT desktop{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &desktop, 0);
    int width = px(DEFAULT_WIDTH),
        height = std::min(px(DEFAULT_HEIGHT), static_cast<int>(desktop.bottom - desktop.top - 32));
    window = CreateWindowExW(WS_EX_APPWINDOW, CLASS_NAME, demo ? L"Codex Pulse · 演示" : L"Codex Pulse",
                             WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, desktop.right - width - 24,
                             desktop.bottom - height - 16, width, height, nullptr, nullptr, inst, nullptr);
    if (!window)
        return 4;
    // Keep Windows' default non-client composition. Attribute 2 takes a rendering
    // policy enum, not BOOL: TRUE would disable DWM rendering of the title bar.
    int corner = 2;
    DwmSetWindowAttribute(window, 33, &corner, sizeof corner);
    updateWindowIcons();
    updateTray();
    if (!startTray || !trayAdded) {
        ShowWindow(window, SW_SHOW);
        UpdateWindow(window);
    }
    layout();
    initializeAutostart();
    refresh();
    MSG message;
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (message.message == WM_KEYDOWN) {
            if (message.wParam == VK_NEXT || message.wParam == VK_PRIOR) {
                SendMessageW(window, WM_VSCROLL, message.wParam == VK_NEXT ? SB_PAGEDOWN : SB_PAGEUP, 0);
                continue;
            }
            if (message.wParam == VK_F5) {
                refresh();
                continue;
            }
            if (message.wParam == VK_ESCAPE) {
                hideWindow();
                continue;
            }
            if (GetKeyState(VK_CONTROL) & 0x8000) {
                if (message.wParam == 'C') {
                    copyReport();
                    continue;
                }
                if (message.wParam == '1' || message.wParam == '2' || message.wParam == '3') {
                    command(ID_OVERVIEW + static_cast<int>(message.wParam - '1'));
                    continue;
                }
                if (message.wParam == 'Q') {
                    DestroyWindow(window);
                    continue;
                }
            }
        }
        if (!IsDialogMessageW(window, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    stopping = true;
    if (worker.joinable())
        worker.join();
    if (trayIcon)
        DestroyIcon(trayIcon);
    if (windowIconSmall)
        DestroyIcon(windowIconSmall);
    if (windowIconLarge)
        DestroyIcon(windowIconLarge);
    GdiplusShutdown(gdip);
    CoUninitialize();
    if (mutex)
        CloseHandle(mutex);
    return 0;
}
