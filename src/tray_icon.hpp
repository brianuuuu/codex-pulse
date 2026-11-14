#pragma once
#include "platform.hpp"

namespace pulse {
// Native-size rounded progress track. Fill represents the least remaining
// quota. No terminal or digits; three dots distinguish unknown from empty.
inline void drawTrayIcon(Gdiplus::Graphics &g, int size, int usage) {
    using namespace Gdiplus;
    g.Clear(Color(0, 0, 0, 0));
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetPixelOffsetMode(PixelOffsetModeHalf);
    const float scale = size / 16.f, stroke = std::max(1.f, std::round(scale));
    const float margin = std::round(size * .25f);
    auto pill = [](GraphicsPath &path, RectF r) {
        const float d = std::min(r.Width, r.Height);
        path.AddArc(r.X, r.Y, d, d, 180, 90);
        path.AddArc(r.GetRight() - d, r.Y, d, d, 270, 90);
        path.AddArc(r.GetRight() - d, r.GetBottom() - d, d, d, 0, 90);
        path.AddArc(r.X, r.GetBottom() - d, d, d, 90, 90);
        path.CloseFigure();
    };
    GraphicsPath track;
    pill(track, RectF(stroke / 2, margin + stroke / 2, size - stroke, size - 2 * margin - stroke));
    const Color tone = usage > 90   ? Color(255, 69, 58)
                       : usage > 80 ? Color(255, 159, 10)
                                    : Color(255, 255, 255);
    Pen outline(tone, stroke);
    g.DrawPath(&outline, &track);
    SolidBrush ink(tone);
    const float inset = stroke + std::max(1.f, scale * .75f);
    const RectF inner(inset, margin + inset, size - 2 * inset, size - 2 * (margin + inset));
    if (usage < 0) {
        const float dot = std::max(1.f, scale), gap = dot;
        const float start = (size - 3 * dot - 2 * gap) / 2;
        for (int i = 0; i < 3; ++i)
            g.FillEllipse(&ink, start + i * (dot + gap), (size - dot) / 2, dot, dot);
    } else {
        const int remaining = 100 - std::clamp(usage, 0, 100);
        if (remaining > 0) {
            const float width = std::max(1.f, inner.Width * remaining / 100.f);
            GraphicsPath fill;
            pill(fill, RectF(inner.X, inner.Y, width, inner.Height));
            g.FillPath(&ink, &fill);
        }
    }
}
inline HICON createTrayIcon(int size, int usage) {
    Gdiplus::Bitmap bitmap(size, size, PixelFormat32bppARGB);
    Gdiplus::Graphics g(&bitmap);
    drawTrayIcon(g, size, usage);
    HICON icon = nullptr;
    bitmap.GetHICON(&icon);
    return icon;
}
} // namespace pulse
