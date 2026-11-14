#pragma once
#include "platform.hpp"
namespace pulse {
enum class Glyph {
    Grid,
    Chart,
    Sliders,
    Refresh,
    Copy,
    More,
    Sun,
    Moon,
    Auto,
    Clock,
    Cloud,
    Tray,
    Bell,
    Power,
    Folder,
    Reset,
    Check,
    Info,
    User,
    Star,
    Coins,
    History,
    Session,
    Warning,
    Left,
    Right,
    Models,
    Minimize,
    Maximize,
    Restore,
    Close
};
inline void drawGlyph(Gdiplus::Graphics &g, Glyph icon, float x, float y, float size, Gdiplus::Color color) {
    using namespace Gdiplus;
    auto state = g.Save();
    g.TranslateTransform(x, y);
    g.ScaleTransform(size / 24.f, size / 24.f);
    Pen p(color, 1.75f);
    p.SetStartCap(LineCapRound);
    p.SetEndCap(LineCapRound);
    p.SetLineJoin(LineJoinRound);
    SolidBrush ink(color);
    auto line = [&](float a, float b, float c, float d) { g.DrawLine(&p, a, b, c, d); };
    auto rect = [&](float a, float b, float c, float d) { g.DrawRectangle(&p, a, b, c, d); };
    auto ellipse = [&](float a, float b, float c, float d) { g.DrawEllipse(&p, a, b, c, d); };
    switch (icon) {
    case Glyph::Minimize:
        line(5, 12, 19, 12);
        break;
    case Glyph::Maximize:
        rect(5, 5, 14, 14);
        break;
    case Glyph::Restore:
        rect(4, 8, 12, 12);
        line(8, 4, 20, 4);
        line(20, 4, 20, 16);
        break;
    case Glyph::Close:
        line(6, 6, 18, 18);
        line(18, 6, 6, 18);
        break;
    case Glyph::Grid:
        for (float yy : {4.f, 14.f})
            for (float xx : {4.f, 14.f})
                rect(xx, yy, 6, 6);
        break;
    case Glyph::Chart:
        line(4, 4, 4, 20);
        line(4, 20, 21, 20);
        line(9, 15, 9, 10);
        line(14, 15, 14, 5);
        line(19, 15, 19, 8);
        break;
    case Glyph::Sliders:
        line(4, 6, 20, 6);
        line(4, 12, 20, 12);
        line(4, 18, 20, 18);
        ellipse(7, 4, 4, 4);
        ellipse(14, 10, 4, 4);
        ellipse(5, 16, 4, 4);
        break;
    case Glyph::Refresh:
        g.DrawArc(&p, 4.f, 4.f, 16.f, 16.f, 40.f, 280.f);
        line(19, 3, 19, 8);
        line(19, 8, 14, 8);
        break;
    case Glyph::Copy:
        rect(8, 8, 12, 13);
        line(16, 4, 4, 4);
        line(4, 4, 4, 16);
        break;
    case Glyph::More:
        for (float xx : {5.f, 12.f, 19.f})
            g.FillEllipse(&ink, xx - 1.2f, 10.8f, 2.4f, 2.4f);
        break;
    case Glyph::Sun:
        ellipse(8, 8, 8, 8);
        for (int i = 0; i < 8; ++i) {
            float a = i * 3.14159265f / 4;
            line(12 + 7 * std::cos(a), 12 + 7 * std::sin(a), 12 + 10 * std::cos(a), 12 + 10 * std::sin(a));
        }
        break;
    case Glyph::Moon: {
        GraphicsPath path;
        path.AddArc(3.f, 3.f, 18.f, 18.f, 70.f, 265.f);
        path.AddBezier(20.f, 8.f, 11.f, 13.f, 10.f, 6.f, 11.f, 3.f);
        g.DrawPath(&p, &path);
        break;
    }
    case Glyph::Auto:
        ellipse(4, 4, 16, 16);
        g.FillPie(&ink, 4.f, 4.f, 16.f, 16.f, 90.f, 180.f);
        break;
    case Glyph::Clock:
        ellipse(3, 3, 18, 18);
        line(12, 7, 12, 12);
        line(12, 12, 16, 14);
        break;
    case Glyph::History:
        g.DrawArc(&p, 4.f, 4.f, 16.f, 16.f, 210.f, 305.f);
        line(3, 4, 3, 9);
        line(3, 9, 8, 9);
        line(12, 7, 12, 12);
        line(12, 12, 15, 14);
        break;
    case Glyph::Cloud: {
        GraphicsPath path;
        path.AddBezier(7.f, 18.f, 1.f, 18.f, 1.f, 10.f, 7.f, 10.f);
        path.AddBezier(7.f, 10.f, 8.f, 1.f, 19.f, 4.f, 18.f, 11.f);
        path.AddBezier(18.f, 11.f, 24.f, 11.f, 23.f, 18.f, 18.f, 18.f);
        path.AddLine(18.f, 18.f, 7.f, 18.f);
        g.DrawPath(&p, &path);
        break;
    }
    case Glyph::Tray:
        line(3, 14, 3, 20);
        line(3, 20, 21, 20);
        line(21, 20, 21, 14);
        line(12, 3, 12, 14);
        line(8, 10, 12, 14);
        line(12, 14, 16, 10);
        break;
    case Glyph::Bell: {
        GraphicsPath path;
        path.AddBezier(6.f, 16.f, 8.f, 13.f, 4.f, 5.f, 12.f, 5.f);
        path.AddBezier(12.f, 5.f, 20.f, 5.f, 16.f, 13.f, 18.f, 16.f);
        path.AddLine(18.f, 16.f, 6.f, 16.f);
        g.DrawPath(&p, &path);
        line(12, 3, 12, 5);
        g.DrawArc(&p, 9.f, 17.f, 6.f, 4.f, 0.f, 180.f);
        break;
    }
    case Glyph::Power:
        g.DrawArc(&p, 4.f, 4.f, 16.f, 16.f, 310.f, 280.f);
        line(12, 2, 12, 11);
        break;
    case Glyph::Folder: {
        PointF points[] = {{3, 7}, {3, 20}, {21, 20}, {21, 7}, {13, 7}, {10, 4}, {3, 4}, {3, 7}};
        g.DrawLines(&p, points, 8);
        break;
    }
    case Glyph::Reset:
        line(7, 4, 3, 8);
        line(3, 8, 7, 12);
        line(3, 8, 14, 8);
        g.DrawArc(&p, 10.f, 8.f, 10.f, 10.f, 270.f, 180.f);
        line(15, 18, 9, 18);
        break;
    case Glyph::Check:
        line(5, 12, 10, 17);
        line(10, 17, 20, 6);
        break;
    case Glyph::Info:
        ellipse(3, 3, 18, 18);
        line(12, 11, 12, 17);
        g.FillEllipse(&ink, 11.f, 6.f, 2.f, 2.f);
        break;
    case Glyph::User:
        ellipse(8, 3, 8, 8);
        g.DrawArc(&p, 4.f, 14.f, 16.f, 12.f, 180.f, 180.f);
        break;
    case Glyph::Star: {
        PointF points[10];
        for (int i = 0; i < 10; ++i) {
            float a = -3.14159265f / 2 + i * 3.14159265f / 5, r = i % 2 ? 4.5f : 10.f;
            points[i] = {12 + r * std::cos(a), 12 + r * std::sin(a)};
        }
        g.DrawPolygon(&p, points, 10);
        break;
    }
    case Glyph::Coins:
        ellipse(3, 4, 15, 6);
        g.DrawArc(&p, 3.f, 8.f, 15.f, 6.f, 0.f, 180.f);
        line(3, 7, 3, 11);
        line(18, 7, 18, 11);
        g.DrawArc(&p, 6.f, 13.f, 15.f, 6.f, 0.f, 180.f);
        line(21, 12, 21, 16);
        break;
    case Glyph::Session:
        rect(3, 4, 18, 13);
        line(7, 17, 7, 21);
        line(7, 21, 12, 17);
        line(7, 9, 17, 9);
        line(7, 13, 14, 13);
        break;
    case Glyph::Warning: {
        PointF points[] = {{12, 3}, {22, 21}, {2, 21}};
        g.DrawPolygon(&p, points, 3);
        line(12, 9, 12, 14);
        g.FillEllipse(&ink, 11.f, 17.f, 2.f, 2.f);
        break;
    }
    case Glyph::Left:
        line(15, 5, 8, 12);
        line(8, 12, 15, 19);
        break;
    case Glyph::Right:
        line(9, 5, 16, 12);
        line(16, 12, 9, 19);
        break;
    case Glyph::Models:
        rect(6, 6, 12, 12);
        rect(10, 10, 4, 4);
        for (float i : {8.f, 12.f, 16.f}) {
            line(i, 3, i, 6);
            line(i, 18, i, 21);
            line(3, i, 6, i);
            line(18, i, 21, i);
        }
        break;
    }
    g.Restore(state);
}
} // namespace pulse
