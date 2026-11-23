#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "../core/Geometry.h"

namespace wd {

struct Ui;

class TouchKeyboard {
public:
    void draw(Ui& ui, const Rect& r, const std::function<void(uint32_t)>& onChar,
              const std::function<void(int)>& onKey, bool& close);

private:
    enum Action { ActChar, ActBack, ActEnter, ActShift, ActSymbols, ActAccents, ActSpace, ActLeft, ActRight, ActHide };
    struct K {
        std::string label;
        uint32_t cp;
        Action act;
        float w;
    };
    std::vector<std::vector<K>> layout() const;
    uint32_t upper(uint32_t cp) const;

    int layer_ = 0;
    bool shift_ = false;
    bool caps_ = false;
    double lastShift_ = -10;
    double repeatT_ = 0;
    int repeatKey_ = -1;
};

}
