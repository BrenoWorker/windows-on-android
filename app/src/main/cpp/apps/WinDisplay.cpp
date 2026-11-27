#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "../core/XClient.h"
#include "Apps.h"
#include "Common.h"

namespace wd {

class WinDisplayWindow : public AppWindow {
public:
    explicit WinDisplayWindow(Shell& s) {
        title = "Windows";
        prefW = 980;
        prefH = 620;
        minW = 360;
        minH = 260;
        icon = IcDialog;
    }

    ~WinDisplayWindow() override {
        if (tex_ && shell) shell->gfx().deleteTexture(tex_);
        x_.close();
        if (shell) {
            shell->sys("wine.stop");
            syncWindowsApps(*shell);
        }
    }

    bool wantsText() const override { return x_.connected(); }

    bool launchProgram(const std::string& exe, const std::string& args) override {
        pending_.push_back({exe, args});
        return true;
    }

    bool onBack() override {
        if (!x_.connected()) return false;
        x_.key(0xFF1B, true);
        x_.key(0xFF1B, false);
        x_.flush();
        return true;
    }

    void draw(Ui& ui, const Rect& c) override {
        Gfx& g = *ui.g;
        const Theme& th = *ui.th;
        if (!started_) {
            started_ = true;
            Rect wa = shell->workArea();
            int w = 1280;
            int h = (int)std::lround(1280.0 * wa.h / std::max(1.f, wa.w));
            h = std::max(600, std::min(h, 960));
            shell->sys("wine.session", std::to_string(w) + "x" + std::to_string(h));
            pollT_ = -1;
        }
        poll();

        Rect bar{c.x, c.y, c.w, 40};
        g.rect(bar, th.dark ? rgb(0x202020) : rgb(0xF3F3F3));
        g.rect({bar.x, bar.b() - 1, bar.w, 1}, th.border);
        float bx = bar.x + 6;
        bool live = x_.connected();
        if (toolButton(ui, {bx, bar.y + 4, 32, 32}, IcGlyphKeyboard, "", false, false, live)) ui.requestKeyboard = true;
        bx += 34;
        if (toolButton(ui, {bx, bar.y + 4, 92, 32}, IcGlyphGrid, "Right click", c.w > 520, rightMode_, live)) rightMode_ = !rightMode_;
        bx += c.w > 520 ? 96 : 34;
        if (ui.button({bx, bar.y + 6, 48, 28}, "Esc", false, live)) {
            x_.key(0xFF1B, true);
            x_.key(0xFF1B, false);
        }
        bx += 52;
        if (ui.button({bx, bar.y + 6, 60, 28}, "Enter", false, live)) {
            x_.key(0xFF0D, true);
            x_.key(0xFF0D, false);
        }
        std::string status = state_ == "ready" ? (running_ > 0 ? std::to_string(running_) + (running_ == 1 ? " app running" : " apps running") : "Ready")
                             : state_ == "failed" ? "Couldn't start"
                             : state_ == "setup"  ? "Setting up Windows..."
                             : state_ == "off"    ? "Stopped"
                                                  : "Starting Windows...";
        float sw = g.textWidth(12, status);
        if (bar.r() - 90 - sw > bx + 70) g.textIn({bar.r() - 96 - sw, bar.y, sw, bar.h}, 12, status, th.textDim);
        if (ui.button({bar.r() - 82, bar.y + 6, 74, 28}, "Shut down")) {
            shell->close(this);
            return;
        }

        Rect area{c.x, bar.b(), c.w, c.h - bar.h};
        g.rect(area, rgb(0x000000));
        if (state_ != "ready" || !tex_) {
            drawWaiting(g, th, area);
            return;
        }
        float k = std::min(area.w / fbW_, area.h / fbH_);
        Rect img{area.x + (area.w - fbW_ * k) * 0.5f, area.y + (area.h - fbH_ * k) * 0.5f, fbW_ * k, fbH_ * k};
        g.image(tex_, img);
        pointer(ui, img, k);
        keyboard(ui);
        x_.flush();
        if (running_ == 0 && launched_ > 0) {
            Rect tip{img.cx() - 150, img.cy() - 40, 300, 80};
            g.rect(tip, rgb(0x000000, 0.6f), 8);
            g.textIn({tip.x, tip.y + 10, tip.w, 30}, 13, "No programs are running", rgb(0xFFFFFF), AlignCenter);
            g.textIn({tip.x, tip.y + 40, tip.w, 24}, 11.5f, "Open an .exe from File Explorer or Start", rgb(0xBBBBBB), AlignCenter);
        }
    }

private:
    struct Launch {
        std::string exe, args;
    };
    XClient x_;
    XFrame frame_;
    std::vector<unsigned char> px_;
    GLuint tex_ = 0;
    int texW_ = 0, texH_ = 0;
    float fbW_ = 1, fbH_ = 1;
    bool started_ = false;
    std::string state_ = "starting";
    std::string socket_, fb_;
    double pollT_ = -1, connectT_ = -1, frameT_ = -1, jobsT_ = -1, syncT_ = -1;
    std::vector<Launch> pending_;
    std::vector<int> jobs_;
    int running_ = 0;
    int launched_ = 0;
    bool rightMode_ = false;
    bool downIn_ = false;
    bool dragging_ = false;
    int lastX_ = -1, lastY_ = -1;

    static std::vector<std::string> split(const std::string& s) {
        std::vector<std::string> out;
        size_t i = 0;
        while (true) {
            size_t j = s.find('\t', i);
            out.push_back(s.substr(i, j == std::string::npos ? std::string::npos : j - i));
            if (j == std::string::npos) break;
            i = j + 1;
        }
        return out;
    }

    void poll() {
        double now = shell->now();
        if (now - pollT_ > (state_ == "ready" ? 1.0 : 0.25)) {
            pollT_ = now;
            auto f = split(shell->sys("wine.display"));
            if (f.size() >= 5) {
                state_ = f[0];
                socket_ = f[1];
                fb_ = f[2];
            }
        }
        if (state_ != "ready" && x_.connected()) x_.close();
        if (state_ == "ready" && !x_.connected() && now - connectT_ > 0.5) {
            connectT_ = now;
            x_.connect(socket_);
        }
        bool front = shell->focused() == this;
        if (state_ == "ready" && now - frameT_ > (front ? 1.0 / 30 : 1.0 / 10)) {
            frameT_ = now;
            if (frame_.read(fb_, px_)) {
                if (tex_ && (texW_ != frame_.width || texH_ != frame_.height)) {
                    shell->gfx().deleteTexture(tex_);
                    tex_ = 0;
                }
                if (!tex_)
                    tex_ = shell->gfx().createTexture(frame_.width, frame_.height, px_.data());
                else
                    shell->gfx().updateTexture(tex_, frame_.width, frame_.height, px_.data());
                texW_ = fbW_ = frame_.width;
                texH_ = fbH_ = frame_.height;
            }
        }
        if (state_ == "ready" && !pending_.empty()) {
            for (auto& l : pending_) {
                std::string id = shell->sys("exe.run", l.exe + "\t" + l.args + "\t1");
                int j = atoi(id.c_str());
                if (j > 0) {
                    jobs_.push_back(j);
                    launched_++;
                }
            }
            pending_.clear();
        }
        if (now - jobsT_ > 0.5) {
            jobsT_ = now;
            int alive = 0;
            std::vector<int> keep;
            for (int j : jobs_) {
                std::string r = shell->sys("exe.poll", std::to_string(j));
                size_t t1 = r.find('\t');
                size_t t2 = t1 == std::string::npos ? std::string::npos : r.find('\t', t1 + 1);
                bool hasText = t2 != std::string::npos && t2 + 1 < r.size();
                if ((!r.empty() && r[0] == '1') || hasText) keep.push_back(j);
            }
            bool finished = keep.size() < jobs_.size();
            jobs_ = keep;
            alive = atoi(shell->sys("wine.running").c_str());
            if (finished || alive != running_) syncT_ = -1;
            running_ = alive;
        }
        if (state_ == "ready" && now - syncT_ > 5.0) {
            syncT_ = now;
            syncWindowsApps(*shell);
        }
    }

    void drawWaiting(Gfx& g, const Theme& th, const Rect& area) {
        if (state_ == "failed") {
            g.textIn({area.x, area.cy() - 30, area.w, 24}, 15, "Windows couldn't start", rgb(0xFFFFFF), AlignCenter);
            g.textIn({area.x, area.cy() + 2, area.w, 20}, 12, "Check Settings > Apps for details, then try again.", rgb(0xAAAAAA), AlignCenter);
            return;
        }
        float a = (float)shell->now() * 5.f;
        g.arc(area.cx(), area.cy() - 20, 16, a, a + 4.2f, 3, th.accent);
        bool setup = state_ == "setup";
        g.textIn({area.x, area.cy() + 12, area.w, 22}, 13, setup ? "Setting up Windows for the first time..." : "Starting Windows...",
                 rgb(0xFFFFFF), AlignCenter);
        if (setup) g.textIn({area.x, area.cy() + 36, area.w, 20}, 11.5f, "This can take a few minutes", rgb(0xAAAAAA), AlignCenter);
    }

    void map(const Rect& img, float k, float ux, float uy, int& x, int& y) {
        x = (int)std::lround(clampf((ux - img.x) / k, 0, fbW_ - 1));
        y = (int)std::lround(clampf((uy - img.y) / k, 0, fbH_ - 1));
    }

    void pointer(Ui& ui, const Rect& img, float k) {
        if (!ui.active || !x_.connected()) return;
        int x, y;
        if (ui.pressed && img.contains(ui.x, ui.y)) {
            downIn_ = true;
            dragging_ = false;
        }
        if (!downIn_) return;
        if (ui.down && ui.dragged && !dragging_ && !ui.longFired) {
            dragging_ = true;
            map(img, k, ui.downX, ui.downY, x, y);
            x_.motion(x, y);
            x_.button(1, true);
        }
        if (dragging_ && ui.down) {
            map(img, k, ui.x, ui.y, x, y);
            if (x != lastX_ || y != lastY_) x_.motion(x, y);
            lastX_ = x;
            lastY_ = y;
        }
        if (ui.longPress) {
            map(img, k, ui.downX, ui.downY, x, y);
            x_.motion(x, y);
            x_.button(3, true);
            x_.button(3, false);
        }
        if (ui.released) {
            map(img, k, ui.x, ui.y, x, y);
            if (dragging_) {
                x_.motion(x, y);
                x_.button(1, false);
            } else if (!ui.longFired) {
                int b = rightMode_ ? 3 : 1;
                x_.motion(x, y);
                x_.button(b, true);
                x_.button(b, false);
                rightMode_ = false;
            }
            downIn_ = false;
            dragging_ = false;
        }
    }

    void keyboard(Ui& ui) {
        if (!ui.focus || !x_.connected()) return;
        for (uint32_t ch : ui.chars)
            if (ch >= 32) x_.type(ch);
        for (int key : ui.keys) {
            uint32_t sym = 0;
            switch (key) {
                case KeyBackspace: sym = 0xFF08; break;
                case KeyDelete: sym = 0xFFFF; break;
                case KeyEnter: sym = 0xFF0D; break;
                case KeyLeft: sym = 0xFF51; break;
                case KeyUp: sym = 0xFF52; break;
                case KeyRight: sym = 0xFF53; break;
                case KeyDown: sym = 0xFF54; break;
                case KeyHome: sym = 0xFF50; break;
                case KeyEnd: sym = 0xFF57; break;
                case KeyTab: sym = 0xFF09; break;
                case KeyEscape: sym = 0xFF1B; break;
                default: break;
            }
            if (sym) {
                x_.key(sym, true);
                x_.key(sym, false);
            }
        }
    }
};

std::unique_ptr<AppWindow> makeWindowsDisplay(Shell& s) { return std::make_unique<WinDisplayWindow>(s); }

}
