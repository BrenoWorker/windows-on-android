#include "Icons.h"

#include <cmath>

#include "../core/Gfx.h"

namespace wd {

static const float kPi = 3.14159265f;

void drawIcon(Gfx& g, IconId id, const Rect& r, Color c, float battery) {
    float u = std::min(r.w, r.h) / 16.0f;
    float ox = r.x + (r.w - 16 * u) * 0.5f, oy = r.y + (r.h - 16 * u) * 0.5f;
    auto X = [&](float v) { return ox + v * u; };
    auto Y = [&](float v) { return oy + v * u; };
    auto R = [&](float x, float y, float w, float h) { return Rect{X(x), Y(y), w * u, h * u}; };
    auto L = [&](float x0, float y0, float x1, float y1, float t, Color col) { g.line(X(x0), Y(y0), X(x1), Y(y1), t * u, col); };
    float t = 1.15f;

    switch (id) {
        case IcFolder:
        case IcExplorer: {
            g.rect(R(1, 2.6f, 6.2f, 3), rgb(0xD99A1E), 1.0f * u);
            g.rect(R(1, 3.6f, 14, 9.6f), rgb(0xE8A929), 1.2f * u);
            g.rectV(R(1, 5.2f, 14, 8.6f), rgb(0xFFD15C), rgb(0xFFBF2E), 1.2f * u);
            if (id == IcExplorer) g.rect(R(1, 11.2f, 14, 2.6f), rgb(0x2F8FE0), 1.2f * u);
            break;
        }
        case IcBrowser:
        case IcGlyphGlobe: {
            bool full = id == IcBrowser;
            Color line = full ? rgb(0xFFFFFF, 0.92f) : c;
            float rr = full ? 6.6f : 6.2f;
            if (full) {
                g.rectV(R(1.4f, 1.4f, 13.2f, 13.2f), rgb(0x45C0FF), rgb(0x0A58C2), 6.6f * u);
                g.circle(X(5.6f), Y(5.0f), 2.4f * u, rgb(0xFFFFFF, 0.18f));
            }
            g.ring(X(8), Y(8), rr * u, 0.9f * u, line);
            L(8 - rr, 8, 8 + rr, 8, 0.9f, line);
            L(8, 8 - rr, 8, 8 + rr, 0.9f, line);
            L(8 - rr * 0.84f, 4.6f, 8 + rr * 0.84f, 4.6f, 0.8f, line);
            L(8 - rr * 0.84f, 11.4f, 8 + rr * 0.84f, 11.4f, 0.8f, line);
            g.arc(X(12.6f), Y(8), 6.3f * u, 2.32f, 3.96f, 0.9f * u, line);
            g.arc(X(3.4f), Y(8), 6.3f * u, -0.82f, 0.82f, 0.9f * u, line);
            break;
        }
        case IcFile: {
            g.rect(R(3, 1, 10, 14), rgb(0xBDBDBD), 1.2f * u);
            g.rect(R(3.5f, 1.5f, 9, 13), rgb(0xFFFFFF), 1.0f * u);
            for (int i = 0; i < 4; i++) L(5, 5 + i * 2.2f, 11, 5 + i * 2.2f, 0.7f, rgb(0x9E9E9E));
            break;
        }
        case IcNotepad: {
            g.rectV(R(2.5f, 1, 11, 14), rgb(0x3D9BE9), rgb(0x1F6FC5), 1.6f * u);
            g.rect(R(4, 4, 8, 9.5f), rgb(0xFFFFFF), 0.8f * u);
            for (int i = 0; i < 4; i++) L(5.2f, 6 + i * 2, 10.8f, 6 + i * 2, 0.7f, rgb(0x7DA7D9));
            for (int i = 0; i < 3; i++) g.circle(X(5 + i * 3), Y(2.4f), 0.7f * u, rgb(0xFFFFFF));
            break;
        }
        case IcCalculator: {
            g.rect(R(2.5f, 1, 11, 14), rgb(0x3B3B3B), 1.8f * u);
            g.rect(R(4, 2.5f, 8, 3), rgb(0x9FD3C7), 0.6f * u);
            for (int yy = 0; yy < 3; yy++)
                for (int xx = 0; xx < 3; xx++) {
                    Color col = (xx == 2 && yy == 2) ? rgb(0xF7630C) : rgb(0x8A8A8A);
                    g.rect(R(4 + xx * 2.9f, 7 + yy * 2.5f, 2.2f, 1.8f), col, 0.4f * u);
                }
            break;
        }
        case IcSettings:
        case IcGlyphGear: {
            Color body = id == IcSettings ? rgb(0x5F7385) : c;
            for (int i = 0; i < 8; i++) {
                float a = i * kPi / 4;
                g.rotRect(X(8) + std::cos(a) * 5.2f * u, Y(8) + std::sin(a) * 5.2f * u, 1.5f * u, 1.3f * u, a, 0.4f * u,
                          body, body);
            }
            if (id == IcSettings) {
                g.circle(X(8), Y(8), 5.2f * u, body);
                g.circle(X(8), Y(8), 2.2f * u, rgb(0xDDE6EE));
            } else {
                g.ring(X(8), Y(8), 5.0f * u, 1.3f * u, body);
                g.ring(X(8), Y(8), 2.0f * u, 1.1f * u, body);
            }
            break;
        }
        case IcTerminal: {
            g.rect(R(1, 2, 14, 12), rgb(0x2B2B2B), 1.8f * u);
            g.rect(R(1, 2, 14, 2.4f), rgb(0x4A4A4A), 1.8f * u);
            g.rect(R(1, 3.4f, 14, 1.0f), rgb(0x4A4A4A));
            L(3.5f, 7, 6, 9, 1.1f, rgb(0xFFFFFF));
            L(6, 9, 3.5f, 11, 1.1f, rgb(0xFFFFFF));
            L(7.5f, 11.3f, 11.5f, 11.3f, 1.1f, rgb(0xFFFFFF));
            break;
        }
        case IcPaint: {
            g.circle(X(8), Y(8), 6.6f * u, rgb(0xF2D7A6));
            g.circle(X(10.6f), Y(10.4f), 1.6f * u, rgb(0xFFFFFF, 0));
            g.circle(X(5.2f), Y(6.2f), 1.3f * u, rgb(0xE81123));
            g.circle(X(8.2f), Y(4.4f), 1.3f * u, rgb(0x0078D4));
            g.circle(X(11.2f), Y(6.0f), 1.3f * u, rgb(0x16C60C));
            g.circle(X(5.4f), Y(10.0f), 1.3f * u, rgb(0xFFB900));
            g.circle(X(10.4f), Y(10.6f), 1.4f * u, rgb(0xC39A5E));
            break;
        }
        case IcMines: {
            g.rect(R(1, 1, 14, 14), rgb(0xB8C4CE), 2 * u);
            for (int i = 0; i < 4; i++) {
                float a = i * kPi / 4;
                L(8 - std::cos(a) * 5, 8 - std::sin(a) * 5, 8 + std::cos(a) * 5, 8 + std::sin(a) * 5, 1.2f, rgb(0x111111));
            }
            g.circle(X(8), Y(8), 3.8f * u, rgb(0x111111));
            g.circle(X(6.8f), Y(6.8f), 1.0f * u, rgb(0xFFFFFF));
            break;
        }
        case IcImage: {
            g.rect(R(1.5f, 2.5f, 13, 11), rgb(0x1F7FD0), 1.4f * u);
            g.rect(R(2.5f, 3.5f, 11, 9), rgb(0xBFE3FF), 0.8f * u);
            g.circle(X(10.5f), Y(6), 1.4f * u, rgb(0xFFC83D));
            L(3, 11.5f, 6.5f, 7.5f, 1.2f, rgb(0x2E9D4F));
            L(6.5f, 7.5f, 9.5f, 11.5f, 1.2f, rgb(0x2E9D4F));
            L(8, 11.5f, 10.5f, 9, 1.2f, rgb(0x41B865));
            L(10.5f, 9, 13, 11.5f, 1.2f, rgb(0x41B865));
            break;
        }
        case IcPC: {
            g.rect(R(1, 2, 14, 9.5f), rgb(0x3A4B5C), 1.2f * u);
            g.rectV(R(2, 3, 12, 7.5f), rgb(0x4FB3F6), rgb(0x1E78D0), 0.6f * u);
            g.rect(R(6.5f, 11.5f, 3, 2), rgb(0x8A97A3));
            g.rect(R(4.5f, 13.2f, 7, 1.3f), rgb(0x8A97A3), 0.6f * u);
            break;
        }
        case IcBin: {
            g.rect(R(3, 4.6f, 10, 1.4f), rgb(0x9AA4AE), 0.6f * u);
            g.rect(R(6.2f, 3.2f, 3.6f, 1.6f), rgb(0x9AA4AE), 0.5f * u);
            g.rectV(R(4, 6.2f, 8, 8.6f), rgb(0xE6ECF1), rgb(0xBFC9D2), 1.0f * u);
            for (int i = 0; i < 3; i++) L(6 + i * 2, 8, 6 + i * 2, 13, 0.7f, rgb(0x8C98A4));
            break;
        }
        case IcDrive: {
            g.rectV(R(1, 5, 14, 7), rgb(0xD6DCE2), rgb(0xA9B3BD), 1.4f * u);
            g.rect(R(2, 9.4f, 12, 0.8f), rgb(0x7E8A96), 0.4f * u);
            g.circle(X(12.4f), Y(7.4f), 0.8f * u, rgb(0x16C60C));
            break;
        }
        case IcLogo: {
            Rect outer = R(1.2f, 1.8f, 13.6f, 12.4f);
            g.shadow({outer.x, outer.y + 0.6f * u, outer.w, outer.h}, 2.4f * u, 1.2f * u, rgb(0x000000, 0.18f));
            g.rectV(outer, rgb(0x5FC3FF), rgb(0x0A5FC6), 2.4f * u);
            Rect inner = R(2.6f, 5.0f, 10.8f, 7.8f);
            g.rectV(inner, rgb(0xFFFFFF, 0.95f), rgb(0xDCEEFF, 0.92f), 1.3f * u);
            g.rect(R(3.2f, 2.9f, 3.4f, 1.0f), rgb(0xFFFFFF, 0.75f), 0.5f * u);
            g.circle(X(12.2f), Y(3.4f), 0.6f * u, rgb(0xFFFFFF, 0.8f));
            break;
        }
        case IcUser: {
            g.circle(X(8), Y(8), 7.6f * u, rgb(0x8E9AAF));
            g.circle(X(8), Y(6.2f), 2.6f * u, rgb(0xFFFFFF));
            g.rect(R(3.8f, 9.6f, 8.4f, 6), rgb(0xFFFFFF), 4 * u);
            g.ring(X(8), Y(8), 7.6f * u, 0.8f * u, rgb(0x8E9AAF));
            break;
        }
        case IcDialog: {
            g.rect(R(1.5f, 3, 13, 10), rgb(0x5B6B7A), 1.4f * u);
            g.rect(R(2.5f, 5.5f, 11, 6.5f), rgb(0xFFFFFF), 0.6f * u);
            g.rect(R(4, 7.6f, 8, 1.6f), rgb(0x0078D4), 0.4f * u);
            break;
        }
        case IcClock: {
            g.circle(X(8), Y(8), 7 * u, rgb(0x1F6FC5));
            g.circle(X(8), Y(8), 5.8f * u, rgb(0xFFFFFF));
            L(8, 8, 8, 4, 1.0f, rgb(0x1B1B1B));
            L(8, 8, 10.6f, 9.4f, 1.0f, rgb(0x1B1B1B));
            break;
        }
        case IcGlyphSearch:
            g.ring(X(6.8f), Y(6.8f), 4.6f * u, t * u, c);
            L(10.2f, 10.2f, 14, 14, t * 1.1f, c);
            break;
        case IcGlyphPower:
            g.arc(X(8), Y(8.6f), 5.6f * u, -kPi / 2 + 0.75f, kPi * 1.5f - 0.75f, t * u, c);
            L(8, 1.8f, 8, 7.6f, t, c);
            break;
        case IcGlyphWifi: {
            for (int i = 0; i < 3; i++) {
                float rr = (4.2f + i * 3.2f) * u;
                g.arc(X(8), Y(13.4f), rr, -kPi / 2 - 0.78f, -kPi / 2 + 0.78f, t * u, c);
            }
            g.circle(X(8), Y(13.2f), 1.1f * u, c);
            break;
        }
        case IcGlyphVolume: {
            g.rect(R(1.6f, 6, 3, 4), c, 0.5f * u);
            L(4.4f, 6, 8, 2.6f, t, c);
            L(4.4f, 10, 8, 13.4f, t, c);
            L(8, 2.6f, 8, 13.4f, t, c);
            g.arc(X(9), Y(8), 3.0f * u, -0.9f, 0.9f, t * u, c);
            g.arc(X(9), Y(8), 5.6f * u, -0.9f, 0.9f, t * u, c);
            break;
        }
        case IcGlyphBattery: {
            g.stroke(R(0.8f, 4.6f, 12.8f, 6.8f), c, 1.4f * u, t * u);
            g.rect(R(13.8f, 6.6f, 1.4f, 2.8f), c, 0.5f * u);
            g.rect(R(2.4f, 6.2f, 9.6f * clampf(battery, 0.05f, 1.0f), 3.6f), c, 0.5f * u);
            break;
        }
        case IcGlyphKeyboard: {
            g.stroke(R(0.8f, 3.6f, 14.4f, 8.8f), c, 1.4f * u, t * u);
            for (int yy = 0; yy < 2; yy++)
                for (int xx = 0; xx < 5; xx++) g.rect(R(2.8f + xx * 2.2f, 5.6f + yy * 2.0f, 1.1f, 1.0f), c, 0.2f * u);
            L(5, 10.0f, 11, 10.0f, 1.0f, c);
            break;
        }
        case IcGlyphBack:
            L(2.5f, 8, 13.5f, 8, t, c);
            L(2.5f, 8, 7, 3.5f, t, c);
            L(2.5f, 8, 7, 12.5f, t, c);
            break;
        case IcGlyphForward:
            L(2.5f, 8, 13.5f, 8, t, c);
            L(13.5f, 8, 9, 3.5f, t, c);
            L(13.5f, 8, 9, 12.5f, t, c);
            break;
        case IcGlyphUp:
            L(8, 2.5f, 8, 13.5f, t, c);
            L(8, 2.5f, 3.5f, 7, t, c);
            L(8, 2.5f, 12.5f, 7, t, c);
            break;
        case IcGlyphDown:
            L(4, 6, 8, 10, t, c);
            L(8, 10, 12, 6, t, c);
            break;
        case IcGlyphChevronRight:
            L(6, 4, 10, 8, t, c);
            L(10, 8, 6, 12, t, c);
            break;
        case IcGlyphBluetooth:
            L(7.6f, 1.8f, 7.6f, 14.2f, t, c);
            L(7.6f, 1.8f, 11.4f, 5.2f, t, c);
            L(11.4f, 5.2f, 4.4f, 11.2f, t, c);
            L(7.6f, 14.2f, 11.4f, 10.8f, t, c);
            L(11.4f, 10.8f, 4.4f, 4.8f, t, c);
            break;
        case IcGlyphPlane:
            L(8, 1.6f, 8, 14, t * 1.4f, c);
            L(1.8f, 9, 8, 6, t, c);
            L(14.2f, 9, 8, 6, t, c);
            L(5, 14.2f, 8, 12.6f, t, c);
            L(11, 14.2f, 8, 12.6f, t, c);
            break;
        case IcGlyphMoon:
            g.arc(X(8), Y(8), 5.6f * u, -0.25f, 4.93f, t * u, c);
            g.arc(X(12.34f), Y(3.52f), 3.27f * u, 1.232f, 3.446f, t * u, c);
            break;
        case IcGlyphSun:
            g.ring(X(8), Y(8), 3.2f * u, t * u, c);
            for (int i = 0; i < 8; i++) {
                float a = i * kPi / 4;
                L(8 + std::cos(a) * 5.2f, 8 + std::sin(a) * 5.2f, 8 + std::cos(a) * 6.8f, 8 + std::sin(a) * 6.8f, t, c);
            }
            break;
        case IcGlyphLeaf:
            g.arc(X(13), Y(13), 10 * u, kPi, kPi * 1.5f, t * u, c);
            g.arc(X(3), Y(3), 10 * u, 0, kPi * 0.5f, t * u, c);
            L(3, 13, 9, 7, t, c);
            break;
        case IcGlyphPlus:
            L(8, 2.5f, 8, 13.5f, t, c);
            L(2.5f, 8, 13.5f, 8, t, c);
            break;
        case IcGlyphTrash:
            L(2.5f, 4, 13.5f, 4, t, c);
            g.stroke(R(6, 1.6f, 4, 2.4f), c, 0.6f * u, t * 0.9f * u);
            g.stroke(R(3.8f, 4, 8.4f, 10.4f), c, 1.2f * u, t * u);
            L(6.6f, 6.6f, 6.6f, 12, 0.9f, c);
            L(9.4f, 6.6f, 9.4f, 12, 0.9f, c);
            break;
        case IcGlyphRename:
            g.stroke(R(1, 4.5f, 14, 7), c, 1.0f * u, t * u);
            L(10.5f, 2.5f, 10.5f, 13.5f, t, c);
            L(9, 2.5f, 12, 2.5f, t, c);
            L(9, 13.5f, 12, 13.5f, t, c);
            break;
        case IcGlyphNewFolder:
            g.stroke(R(1, 3.5f, 13, 10), c, 1.2f * u, t * u);
            L(1.5f, 6, 14, 6, t, c);
            L(10.5f, 8, 10.5f, 12.5f, t, rgb(0x16C60C));
            L(8.2f, 10.2f, 12.8f, 10.2f, t, rgb(0x16C60C));
            break;
        case IcGlyphRefresh:
            g.arc(X(8), Y(8), 5.4f * u, -kPi * 0.35f, kPi * 1.45f, t * u, c);
            L(12.4f, 1.8f, 12.4f, 5.0f, t, c);
            L(12.4f, 5.0f, 9.2f, 5.0f, t, c);
            break;
        case IcGlyphBackspace:
            L(1.5f, 8, 5.5f, 3.5f, t, c);
            L(1.5f, 8, 5.5f, 12.5f, t, c);
            L(5.5f, 3.5f, 14.5f, 3.5f, t, c);
            L(5.5f, 12.5f, 14.5f, 12.5f, t, c);
            L(14.5f, 3.5f, 14.5f, 12.5f, t, c);
            L(8, 6, 12, 10, t, c);
            L(12, 6, 8, 10, t, c);
            break;
        case IcGlyphShift:
            L(8, 2, 2, 8.5f, t, c);
            L(8, 2, 14, 8.5f, t, c);
            L(2, 8.5f, 5, 8.5f, t, c);
            L(14, 8.5f, 11, 8.5f, t, c);
            L(5, 8.5f, 5, 14, t, c);
            L(11, 8.5f, 11, 14, t, c);
            L(5, 14, 11, 14, t, c);
            break;
        case IcGlyphEnter:
            L(13.5f, 3, 13.5f, 9.5f, t, c);
            L(13.5f, 9.5f, 2.5f, 9.5f, t, c);
            L(2.5f, 9.5f, 6, 6, t, c);
            L(2.5f, 9.5f, 6, 13, t, c);
            break;
        case IcGlyphClose:
            L(3, 3, 13, 13, t, c);
            L(13, 3, 3, 13, t, c);
            break;
        case IcGlyphBrush:
            L(13.5f, 2.5f, 7, 9, t * 1.6f, c);
            g.circle(X(5), Y(11.2f), 2.6f * u, c);
            L(3, 13.2f, 1.5f, 14.6f, t * 1.6f, c);
            break;
        case IcGlyphEraser:
            g.rotRect(X(8), Y(8), 6 * u, 3 * u, -kPi / 4, 0.8f * u, c, c);
            L(2, 14.5f, 14, 14.5f, t, c);
            break;
        case IcGlyphSave:
            g.stroke(R(2, 2, 12, 12), c, 1.4f * u, t * u);
            g.stroke(R(5, 2, 6, 4), c, 0.4f * u, t * u);
            g.stroke(R(4.5f, 9, 7, 5), c, 0.4f * u, t * u);
            break;
        case IcGlyphOpen:
            g.stroke(R(1.5f, 3, 13, 10.5f), c, 1.2f * u, t * u);
            L(1.5f, 6, 6, 6, t, c);
            L(6, 6, 7.5f, 3.5f, t, c);
            break;
        case IcGlyphNew:
            g.stroke(R(3, 1.5f, 10, 13), c, 1.2f * u, t * u);
            L(8, 5.5f, 8, 10.5f, t, c);
            L(5.5f, 8, 10.5f, 8, t, c);
            break;
        case IcGlyphLock:
            g.stroke(R(3, 7, 10, 7.5f), c, 1.4f * u, t * u);
            g.arc(X(8), Y(7), 3.2f * u, kPi, kPi * 2, t * u, c);
            g.circle(X(8), Y(10.6f), 1.0f * u, c);
            break;
        case IcGlyphRestart:
            g.arc(X(8), Y(8.4f), 5.6f * u, -kPi * 0.15f, kPi * 1.5f, t * u, c);
            L(8, 2.8f, 10.8f, 1.0f, t, c);
            L(8, 2.8f, 10.8f, 4.8f, t, c);
            break;
        case IcGlyphSleep:
            g.arc(X(8), Y(8), 5.6f * u, -kPi * 0.25f, kPi * 1.25f, t * u, c);
            L(9.5f, 3, 13, 3, t, c);
            L(13, 3, 9.5f, 6.5f, t, c);
            L(9.5f, 6.5f, 13, 6.5f, t, c);
            break;
        case IcGlyphFlag:
            L(4, 2, 4, 14.5f, t * 1.2f, c);
            g.rect(R(4.5f, 2, 8, 5.5f), rgb(0xE81123), 0.4f * u);
            L(2, 14.5f, 9, 14.5f, t * 1.2f, c);
            break;
        case IcGlyphGrid:
            for (int yy = 0; yy < 2; yy++)
                for (int xx = 0; xx < 2; xx++) g.stroke(R(2 + xx * 6.5f, 2 + yy * 6.5f, 5.5f, 5.5f), c, 1.0f * u, t * u);
            break;
        case IcGlyphExternal:
            g.stroke(R(2, 4, 10, 10), c, 1.2f * u, t * u);
            L(8, 8, 14, 2, t, c);
            L(9.5f, 2, 14, 2, t, c);
            L(14, 2, 14, 6.5f, t, c);
            break;
        case IcGlyphMonitor:
            g.stroke(R(1.5f, 2.5f, 13, 8.5f), c, 1.0f * u, t * u);
            L(8, 11, 8, 13.5f, t, c);
            L(5, 13.8f, 11, 13.8f, t, c);
            break;
        case IcGlyphHome:
            L(1.6f, 8, 8, 2, t, c);
            L(8, 2, 14.4f, 8, t, c);
            g.stroke(R(3.5f, 7, 9, 7.5f), c, 0.6f * u, t * u);
            g.rect(R(7, 10, 2, 4.5f), c, 0.3f * u);
            break;
    }
}

}
