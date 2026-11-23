#include "TouchKeyboard.h"

#include "../core/Gfx.h"
#include "../core/Theme.h"
#include "../core/Ui.h"
#include "../core/Utf8.h"
#include "Icons.h"

namespace wd {

uint32_t TouchKeyboard::upper(uint32_t cp) const {
    if (cp >= 'a' && cp <= 'z') return cp - 32;
    if (cp >= 0xE0 && cp <= 0xFE && cp != 0xF7) return cp - 32;
    return cp;
}

std::vector<std::vector<TouchKeyboard::K>> TouchKeyboard::layout() const {
    auto row = [](const std::string& chars) {
        std::vector<K> out;
        size_t i = 0;
        while (i < chars.size()) {
            uint32_t cp = utf8Decode(chars, i);
            out.push_back({utf8From(cp), cp, ActChar, 1});
        }
        return out;
    };
    std::vector<std::vector<K>> rows;
    std::string r1, r2, r3;
    if (layer_ == 0) {
        r1 = "qwertyuiop";
        r2 = "asdfghjkl";
        r3 = "zxcvbnm,.";
    } else if (layer_ == 1) {
        r1 = "1234567890";
        r2 = "@#$%&*-+()";
        r3 = "!\"':;/?_=";
    } else {
        r1 = "\xC3\xA1\xC3\xA0\xC3\xA2\xC3\xA3\xC3\xA9\xC3\xAA\xC3\xAD\xC3\xB3\xC3\xB4\xC3\xB5";
        r2 = "\xC3\xBA\xC3\xBC\xC3\xA7\xC3\xB1\xC3\xA8\xC3\xAC\xC3\xB2\xC3\xB9\xC2\xBA";
        r3 = "\xC2\xAA\xC2\xBF\xC2\xA1\xE2\x82\xAC<>[]{";
    }
    auto a = row(r1);
    a.push_back({"", 0, ActBack, 1.5f});
    rows.push_back(a);
    auto b = row(r2);
    if (layer_ == 0) b.insert(b.begin(), {"", 0, ActChar, 0.5f});
    b.push_back({"", 0, ActEnter, layer_ == 0 ? 2.0f : 1.5f});
    rows.push_back(b);
    auto c = row(r3);
    c.insert(c.begin(), {"", 0, ActShift, 1.5f});
    c.push_back({"", 0, ActShift, 1.0f});
    rows.push_back(c);
    std::vector<K> d;
    d.push_back({layer_ == 1 ? "abc" : "&123", 0, ActSymbols, 1.5f});
    d.push_back({layer_ == 2 ? "abc" : "\xC3\xA1\xC3\xA7", 0, ActAccents, 1.2f});
    d.push_back({"", ' ', ActSpace, 5.6f});
    d.push_back({"\xE2\x86\x90", 0, ActLeft, 1.0f});
    d.push_back({"\xE2\x86\x92", 0, ActRight, 1.0f});
    d.push_back({"", 0, ActHide, 1.2f});
    rows.push_back(d);
    return rows;
}

void TouchKeyboard::draw(Ui& ui, const Rect& r, const std::function<void(uint32_t)>& onChar,
                         const std::function<void(int)>& onKey, bool& close) {
    Gfx& g = *ui.g;
    Theme& th = *ui.th;
    Color bg = th.dark ? rgb(0x202020, 0.98f) : rgb(0xE6E6E6, 0.98f);
    Color keyC = th.dark ? rgb(0x3A3A3A) : rgb(0xFFFFFF);
    Color modC = th.dark ? rgb(0x2C2C2C) : rgb(0xD3D3D3);
    g.shadow(r, 0, 14, alpha(th.shadow, 0.6f));
    g.rect(r, bg);
    g.rect({r.x, r.y, r.w, 1}, th.border);

    auto rows = layout();
    float pad = 6;
    Rect area = r.inset(pad * 2, pad);
    float rowH = area.h / (float)rows.size();
    float maxUnits = 0;
    for (auto& rw : rows) {
        float u = 0;
        for (auto& k : rw) u += k.w;
        maxUnits = std::max(maxUnits, u);
    }
    float unit = area.w / maxUnits;
    bool upperCase = shift_ || caps_;
    int idx = 0;
    for (size_t ri = 0; ri < rows.size(); ri++) {
        float units = 0;
        for (auto& k : rows[ri]) units += k.w;
        float x = area.x + (area.w - units * unit) * 0.5f;
        float y = area.y + rowH * ri;
        for (auto& k : rows[ri]) {
            Rect kr{x + 3, y + 3, k.w * unit - 6, rowH - 6};
            x += k.w * unit;
            idx++;
            if (k.act == ActChar && k.cp == 0) continue;
            bool mod = k.act != ActChar && k.act != ActSpace;
            Color c = mod ? modC : keyC;
            if (k.act == ActEnter) c = th.accent;
            if (k.act == ActShift && (shift_ || caps_)) c = th.accent;
            if (ui.held(kr)) c = k.act == ActEnter ? darken(th.accent, 0.2f) : mix(c, th.text, 0.18f);
            g.rect(kr, c, 5);
            Color tc = (k.act == ActEnter || (k.act == ActShift && (shift_ || caps_))) ? th.accentText : th.text;
            Rect ic = kr.centered(std::min(kr.h * 0.5f, 18.f), std::min(kr.h * 0.5f, 18.f));
            switch (k.act) {
                case ActChar: {
                    uint32_t cp = upperCase ? upper(k.cp) : k.cp;
                    g.textIn(kr, std::min(20.f, kr.h * 0.42f), utf8From(cp), tc, AlignCenter);
                    break;
                }
                case ActBack: drawIcon(g, IcGlyphBackspace, ic, tc); break;
                case ActEnter: drawIcon(g, IcGlyphEnter, ic, tc); break;
                case ActShift: drawIcon(g, IcGlyphShift, ic, tc); break;
                case ActHide: drawIcon(g, IcGlyphKeyboard, ic, tc); break;
                case ActLeft: drawIcon(g, IcGlyphBack, ic.centered(ic.w * 0.8f, ic.h * 0.8f), tc); break;
                case ActRight: drawIcon(g, IcGlyphForward, ic.centered(ic.w * 0.8f, ic.h * 0.8f), tc); break;
                case ActSpace: break;
                default: g.textIn(kr, 14, k.label, tc, AlignCenter); break;
            }

            bool fire = ui.clicked(kr);
            if (k.act == ActBack && ui.held(kr)) {
                if (repeatKey_ != idx) {
                    repeatKey_ = idx;
                    repeatT_ = ui.time + 0.45;
                } else if (ui.time > repeatT_) {
                    repeatT_ = ui.time + 0.06;
                    onKey(KeyBackspace);
                }
            }
            if (!fire) continue;
            switch (k.act) {
                case ActChar:
                    onChar(upperCase ? upper(k.cp) : k.cp);
                    if (shift_ && !caps_) shift_ = false;
                    break;
                case ActSpace: onChar(' '); break;
                case ActBack: onKey(KeyBackspace); break;
                case ActEnter: onKey(KeyEnter); break;
                case ActLeft: onKey(KeyLeft); break;
                case ActRight: onKey(KeyRight); break;
                case ActShift:
                    if (caps_) {
                        caps_ = false;
                        shift_ = false;
                    } else if (ui.time - lastShift_ < 0.35) {
                        caps_ = true;
                        shift_ = false;
                    } else {
                        shift_ = !shift_;
                    }
                    lastShift_ = ui.time;
                    break;
                case ActSymbols: layer_ = layer_ == 1 ? 0 : 1; break;
                case ActAccents: layer_ = layer_ == 2 ? 0 : 2; break;
                case ActHide: close = true; break;
            }
        }
    }
    if (!ui.active || !ui.down) repeatKey_ = -1;
}

}
