#include "Ui.h"

#include <cmath>

#include "Gfx.h"
#include "Theme.h"
#include "Utf8.h"

namespace wd {

bool LineEdit::apply(const std::vector<uint32_t>& chars, const std::vector<int>& keys) {
    if (caret > text.size()) caret = text.size();
    for (uint32_t c : chars) {
        if (c < 32 || c == 127) continue;
        std::string s = utf8From(c);
        text.insert(caret, s);
        caret += s.size();
    }
    bool enter = false;
    for (int k : keys) {
        switch (k) {
            case KeyBackspace:
                if (caret > 0) {
                    size_t p = utf8Prev(text, caret);
                    text.erase(p, caret - p);
                    caret = p;
                }
                break;
            case KeyDelete:
                if (caret < text.size()) text.erase(caret, utf8Next(text, caret) - caret);
                break;
            case KeyLeft: caret = utf8Prev(text, caret); break;
            case KeyRight: caret = utf8Next(text, caret); break;
            case KeyHome: caret = 0; break;
            case KeyEnd: caret = text.size(); break;
            case KeyEnter: enter = true; break;
            default: break;
        }
    }
    return enter;
}

bool Ui::clicked(const Rect& r) const {
    return active && released && !dragged && !longFired && r.contains(x, y) && r.contains(downX, downY);
}

bool Ui::held(const Rect& r) const {
    return active && down && !dragged && r.contains(downX, downY) && r.contains(x, y);
}

bool Ui::pressedIn(const Rect& r) const { return active && pressed && r.contains(x, y); }

bool Ui::dragging(const Rect& r) const { return active && down && dragged && r.contains(downX, downY); }

bool Ui::longPressed(const Rect& r) const { return active && longPress && r.contains(x, y); }

void Ui::pressFill(const Rect& r, float radius, bool selected) {
    if (selected) g->rect(r, th->selection, radius);
    if (held(r)) g->rect(r, th->press, radius);
}

bool Ui::button(const Rect& r, const std::string& label, bool primary, bool enabled) {
    bool h = enabled && held(r);
    if (primary) {
        Color c = enabled ? th->accent : alpha(th->accent, 0.45f);
        if (h) c = darken(c, 0.15f);
        g->rect(r, c, 4);
        g->textIn(r, 13, label, th->accentText, AlignCenter);
    } else {
        g->rect(r, th->border, 4);
        Color c = th->control;
        if (h) c = mix(c, th->text, 0.06f);
        g->rect(r.inset(1), c, 3.5f);
        g->textIn(r, 13, label, enabled ? th->text : th->textDim, AlignCenter);
    }
    return enabled && clicked(r);
}

bool Ui::flatButton(const Rect& r, const std::string& label, float size, bool selected) {
    pressFill(r, 4, selected);
    g->textIn(r, size, label, th->text, AlignCenter);
    return clicked(r);
}

bool Ui::toggle(const Rect& r, bool& v) {
    Rect t{r.x, r.cy() - 10, 40, 20};
    bool c = clicked(r);
    if (c) v = !v;
    if (v) {
        g->rect(t, held(r) ? darken(th->accent, 0.15f) : th->accent, 10);
        g->circle(t.r() - 10, t.cy(), 6, th->accentText);
    } else {
        g->rect(t, th->dark ? rgb(0x2A2A2A) : rgb(0xF5F5F5), 10);
        g->stroke(t, th->textDim, 10, 1);
        g->circle(t.x + 10, t.cy(), 5, th->textDim);
    }
    return c;
}

bool Ui::slider(const Rect& r, float& v) {
    bool changed = false;
    if (active && down && r.contains(downX, downY)) {
        float nv = clampf((x - r.x) / r.w, 0, 1);
        if (nv != v) {
            v = nv;
            changed = true;
        }
    }
    float cy = r.cy();
    g->rect({r.x, cy - 2, r.w, 4}, th->dark ? rgb(0x9A9A9A) : rgb(0x8A8A8A), 2);
    g->rect({r.x, cy - 2, r.w * v, 4}, th->accent, 2);
    float kx = r.x + r.w * v;
    g->circle(kx, cy, 10, th->dark ? rgb(0x454545) : rgb(0xFFFFFF));
    g->ring(kx, cy, 10, 1, th->border);
    g->circle(kx, cy, held(r) || (active && down && r.contains(downX, downY)) ? 5 : 6, th->accent);
    return changed;
}

bool Ui::radio(const Rect& r, const std::string& label, bool selected) {
    pressFill(r, 4);
    float cx = r.x + 14, cy = r.cy();
    if (selected) {
        g->circle(cx, cy, 10, th->accent);
        g->circle(cx, cy, 4.5f, th->accentText);
    } else {
        g->ring(cx, cy, 10, 1, th->textDim);
    }
    g->textIn({r.x + 32, r.y, r.w - 32, r.h}, 13, label, th->text);
    return clicked(r);
}

bool Ui::lineEdit(const Rect& r, LineEdit& e, bool& focused, const std::string& placeholder, float size, float indent) {
    if (clicked(r)) {
        focused = true;
        requestKeyboard = true;
        e.caret = g->hitIndex(size, e.text, x - (r.x + 10 + indent) + e.scroll);
    }
    bool enter = false;
    if (focused && focus) enter = e.apply(chars, keys);
    if (e.caret > e.text.size()) e.caret = e.text.size();

    g->rect(r, th->border, 4);
    g->rect(r.inset(1), focused ? (th->dark ? rgb(0x1F1F1F) : rgb(0xFFFFFF)) : th->control, 3.5f);
    if (focused) g->rect({r.x + 1, r.b() - 2, r.w - 2, 2}, th->accent, 1);

    Rect in{r.x + 10 + indent, r.y, r.w - 20 - indent, r.h};
    float caretX = g->textWidth(size, e.text.substr(0, e.caret));
    if (caretX - e.scroll > in.w - 4) e.scroll = caretX - in.w + 4;
    if (caretX - e.scroll < 0) e.scroll = caretX;
    if (e.scroll < 0) e.scroll = 0;

    g->pushClip(in);
    float lh = g->lineHeight(size);
    float ty = r.y + (r.h - lh) * 0.5f;
    if (e.text.empty() && !placeholder.empty()) g->text(in.x, ty, size, placeholder, th->textDim);
    g->text(in.x - e.scroll, ty, size, e.text, th->text);
    if (focused && std::fmod(time, 1.0) < 0.6) g->rect({in.x - e.scroll + caretX, ty, 1.2f, lh}, th->text);
    g->popClip();
    return enter;
}

}
