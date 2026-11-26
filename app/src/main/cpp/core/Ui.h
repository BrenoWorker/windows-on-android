#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "Geometry.h"

namespace wd {

class Gfx;
struct Theme;

enum Key {
    KeyNone = 0,
    KeyBackspace,
    KeyDelete,
    KeyEnter,
    KeyLeft,
    KeyRight,
    KeyUp,
    KeyDown,
    KeyHome,
    KeyEnd,
    KeyTab,
    KeyEscape
};

struct LineEdit {
    std::string text;
    size_t caret = 0;
    float scroll = 0;
    void set(const std::string& s) {
        text = s;
        caret = s.size();
        scroll = 0;
    }
    bool apply(const std::vector<uint32_t>& chars, const std::vector<int>& keys);
};

struct Ui {
    Gfx* g = nullptr;
    Theme* th = nullptr;
    bool active = false;
    bool focus = false;
    bool down = false;
    bool pressed = false;
    bool released = false;
    bool dragged = false;
    bool longPress = false;
    bool longFired = false;
    bool requestKeyboard = false;
    float x = 0, y = 0, px = 0, py = 0, downX = 0, downY = 0;
    int taps = 1;
    double time = 0;
    float dt = 0;
    std::vector<uint32_t> chars;
    std::vector<int> keys;

    float dx() const { return active ? x - px : 0; }
    float dy() const { return active ? y - py : 0; }
    bool clicked(const Rect& r) const;
    bool doubleClicked(const Rect& r) const { return clicked(r) && taps >= 2; }
    bool held(const Rect& r) const;
    bool pressedIn(const Rect& r) const;
    bool dragging(const Rect& r) const;
    bool longPressed(const Rect& r) const;
    bool startedIn(const Rect& r) const { return active && r.contains(downX, downY); }

    void pressFill(const Rect& r, float radius, bool selected = false);
    bool button(const Rect& r, const std::string& label, bool primary = false, bool enabled = true);
    bool flatButton(const Rect& r, const std::string& label, float size = 13, bool selected = false);
    bool toggle(const Rect& r, bool& v);
    bool slider(const Rect& r, float& v);
    bool radio(const Rect& r, const std::string& label, bool selected);
    bool lineEdit(const Rect& r, LineEdit& e, bool& focused, const std::string& placeholder, float size = 13, float indent = 0,
                  float radius = 4);
};

}
