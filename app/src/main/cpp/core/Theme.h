#pragma once
#include "Geometry.h"

namespace wd {

struct Theme {
    bool dark = false;
    Color accent;
    Color accentText;
    Color mica;
    Color surface;
    Color card;
    Color text;
    Color textDim;
    Color border;
    Color taskbar;
    Color flyout;
    Color hover;
    Color press;
    Color selection;
    Color control;
    Color shadow;
    Color danger;

    void build(bool isDark, Color acc) {
        dark = isDark;
        accent = acc;
        accentText = {1, 1, 1, 1};
        danger = rgb(0xC42B1C);
        if (dark) {
            mica = rgb(0x202020);
            surface = rgb(0x272727);
            card = rgb(0x2D2D2D);
            text = rgb(0xFFFFFF);
            textDim = rgb(0xC5C5C5);
            border = rgb(0x3D3D3D);
            taskbar = rgb(0x1C1C1C, 0.94f);
            flyout = rgb(0x2B2B2B, 0.98f);
            hover = rgb(0xFFFFFF, 0.06f);
            press = rgb(0xFFFFFF, 0.10f);
            selection = alpha(lighten(acc, 0.2f), 0.35f);
            control = rgb(0x3A3A3A);
            shadow = rgb(0x000000, 0.55f);
        } else {
            mica = rgb(0xF3F3F3);
            surface = rgb(0xFAFAFA);
            card = rgb(0xFFFFFF);
            text = rgb(0x1B1B1B);
            textDim = rgb(0x606060);
            border = rgb(0xE0E0E0);
            taskbar = rgb(0xF0F0F0, 0.94f);
            flyout = rgb(0xF9F9F9, 0.98f);
            hover = rgb(0x000000, 0.04f);
            press = rgb(0x000000, 0.08f);
            selection = alpha(acc, 0.18f);
            control = rgb(0xFFFFFF);
            shadow = rgb(0x000000, 0.28f);
        }
    }
};

}
