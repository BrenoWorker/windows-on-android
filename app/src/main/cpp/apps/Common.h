#pragma once
#include <string>
#include <vector>

#include "../core/Gfx.h"
#include "../core/Theme.h"
#include "../core/Ui.h"
#include "../core/Utf8.h"
#include "../shell/Icons.h"

namespace wd {

inline std::vector<std::string> wrapText(Gfx& g, float size, const std::string& text, float width, int font = 0) {
    std::vector<std::string> out;
    size_t p = 0;
    while (p <= text.size()) {
        size_t nl = text.find('\n', p);
        if (nl == std::string::npos) nl = text.size();
        std::string para = text.substr(p, nl - p);
        std::string line;
        size_t i = 0;
        while (i < para.size()) {
            size_t sp = para.find(' ', i);
            if (sp == std::string::npos) sp = para.size();
            std::string word = para.substr(i, sp - i);
            std::string cand = line.empty() ? word : line + " " + word;
            if (!line.empty() && g.textWidth(size, cand, font) > width) {
                out.push_back(line);
                line = word;
            } else {
                line = cand;
            }
            i = sp + 1;
        }
        out.push_back(line);
        p = nl + 1;
    }
    return out;
}

inline void card(Gfx& g, const Theme& th, const Rect& r, float radius = 6) {
    g.rect(r, th.dark ? rgb(0x2B2B2B) : rgb(0xFFFFFF, 0.85f), radius);
    g.stroke(r, th.dark ? rgb(0x1D1D1D) : rgb(0xE5E5E5), radius, 1);
}

inline bool toolButton(Ui& ui, const Rect& r, IconId icon, const std::string& label, bool showLabel = true,
                       bool selected = false, bool enabled = true) {
    Gfx& g = *ui.g;
    const Theme& th = *ui.th;
    if (selected) g.rect(r, th.press, 4);
    if (enabled && ui.held(r)) g.rect(r, th.press, 4);
    Color c = enabled ? th.text : th.textDim;
    if (showLabel && !label.empty()) {
        drawIcon(g, icon, {r.x + 8, r.cy() - 8, 16, 16}, c);
        g.textIn({r.x + 30, r.y, r.w - 32, r.h}, 12, label, c);
    } else {
        drawIcon(g, icon, r.centered(16, 16), c);
    }
    return enabled && ui.clicked(r);
}

inline float toolButtonWidth(Gfx& g, const std::string& label, bool showLabel) {
    return showLabel ? g.textWidth(12, label) + 42 : 34;
}

}
