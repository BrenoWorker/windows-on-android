#include <unistd.h>

#include <cstdio>
#include <ctime>
#include <memory>
#include <sstream>

#include "Apps.h"
#include "Common.h"

namespace wd {

namespace {

const Color kBg = rgb(0x0C0C0C);
const Color kFg = rgb(0xCCCCCC);
const Color kRed = rgb(0xE74856);
const Color kYellow = rgb(0xF9F1A5);
const Color kGray = rgb(0x767676);
const Color kCyan = rgb(0x61D6D6);
const Color kGreen = rgb(0x16C60C);
const Color kBlue = rgb(0x3B78FF);
const Color kWhite = rgb(0xF2F2F2);

enum Profile { ProfilePowerShell = 0, ProfileCmd = 1 };

struct Seg {
    std::string t;
    Color c;
};

using Rich = std::vector<Seg>;

std::vector<std::string> splitArgs(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    char quote = 0;
    bool any = false;
    for (char ch : s) {
        if (quote) {
            if (ch == quote)
                quote = 0;
            else
                cur += ch;
            continue;
        }
        if (ch == '"' || ch == '\'') {
            quote = ch;
            any = true;
            continue;
        }
        if (ch == ' ') {
            if (!cur.empty() || any) out.push_back(cur);
            cur.clear();
            any = false;
        } else {
            cur += ch;
        }
    }
    if (!cur.empty() || any) out.push_back(cur);
    return out;
}

std::string padLeft(const std::string& s, size_t n) {
    size_t len = utf8Length(s);
    return len >= n ? s : std::string(n - len, ' ') + s;
}

std::string padRight(const std::string& s, size_t n) {
    size_t len = utf8Length(s);
    return len >= n ? s : s + std::string(n - len, ' ');
}

Rich highlight(const std::string& line) {
    Rich out;
    size_t i = 0;
    bool first = true;
    while (i < line.size()) {
        if (line[i] == ' ') {
            size_t j = i;
            while (j < line.size() && line[j] == ' ') j++;
            out.push_back({line.substr(i, j - i), kFg});
            i = j;
            continue;
        }
        if (line[i] == '"' || line[i] == '\'') {
            char q = line[i];
            size_t j = line.find(q, i + 1);
            j = j == std::string::npos ? line.size() : j + 1;
            out.push_back({line.substr(i, j - i), kCyan});
            i = j;
            first = false;
            continue;
        }
        size_t j = line.find(' ', i);
        if (j == std::string::npos) j = line.size();
        std::string tok = line.substr(i, j - i);
        Color c = kFg;
        if (first)
            c = kYellow;
        else if (tok[0] == '-')
            c = kGray;
        else if (tok[0] == '$')
            c = kGreen;
        out.push_back({tok, c});
        first = false;
        i = j;
    }
    return out;
}

}

class TerminalWindow : public AppWindow {
public:
    TerminalWindow(Shell& s, const std::string& arg) {
        prefW = 720;
        prefH = 430;
        minW = 360;
        minH = 220;
        titleH = 40;
        darkChrome = true;
        startDir_ = (!arg.empty() && fs::isDir(arg)) ? arg : s.userDir();
        addTab(ProfilePowerShell, startDir_, s);
    }

    bool wantsText() const override { return true; }
    bool customTitle() const override { return true; }
    Color titleColor(const Theme&, bool focused) const override { return focused ? rgb(0x1F1F1F) : rgb(0x2B2B2B); }

    bool titleHit(const Rect& bar, float x, float y) override {
        Layout L = layout(bar);
        if (L.v.contains(x, y) || L.plus.contains(x, y)) return true;
        for (auto& t : L.tabs)
            if (t.contains(x, y)) return true;
        return false;
    }

    void drawTitle(Ui& ui, const Rect& bar, bool focused) override {
        Gfx& g = *ui.g;
        Layout L = layout(bar);
        int closeIdx = -1;
        for (size_t i = 0; i < tabs_.size(); i++) {
            Rect t = L.tabs[i];
            bool active = (int)i == cur_;
            if (active) {
                g.rect(t, kBg, 7);
                g.rect({t.x, t.b() - 8, t.w, 8}, kBg);
            } else if (ui.held(t)) {
                g.rect(t, rgb(0xFFFFFF, 0.08f), 7);
            }
            drawProfileIcon(g, tabs_[i]->profile, {t.x + 10, t.cy() - 8, 16, 16});
            float textW = t.w - 34 - (t.w > 100 ? 26 : 0);
            Color tc = active ? kWhite : rgb(0xB5B5B5);
            if (!focused) tc = alpha(tc, 0.75f);
            g.textIn({t.x + 34, t.y, textW, t.h}, 12, g.ellipsize(12, tabTitle(*tabs_[i]), textW), tc);
            if (t.w > 100) {
                Rect x{t.r() - 28, t.cy() - 11, 22, 22};
                if (ui.held(x)) g.rect(x, rgb(0xFFFFFF, 0.12f), 4);
                g.line(x.cx() - 4, x.cy() - 4, x.cx() + 4, x.cy() + 4, 1.1f, tc);
                g.line(x.cx() + 4, x.cy() - 4, x.cx() - 4, x.cy() + 4, 1.1f, tc);
                if (ui.clicked(x)) closeIdx = (int)i;
                else if (ui.clicked(t)) cur_ = (int)i;
            } else if (ui.clicked(t)) {
                cur_ = (int)i;
            }
        }
        if (ui.held(L.plus)) g.rect(L.plus, rgb(0xFFFFFF, 0.1f), 5);
        drawIcon(g, IcGlyphPlus, L.plus.centered(12, 12), kWhite);
        if (ui.held(L.v) || menuOpen_) g.rect(L.v, rgb(0xFFFFFF, 0.1f), 5);
        drawIcon(g, IcGlyphDown, L.v.centered(12, 12), kWhite);
        if (ui.clicked(L.plus)) addTab(ProfilePowerShell, curSession().cwd, *shell);
        if (ui.clicked(L.v)) {
            menuOpen_ = !menuOpen_;
            menuX_ = L.v.x;
        }
        if (closeIdx >= 0) closeTab(closeIdx);
    }

    void draw(Ui& ui, const Rect& c) override {
        Gfx& g = *ui.g;
        if (tabs_.empty()) return;
        g.rect(c, kBg);
        if (ui.focus && !menuOpen_) handleInput(ui, curSession());
        if (tabs_.empty()) return;
        Session& s = curSession();
        int mono = shell->monoFont();
        title = tabTitle(s);

        float size = 13;
        float cw = std::max(1.f, g.textWidth(size, "M", mono));
        float lh = g.lineHeight(size, mono) * 1.18f;
        Rect area = c.inset(10, 6);
        int cols = std::max(10, (int)((area.w - 8) / cw));

        std::vector<Rich> rows;
        for (auto& l : s.out) wrap(l, cols, rows);
        Rich curLine = prompt(s);
        size_t promptLen = 0;
        for (auto& sg : curLine) promptLen += utf8Length(sg.t);
        Rich hl = s.profile == ProfilePowerShell ? highlight(s.input.text) : Rich{{s.input.text, kFg}};
        curLine.insert(curLine.end(), hl.begin(), hl.end());
        size_t curStart = rows.size();
        wrap(curLine, cols, rows);

        float contentH = rows.size() * lh;
        float maxScroll = std::max(0.f, contentH - area.h);
        if (!menuOpen_ && ui.dragging(c)) {
            s.scroll -= ui.dy();
            s.stick = false;
        }
        if (s.stick) s.scroll = maxScroll;
        s.scroll = clampf(s.scroll, 0, maxScroll);
        if (s.scroll >= maxScroll - 1) s.stick = true;
        if (!menuOpen_ && ui.clicked(c)) ui.requestKeyboard = true;

        g.pushClip(c);
        for (size_t r = 0; r < rows.size(); r++) {
            float y = area.y + r * lh - s.scroll;
            if (y + lh < c.y || y > c.b()) continue;
            float x = area.x;
            for (auto& sg : rows[r]) {
                g.text(x, y, size, sg.t, sg.c, mono);
                x += utf8Length(sg.t) * cw;
            }
        }
        size_t caretChars = promptLen + utf8Length(s.input.text.substr(0, s.input.caret));
        size_t row = curStart + caretChars / (size_t)cols;
        size_t col = caretChars % (size_t)cols;
        float blink = std::fmod(ui.time - lastInput_, 1.06);
        if (ui.focus && blink < 0.53) {
            float y = area.y + row * lh - s.scroll;
            g.rect({area.x + col * cw, y + 1, 1.6f, lh - 2}, kWhite);
        }
        if (contentH > area.h) {
            float bh = std::max(24.f, c.h * area.h / contentH);
            float by = c.y + (c.h - bh) * (s.scroll / std::max(1.f, maxScroll));
            g.rect({c.r() - 6, by, 3, bh}, rgb(0xFFFFFF, 0.28f), 1.5f);
        }
        g.popClip();

        if (menuOpen_) drawMenu(ui, c);
    }

private:
    struct Session {
        int profile = ProfilePowerShell;
        std::string cwd;
        std::vector<Rich> out;
        std::vector<std::string> history;
        int histPos = 0;
        LineEdit input;
        float scroll = 0;
        bool stick = true;
    };
    struct Layout {
        std::vector<Rect> tabs;
        Rect plus;
        Rect v;
    };

    std::vector<std::unique_ptr<Session>> tabs_;
    int cur_ = 0;
    std::string startDir_;
    bool menuOpen_ = false;
    float menuX_ = 0;
    double lastInput_ = 0;

    Session& curSession() { return *tabs_[(size_t)cur_]; }

    Layout layout(const Rect& bar) const {
        Layout L;
        float x = bar.x + 8;
        float avail = bar.w - 8 - 76;
        float tw = tabs_.empty() ? 200 : clampf(avail / tabs_.size(), 56, 230);
        for (size_t i = 0; i < tabs_.size(); i++) {
            L.tabs.push_back({x, bar.y + 6, tw, bar.h - 6});
            x += tw + 1;
        }
        L.plus = {x + 4, bar.y + 8, 32, bar.h - 14};
        L.v = {x + 38, bar.y + 8, 28, bar.h - 14};
        return L;
    }

    std::string tabTitle(const Session& s) const {
        if (s.profile == ProfilePowerShell) return "PowerShell";
        return "Command Prompt";
    }

    static void drawProfileIcon(Gfx& g, int profile, const Rect& r) {
        if (profile == ProfileCmd) {
            drawIcon(g, IcTerminal, r, kWhite);
            return;
        }
        float u = r.w / 16;
        g.rectV({r.x + 1 * u, r.y + 2 * u, 14 * u, 12 * u}, rgb(0x4D8FE0), rgb(0x1F4E9A), 2 * u);
        g.line(r.x + 4 * u, r.y + 5.5f * u, r.x + 7 * u, r.y + 8 * u, 1.2f * u, kWhite);
        g.line(r.x + 7 * u, r.y + 8 * u, r.x + 4 * u, r.y + 10.5f * u, 1.2f * u, kWhite);
        g.line(r.x + 8.5f * u, r.y + 10.6f * u, r.x + 12 * u, r.y + 10.6f * u, 1.2f * u, kWhite);
    }

    void addTab(int profile, const std::string& cwd, Shell& s) {
        auto t = std::make_unique<Session>();
        t->profile = profile;
        t->cwd = cwd;
        if (profile == ProfilePowerShell) {
            t->out.push_back({{"WinDroid PowerShell 7.4.6", kFg}});
            t->out.push_back({{"Type 'help' to see the available commands.", kGray}});
            t->out.push_back({});
        } else {
            t->out.push_back({{"WinDroid [Version 11.0.26100]", kFg}});
            t->out.push_back({{"(c) WinDroid. All rights reserved.", kFg}});
            t->out.push_back({});
        }
        tabs_.push_back(std::move(t));
        cur_ = (int)tabs_.size() - 1;
        (void)s;
    }

    void closeTab(int i) {
        if (i < 0 || i >= (int)tabs_.size()) return;
        tabs_.erase(tabs_.begin() + i);
        if (tabs_.empty()) {
            addTab(ProfilePowerShell, startDir_, *shell);
            shell->close(this);
            return;
        }
        if (cur_ >= (int)tabs_.size()) cur_ = (int)tabs_.size() - 1;
    }

    Rich prompt(const Session& s) const {
        if (s.profile == ProfilePowerShell) return {{"PS " + shell->displayPath(s.cwd) + "> ", kFg}};
        return {{shell->displayPath(s.cwd) + ">", kFg}};
    }

    static void wrap(const Rich& line, int cols, std::vector<Rich>& rows) {
        Rich row;
        int n = 0;
        for (auto& sg : line) {
            size_t i = 0;
            std::string part;
            while (i < sg.t.size()) {
                if (n >= cols) {
                    if (!part.empty()) row.push_back({part, sg.c});
                    rows.push_back(row);
                    row.clear();
                    part.clear();
                    n = 0;
                }
                size_t ni = utf8Next(sg.t, i);
                part += sg.t.substr(i, ni - i);
                i = ni;
                n++;
            }
            if (!part.empty()) row.push_back({part, sg.c});
        }
        rows.push_back(row);
    }

    void handleInput(Ui& ui, Session& s) {
        if (!ui.chars.empty() || !ui.keys.empty()) lastInput_ = ui.time;
        for (int k : ui.keys) {
            if (k == KeyUp && !s.history.empty()) {
                s.histPos = std::max(0, s.histPos - 1);
                s.input.set(s.history[(size_t)s.histPos]);
            }
            if (k == KeyDown && !s.history.empty()) {
                s.histPos = std::min((int)s.history.size(), s.histPos + 1);
                s.input.set(s.histPos < (int)s.history.size() ? s.history[(size_t)s.histPos] : "");
            }
            if (k == KeyEscape) s.input.set("");
        }
        bool enter = s.input.apply(ui.chars, ui.keys);
        if (!ui.chars.empty() || !ui.keys.empty()) s.stick = true;
        if (!enter) return;
        std::string cmd = s.input.text;
        Rich echoLine = prompt(s);
        Rich hl = s.profile == ProfilePowerShell ? highlight(cmd) : Rich{{cmd, kFg}};
        echoLine.insert(echoLine.end(), hl.begin(), hl.end());
        s.out.push_back(echoLine);
        if (!cmd.empty()) {
            s.history.push_back(cmd);
            s.histPos = (int)s.history.size();
        }
        s.input.set("");
        bool cleared = false;
        Session* sp = &s;
        run(sp, cmd, cleared);
        if (tabs_.empty()) return;
        bool alive = false;
        for (auto& t : tabs_)
            if (t.get() == sp) alive = true;
        if (!alive) return;
        if (!cleared && sp->profile == ProfileCmd) sp->out.push_back({});
        if (sp->out.size() > 3000) sp->out.erase(sp->out.begin(), sp->out.begin() + (long)(sp->out.size() - 3000));
        sp->stick = true;
    }

    void drawMenu(Ui& ui, const Rect& c) {
        Gfx& g = *ui.g;
        const char* items[] = {"PowerShell", "Command Prompt", "Clear buffer", "Close other tabs"};
        int n = 4;
        Rect m{std::min(menuX_, c.r() - 230), c.y + 4, 222, n * 36.f + 8 + 9};
        g.shadow(m, 8, 16, rgb(0x000000, 0.6f));
        g.rect(m, rgb(0x2C2C2C), 8);
        g.stroke(m, rgb(0xFFFFFF, 0.08f), 8, 1);
        int chosen = -1;
        float y = m.y + 4;
        for (int i = 0; i < n; i++) {
            if (i == 2) {
                g.rect({m.x + 8, y + 4, m.w - 16, 1}, rgb(0xFFFFFF, 0.1f));
                y += 9;
            }
            Rect r{m.x + 4, y, m.w - 8, 34};
            if (ui.held(r)) g.rect(r, rgb(0xFFFFFF, 0.08f), 4);
            if (i < 2)
                drawProfileIcon(g, i, {r.x + 10, r.cy() - 8, 16, 16});
            else
                drawIcon(g, i == 2 ? IcGlyphTrash : IcGlyphClose, {r.x + 10, r.cy() - 8, 16, 16}, kWhite);
            g.textIn({r.x + 38, r.y, r.w - 40, r.h}, 12.5f, items[i], kWhite);
            if (ui.clicked(r)) chosen = i;
            y += 36;
        }
        if (chosen >= 0) {
            menuOpen_ = false;
            if (chosen == 0) addTab(ProfilePowerShell, curSession().cwd, *shell);
            if (chosen == 1) addTab(ProfileCmd, curSession().cwd, *shell);
            if (chosen == 2) curSession().out.clear();
            if (chosen == 3) {
                auto keep = std::move(tabs_[(size_t)cur_]);
                tabs_.clear();
                tabs_.push_back(std::move(keep));
                cur_ = 0;
            }
        } else if (ui.released && !ui.dragged && !m.contains(ui.x, ui.y)) {
            menuOpen_ = false;
        }
    }

    void say(Session& s, const std::string& t, Color c = kFg) { s.out.push_back({{t, c}}); }

    void err(Session& s, const std::string& t) { say(s, t, s.profile == ProfilePowerShell ? kRed : kFg); }

    std::string arg1(const std::vector<std::string>& a, size_t from = 1) {
        for (size_t i = from; i < a.size(); i++)
            if (a[i].empty() || a[i][0] != '-') return a[i];
        return "";
    }

    bool hasFlag(const std::vector<std::string>& a, const std::string& f) {
        for (auto& x : a)
            if (toLower(x) == f) return true;
        return false;
    }

    std::string flagValue(const std::vector<std::string>& a, const std::string& f) {
        for (size_t i = 0; i + 1 < a.size(); i++)
            if (toLower(a[i]) == f) return a[i + 1];
        return "";
    }

    void listDir(Session& s, const std::string& target, const std::string& shown) {
        Shell* sh = shell;
        std::string p = target.empty() ? s.cwd : sh->realPath(target, s.cwd);
        bool ps = s.profile == ProfilePowerShell;
        if (!fs::isDir(p)) {
            if (ps)
                err(s, "Get-ChildItem: Cannot find path '" + sh->displayPath(p) + "' because it does not exist.");
            else
                say(s, "File Not Found");
            return;
        }
        auto entries = fs::list(p);
        if (ps) {
            say(s, "");
            say(s, "    Directory: " + sh->displayPath(p), kFg);
            say(s, "");
            say(s, "Mode                 LastWriteTime         Length Name", kGreen);
            say(s, "----                 -------------         ------ ----", kGreen);
            for (auto& e : entries) {
                struct tm lt;
                localtime_r(&e.mtime, &lt);
                char buf[32];
                strftime(buf, sizeof buf, "%d/%m/%Y    %H:%M", &lt);
                std::string left = std::string(e.dir ? "d----" : "-a---") + "          " + buf;
                std::string len = e.dir ? std::string(15, ' ') : padLeft(std::to_string(e.size), 15);
                s.out.push_back({{left + len + " ", kFg}, {e.name, e.dir ? kBlue : kFg}});
            }
            say(s, "");
            return;
        }
        (void)shown;
        say(s, " Volume in drive C has no label.");
        say(s, "");
        say(s, " Directory of " + sh->displayPath(p));
        say(s, "");
        int files = 0, dirs = 0;
        long long bytes = 0;
        for (auto& e : entries) {
            std::string ts = fs::formatTime(e.mtime);
            if (e.dir) {
                say(s, ts + "    <DIR>          " + e.name);
                dirs++;
            } else {
                say(s, ts + "    " + padLeft(std::to_string(e.size), 14) + " " + e.name);
                files++;
                bytes += e.size;
            }
        }
        say(s, padLeft(std::to_string(files), 16) + " File(s) " + padLeft(std::to_string(bytes), 14) + " bytes");
        long long total = 0, freeB = 0;
        fs::diskUsage(p, total, freeB);
        say(s, padLeft(std::to_string(dirs), 16) + " Dir(s)  " + padLeft(std::to_string(freeB), 14) + " bytes free");
    }

    void tree(Session& s, const std::string& dir, const std::string& prefix, int depth) {
        if (depth > 6) return;
        std::vector<fs::Entry> ds;
        for (auto& e : fs::list(dir))
            if (e.dir) ds.push_back(e);
        for (size_t i = 0; i < ds.size(); i++) {
            bool last = i + 1 == ds.size();
            say(s, prefix + (last ? "\\---" : "+---") + ds[i].name);
            tree(s, fs::join(dir, ds[i].name), prefix + (last ? "    " : "|   "), depth + 1);
        }
    }

    void help(Session& s) {
        bool ps = s.profile == ProfilePowerShell;
        if (ps) {
            say(s, "");
            say(s, "Commands and aliases", kGreen);
            say(s, "--------------------", kGreen);
        }
        const char* rows[][2] = {
            {"cd / Set-Location", "Change the current directory"},
            {"ls / dir / Get-ChildItem", "List files and folders"},
            {"cat / type / Get-Content", "Show a text file"},
            {"mkdir / New-Item", "Create a folder (New-Item -ItemType File for files)"},
            {"rm / del / Remove-Item", "Delete a file or folder"},
            {"mv / move / Move-Item", "Move a file or folder"},
            {"cp / copy / Copy-Item", "Copy a file"},
            {"ren / Rename-Item", "Rename a file or folder"},
            {"pwd / Get-Location", "Show the current directory"},
            {"echo / Write-Output", "Print text"},
            {"cls / clear / Clear-Host", "Clear the screen"},
            {"Get-Date / date / time", "Show the date and time"},
            {"tree", "Show the folder structure"},
            {"systeminfo / ipconfig / ping", "System and network information"},
            {"history", "Show the command history"},
            {"start <app> / Start-Process", "Open notepad, calc, paint, explorer, browser, settings"},
            {"powershell / cmd", "Open a new tab with that shell"},
            {"shutdown /s /r /l", "Shut down, restart or lock WinDroid"},
            {"exit", "Close the tab"},
        };
        for (auto& r : rows) {
            if (ps)
                s.out.push_back({{padRight(r[0], 32), kYellow}, {r[1], kFg}});
            else
                say(s, padRight(r[0], 32) + r[1]);
        }
        if (ps) say(s, "");
    }

    bool openApp(const std::string& name, Session& s) {
        std::string w = toLower(name);
        Shell* sh = shell;
        if (w == "explorer" || w == "explorer.exe" || w == ".") sh->open(AppKind::Explorer, s.cwd);
        else if (w == "notepad" || w == "notepad.exe") sh->open(AppKind::Notepad);
        else if (w == "calc" || w == "calc.exe") sh->open(AppKind::Calculator);
        else if (w == "paint" || w == "mspaint" || w == "mspaint.exe") sh->open(AppKind::Paint);
        else if (w == "settings" || w == "ms-settings:") sh->open(AppKind::Settings);
        else if (w == "mines" || w == "minesweeper") sh->open(AppKind::Minesweeper);
        else if (w == "browser" || w == "chrome" || w == "msedge") sh->open(AppKind::Browser);
        else if (w.rfind("http://", 0) == 0 || w.rfind("https://", 0) == 0) sh->open(AppKind::Browser, name);
        else if (w == "cmd" || w == "cmd.exe") addTab(ProfileCmd, s.cwd, *sh);
        else if (w == "powershell" || w == "pwsh" || w == "powershell.exe") addTab(ProfilePowerShell, s.cwd, *sh);
        else {
            std::string p = sh->realPath(name, s.cwd);
            if (!fs::exists(p)) return false;
            sh->openFile(p);
        }
        return true;
    }

    void run(Session*& sp, const std::string& line, bool& cleared) {
        Session& s = *sp;
        auto a = splitArgs(line);
        if (a.empty()) return;
        std::string c = toLower(a[0]);
        bool ps = s.profile == ProfilePowerShell;
        Shell* sh = shell;
        std::string target = arg1(a);
        std::string restText;
        {
            size_t sp0 = line.find(' ');
            if (sp0 != std::string::npos) {
                size_t st = line.find_first_not_of(' ', sp0);
                if (st != std::string::npos) restText = line.substr(st);
            }
        }

        if (c == "help" || c == "get-help" || c == "man" || c == "/?") {
            help(s);
        } else if (c == "cls" || c == "clear" || c == "clear-host") {
            s.out.clear();
            cleared = true;
        } else if (c == "echo" || c == "write-output" || c == "write-host") {
            std::string t;
            for (size_t i = 1; i < a.size(); i++) t += (i > 1 ? " " : "") + a[i];
            if (!ps && a.size() < 2) t = "ECHO is on.";
            say(s, t);
        } else if (c == "ver") {
            say(s, "");
            say(s, "WinDroid [Version 11.0.26100]");
        } else if (c == "$psversiontable") {
            say(s, "");
            say(s, "Name                           Value", kGreen);
            say(s, "----                           -----", kGreen);
            say(s, "PSVersion                      7.4.6");
            say(s, "PSEdition                      Core");
            say(s, "OS                             WinDroid 11 on Android " + sh->platform().osVersion);
            say(s, "Platform                       Win32NT");
            say(s, "");
        } else if (c == "whoami") {
            say(s, "windroid\\" + toLower(sh->config().user));
        } else if (c == "hostname") {
            say(s, sh->platform().model);
        } else if (c == "get-date" || c == "date" || c == "time") {
            time_t t = time(nullptr);
            struct tm lt;
            localtime_r(&t, &lt);
            char buf[80];
            const char* f = ps ? "%A, %d %B %Y %H:%M:%S" : (c == "date" ? "The current date is: %a %d/%m/%Y" : "The current time is: %H:%M:%S");
            strftime(buf, sizeof buf, f, &lt);
            if (ps) say(s, "");
            say(s, buf);
            if (ps) say(s, "");
        } else if (c == "pwd" || c == "gl" || c == "get-location") {
            say(s, "");
            say(s, "Path", kGreen);
            say(s, "----", kGreen);
            say(s, sh->displayPath(s.cwd));
            say(s, "");
        } else if (c == "cd" || c == "chdir" || c == "sl" || c == "set-location" || c == "cd.." || c == "cd\\") {
            if (c == "cd..") target = "..";
            if (c == "cd\\") target = "\\";
            if (target.empty()) {
                if (ps)
                    s.cwd = sh->userDir();
                else
                    say(s, sh->displayPath(s.cwd));
                return;
            }
            if (target == "~") {
                s.cwd = sh->userDir();
                return;
            }
            std::string p = sh->realPath(target, s.cwd);
            if (fs::isDir(p))
                s.cwd = p;
            else if (ps)
                err(s, "Set-Location: Cannot find path '" + sh->displayPath(p) + "' because it does not exist.");
            else
                say(s, "The system cannot find the path specified.");
        } else if (c == "ls" || c == "dir" || c == "gci" || c == "get-childitem") {
            listDir(s, target, a[0]);
        } else if (c == "cat" || c == "type" || c == "gc" || c == "get-content") {
            std::string p = sh->realPath(target, s.cwd);
            std::string data;
            if (target.empty() || fs::isDir(p) || !fs::readText(p, data)) {
                if (ps)
                    err(s, "Get-Content: Cannot find path '" + sh->displayPath(p) + "' because it does not exist.");
                else
                    say(s, "The system cannot find the file specified.");
                return;
            }
            std::stringstream ss(data);
            std::string l;
            while (std::getline(ss, l)) say(s, l);
        } else if (c == "mkdir" || c == "md" || c == "new-item" || c == "ni") {
            std::string name = target;
            if (c == "new-item" || c == "ni") {
                std::string nm = flagValue(a, "-name");
                if (!nm.empty()) name = nm;
            }
            if (name.empty()) {
                err(s, ps ? "New-Item: Missing an argument for parameter 'Path'." : "The syntax of the command is incorrect.");
                return;
            }
            std::string p = sh->realPath(name, s.cwd);
            std::string type = toLower(flagValue(a, "-itemtype"));
            if (type.empty()) type = toLower(flagValue(a, "-type"));
            bool file = (c == "new-item" || c == "ni") && type == "file";
            if (fs::exists(p)) {
                err(s, ps ? "New-Item: An item with the specified name " + sh->displayPath(p) + " already exists."
                          : "A subdirectory or file " + name + " already exists.");
                return;
            }
            if (file)
                fs::writeText(p, "");
            else
                fs::makeDirs(p);
            sh->notifyFs();
            if (ps) {
                say(s, "");
                say(s, "    Directory: " + sh->displayPath(fs::parent(p)));
                say(s, "");
                say(s, "Mode                 LastWriteTime         Length Name", kGreen);
                say(s, "----                 -------------         ------ ----", kGreen);
                time_t t = time(nullptr);
                struct tm lt;
                localtime_r(&t, &lt);
                char buf[32];
                strftime(buf, sizeof buf, "%d/%m/%Y    %H:%M", &lt);
                std::string left = std::string(file ? "-a---" : "d----") + "          " + buf;
                std::string len = file ? padLeft("0", 15) : std::string(15, ' ');
                s.out.push_back({{left + len + " ", kFg}, {fs::baseName(p), file ? kFg : kBlue}});
                say(s, "");
            }
        } else if (c == "rm" || c == "del" || c == "erase" || c == "rmdir" || c == "rd" || c == "remove-item" || c == "ri") {
            std::string p = sh->realPath(target, s.cwd);
            if (target.empty() || !fs::exists(p)) {
                err(s, ps ? "Remove-Item: Cannot find path '" + sh->displayPath(p) + "' because it does not exist."
                          : "Could Not Find " + sh->displayPath(p));
                return;
            }
            if (p == sh->root() || s.cwd.compare(0, p.size(), p) == 0) {
                err(s, ps ? "Remove-Item: The process cannot access the file because it is being used by another process."
                          : "The process cannot access the file because it is being used by another process.");
                return;
            }
            if (!ps && (c == "del" || c == "erase") && fs::isDir(p)) {
                say(s, "Could Not Find " + sh->displayPath(p));
                return;
            }
            fs::removeAll(p);
            sh->notifyFs();
        } else if (c == "ren" || c == "rename" || c == "rename-item" || c == "rni" || c == "mv" || c == "move" ||
                   c == "move-item" || c == "mi") {
            std::vector<std::string> plain;
            for (size_t i = 1; i < a.size(); i++)
                if (!a[i].empty() && a[i][0] != '-') plain.push_back(a[i]);
            if (plain.size() < 2) {
                err(s, ps ? "Missing an argument for parameter 'Destination'." : "The syntax of the command is incorrect.");
                return;
            }
            bool rename = c == "ren" || c == "rename" || c == "rename-item" || c == "rni";
            std::string from = sh->realPath(plain[0], s.cwd);
            std::string to = rename ? fs::join(fs::parent(from), plain[1]) : sh->realPath(plain[1], s.cwd);
            if (!rename && fs::isDir(to)) to = fs::join(to, fs::baseName(from));
            if (!fs::exists(from) || !fs::renamePath(from, to)) {
                err(s, ps ? "Cannot find path '" + sh->displayPath(from) + "' because it does not exist."
                          : "The system cannot find the file specified.");
                return;
            }
            sh->notifyFs();
        } else if (c == "cp" || c == "copy" || c == "copy-item" || c == "cpi") {
            std::vector<std::string> plain;
            for (size_t i = 1; i < a.size(); i++)
                if (!a[i].empty() && a[i][0] != '-') plain.push_back(a[i]);
            if (plain.size() < 2) {
                err(s, ps ? "Copy-Item: Missing an argument for parameter 'Destination'." : "The syntax of the command is incorrect.");
                return;
            }
            std::string from = sh->realPath(plain[0], s.cwd);
            std::string to = sh->realPath(plain[1], s.cwd);
            if (fs::isDir(to)) to = fs::join(to, fs::baseName(from));
            std::vector<unsigned char> data;
            if (!fs::readBinary(from, data) || !fs::writeBinary(to, data)) {
                err(s, ps ? "Copy-Item: Cannot find path '" + sh->displayPath(from) + "' because it does not exist."
                          : "The system cannot find the file specified.");
                return;
            }
            if (!ps) say(s, "        1 file(s) copied.");
            sh->notifyFs();
        } else if (c == "tree") {
            say(s, "Folder PATH listing");
            say(s, sh->displayPath(s.cwd));
            tree(s, s.cwd, "", 0);
        } else if (c == "history" || c == "h" || c == "get-history") {
            for (size_t i = 0; i < s.history.size(); i++) say(s, padLeft(std::to_string(i + 1), 4) + " " + s.history[i]);
        } else if (c == "systeminfo") {
            const Platform& p = sh->platform();
            say(s, "");
            say(s, "Host Name:                 " + p.model);
            say(s, "OS Name:                   WinDroid 11 Home");
            say(s, "OS Version:                11.0.26100 Build 26100");
            say(s, "System Manufacturer:       " + p.manufacturer);
            say(s, "System Model:              " + p.model);
            say(s, "System Type:               ARM64-based PC (" + p.abi + ")");
            say(s, "Processor(s):              " + std::to_string(sysconf(_SC_NPROCESSORS_ONLN)) + " Processor(s) Installed.");
            say(s, "Android Version:           " + (p.osVersion.empty() ? std::string("Unknown") : p.osVersion));
        } else if (c == "ipconfig") {
            say(s, "");
            say(s, "WinDroid IP Configuration");
            say(s, "");
            say(s, "Wireless LAN adapter Wi-Fi:");
            say(s, "");
            say(s, "   Connection-specific DNS Suffix  . : home");
            say(s, "   IPv4 Address. . . . . . . . . . . : 192.168.0.42");
            say(s, "   Subnet Mask . . . . . . . . . . . : 255.255.255.0");
            say(s, "   Default Gateway . . . . . . . . . : 192.168.0.1");
        } else if (c == "ping" || c == "test-connection") {
            std::string host = target.empty() ? "localhost" : target;
            say(s, "");
            say(s, "Pinging " + host + " with 32 bytes of data:");
            for (int i = 0; i < 4; i++) say(s, "Reply from " + host + ": bytes=32 time=" + std::to_string(12 + (i * 7) % 9) + "ms TTL=117");
        } else if (c == "start" || c == "start-process" || c == "saps" || c == "ii" || c == "invoke-item") {
            std::string w = target.empty() ? "explorer" : target;
            if (!openApp(w, s)) err(s, ps ? "Start-Process: This command cannot be run because \"" + w + "\" was not found."
                                         : "WinDroid cannot find '" + w + "'. Make sure you typed the name correctly, and then try again.");
        } else if (c == "notepad" || c == "notepad.exe") {
            sh->open(AppKind::Notepad, target.empty() ? "" : sh->realPath(target, s.cwd));
        } else if (c == "explorer" || c == "explorer.exe") {
            sh->open(AppKind::Explorer, target.empty() ? s.cwd : sh->realPath(target, s.cwd));
        } else if (c == "calc" || c == "calc.exe" || c == "mspaint" || c == "paint" || c == "browser" || c == "chrome" ||
                   c == "msedge" || c == "winver") {
            if (c == "winver")
                sh->open(AppKind::Settings, "about");
            else if (c == "browser" || c == "chrome" || c == "msedge")
                sh->open(AppKind::Browser, restText);
            else
                openApp(c, s);
        } else if (c == "cmd" || c == "cmd.exe") {
            addTab(ProfileCmd, s.cwd, *sh);
        } else if (c == "powershell" || c == "pwsh" || c == "powershell.exe" || c == "pwsh.exe") {
            addTab(ProfilePowerShell, s.cwd, *sh);
        } else if (c == "shutdown" || c == "stop-computer" || c == "restart-computer") {
            std::string f = toLower(restText);
            if (c == "stop-computer" || f.find("/s") != std::string::npos)
                sh->shutdown();
            else if (c == "restart-computer" || f.find("/r") != std::string::npos)
                sh->restart();
            else if (f.find("/l") != std::string::npos)
                sh->lock();
            else
                say(s, "Usage: shutdown [/s | /r | /l]");
        } else if (c == "exit") {
            for (size_t i = 0; i < tabs_.size(); i++)
                if (tabs_[i].get() == sp) {
                    sp = nullptr;
                    closeTab((int)i);
                    break;
                }
        } else if (ps) {
            err(s, a[0] + ": The term '" + a[0] + "' is not recognized as a name of a cmdlet, function, script file, or executable program.");
            err(s, "Check the spelling of the name, or if a path was included, verify that the path is correct and try again.");
        } else {
            say(s, "'" + a[0] + "' is not recognized as an internal or external command,");
            say(s, "operable program or batch file.");
        }
    }
};

std::unique_ptr<AppWindow> makeTerminal(Shell& s, const std::string& arg) { return std::make_unique<TerminalWindow>(s, arg); }

}
