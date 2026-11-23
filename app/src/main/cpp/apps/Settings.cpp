#include <GLES2/gl2.h>
#include <unistd.h>

#include <cstdio>
#include <ctime>
#include <memory>

#include "Apps.h"
#include "Common.h"

namespace wd {

static const uint32_t kAccentColors[] = {0x0078D4, 0x8764B8, 0x107C10, 0xCA5010, 0xE3008C, 0x038387, 0xC30052, 0x6B69D6};

class SettingsWindow : public AppWindow {
public:
    SettingsWindow(const std::string& arg) {
        prefW = 800;
        prefH = 520;
        minW = 360;
        minH = 300;
        if (arg == "personalization") page_ = 1;
        if (arg == "accounts") page_ = 2;
        if (arg == "time") page_ = 3;
        if (arg == "about") page_ = 4;
    }

    void draw(Ui& ui, const Rect& c) override {
        Gfx& g = *ui.g;
        const Theme& th = *ui.th;
        g.rect(c, th.mica);
        bool compact = c.w < 620;
        float navW = compact ? 60 : 250;
        Rect nav{c.x, c.y, navW, c.h};
        drawNav(ui, nav, compact);
        Rect content{c.x + navW, c.y, c.w - navW, c.h};

        const char* titles[] = {"System", "Personalization", "Accounts", "Time & language", "About"};
        if (ui.dragging(content)) scroll_ -= ui.dy();
        if (page_ != lastPage_) {
            scroll_ = 0;
            lastPage_ = page_;
        }
        scroll_ = clampf(scroll_, 0, std::max(0.f, contentH_ - content.h + 20));
        g.pushClip(content);
        float x = content.x + 24, y = content.y + 14 - scroll_;
        float w = content.w - 48;
        g.textBold(x, y, 24, titles[page_], th.text);
        y += 50;
        float top = y;
        switch (page_) {
            case 0: y = pageSystem(ui, x, y, w); break;
            case 1: y = pagePersonal(ui, x, y, w); break;
            case 2: y = pageAccounts(ui, x, y, w); break;
            case 3: y = pageTime(ui, x, y, w); break;
            case 4: y = pageAbout(ui, x, y, w); break;
        }
        contentH_ = y - top + 70;
        g.popClip();
    }

private:
    int page_ = 0;
    int lastPage_ = 0;
    float scroll_ = 0;
    float contentH_ = 0;
    std::string glInfo_;
    double sizeT_ = -1;
    long long cachedSize_ = 0;

    void drawNav(Ui& ui, const Rect& r, bool compact) {
        Gfx& g = *ui.g;
        const Theme& th = *ui.th;
        Config& cfg = shell->config();
        float y = r.y + 12;
        if (!compact) {
            drawIcon(g, IcUser, {r.x + 18, y, 56, 56}, th.text);
            g.textBold(r.x + 86, y + 10, 14, g.ellipsize(14, cfg.user, r.w - 100), th.text);
            g.text(r.x + 86, y + 32, 11.5f, "Local Account", th.textDim);
            y += 76;
        } else {
            y += 6;
        }
        IconId icons[] = {IcPC, IcGlyphBrush, IcUser, IcClock, IcGlyphGear};
        const char* labels[] = {"System", "Personalization", "Accounts", "Time & language", "About"};
        for (int i = 0; i < 5; i++) {
            Rect ir{r.x + 6, y, r.w - 12, 38};
            bool sel = page_ == i;
            if (sel) g.rect(ir, th.press, 5);
            if (ui.held(ir)) g.rect(ir, th.press, 5);
            if (sel) g.rect({ir.x, ir.cy() - 8, 3, 16}, th.accent, 1.5f);
            if (compact) {
                drawIcon(g, icons[i], ir.centered(18, 18), th.text);
            } else {
                drawIcon(g, icons[i], {ir.x + 14, ir.cy() - 9, 18, 18}, th.text);
                g.textIn({ir.x + 44, ir.y, ir.w - 44, ir.h}, 12.5f, labels[i], th.text);
            }
            if (ui.clicked(ir)) page_ = i;
            y += 40;
        }
    }

    float row(Ui& ui, float x, float y, float w, IconId icon, const std::string& label, const std::string& desc, float h = 64) {
        Gfx& g = *ui.g;
        const Theme& th = *ui.th;
        card(g, th, {x, y, w, h});
        drawIcon(g, icon, {x + 18, y + h / 2 - 10, 20, 20}, th.text);
        if (desc.empty()) {
            g.textIn({x + 54, y, w * 0.5f, h}, 13, label, th.text);
        } else {
            g.text(x + 54, y + h / 2 - 18, 13, label, th.text);
            g.text(x + 54, y + h / 2 + 2, 11.5f, g.ellipsize(11.5f, desc, w * 0.55f), th.textDim);
        }
        return y + h + 4;
    }

    float pageSystem(Ui& ui, float x, float y, float w) {
        Gfx& g = *ui.g;
        const Theme& th = *ui.th;
        Config& cfg = shell->config();
        g.textBold(x, y, 13, "Display", th.text);
        y += 26;
        float ny = row(ui, x, y, w, IcPC, "Scale", "Size of text and apps", 72);
        int opts[] = {75, 100, 125, 150};
        float bw = 62;
        float bx = x + w - 4 * (bw + 6) - 12;
        if (w < 520) bx = x + w - 4 * (bw + 6) - 6;
        for (int i = 0; i < 4; i++) {
            Rect b{bx + i * (bw + 6), y + 20, bw, 32};
            if (ui.button(b, std::to_string(opts[i]) + "%", cfg.scalePct == opts[i])) {
                cfg.scalePct = opts[i];
                shell->applyConfig();
                shell->saveConfig();
            }
        }
        y = ny;
        float ty = y;
        y = row(ui, x, y, w, IcGlyphSun, "Night light", "Use warmer colors to help block blue light");
        bool nl = shell->nightLight();
        if (ui.toggle({x + w - 64, ty + 22, 48, 20}, nl)) shell->setNightLight(nl);
        g.text(x + w - 104, ty + 23, 12, nl ? "On" : "Off", th.text);
        y += 18;

        g.textBold(x, y, 13, "Storage", th.text);
        y += 26;
        card(g, th, {x, y, w, 96});
        long long total = 0, freeB = 0;
        fs::diskUsage(shell->root(), total, freeB);
        drawIcon(g, IcDrive, {x + 16, y + 14, 32, 32}, th.text);
        g.text(x + 60, y + 14, 13, "Local Disk (C:)", th.text);
        g.text(x + 60, y + 34, 11.5f, fs::formatSize(total - freeB) + " used of " + fs::formatSize(total), th.textDim);
        float used = total > 0 ? (float)(total - freeB) / total : 0;
        Rect bar{x + 60, y + 62, w - 80, 8};
        g.rect(bar, th.dark ? rgb(0x3A3A3A) : rgb(0xE6E6E6), 4);
        g.rect({bar.x, bar.y, bar.w * used, bar.h}, th.accent, 4);
        if (ui.time - sizeT_ > 3 || sizeT_ < 0) {
            sizeT_ = ui.time;
            cachedSize_ = fs::dirSize(shell->root());
        }
        g.text(x + 60, y + 74, 11, "WinDroid files: " + fs::formatSize(cachedSize_), th.textDim);
        y += 100;
        return y;
    }

    float pagePersonal(Ui& ui, float x, float y, float w) {
        Gfx& g = *ui.g;
        const Theme& th = *ui.th;
        Config& cfg = shell->config();
        Rect prev{x, y, std::min(300.f, w), 170};
        g.pushClip(prev);
        shell->drawWallpaper(prev);
        g.popClip();
        g.stroke(prev, alpha(th.text, 0.15f), 0, 1);
        Rect mw{prev.x + prev.w * 0.18f, prev.y + prev.h * 0.16f, prev.w * 0.55f, prev.h * 0.5f};
        g.shadow(mw, 4, 8, th.shadow);
        g.rect(mw, th.mica, 4);
        g.rect({mw.x, mw.y, mw.w, 10}, th.mica, 4);
        g.rect({mw.x + 8, mw.y + 18, mw.w * 0.5f, 6}, alpha(th.text, 0.25f), 3);
        g.rect({mw.x + 8, mw.y + 30, mw.w * 0.3f, 14}, th.accent, 3);
        Rect tb{prev.x, prev.b() - 16, prev.w, 16};
        g.rect(tb, th.taskbar);
        for (int i = 0; i < 5; i++) g.rect({prev.cx() - 30 + i * 12, tb.y + 4, 8, 8}, i == 0 ? th.accent : alpha(th.text, 0.4f), 2);
        y += prev.h + 22;

        g.textBold(x, y, 13, "Choose your mode", th.text);
        y += 26;
        float ty = y;
        y = row(ui, x, y, w, IcGlyphMoon, "App and system theme", "Change the colors that appear in WinDroid");
        if (ui.button({x + w - 186, ty + 16, 84, 32}, "Light", !cfg.dark)) {
            cfg.dark = false;
            shell->applyConfig();
            shell->saveConfig();
        }
        if (ui.button({x + w - 96, ty + 16, 84, 32}, "Dark", cfg.dark)) {
            cfg.dark = true;
            shell->applyConfig();
            shell->saveConfig();
        }
        y += 14;

        g.textBold(x, y, 13, "Accent color", th.text);
        y += 26;
        card(g, th, {x, y, w, 64});
        for (int i = 0; i < 8; i++) {
            float cx = x + 36 + i * 46, cy = y + 32;
            if (cx > x + w - 20) break;
            Rect hit{cx - 20, cy - 20, 40, 40};
            if (cfg.accent == i) g.ring(cx, cy, 19, 2, th.text);
            g.circle(cx, cy, ui.held(hit) ? 13 : 15, rgb(kAccentColors[i]));
            if (ui.clicked(hit)) {
                cfg.accent = i;
                shell->applyConfig();
                shell->saveConfig();
            }
        }
        y += 82;

        g.textBold(x, y, 13, "Background", th.text);
        y += 26;
        float tw = 104, tht = 66;
        int cols = std::max(1, (int)((w - 24) / (tw + 10)));
        int rows = (5 + cols - 1) / cols;
        card(g, th, {x, y, w, rows * (tht + 10) + 22});
        for (int i = 0; i < 5; i++) {
            Rect t{x + 14 + (i % cols) * (tw + 10), y + 12 + (i / cols) * (tht + 10), tw, tht};
            g.pushClip(t);
            shell->drawWallpaper(t, i);
            g.popClip();
            if (cfg.wallpaper == i) g.stroke(t.inset(-3), th.accent, 4, 2.5f);
            if (ui.held(t)) g.rect(t, rgb(0x000000, 0.15f));
            if (ui.clicked(t)) {
                cfg.wallpaper = i;
                shell->saveConfig();
            }
        }
        y += rows * (tht + 10) + 36;

        g.textBold(x, y, 13, "Taskbar", th.text);
        y += 26;
        ty = y;
        y = row(ui, x, y, w, IcGlyphGrid, "Taskbar alignment", "");
        if (ui.button({x + w - 186, ty + 16, 84, 32}, "Center", !cfg.taskbarLeft)) {
            cfg.taskbarLeft = false;
            shell->saveConfig();
        }
        if (ui.button({x + w - 96, ty + 16, 84, 32}, "Left", cfg.taskbarLeft)) {
            cfg.taskbarLeft = true;
            shell->saveConfig();
        }
        return y;
    }

    float pageAccounts(Ui& ui, float x, float y, float w) {
        Gfx& g = *ui.g;
        const Theme& th = *ui.th;
        Config& cfg = shell->config();
        card(g, th, {x, y, w, 120});
        drawIcon(g, IcUser, {x + 20, y + 20, 80, 80}, th.text);
        g.textBold(x + 118, y + 26, 20, g.ellipsize(20, cfg.user, w - 140), th.text);
        g.text(x + 118, y + 58, 12, "Local account", th.textDim);
        g.text(x + 118, y + 76, 12, "Administrator", th.textDim);
        y += 128;
        float ty = y;
        y = row(ui, x, y, w, IcGlyphRename, "Your name", "Shown on the Start menu and the sign-in screen");
        Shell* sh = shell;
        if (ui.button({x + w - 136, ty + 16, 120, 32}, "Change name")) {
            sh->prompt("Change name", "Your name:", cfg.user, [sh](const std::string& n) {
                if (n.empty()) return;
                sh->config().user = n;
                sh->saveConfig();
            });
        }
        ty = y;
        y = row(ui, x, y, w, IcGlyphLock, "Lock screen", "Lock WinDroid now");
        if (ui.button({x + w - 136, ty + 16, 120, 32}, "Lock")) shell->lock();
        return y;
    }

    float pageTime(Ui& ui, float x, float y, float w) {
        Gfx& g = *ui.g;
        const Theme& th = *ui.th;
        time_t t = time(nullptr);
        struct tm lt;
        localtime_r(&t, &lt);
        char buf[64];
        strftime(buf, sizeof buf, "%H:%M:%S", &lt);
        card(g, th, {x, y, w, 100});
        g.textBold(x + 20, y + 16, 36, buf, th.text);
        strftime(buf, sizeof buf, "%A, %d %B %Y", &lt);
        g.text(x + 22, y + 66, 13, buf, th.textDim);
        y += 108;
        y = row(ui, x, y, w, IcClock, "Time zone", lt.tm_zone ? std::string(lt.tm_zone) : "Device default");
        y = row(ui, x, y, w, IcGlyphSearch, "Language", "English (United States)");
        y = row(ui, x, y, w, IcGlyphGrid, "Formats", "24-hour clock, dd/mm/yyyy");
        return y;
    }

    float pageAbout(Ui& ui, float x, float y, float w) {
        Gfx& g = *ui.g;
        const Theme& th = *ui.th;
        const Platform& p = shell->platform();
        if (glInfo_.empty()) {
            const char* r = (const char*)glGetString(GL_RENDERER);
            const char* v = (const char*)glGetString(GL_VERSION);
            glInfo_ = std::string(r ? r : "Unknown GPU") + (v ? std::string(" (") + v + ")" : "");
        }
        long cores = sysconf(_SC_NPROCESSORS_ONLN);
        long long memKb = 0;
        FILE* f = fopen("/proc/meminfo", "r");
        if (f) {
            char line[256];
            while (fgets(line, sizeof line, f))
                if (sscanf(line, "MemTotal: %lld kB", &memKb) == 1) break;
            fclose(f);
        }
        card(g, th, {x, y, w, 76});
        drawIcon(g, IcLogo, {x + 18, y + 16, 44, 44}, th.text);
        g.textBold(x + 76, y + 16, 15, p.manufacturer + " " + p.model, th.text);
        g.text(x + 76, y + 42, 12, "WinDroid 11 Home", th.textDim);
        y += 92;
        g.textBold(x, y, 13, "Device specifications", th.text);
        y += 26;
        std::vector<std::pair<std::string, std::string>> rows = {
            {"Device name", p.model},
            {"Processor", std::to_string(cores) + " cores"},
            {"Installed RAM", memKb > 0 ? fs::formatSize(memKb * 1024) : "Unknown"},
            {"System type", "64-bit operating system, ARM-based (" + p.abi + ")"},
            {"Graphics", glInfo_},
            {"Display", std::to_string(ui.g->pixelWidth()) + " x " + std::to_string(ui.g->pixelHeight()) + " px"},
        };
        float h = rows.size() * 30.f + 16;
        card(g, th, {x, y, w, h});
        for (size_t i = 0; i < rows.size(); i++) {
            float ry = y + 8 + i * 30.f;
            g.textIn({x + 18, ry, 140, 30}, 12.5f, rows[i].first, th.textDim);
            g.textIn({x + 170, ry, w - 186, 30}, 12.5f, g.ellipsize(12.5f, rows[i].second, w - 186), th.text);
        }
        y += h + 16;
        g.textBold(x, y, 13, "Windows specifications", th.text);
        y += 26;
        std::vector<std::pair<std::string, std::string>> rows2 = {
            {"Edition", "WinDroid 11 Home"},
            {"Version", "24H2"},
            {"Android", p.osVersion.empty() ? "Unknown" : "Android " + p.osVersion},
            {"Engine", "Native C++ / OpenGL ES 2.0"},
        };
        h = rows2.size() * 30.f + 16;
        card(g, th, {x, y, w, h});
        for (size_t i = 0; i < rows2.size(); i++) {
            float ry = y + 8 + i * 30.f;
            g.textIn({x + 18, ry, 140, 30}, 12.5f, rows2[i].first, th.textDim);
            g.textIn({x + 170, ry, w - 186, 30}, 12.5f, rows2[i].second, th.text);
        }
        y += h;
        return y;
    }
};

std::unique_ptr<AppWindow> makeSettings(Shell&, const std::string& arg) { return std::make_unique<SettingsWindow>(arg); }

}
