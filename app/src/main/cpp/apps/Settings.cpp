#include <GLES2/gl2.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <memory>

#include "Apps.h"
#include "Common.h"
#include "../shell/Programs.h"

namespace wd {

static const uint32_t kAccentColors[] = {0x0078D4, 0x8764B8, 0x107C10, 0xCA5010, 0xE3008C, 0x038387, 0xC30052, 0x6B69D6};

class SettingsWindow : public AppWindow {
public:
    SettingsWindow(const std::string& arg) {
        prefW = 800;
        prefH = 520;
        minW = 360;
        minH = 300;
        if (arg == "bluetooth") page_ = 1;
        if (arg == "personalization") page_ = 2;
        if (arg == "apps") page_ = 3;
        if (arg == "accounts") page_ = 4;
        if (arg == "time") page_ = 5;
        if (arg == "about") page_ = 6;
    }

    bool wantsText() const override { return findFocus_; }

    void draw(Ui& ui, const Rect& c) override {
        Gfx& g = *ui.g;
        const Theme& th = *ui.th;
        g.rect(c, th.mica);
        bool compact = c.w < 620;
        float navW = compact ? 60 : 250;
        Rect nav{c.x, c.y, navW, c.h};
        drawNav(ui, nav, compact);
        Rect content{c.x + navW, c.y, c.w - navW, c.h};

        const char* titles[] = {"System", "Bluetooth & devices", "Personalization", "Apps", "Accounts", "Time & language", "About"};
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
            case 1: y = pageBluetooth(ui, x, y, w); break;
            case 2: y = pagePersonal(ui, x, y, w); break;
            case 3: y = pageApps(ui, x, y, w); break;
            case 4: y = pageAccounts(ui, x, y, w); break;
            case 5: y = pageTime(ui, x, y, w); break;
            case 6: y = pageAbout(ui, x, y, w); break;
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

    LineEdit find_;
    bool findFocus_ = false;

    static bool matches(const std::string& label, const std::string& q) {
        std::string a, b;
        for (char ch : label) a += (char)tolower((unsigned char)ch);
        for (char ch : q) b += (char)tolower((unsigned char)ch);
        size_t s = b.find_first_not_of(' ');
        if (s == std::string::npos) return true;
        b = b.substr(s);
        while (!b.empty() && b.back() == ' ') b.pop_back();
        return a.find(b) != std::string::npos;
    }

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
        IconId icons[] = {IcPC, IcGlyphBluetooth, IcGlyphBrush, IcGlyphGrid, IcUser, IcClock, IcGlyphGear};
        const char* labels[] = {"System", "Bluetooth & devices", "Personalization", "Apps", "Accounts", "Time & language", "About"};
        std::string q;
        if (!compact) {
            Rect sb{r.x + 10, y, r.w - 20, 36};
            if (ui.lineEdit(sb, find_, findFocus_, "Find a setting", 12.5f, 22, 18)) {
                for (int i = 0; i < 7; i++)
                    if (matches(labels[i], find_.text)) {
                        page_ = i;
                        break;
                    }
                findFocus_ = false;
            }
            if (findFocus_ && ui.pressed && !sb.contains(ui.x, ui.y)) findFocus_ = false;
            drawIcon(g, IcGlyphSearch, {sb.r() - 28, sb.cy() - 7, 14, 14}, th.textDim);
            q = find_.text;
            y += 48;
        }
        for (int i = 0; i < 7; i++) {
            if (!q.empty() && !matches(labels[i], q)) continue;
            Rect ir{r.x + 6, y, r.w - 12, 42};
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
            y += 44;
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

        g.textBold(x, y, 13, "Sound", th.text);
        y += 26;
        ty = y;
        y = row(ui, x, y, w, IcGlyphVolume, "System sounds", "Play sounds for startup, sign in, notifications and devices");
        bool snd = cfg.sounds;
        if (ui.toggle({x + w - 64, ty + 22, 48, 20}, snd)) {
            cfg.sounds = snd;
            shell->saveConfig();
            shell->applySoundConfig();
            if (snd) shell->playSound("notify");
        }
        g.text(x + w - 104, ty + 23, 12, cfg.sounds ? "On" : "Off", th.text);
        if (cfg.sounds) {
            ty = y;
            y = row(ui, x, y, w, IcGlyphVolume, "Try the sounds", "");
            const char* names[] = {"startup", "notify", "error", "device-in"};
            const char* labels[] = {"Startup", "Notify", "Error", "Device"};
            float bw = 74;
            for (int i = 0; i < 4; i++) {
                Rect b{x + w - (4 - i) * (bw + 6) - 10, ty + 16, bw, 32};
                if (b.x < x + 170) continue;
                if (ui.button(b, labels[i])) shell->playSound(names[i]);
            }
        }
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

    struct BtDevice {
        std::string address, name, kind;
        int bond = 0;
        bool connected = false;
        int battery = -1;
        int rssi = 0;
    };
    double btPoll_ = -10;
    bool btSupported_ = false, btOn_ = false, btScanning_ = false, btPerm_ = false;
    std::string btName_;
    std::vector<BtDevice> btDevices_;
    std::string toast_;
    double toastT_ = -10;
    double rtPoll_ = -10;
    std::vector<std::string> rt_;
    int rtSeq_ = -1;

    static std::vector<std::string> split(const std::string& s, char sep) {
        std::vector<std::string> out;
        size_t i = 0;
        while (true) {
            size_t j = s.find(sep, i);
            out.push_back(s.substr(i, j == std::string::npos ? std::string::npos : j - i));
            if (j == std::string::npos) break;
            i = j + 1;
        }
        return out;
    }

    void showToast(const std::string& t) {
        if (t.empty()) return;
        toast_ = t;
        toastT_ = shell->now();
    }

    void pollBluetooth() {
        if (shell->now() - btPoll_ < 0.6) return;
        btPoll_ = shell->now();
        auto st = split(shell->sys("bt.state"), '\t');
        btSupported_ = st.size() >= 4 && st[0] == "1";
        btOn_ = st.size() >= 2 && st[1] == "1";
        btScanning_ = st.size() >= 3 && st[2] == "1";
        btPerm_ = st.size() >= 4 && st[3] == "1";
        btName_ = st.size() >= 5 ? st[4] : "";
        if (st.size() >= 6) showToast(st[5]);
        btDevices_.clear();
        for (auto& line : split(shell->sys("bt.devices"), '\n')) {
            auto f = split(line, '\t');
            if (f.size() < 7) continue;
            BtDevice d;
            d.address = f[0];
            d.name = f[1];
            d.kind = f[2];
            d.bond = atoi(f[3].c_str());
            d.connected = f[4] == "1";
            d.battery = atoi(f[5].c_str());
            d.rssi = atoi(f[6].c_str());
            btDevices_.push_back(d);
        }
        std::stable_sort(btDevices_.begin(), btDevices_.end(), [](const BtDevice& a, const BtDevice& b) {
            if (a.connected != b.connected) return a.connected;
            return false;
        });
    }

    static IconId kindIcon(const std::string& k) {
        if (k == "audio" || k == "headset") return IcGlyphVolume;
        if (k == "keyboard") return IcGlyphKeyboard;
        if (k == "gamepad") return IcGlyphGamepad;
        if (k == "computer") return IcPC;
        if (k == "watch") return IcClock;
        if (k == "mouse" || k == "input") return IcGlyphGrid;
        return IcGlyphBluetooth;
    }

    static std::string kindName(const std::string& k) {
        if (k == "audio") return "Audio";
        if (k == "headset") return "Headphones";
        if (k == "keyboard") return "Keyboard";
        if (k == "gamepad") return "Game controller";
        if (k == "mouse") return "Mouse";
        if (k == "input") return "Input device";
        if (k == "phone") return "Phone";
        if (k == "computer") return "Computer";
        if (k == "watch") return "Wearable";
        return "Other device";
    }

    float drawToast(Ui& ui, float x, float y, float w) {
        Gfx& g = *ui.g;
        const Theme& th = *ui.th;
        if (toast_.empty() || shell->now() - toastT_ > 5) return y;
        Rect r{x, y, w, 40};
        g.rect(r, th.dark ? rgb(0x1F3A52) : rgb(0xDCEBFA), 6);
        g.textIn({r.x + 14, r.y, r.w - 28, r.h}, 12.5f, g.ellipsize(12.5f, toast_, r.w - 28), th.text);
        return y + 48;
    }

    float pageBluetooth(Ui& ui, float x, float y, float w) {
        Gfx& g = *ui.g;
        const Theme& th = *ui.th;
        if (!shell->platform().sys) {
            return row(ui, x, y, w, IcGlyphBluetooth, "Bluetooth", "Bluetooth is only available on Android");
        }
        pollBluetooth();
        y = drawToast(ui, x, y, w);
        if (!btSupported_) return row(ui, x, y, w, IcGlyphBluetooth, "Bluetooth", "This device doesn't have Bluetooth");
        if (!btPerm_) {
            float ty = y;
            y = row(ui, x, y, w, IcGlyphLock, "Allow Bluetooth access", "WinDroid needs permission to find and connect to nearby devices", 72);
            if (ui.button({x + w - 116, ty + 20, 100, 32}, "Allow", true)) shell->sys("bt.permission");
            y += 10;
        }
        float ty = y;
        std::string desc = btOn_ ? (btName_.empty() ? "On" : "Discoverable as \"" + btName_ + "\"") : "Off";
        y = row(ui, x, y, w, IcGlyphBluetooth, "Bluetooth", desc, 72);
        bool on = btOn_;
        if (ui.toggle({x + w - 64, ty + 26, 48, 20}, on)) {
            shell->sys("bt.enable", on ? "1" : "0");
            btOn_ = on;
            btPoll_ = shell->now() + 1;
        }
        g.text(x + w - 104, ty + 27, 12, btOn_ ? "On" : "Off", th.text);
        if (btOn_) {
            ty = y;
            y = row(ui, x, y, w, IcGlyphPlus, "Add device", btScanning_ ? "Searching for devices..." : "Find headphones, controllers, keyboards and more");
            if (btScanning_) {
                float a = (float)shell->now() * 5.f;
                g.arc(x + w - 150, ty + 32, 8, a, a + 4.2f, 2, th.accent);
                if (ui.button({x + w - 116, ty + 16, 100, 32}, "Stop")) {
                    shell->sys("bt.scan", "0");
                    btPoll_ = -10;
                }
            } else if (ui.button({x + w - 116, ty + 16, 100, 32}, "Add device", true)) {
                shell->sys("bt.scan", "1");
                btPoll_ = shell->now() - 0.3;
            }
        }
        y += 14;

        std::vector<const BtDevice*> paired, found;
        for (auto& d : btDevices_) (d.bond == 1 ? paired : found).push_back(&d);
        if (btOn_ || !paired.empty()) {
            g.textBold(x, y, 13, "Your devices", th.text);
            y += 26;
            if (paired.empty()) y = row(ui, x, y, w, IcGlyphBluetooth, "No paired devices", "Tap Add device to pair one");
            for (auto* d : paired) {
                ty = y;
                std::string st = d->connected ? "Connected" : "Paired";
                if (d->battery >= 0) st += "  \xC2\xB7  " + std::to_string(d->battery) + "% battery";
                st = kindName(d->kind) + "  \xC2\xB7  " + st;
                y = row(ui, x, y, w, kindIcon(d->kind), d->name, st);
                if (d->connected) g.circle(x + 40, ty + 44, 4, rgb(0x6CCB5F));
                if (d->battery >= 0 && w > 460) drawIcon(g, IcGlyphBattery, {x + w - 160, ty + 24, 22, 16}, th.text, d->battery / 100.f);
                std::string addr = d->address;
                if (ui.button({x + w - 116, ty + 16, 100, 32}, "Remove")) {
                    shell->sys("bt.unpair", addr);
                    btPoll_ = shell->now() - 0.2;
                }
            }
            y += 14;
        }
        if (btOn_ && (btScanning_ || !found.empty())) {
            g.textBold(x, y, 13, "Available devices", th.text);
            y += 26;
            if (found.empty()) y = row(ui, x, y, w, IcGlyphSearch, "Looking for devices", "Make sure your device is turned on and in pairing mode");
            for (auto* d : found) {
                ty = y;
                std::string st = kindName(d->kind);
                if (d->rssi != 0) st += d->rssi > -60 ? "  \xC2\xB7  Strong signal" : (d->rssi > -80 ? "  \xC2\xB7  Good signal" : "  \xC2\xB7  Weak signal");
                y = row(ui, x, y, w, kindIcon(d->kind), d->name, st);
                std::string addr = d->address;
                if (d->bond == 2) {
                    g.text(x + w - 104, ty + 23, 12, "Pairing...", th.textDim);
                } else if (ui.button({x + w - 116, ty + 16, 100, 32}, "Pair", true)) {
                    shell->sys("bt.pair", addr);
                    showToast("Pairing with " + d->name + "...");
                }
            }
            y += 14;
        }
        g.textBold(x, y, 13, "More settings", th.text);
        y += 26;
        ty = y;
        y = row(ui, x, y, w, IcGlyphRename, "Device name", btName_.empty() ? "Unknown" : btName_);
        Shell* sh = shell;
        if (ui.button({x + w - 116, ty + 16, 100, 32}, "Rename", false, btPerm_)) {
            sh->prompt("Rename this device", "Bluetooth name:", btName_, [sh](const std::string& n) {
                if (!n.empty()) sh->sys("bt.rename", n);
            });
        }
        ty = y;
        y = row(ui, x, y, w, IcGlyphGamepad, "Game controllers", "Controllers work in WinDroid and in Browser games");
        ty = y;
        y = row(ui, x, y, w, IcGlyphExternal, "Android Bluetooth settings", "Connect audio devices and manage advanced options");
        if (ui.button({x + w - 116, ty + 16, 100, 32}, "Open")) shell->sys("bt.settings");
        return y;
    }

    void pollRuntime() {
        if (shell->now() - rtPoll_ < 0.4) return;
        rtPoll_ = shell->now();
        rt_ = split(shell->sys("rt.status"), '\t');
        while (rt_.size() < 8) rt_.push_back("");
        int seq = atoi(rt_[7].c_str());
        if (seq != rtSeq_) {
            if (rtSeq_ >= 0 && !rt_[5].empty()) showToast(rt_[5]);
            rtSeq_ = seq;
        }
    }

    float pageApps(Ui& ui, float x, float y, float w) {
        Gfx& g = *ui.g;
        const Theme& th = *ui.th;
        Shell* sh = shell;
        y = drawToast(ui, x, y, w);
        g.textBold(x, y, 13, "Windows compatibility", th.text);
        y += 26;
        if (!shell->platform().sys) {
            y = row(ui, x, y, w, IcDialog, "Windows programs", "Running .exe files is only available on Android");
        } else {
            pollRuntime();
            bool box = rt_[0] == "1", inst = rt_[1] == "1";
            std::string state = rt_[2];
            int progress = atoi(rt_[3].c_str());
            float ty = y;
            if (!box) {
                y = row(ui, x, y, w, IcDialog, "proot is missing", "This build of WinDroid can't start the Windows runtime", 72);
            } else if (state == "downloading" || state == "extracting") {
                card(g, th, {x, y, w, 88});
                drawIcon(g, IcGlyphDown, {x + 18, y + 18, 20, 20}, th.text);
                long long done = atoll(rt_[6].c_str());
                std::string label = state == "downloading" ? "Downloading runtime" : "Installing runtime";
                g.text(x + 54, y + 16, 13, label, th.text);
                std::string sub = fs::formatSize(done) + (progress >= 0 ? "  \xC2\xB7  " + std::to_string(progress / 10) + "%" : "");
                g.text(x + 54, y + 36, 11.5f, sub, th.textDim);
                Rect bar{x + 54, y + 62, w - 190, 6};
                g.rect(bar, th.dark ? rgb(0x3A3A3A) : rgb(0xE6E6E6), 3);
                if (progress >= 0) g.rect({bar.x, bar.y, bar.w * progress / 1000.f, bar.h}, th.accent, 3);
                if (ui.button({x + w - 116, y + 28, 100, 32}, "Cancel")) sh->sys("rt.cancel");
                y += 92;
            } else if (!inst) {
                std::string sub = state == "failed" && !rt_[5].empty() ? "Last attempt failed: " + rt_[5] : "Download Box64 + Wine once (about 300 MB) to open .exe files";
                y = row(ui, x, y, w, IcDialog, "Run Windows programs", sub, 72);
                if (ui.button({x + w - 116, ty + 20, 100, 32}, "Download", true)) sh->sys("rt.install", "");
                ty = y;
                y = row(ui, x, y, w, IcGlyphOpen, "Install from a file", "Use a windroid-runtime .tar.gz you already downloaded");
                if (ui.button({x + w - 116, ty + 16, 100, 32}, "Choose")) {
                    std::string def = shell->displayPath(fs::join(shell->userDir(), "Downloads/windroid-runtime-arm64.tar.gz"));
                    sh->prompt("Install runtime", "Path to the runtime package:", def, [sh](const std::string& p) {
                        if (!p.empty()) sh->sys("rt.install", sh->realPath(p, sh->root()));
                    });
                }
            } else {
                std::string ver = rt_[4].empty() ? "Installed" : "Installed  \xC2\xB7  " + rt_[4];
                y = row(ui, x, y, w, IcDialog, "Windows compatibility runtime", ver, 72);
                g.circle(x + 40, ty + 50, 4, rgb(0x6CCB5F));
                if (ui.button({x + w - 116, ty + 20, 100, 32}, "Remove")) sh->sys("rt.remove");
                ty = y;
                y = row(ui, x, y, w, IcGlyphRestart, "Reset Windows settings", "Deletes the Wine prefix (registry and C:\\windows) and starts fresh");
                if (ui.button({x + w - 116, ty + 16, 100, 32}, "Reset")) sh->sys("rt.reset");
            }
        }
        y += 14;
        g.textBold(x, y, 13, "Installed apps", th.text);
        y += 26;
        auto progs = installedPrograms(*shell);
        if (progs.empty()) y = row(ui, x, y, w, IcGlyphGrid, "No Windows apps installed", "Open an .exe file and choose Install");
        for (auto& p : progs) {
            float ty = y;
            card(g, th, {x, y, w, 64});
            shell->drawFileIcon(p.target, IcDialog, {x + 16, y + 18, 28, 28}, th.text);
            g.text(x + 58, y + 14, 13, g.ellipsize(13, p.name, w - 260), th.text);
            g.text(x + 58, y + 34, 11.5f, g.ellipsize(11.5f, shell->displayPath(p.target), w - 260), th.textDim);
            y += 68;
            Shortcut sc = p;
            if (ui.button({x + w - 116, ty + 16, 100, 32}, "Uninstall")) {
                uninstallProgram(*sh, sc);
                showToast(sc.name + " was uninstalled");
            }
            if (ui.button({x + w - 222, ty + 16, 100, 32}, "Open", true)) sh->openFile(sc.path);
        }
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
