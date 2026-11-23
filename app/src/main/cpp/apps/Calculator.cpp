#include <cmath>
#include <cstdio>

#include "Apps.h"
#include "Common.h"

namespace wd {

class CalculatorWindow : public AppWindow {
public:
    CalculatorWindow() {
        prefW = 330;
        prefH = 520;
        minW = 260;
        minH = 380;
    }

    void draw(Ui& ui, const Rect& c) override {
        Gfx& g = *ui.g;
        const Theme& th = *ui.th;
        g.rect(c, th.mica);
        if (ui.focus) {
            for (uint32_t ch : ui.chars) {
                if (ch >= '0' && ch <= '9') digit((char)ch);
                if (ch == '.' || ch == ',') dot();
                if (ch == '+') op('+');
                if (ch == '-') op('-');
                if (ch == '*' || ch == 'x') op('*');
                if (ch == '/') op('/');
                if (ch == '=') equals();
                if (ch == '%') percent();
            }
            for (int k : ui.keys) {
                if (k == KeyEnter) equals();
                if (k == KeyBackspace) backspace();
                if (k == KeyDelete) clearEntry();
            }
        }

        g.textBold(c.x + 16, c.y + 8, 18, "Standard", th.text);
        float dispTop = c.y + 40;
        float gridH = std::min(c.h * 0.66f, (c.h - 40) - 90);
        Rect disp{c.x + 12, dispTop, c.w - 24, c.b() - gridH - dispTop - 6};
        g.textIn({disp.x, disp.y, disp.w - 4, 22}, 13, g.ellipsize(13, expr_, disp.w), th.textDim, AlignRight);
        std::string shown = error_.empty() ? group(entry_) : error_;
        float size = std::min(46.f, disp.h - 26);
        while (size > 14 && g.textWidth(size, shown) > disp.w - 8) size -= 2;
        g.textIn({disp.x, disp.y + 22, disp.w - 4, disp.h - 22}, size, shown, th.text, AlignRight);

        const char* labels[6][4] = {{"%", "CE", "C", "\x08"},
                                    {"\xC2\xB9/x", "x\xC2\xB2", "\xE2\x88\x9Ax", "\xC3\xB7"},
                                    {"7", "8", "9", "\xC3\x97"},
                                    {"4", "5", "6", "\xE2\x88\x92"},
                                    {"1", "2", "3", "+"},
                                    {"+/\xE2\x88\x92", "0", ".", "="}};
        Rect grid{c.x + 4, c.b() - gridH - 4, c.w - 8, gridH};
        float bw = grid.w / 4, bh = grid.h / 6;
        for (int r = 0; r < 6; r++)
            for (int col = 0; col < 4; col++) {
                Rect b{grid.x + col * bw + 2, grid.y + r * bh + 2, bw - 4, bh - 4};
                std::string l = labels[r][col];
                bool isNum = r >= 2 && col < 3;
                bool isEq = r == 5 && col == 3;
                Color bg = isEq ? th.accent : (isNum ? (th.dark ? rgb(0x3B3B3B) : rgb(0xFFFFFF)) : (th.dark ? rgb(0x323232) : rgb(0xF9F9F9)));
                if (ui.held(b)) bg = isEq ? darken(bg, 0.15f) : mix(bg, th.text, 0.08f);
                g.rect(b, th.dark ? rgb(0x000000, 0.2f) : rgb(0x000000, 0.06f), 5);
                g.rect({b.x, b.y, b.w, b.h - 1}, bg, 5);
                Color tc = isEq ? th.accentText : th.text;
                if (l == "\x08")
                    drawIcon(g, IcGlyphBackspace, b.centered(18, 18), tc);
                else
                    g.textIn(b, isNum ? 18 : 15, l, tc, AlignCenter);
                if (ui.clicked(b)) press(r, col);
            }
    }

private:
    std::string entry_ = "0";
    std::string expr_;
    std::string error_;
    double acc_ = 0;
    char op_ = 0;
    bool fresh_ = true;
    double lastB_ = 0;
    char lastOp_ = 0;

    static std::string fmt(double v) {
        if (std::isnan(v) || std::isinf(v)) return "Error";
        char buf[64];
        if (std::fabs(v - std::round(v)) < 1e-12 && std::fabs(v) < 1e15)
            snprintf(buf, sizeof buf, "%.0f", v);
        else
            snprintf(buf, sizeof buf, "%.12g", v);
        std::string s = buf;
        if (s == "-0") s = "0";
        return s;
    }

    static std::string group(const std::string& s) {
        size_t start = (s[0] == '-') ? 1 : 0;
        size_t dotPos = s.find('.');
        if (s.find('e') != std::string::npos) return s;
        size_t intEnd = dotPos == std::string::npos ? s.size() : dotPos;
        std::string out = s.substr(0, start);
        std::string ip = s.substr(start, intEnd - start);
        for (size_t i = 0; i < ip.size(); i++) {
            out += ip[i];
            size_t left = ip.size() - i - 1;
            if (left > 0 && left % 3 == 0) out += ',';
        }
        return out + s.substr(intEnd);
    }

    static const char* sym(char o) {
        switch (o) {
            case '+': return "+";
            case '-': return "\xE2\x88\x92";
            case '*': return "\xC3\x97";
            case '/': return "\xC3\xB7";
        }
        return "";
    }

    double cur() const { return atof(entry_.c_str()); }

    bool apply(double a, char o, double b, double& out) {
        switch (o) {
            case '+': out = a + b; return true;
            case '-': out = a - b; return true;
            case '*': out = a * b; return true;
            case '/':
                if (b == 0) {
                    error_ = "Cannot divide by zero";
                    return false;
                }
                out = a / b;
                return true;
        }
        out = b;
        return true;
    }

    void resetIfError() {
        if (!error_.empty()) {
            error_.clear();
            entry_ = "0";
            expr_.clear();
            op_ = 0;
            acc_ = 0;
            fresh_ = true;
        }
    }

    void digit(char d) {
        resetIfError();
        if (fresh_) {
            entry_ = std::string(1, d);
            fresh_ = false;
            if (op_ == 0) expr_.clear();
        } else if (entry_ == "0") {
            entry_ = std::string(1, d);
        } else if (entry_.size() < 16) {
            entry_ += d;
        }
    }

    void dot() {
        resetIfError();
        if (fresh_) {
            entry_ = "0.";
            fresh_ = false;
        } else if (entry_.find('.') == std::string::npos) {
            entry_ += '.';
        }
    }

    void op(char o) {
        resetIfError();
        if (op_ && !fresh_) {
            double r;
            if (!apply(acc_, op_, cur(), r)) return;
            acc_ = r;
        } else if (!op_) {
            acc_ = cur();
        }
        op_ = o;
        entry_ = fmt(acc_);
        expr_ = fmt(acc_) + " " + sym(o);
        fresh_ = true;
    }

    void equals() {
        resetIfError();
        double r;
        if (op_) {
            double b = cur();
            expr_ = fmt(acc_) + " " + sym(op_) + " " + fmt(b) + " =";
            if (!apply(acc_, op_, b, r)) return;
            lastB_ = b;
            lastOp_ = op_;
            acc_ = r;
            op_ = 0;
        } else if (lastOp_) {
            double a = cur();
            expr_ = fmt(a) + " " + sym(lastOp_) + " " + fmt(lastB_) + " =";
            if (!apply(a, lastOp_, lastB_, r)) return;
            acc_ = r;
        } else {
            expr_ = fmt(cur()) + " =";
            acc_ = cur();
        }
        entry_ = fmt(acc_);
        fresh_ = true;
    }

    void percent() {
        resetIfError();
        double v = op_ ? acc_ * cur() / 100.0 : cur() / 100.0;
        entry_ = fmt(v);
        fresh_ = true;
    }

    void unary(int kind) {
        resetIfError();
        double v = cur();
        std::string e;
        if (kind == 0) {
            if (v == 0) {
                error_ = "Cannot divide by zero";
                return;
            }
            e = "1/(" + fmt(v) + ")";
            v = 1.0 / v;
        } else if (kind == 1) {
            e = "sqr(" + fmt(v) + ")";
            v = v * v;
        } else {
            if (v < 0) {
                error_ = "Invalid input";
                return;
            }
            e = "\xE2\x88\x9A(" + fmt(v) + ")";
            v = std::sqrt(v);
        }
        expr_ = op_ ? fmt(acc_) + " " + sym(op_) + " " + e : e;
        entry_ = fmt(v);
        fresh_ = false;
    }

    void backspace() {
        resetIfError();
        if (fresh_) return;
        entry_.pop_back();
        if (entry_.empty() || entry_ == "-") entry_ = "0";
    }

    void clearEntry() {
        error_.clear();
        entry_ = "0";
        fresh_ = true;
    }

    void press(int r, int c) {
        if (r >= 2 && c < 3 && r < 5) {
            digit((char)('0' + (4 - r) * 3 + c + 1));
            return;
        }
        if (r == 5 && c == 1) digit('0');
        if (r == 5 && c == 2) dot();
        if (r == 5 && c == 3) equals();
        if (r == 5 && c == 0) {
            resetIfError();
            if (entry_ != "0") entry_ = entry_[0] == '-' ? entry_.substr(1) : "-" + entry_;
            fresh_ = false;
        }
        if (c == 3 && r >= 1 && r <= 4) {
            const char ops[] = {'/', '*', '-', '+'};
            op(ops[r - 1]);
        }
        if (r == 0) {
            if (c == 0) percent();
            if (c == 1) clearEntry();
            if (c == 2) {
                error_.clear();
                entry_ = "0";
                expr_.clear();
                op_ = 0;
                acc_ = 0;
                fresh_ = true;
                lastOp_ = 0;
            }
            if (c == 3) backspace();
        }
        if (r == 1 && c < 3) unary(c);
    }
};

std::unique_ptr<AppWindow> makeCalculator(Shell&) { return std::make_unique<CalculatorWindow>(); }

}
