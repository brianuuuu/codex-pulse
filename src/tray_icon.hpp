#pragma once
#include "platform.hpp"

namespace pulse {
// Native-size vertical progress track. Bottom-up fill represents the least
// remaining quota. Three vertical dots distinguish unknown from empty.
inline void drawTrayIcon(Gdiplus::Graphics &g, int size, double usage) {
    using namespace Gdiplus;
    g.Clear(Color(0, 0, 0, 0));
    // A native-size tray glyph needs opaque, grid-aligned strokes. Only the
    // fractional progress row uses transparency; the outline never does.
    g.SetSmoothingMode(SmoothingModeNone);
    g.SetPixelOffsetMode(PixelOffsetModeHalf);
    const float scale = size / 16.f;
    const int stroke = std::max(1, static_cast<int>(std::lround(scale)));
    // Keep the full height for progress resolution, but widen the track to
    // roughly 5:8 so the fill remains legible at native 16/20px tray sizes.
    const int margin = static_cast<int>(std::lround(size * .1875f));
    const int radius = std::max(1, static_cast<int>(std::lround(scale * 1.5f)));
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
    roundedPath(track, RectF(margin, 0, size - 2 * margin, size), static_cast<float>(radius));
    roundedPath(track, RectF(margin + stroke, stroke, size - 2 * (margin + stroke), size - 2 * stroke),
                static_cast<float>(std::max(0, radius - stroke)));
    const bool available = std::isfinite(usage) && usage >= 0;
    const Color tone = available && usage > 90   ? Color(255, 69, 58)
                       : available && usage > 80 ? Color(255, 159, 10)
                                    : Color(255, 255, 255);
    SolidBrush ink(tone);
    g.FillPath(&ink, &track);
    const int inset = stroke + std::max(1, static_cast<int>(std::lround(scale * .5f)));
    const Rect inner(margin + inset, inset, size - 2 * (margin + inset), size - 2 * inset);
    if (!available) {
        const int dot = std::max(1, static_cast<int>(std::lround(scale))), gap = dot;
        const int start = (size - 3 * dot - 2 * gap) / 2;
        for (int i = 0; i < 3; ++i)
            g.FillRectangle(&ink, (size - dot) / 2, start + i * (dot + gap), dot, dot);
    } else {
        // A rectangular fill keeps visible area proportional even at very low
        // remaining quota. Preserve fractional percentages without a 1px floor.
        const double height = inner.Height * (100 - std::clamp(usage, 0.0, 100.0)) / 100;
        const int whole = static_cast<int>(std::floor(height));
        if (whole > 0)
            g.FillRectangle(&ink, inner.X, inner.GetBottom() - whole, inner.Width, whole);
        const int alpha = static_cast<int>(std::lround((height - whole) * 255));
        if (alpha > 0 && whole < inner.Height) {
            SolidBrush edge(Color(static_cast<BYTE>(alpha), tone.GetR(), tone.GetG(), tone.GetB()));
            g.FillRectangle(&edge, inner.X, inner.GetBottom() - whole - 1, inner.Width, 1);
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
