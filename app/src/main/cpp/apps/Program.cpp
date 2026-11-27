#include <algorithm>
#include <cstdlib>

#include "../core/Pe.h"
#include "../shell/Programs.h"
#include "Apps.h"
#include "Common.h"

namespace wd {

class ProgramWindow : public AppWindow {
public:
    ProgramWindow(Shell& s, const std::string& exe, bool run, const std::string& args) : exe_(exe), args_(args) {
        std::string ext = fs::extension(exe);
        msi_ = ext == "msi";
        batch_ = ext == "bat" || ext == "cmd";
        if (msi_ || batch_)
            name_ = fs::stem(fs::baseName(exe));
        else {
            readPe(exe, pe_, true);
            name_ = programName(pe_, exe);
        }
        title = name_;
        prefW = run ? 720 : 520;
        prefH = run ? 440 : 400;
        minW = 360;
        minH = 260;
        runNow_ = run;
        AppWindow::icon = IcDialog;
        desktop_ = true;
    }

    ~ProgramWindow() override {
        if (tex_ && shell) shell->gfx().deleteTexture(tex_);
        if (job_ > 0 && running_ && shell) shell->sys("exe.kill", std::to_string(job_));
    }

    bool wantsText() const override { return mode_ == Mode::Console && running_; }

    void draw(Ui& ui, const Rect& c) override {
        Gfx& g = *ui.g;
        const Theme& th = *ui.th;
        if (!tex_ && pe_.iconW > 0 && !pe_.icon.empty()) tex_ = g.createTexture(pe_.iconW, pe_.iconH, pe_.icon.data());
        if (!checkedRuntime_) {
            checkedRuntime_ = true;
            std::string st = shell->sys("rt.status");
            needsWow64_ = st.size() > 3 && st[2] == '1' && st.compare(st.size() - 2, 2, "\t0") == 0;
        }
        if (runNow_) {
            runNow_ = false;
            start();
        }
        g.rect(c, th.mica);
        if (mode_ == Mode::Info)
            drawInfo(ui, c);
        else if (mode_ == Mode::NoRuntime)
            drawNoRuntime(ui, c);
        else
            drawConsole(ui, c);
    }

private:
    enum class Mode { Info, NoRuntime, Console };
    std::string exe_;
    std::string args_;
    std::string name_;
    PeInfo pe_;
    bool msi_ = false;
    bool batch_ = false;
    GLuint tex_ = 0;
    Mode mode_ = Mode::Info;
    bool runNow_ = false;
    bool desktop_ = true;
    bool needsWow64_ = false;
    bool checkedRuntime_ = false;
    bool setupNoted_ = false;
    int job_ = 0;
    bool running_ = false;
    int exit_ = 0;
    double pollT_ = 0;
    std::vector<std::string> lines_;
    std::string partial_;
    float scroll_ = 0;
    bool follow_ = true;
    LineEdit input_;
    bool inputFocus_ = true;

    void drawAppIcon(Gfx& g, const Theme& th, const Rect& r) {
        if (tex_)
            g.image(tex_, r);
        else
            drawIcon(g, IcDialog, r, th.text);
    }

    void start() {
        std::string st = shell->sys("rt.status");
        if (st.size() < 3 || st[2] != '1') {
            mode_ = Mode::NoRuntime;
            return;
        }
        if (!batch_ && (msi_ || !pe_.console)) {
            shell->runWindowsApp(exe_, args_);
            shell->close(this);
            return;
        }
        mode_ = Mode::Console;
        lines_.clear();
        partial_.clear();
        addText("Starting " + name_ + "...\n\n");
        std::string id = shell->sys("exe.run", exe_ + "\t" + args_ + "\t0");
        job_ = atoi(id.c_str());
        running_ = job_ > 0;
        if (job_ <= 0) addText("Couldn't start the program.\n");
    }

    void addText(const std::string& t) {
        for (char ch : t) {
            if (ch == '\r') continue;
            if (ch == '\n') {
                lines_.push_back(partial_);
                partial_.clear();
            } else {
                partial_ += ch;
            }
        }
        if (lines_.size() > 4000) lines_.erase(lines_.begin(), lines_.begin() + (lines_.size() - 4000));
    }

    void poll() {
        if (job_ <= 0 || !running_) return;
        if (shell->now() - pollT_ < 0.08) return;
        pollT_ = shell->now();
        std::string r = shell->sys("exe.poll", std::to_string(job_));
        size_t t1 = r.find('\t');
        size_t t2 = t1 == std::string::npos ? std::string::npos : r.find('\t', t1 + 1);
        if (t2 == std::string::npos) return;
        bool alive = r[0] == '1';
        int code = atoi(r.substr(t1 + 1, t2 - t1 - 1).c_str());
        std::string text = r.substr(t2 + 1);
        if (!setupNoted_ && text.empty() && alive && shell->sys("rt.status").find("\tsetup\t") != std::string::npos) {
            setupNoted_ = true;
            addText("Setting up Windows for the first time. This can take a few minutes...\n\n");
        }
        if (!text.empty()) addText(text);
        if (!alive && text.empty()) {
            running_ = false;
            exit_ = code;
            if (!partial_.empty()) addText("\n");
            addText("\n[Process exited with code " + std::to_string(code) + "]\n");
            syncWindowsApps(*shell);
        }
    }

    void row(Gfx& g, const Theme& th, float x, float& y, float w, const std::string& k, const std::string& v) {
        if (v.empty()) return;
        g.textIn({x, y, 110, 22}, 12, k, th.textDim);
        g.textIn({x + 116, y, w - 116, 22}, 12, g.ellipsize(12, v, w - 116), th.text);
        y += 24;
    }

    void drawInfo(Ui& ui, const Rect& c) {
        Gfx& g = *ui.g;
        const Theme& th = *ui.th;
        float x = c.x + 22, w = c.w - 44;
        drawAppIcon(g, th, {x, c.y + 20, 48, 48});
        g.textBold(x + 62, c.y + 22, 16, g.ellipsize(16, name_, w - 62), th.text);
        bool script = msi_ || batch_;
        std::string sub = pe_.company.empty() ? "Unknown publisher" : pe_.company;
        g.text(x + 62, c.y + 48, 12, g.ellipsize(12, sub, w - 62), th.textDim);
        float y = c.y + 88;
        row(g, th, x, y, w, "File", shell->displayPath(exe_));
        row(g, th, x, y, w, "Version", pe_.version);
        std::string type = !pe_.valid ? "Not a valid Windows program" : (pe_.dll ? "Library (DLL)" : (pe_.console ? "Console application" : "Windows application"));
        if (pe_.valid) type += ", " + peMachineName(pe_.machine);
        if (msi_) type = "Windows Installer package";
        if (batch_) type = "Windows batch script";
        row(g, th, x, y, w, "Type", type);
        if (!pe_.installer.empty()) row(g, th, x, y, w, "Installer", pe_.installer + " setup");
        if (pe_.dotnet) row(g, th, x, y, w, "Framework", ".NET");

        std::string warn;
        bool oldRuntime = pe_.machine == 0x14C && needsWow64_;
        if (!script && !pe_.valid) warn = "This file isn't a Windows program WinDroid can open.";
        else if (!script && !peMachineSupported(pe_.machine)) warn = "This program is built for " + peMachineName(pe_.machine) + " Windows and can't run with the x64 runtime.";
        else if (oldRuntime) warn = "The installed runtime can't run 32-bit programs. Update it in Settings > Apps.";
        if (!warn.empty()) {
            y += 6;
            auto lines = wrapText(g, 12, warn, w - 20);
            Rect box{x - 8, y, w + 16, lines.size() * 18.f + 16};
            g.rect(box, th.dark ? rgb(0x433519) : rgb(0xFFF4CE), 6);
            float ly = y + 8;
            for (auto& l : lines) {
                g.text(x + 4, ly, 12, l, th.text);
                ly += 18;
            }
            y = box.b() + 8;
        }

        bool installer = msi_ || !pe_.installer.empty();
        bool canRun = script || (pe_.valid && peMachineSupported(pe_.machine) && !pe_.dll && !oldRuntime);
        bool installed = isInstalledPath(*shell, exe_);
        bool canCopy = !installer && !batch_ && !installed;
        Rect bar{c.x, c.b() - 64, c.w, 64};
        if (canRun && canCopy && y + 30 < bar.y) {
            Rect tr{x - 4, bar.y - 40, w, 32};
            ui.toggle({tr.x, tr.y + 6, 48, 20}, desktop_);
            g.textIn({tr.x + 56, tr.y, tr.w - 56, tr.h}, 12.5f, "Create a desktop shortcut", th.text);
        }
        g.rect(bar, th.dark ? rgb(0x202020) : rgb(0xF0F0F0));
        g.rect({bar.x, bar.y, bar.w, 1}, th.border);
        float bw = std::min(140.f, (c.w - 60) / 3);
        Rect cancel{c.r() - 20 - bw, bar.y + 16, bw, 32};
        Rect runB{cancel.x - 10 - bw, bar.y + 16, bw, 32};
        Rect instB{runB.x - 10 - bw, bar.y + 16, bw, 32};
        if (ui.button(cancel, "Cancel")) shell->close(this);
        if (ui.button(runB, installer ? "Run setup" : "Run", installer || installed, canRun)) {
            prefW = 720;
            start();
            if (frame.w < 600) {
                Rect wa = shell->workArea();
                frame.w = std::min(720.f, wa.w - 16);
                frame.h = std::min(440.f, wa.h - 16);
                frame.x = std::max(wa.x + 4, std::min(frame.x, wa.r() - frame.w - 4));
            }
        }
        if (canCopy && ui.button(instB, "Install", true, canRun)) {
            std::string target, err;
            if (installProgram(*shell, exe_, pe_, desktop_, target, err)) {
                exe_ = target;
                shell->message("Installed", name_ + " was installed. You'll find it in All apps on the Start menu.");
                shell->close(this);
            } else {
                shell->message(name_, err);
            }
        }
    }

    void drawNoRuntime(Ui& ui, const Rect& c) {
        Gfx& g = *ui.g;
        const Theme& th = *ui.th;
        float x = c.x + 22, w = c.w - 44;
        drawAppIcon(g, th, {x, c.y + 20, 40, 40});
        g.textBold(x + 54, c.y + 26, 15, "Windows compatibility is not installed", th.text);
        auto lines = wrapText(g, 12.5f, "To run Windows programs, WinDroid uses Box64 and Wine. Download the runtime once from Settings > Apps, then open "
                                        + name_ + " again.", w);
        float y = c.y + 78;
        for (auto& l : lines) {
            g.text(x, y, 12.5f, l, th.text);
            y += 19;
        }
        Rect bar{c.x, c.b() - 64, c.w, 64};
        g.rect(bar, th.dark ? rgb(0x202020) : rgb(0xF0F0F0));
        g.rect({bar.x, bar.y, bar.w, 1}, th.border);
        if (ui.button({c.r() - 160, bar.y + 16, 140, 32}, "Close")) shell->close(this);
        if (ui.button({c.r() - 310, bar.y + 16, 140, 32}, "Open Settings", true)) {
            shell->open(AppKind::Settings, "apps");
            shell->close(this);
        }
    }

    void drawConsole(Ui& ui, const Rect& c) {
        Gfx& g = *ui.g;
        const Theme& th = *ui.th;
        poll();
        Rect head{c.x, c.y, c.w, 40};
        g.rect(head, th.dark ? rgb(0x202020) : rgb(0xF3F3F3));
        drawAppIcon(g, th, {head.x + 12, head.cy() - 10, 20, 20});
        std::string st = running_ ? "Running" : (job_ > 0 ? "Exited (" + std::to_string(exit_) + ")" : "Stopped");
        g.textIn({head.x + 42, head.y, head.w - 200, head.h}, 12.5f, g.ellipsize(12.5f, name_ + "  \xC2\xB7  " + st, head.w - 200), th.text);
        Rect act{head.r() - 96, head.y + 5, 86, 30};
        if (running_) {
            if (ui.button(act, "Stop")) shell->sys("exe.kill", std::to_string(job_));
        } else {
            if (ui.button(act, "Run again", true)) start();
        }
        g.rect({head.x, head.b() - 1, head.w, 1}, th.border);

        Rect inBar{c.x, c.b() - (running_ ? 44.f : 0.f), c.w, running_ ? 44.f : 0.f};
        Rect out{c.x, head.b(), c.w, inBar.y - head.b()};
        g.rect(out, rgb(0x0C0C0C));
        int mono = shell->monoFont();
        float size = 12.5f;
        float lh = g.lineHeight(size, mono) * 1.15f;
        size_t total = lines_.size() + 1;
        float contentH = total * lh + 16;
        float maxScroll = std::max(0.f, contentH - out.h);
        if (ui.dragging(out)) {
            scroll_ -= ui.dy();
            follow_ = scroll_ >= maxScroll - 4;
        }
        if (follow_) scroll_ = maxScroll;
        scroll_ = clampf(scroll_, 0, maxScroll);
        g.pushClip(out);
        size_t first = (size_t)std::max(0.f, (scroll_ - 8) / lh);
        for (size_t i = first; i < total; i++) {
            float y = out.y + 8 + i * lh - scroll_;
            if (y > out.b()) break;
            const std::string& l = i < lines_.size() ? lines_[i] : partial_;
            g.text(out.x + 10, y, size, l, rgb(0xCCCCCC), mono);
        }
        g.popClip();

        if (running_) {
            g.rect(inBar, th.dark ? rgb(0x1C1C1C) : rgb(0xFFFFFF));
            g.rect({inBar.x, inBar.y, inBar.w, 1}, th.border);
            bool enter = ui.lineEdit({inBar.x + 8, inBar.y + 6, inBar.w - 16, 32}, input_, inputFocus_, "Type input for the program and press Enter",
                                     12.5f);
            if (enter) {
                shell->sys("exe.write", std::to_string(job_) + "\t" + input_.text + "\r\n");
                addText(input_.text + "\n");
                input_.set("");
                inputFocus_ = true;
                follow_ = true;
            }
        }
    }
};

std::unique_ptr<AppWindow> makeProgram(Shell& s, const std::string& exe, bool run, const std::string& args) {
    auto w = std::make_unique<ProgramWindow>(s, exe, run, args);
    return w;
}

}
