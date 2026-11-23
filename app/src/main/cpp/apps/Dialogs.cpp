#include "Apps.h"
#include "Common.h"

namespace wd {

class PromptWindow : public AppWindow {
public:
    PromptWindow(const std::string& t, const std::string& m, const std::string& init,
                 std::function<void(const std::string&)> cb)
        : message_(m), ok_(std::move(cb)) {
        title = t;
        edit_.set(init);
        size_t dot = init.find_last_of('.');
        if (dot != std::string::npos && dot > 0) edit_.caret = dot;
        prefW = 400;
        prefH = 200;
        minW = 300;
        minH = 180;
        resizable = false;
    }
    bool wantsText() const override { return true; }
    void draw(Ui& ui, const Rect& c) override {
        Gfx& g = *ui.g;
        const Theme& th = *ui.th;
        g.rect(c, th.mica);
        g.text(c.x + 20, c.y + 12, 13, message_, th.text);
        bool enter = ui.lineEdit({c.x + 20, c.y + 40, c.w - 40, 34}, edit_, focus_, "");
        Rect bar{c.x, c.b() - 64, c.w, 64};
        g.rect(bar, th.dark ? rgb(0x202020) : rgb(0xF0F0F0));
        g.rect({bar.x, bar.y, bar.w, 1}, th.border);
        float bw = (c.w - 52) / 2;
        Rect okR{c.x + 20, bar.y + 16, bw, 32};
        Rect cancelR{okR.r() + 12, bar.y + 16, bw, 32};
        if (ui.button(okR, "OK", true) || enter) {
            if (ok_) ok_(edit_.text);
            shell->close(this);
        }
        if (ui.button(cancelR, "Cancel")) shell->close(this);
    }

private:
    std::string message_;
    std::function<void(const std::string&)> ok_;
    LineEdit edit_;
    bool focus_ = true;
};

class MessageWindow : public AppWindow {
public:
    MessageWindow(const std::string& t, const std::string& text) : text_(text) {
        title = t;
        prefW = 380;
        prefH = 190;
        resizable = false;
    }
    void draw(Ui& ui, const Rect& c) override {
        Gfx& g = *ui.g;
        const Theme& th = *ui.th;
        g.rect(c, th.mica);
        auto lines = wrapText(g, 13, text_, c.w - 40);
        float y = c.y + 14;
        for (auto& l : lines) {
            g.text(c.x + 20, y, 13, l, th.text);
            y += 19;
        }
        Rect bar{c.x, c.b() - 60, c.w, 60};
        g.rect(bar, th.dark ? rgb(0x202020) : rgb(0xF0F0F0));
        g.rect({bar.x, bar.y, bar.w, 1}, th.border);
        if (ui.button({c.r() - 140, bar.y + 14, 120, 32}, "OK", true)) shell->close(this);
        for (int k : ui.keys)
            if (ui.focus && k == KeyEnter) shell->close(this);
    }

private:
    std::string text_;
};

std::unique_ptr<AppWindow> makePrompt(Shell&, const std::string& title, const std::string& message,
                                      const std::string& initial, std::function<void(const std::string&)> ok) {
    return std::make_unique<PromptWindow>(title, message, initial, std::move(ok));
}

std::unique_ptr<AppWindow> makeMessage(Shell&, const std::string& title, const std::string& text) {
    return std::make_unique<MessageWindow>(title, text);
}

}
