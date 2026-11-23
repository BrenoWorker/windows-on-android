#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace wd {

struct Color {
    float r = 0, g = 0, b = 0, a = 1;
};

inline Color rgb(uint32_t h, float a = 1.f) {
    return {((h >> 16) & 255) / 255.f, ((h >> 8) & 255) / 255.f, (h & 255) / 255.f, a};
}

inline Color alpha(Color c, float a) {
    c.a *= a;
    return c;
}

inline Color mix(Color x, Color y, float t) {
    return {x.r + (y.r - x.r) * t, x.g + (y.g - x.g) * t, x.b + (y.b - x.b) * t, x.a + (y.a - x.a) * t};
}

inline Color lighten(Color c, float t) { return mix(c, {1, 1, 1, c.a}, t); }
inline Color darken(Color c, float t) { return mix(c, {0, 0, 0, c.a}, t); }

struct Rect {
    float x = 0, y = 0, w = 0, h = 0;
    bool contains(float px, float py) const { return px >= x && py >= y && px < x + w && py < y + h; }
    Rect inset(float d) const { return {x + d, y + d, w - 2 * d, h - 2 * d}; }
    Rect inset(float dx, float dy) const { return {x + dx, y + dy, w - 2 * dx, h - 2 * dy}; }
    float r() const { return x + w; }
    float b() const { return y + h; }
    float cx() const { return x + w * 0.5f; }
    float cy() const { return y + h * 0.5f; }
    Rect left(float v) const { return {x, y, v, h}; }
    Rect right(float v) const { return {x + w - v, y, v, h}; }
    Rect top(float v) const { return {x, y, w, v}; }
    Rect bottom(float v) const { return {x, y + h - v, w, v}; }
    Rect cutLeft(float v) const { return {x + v, y, w - v, h}; }
    Rect cutRight(float v) const { return {x, y, w - v, h}; }
    Rect cutTop(float v) const { return {x, y + v, w, h - v}; }
    Rect cutBottom(float v) const { return {x, y, w, h - v}; }
    Rect centered(float cw, float ch) const { return {x + (w - cw) * 0.5f, y + (h - ch) * 0.5f, cw, ch}; }
    Rect intersect(const Rect& o) const {
        float nx = std::max(x, o.x), ny = std::max(y, o.y);
        float nr = std::min(r(), o.r()), nb = std::min(b(), o.b());
        return {nx, ny, std::max(0.f, nr - nx), std::max(0.f, nb - ny)};
    }
};

inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

inline float approach(float v, float target, float step) {
    if (v < target) return std::min(target, v + step);
    return std::max(target, v - step);
}

inline float easeOut(float t) {
    t = clampf(t, 0, 1);
    return 1 - (1 - t) * (1 - t) * (1 - t);
}

}
