#include <unistd.h>

#include <cstdio>
#include <ctime>
#include <sstream>

#include "Apps.h"
#include "Common.h"

namespace wd {

class TerminalWindow : public AppWindow {
public:
    TerminalWindow(Shell& s, const std::string& arg) {
        prefW = 680;
        prefH = 400;
        minW = 300;
        minH = 200;
        title = "Command Prompt";
        cwd_ = (!arg.empty() && fs::isDir(arg)) ? arg : s.userDir();
        out_.push_back("WinDroid [Version 11.0.26100]");
        out_.push_back("(c) WinDroid. All rights reserved.");
        out_.push_back("");
    }

    bool wantsText() const override { return true; }

    void draw(Ui& ui, const Rect& c) override {
        Gfx& g = *ui.g;
        const Theme& th = *ui.th;
        int mono = shell->monoFont();
        Rect tabs{c.x, c.y, c.w, 36};
        g.rect(tabs, th.dark ? rgb(0x1F1F1F) : rgb(0x2B2B2B));
        Rect tab{c.x + 8, c.y + 4, 190, 32};
        g.rect(tab, rgb(0x0C0C0C), 6);
        drawIcon(g, IcTerminal, {tab.x + 10, tab.cy() - 8, 16, 16}, rgb(0xFFFFFF));
        g.textIn({tab.x + 34, tab.y, tab.w - 40, tab.h}, 12, "Command Prompt", rgb(0xFFFFFF));
        Rect plus{tab.r() + 6, c.y + 6, 28, 28};
        if (ui.held(plus)) g.rect(plus, rgb(0xFFFFFF, 0.1f), 4);
        drawIcon(g, IcGlyphPlus, plus.centered(12, 12), rgb(0xFFFFFF));
        if (ui.clicked(plus)) shell->open(AppKind::Terminal, cwd_);

        Rect area{c.x, tabs.b(), c.w, c.h - tabs.h};
        g.rect(area, rgb(0x0C0C0C));
        float size = 13;
        float cw = g.textWidth(size, "M", mono);
        float lh = g.lineHeight(size, mono) * 1.15f;
        int cols = std::max(10, (int)((area.w - 20) / std::max(1.f, cw)));

        if (ui.focus) {
            bool enter = false;
            for (int k : ui.keys) {
                if (k == KeyUp && !history_.empty()) {
                    histPos_ = std::max(0, histPos_ - 1);
                    input_.set(history_[(size_t)histPos_]);
                }
                if (k == KeyDown && !history_.empty()) {
                    histPos_ = std::min((int)history_.size(), histPos_ + 1);
                    input_.set(histPos_ < (int)history_.size() ? history_[(size_t)histPos_] : "");
                }
            }
            enter = input_.apply(ui.chars, ui.keys);
            if (!ui.chars.empty() || !ui.keys.empty()) stick_ = true;
            if (enter) {
                std::string cmd = input_.text;
                out_.push_back(prompt() + cmd);
                if (!cmd.empty()) {
                    history_.push_back(cmd);
                    histPos_ = (int)history_.size();
                }
                input_.set("");
                run(cmd);
                if (!cleared_) out_.push_back("");
                cleared_ = false;
                stick_ = true;
            }
        }
        if (out_.size() > 2000) out_.erase(out_.begin(), out_.begin() + (long)(out_.size() - 2000));

        std::vector<std::string> vis;
        auto pushWrapped = [&](const std::string& s) {
            if (s.empty()) {
                vis.push_back("");
                return;
            }
            size_t i = 0;
            while (i < s.size()) {
                size_t start = i;
                int n = 0;
                while (i < s.size() && n < cols) {
                    i = utf8Next(s, i);
                    n++;
                }
                vis.push_back(s.substr(start, i - start));
            }
        };
        for (auto& l : out_) pushWrapped(l);
        std::string cur = prompt() + input_.text;
        size_t curStart = vis.size();
        pushWrapped(cur);
        float contentH = vis.size() * lh + 16;
        float maxScroll = std::max(0.f, contentH - area.h);
        if (ui.dragging(area)) {
            scroll_ -= ui.dy();
            stick_ = false;
        }
        if (stick_) scroll_ = maxScroll;
        scroll_ = clampf(scroll_, 0, maxScroll);
        if (scroll_ >= maxScroll - 1) stick_ = true;
        if (ui.clicked(area)) ui.requestKeyboard = true;

        g.pushClip(area);
        Color fg = rgb(0xCCCCCC);
        for (size_t i = 0; i < vis.size(); i++) {
            float y = area.y + 8 + i * lh - scroll_;
            if (y + lh < area.y || y > area.b()) continue;
            g.text(area.x + 10, y, size, vis[i], fg, mono);
        }
        size_t caretChars = utf8Length(prompt()) + utf8Length(input_.text.substr(0, input_.caret));
        size_t row = curStart + caretChars / (size_t)cols;
        size_t col = caretChars % (size_t)cols;
        if (ui.focus && std::fmod(ui.time, 1.0) < 0.6) {
            float y = area.y + 8 + row * lh - scroll_;
            g.rect({area.x + 10 + col * cw, y + lh - 3, cw, 2}, fg);
        }
        g.popClip();
    }

private:
    std::string cwd_;
    std::vector<std::string> out_;
    std::vector<std::string> history_;
    int histPos_ = 0;
    LineEdit input_;
    float scroll_ = 0;
    bool stick_ = true;
    bool cleared_ = false;

    std::string prompt() const { return shell->displayPath(cwd_) + ">"; }

    static std::vector<std::string> split(const std::string& s) {
        std::vector<std::string> out;
        std::string cur;
        bool quote = false;
        for (char ch : s) {
            if (ch == '"') {
                quote = !quote;
                continue;
            }
            if (ch == ' ' && !quote) {
                if (!cur.empty()) out.push_back(cur);
                cur.clear();
            } else {
                cur += ch;
            }
        }
        if (!cur.empty()) out.push_back(cur);
        return out;
    }

    static std::string pad(const std::string& s, size_t n, bool right = false) {
        size_t len = utf8Length(s);
        if (len >= n) return s;
        return right ? std::string(n - len, ' ') + s : s + std::string(n - len, ' ');
    }

    void say(const std::string& s) { out_.push_back(s); }

    std::string rest(const std::string& cmd) {
        size_t sp = cmd.find(' ');
        if (sp == std::string::npos) return "";
        size_t st = cmd.find_first_not_of(' ', sp);
        return st == std::string::npos ? "" : cmd.substr(st);
    }

    void run(const std::string& line) {
        auto a = split(line);
        if (a.empty()) return;
        std::string c = toLower(a[0]);
        std::string arg = rest(line);
        if (!arg.empty() && arg.front() == '"' && arg.back() == '"' && arg.size() > 1) arg = arg.substr(1, arg.size() - 2);
        Shell* sh = shell;
        if (c == "help") {
            const char* lines[] = {"For more information on a specific command, type the command name.",
                                   "CD        Displays or changes the current directory.",
                                   "CLS       Clears the screen.",
                                   "DATE      Displays the date.",
                                   "DEL       Deletes one or more files.",
                                   "DIR       Displays a list of files and subdirectories.",
                                   "ECHO      Displays messages.",
                                   "EXIT      Quits the command prompt.",
                                   "MKDIR     Creates a directory.",
                                   "REN       Renames a file or directory.",
                                   "RMDIR     Removes a directory.",
                                   "START     Starts an app: explorer, notepad, calc, paint, settings, mines.",
                                   "SYSTEMINFO Displays system information.",
                                   "TIME      Displays the system time.",
                                   "TREE      Graphically displays the folder structure.",
                                   "TYPE      Displays the contents of a text file.",
                                   "VER       Displays the WinDroid version.",
                                   "WHOAMI    Displays the current user.",
                                   "SHUTDOWN  /s shut down, /l lock."};
            for (auto* l : lines) say(l);
        } else if (c == "cls" || c == "clear") {
            out_.clear();
            cleared_ = true;
        } else if (c == "echo") {
            say(arg.empty() ? "ECHO is on." : arg);
        } else if (c == "ver") {
            say("");
            say("WinDroid [Version 11.0.26100]");
        } else if (c == "whoami") {
            say("windroid\\" + toLower(sh->config().user));
        } else if (c == "hostname") {
            say(sh->platform().model);
        } else if (c == "date" || c == "time") {
            time_t t = time(nullptr);
            struct tm lt;
            localtime_r(&t, &lt);
            char buf[64];
            strftime(buf, sizeof buf, c == "date" ? "The current date is: %a %d/%m/%Y" : "The current time is: %H:%M:%S", &lt);
            say(buf);
        } else if (c == "cd" || c == "chdir") {
            if (arg.empty()) {
                say(sh->displayPath(cwd_));
                return;
            }
            std::string p = sh->realPath(arg, cwd_);
            if (fs::isDir(p))
                cwd_ = p;
            else
                say("The system cannot find the path specified.");
        } else if (c == "cd..") {
            if (cwd_ != sh->root()) cwd_ = fs::parent(cwd_);
        } else if (c == "dir" || c == "ls") {
            std::string p = arg.empty() ? cwd_ : sh->realPath(arg, cwd_);
            if (!fs::isDir(p)) {
                say("File Not Found");
                return;
            }
            say(" Volume in drive C has no label.");
            say("");
            say(" Directory of " + sh->displayPath(p));
            say("");
            int files = 0, dirs = 0;
            long long bytes = 0;
            for (auto& e : fs::list(p)) {
                std::string ts = fs::formatTime(e.mtime);
                if (e.dir) {
                    say(ts + "    <DIR>          " + e.name);
                    dirs++;
                } else {
                    say(ts + "    " + pad(std::to_string(e.size), 14, true) + " " + e.name);
                    files++;
                    bytes += e.size;
                }
            }
            say(pad(std::to_string(files), 16, true) + " File(s) " + pad(std::to_string(bytes), 14, true) + " bytes");
            long long total = 0, freeB = 0;
            fs::diskUsage(p, total, freeB);
            say(pad(std::to_string(dirs), 16, true) + " Dir(s)  " + pad(std::to_string(freeB), 14, true) + " bytes free");
        } else if (c == "mkdir" || c == "md") {
            if (arg.empty()) {
                say("The syntax of the command is incorrect.");
                return;
            }
            std::string p = sh->realPath(arg, cwd_);
            if (fs::exists(p))
                say("A subdirectory or file " + arg + " already exists.");
            else {
                fs::makeDirs(p);
                sh->notifyFs();
            }
        } else if (c == "rmdir" || c == "rd") {
            std::string p = sh->realPath(arg, cwd_);
            if (arg.empty() || !fs::isDir(p)) {
                say("The system cannot find the file specified.");
                return;
            }
            if (p == sh->root() || cwd_.compare(0, p.size(), p) == 0) {
                say("The process cannot access the file because it is being used by another process.");
                return;
            }
            fs::removeAll(p);
            sh->notifyFs();
        } else if (c == "del" || c == "erase" || c == "rm") {
            std::string p = sh->realPath(arg, cwd_);
            if (arg.empty() || !fs::exists(p) || fs::isDir(p)) {
                say("Could Not Find " + sh->displayPath(p));
                return;
            }
            fs::removeAll(p);
            sh->notifyFs();
        } else if (c == "type" || c == "cat") {
            std::string p = sh->realPath(arg, cwd_);
            std::string data;
            if (arg.empty() || fs::isDir(p) || !fs::readText(p, data)) {
                say("The system cannot find the file specified.");
                return;
            }
            std::stringstream ss(data);
            std::string l;
            while (std::getline(ss, l)) say(l);
        } else if (c == "ren" || c == "rename" || c == "move") {
            if (a.size() < 3) {
                say("The syntax of the command is incorrect.");
                return;
            }
            std::string from = sh->realPath(a[1], cwd_);
            std::string to = c == "move" ? sh->realPath(a[2], cwd_) : fs::join(fs::parent(from), a[2]);
            if (fs::isDir(to)) to = fs::join(to, fs::baseName(from));
            if (!fs::exists(from) || !fs::renamePath(from, to))
                say("The system cannot find the file specified.");
            else
                sh->notifyFs();
        } else if (c == "copy") {
            if (a.size() < 3) {
                say("The syntax of the command is incorrect.");
                return;
            }
            std::string from = sh->realPath(a[1], cwd_);
            std::string to = sh->realPath(a[2], cwd_);
            if (fs::isDir(to)) to = fs::join(to, fs::baseName(from));
            std::vector<unsigned char> data;
            if (!fs::readBinary(from, data) || !fs::writeBinary(to, data)) {
                say("The system cannot find the file specified.");
                return;
            }
            say("        1 file(s) copied.");
            sh->notifyFs();
        } else if (c == "tree") {
            say("Folder PATH listing");
            say(sh->displayPath(cwd_));
            tree(cwd_, "", 0);
        } else if (c == "systeminfo") {
            const Platform& p = sh->platform();
            say("Host Name:                 " + p.model);
            say("OS Name:                   WinDroid 11 Home");
            say("OS Version:                11.0.26100 Build 26100");
            say("System Manufacturer:       " + p.manufacturer);
            say("System Model:              " + p.model);
            say("System Type:               ARM64-based PC (" + p.abi + ")");
            say("Processor(s):              " + std::to_string(sysconf(_SC_NPROCESSORS_ONLN)) + " Processor(s) Installed.");
            say("Android Version:           " + (p.osVersion.empty() ? std::string("Unknown") : p.osVersion));
        } else if (c == "ipconfig") {
            say("");
            say("WinDroid IP Configuration");
            say("");
            say("Wireless LAN adapter Wi-Fi:");
            say("");
            say("   Connection-specific DNS Suffix  . : home");
            say("   IPv4 Address. . . . . . . . . . . : 192.168.0.42");
            say("   Subnet Mask . . . . . . . . . . . : 255.255.255.0");
            say("   Default Gateway . . . . . . . . . : 192.168.0.1");
        } else if (c == "ping") {
            std::string host = arg.empty() ? "localhost" : a[1];
            say("");
            say("Pinging " + host + " with 32 bytes of data:");
            for (int i = 0; i < 4; i++) say("Reply from " + host + ": bytes=32 time=" + std::to_string(12 + (i * 7) % 9) + "ms TTL=117");
        } else if (c == "notepad") {
            sh->open(AppKind::Notepad, arg.empty() ? "" : sh->realPath(arg, cwd_));
        } else if (c == "explorer") {
            sh->open(AppKind::Explorer, arg.empty() ? cwd_ : sh->realPath(arg, cwd_));
        } else if (c == "calc") {
            sh->open(AppKind::Calculator);
        } else if (c == "mspaint" || c == "paint") {
            sh->open(AppKind::Paint, arg.empty() ? "" : sh->realPath(arg, cwd_));
        } else if (c == "winver") {
            sh->open(AppKind::Settings, "about");
        } else if (c == "start") {
            std::string w = toLower(arg);
            if (w == "explorer" || w.empty())
                sh->open(AppKind::Explorer, cwd_);
            else if (w == "notepad")
                sh->open(AppKind::Notepad);
            else if (w == "calc")
                sh->open(AppKind::Calculator);
            else if (w == "paint" || w == "mspaint")
                sh->open(AppKind::Paint);
            else if (w == "settings" || w == "ms-settings:")
                sh->open(AppKind::Settings);
            else if (w == "mines" || w == "minesweeper")
                sh->open(AppKind::Minesweeper);
            else if (w == "cmd" || w == "terminal")
                sh->open(AppKind::Terminal, cwd_);
            else
                say("WinDroid cannot find '" + arg + "'. Make sure you typed the name correctly, and then try again.");
        } else if (c == "shutdown") {
            std::string f = toLower(arg);
            if (f.find("/s") != std::string::npos)
                sh->shutdown();
            else if (f.find("/l") != std::string::npos)
                sh->lock();
            else if (f.find("/r") != std::string::npos)
                sh->restart();
            else
                say("Usage: shutdown [/s | /r | /l]");
        } else if (c == "exit") {
            sh->close(this);
        } else if (c == "cmd") {
            say("WinDroid [Version 11.0.26100]");
        } else {
            say("'" + a[0] + "' is not recognized as an internal or external command,");
            say("operable program or batch file.");
        }
    }

    void tree(const std::string& dir, const std::string& prefix, int depth) {
        if (depth > 6) return;
        std::vector<fs::Entry> ds;
        for (auto& e : fs::list(dir))
            if (e.dir) ds.push_back(e);
        for (size_t i = 0; i < ds.size(); i++) {
            bool last = i + 1 == ds.size();
            say(prefix + (last ? "\\---" : "+---") + ds[i].name);
            tree(fs::join(dir, ds[i].name), prefix + (last ? "    " : "|   "), depth + 1);
        }
    }
};

std::unique_ptr<AppWindow> makeTerminal(Shell& s, const std::string& arg) { return std::make_unique<TerminalWindow>(s, arg); }

}
