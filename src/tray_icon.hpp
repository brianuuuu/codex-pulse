#pragma once
#include "platform.hpp"

namespace pulse {
// Native-size rounded progress track. Fill represents the least remaining
// quota. No terminal or digits; three dots distinguish unknown from empty.
inline void drawTrayIcon(Gdiplus::Graphics &g, int size, double usage) {
    using namespace Gdiplus;
    g.Clear(Color(0, 0, 0, 0));
    // A native-size tray glyph needs opaque, grid-aligned strokes. Only the
    // fractional progress column uses transparency; the outline never does.
    g.SetSmoothingMode(SmoothingModeNone);
    g.SetPixelOffsetMode(PixelOffsetModeHalf);
    const float scale = size / 16.f;
    const int stroke = std::max(2, static_cast<int>(std::lround(scale * 1.8f)));
    const int margin = static_cast<int>(std::lround(size * .1875f));
    const int radius = std::max(2, static_cast<int>(std::lround(scale * 2)));
    auto roundedPath = [](GraphicsPath &path, RectF r, float radius) {
        const float d = std::min({radius * 2, r.Width, r.Height});
        if (d <= 0) {
            path.AddRectangle(r);
            return;
        }
        path.AddArc(r.X, r.Y, d, d, 180, 90);
        path.AddArc(r.GetRight() - d, r.Y, d, d, 270, 90);
        path.AddArc(r.GetRight() - d, r.GetBottom() - d, d, d, 0, 90);
        path.AddArc(r.X, r.GetBottom() - d, d, d, 90, 90);
        path.CloseFigure();
    };
    GraphicsPath track(FillModeAlternate);
    roundedPath(track, RectF(0, margin, size, size - 2 * margin), static_cast<float>(radius));
    roundedPath(track, RectF(stroke, margin + stroke, size - 2 * stroke, size - 2 * (margin + stroke)),
                static_cast<float>(std::max(0, radius - stroke)));
    const bool available = std::isfinite(usage) && usage >= 0;
    const Color tone = available && usage > 90   ? Color(255, 69, 58)
                       : available && usage > 80 ? Color(255, 159, 10)
                                    : Color(255, 255, 255);
    SolidBrush ink(tone);
    g.FillPath(&ink, &track);
    const int inset = stroke + std::max(1, static_cast<int>(std::lround(scale * .5f)));
    const Rect inner(inset, margin + inset, size - 2 * inset, size - 2 * (margin + inset));
    if (!available) {
        const int dot = std::max(1, static_cast<int>(std::lround(scale))), gap = dot;
        const int start = (size - 3 * dot - 2 * gap) / 2;
        for (int i = 0; i < 3; ++i)
            g.FillRectangle(&ink, start + i * (dot + gap), (size - dot) / 2, dot, dot);
    } else {
        // A rectangular fill keeps visible area proportional even at very low
        // remaining quota. Preserve fractional percentages without a 1px floor.
        const double width = inner.Width * (100 - std::clamp(usage, 0.0, 100.0)) / 100;
        const int whole = static_cast<int>(std::floor(width));
        if (whole > 0)
            g.FillRectangle(&ink, inner.X, inner.Y, whole, inner.Height);
        const int alpha = static_cast<int>(std::lround((width - whole) * 255));
        if (alpha > 0 && whole < inner.Width) {
            SolidBrush edge(Color(static_cast<BYTE>(alpha), tone.GetR(), tone.GetG(), tone.GetB()));
            g.FillRectangle(&edge, inner.X + whole, inner.Y, 1, inner.Height);
        }
    }
}
inline HICON createTrayIcon(int size, double usage) {
    Gdiplus::Bitmap bitmap(size, size, PixelFormat32bppARGB);
    Gdiplus::Graphics g(&bitmap);
    drawTrayIcon(g, size, usage);
    HICON icon = nullptr;
    bitmap.GetHICON(&icon);
    return icon;
}
} // namespace pulse
