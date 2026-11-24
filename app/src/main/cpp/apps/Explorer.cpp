#include <memory>

#include "Apps.h"
#include "Common.h"

namespace wd {

class ExplorerWindow : public AppWindow {
public:
    ExplorerWindow(Shell& s, const std::string& arg) : alive_(std::make_shared<bool>(true)) {
        prefW = 760;
        prefH = 470;
        minW = 340;
        minH = 260;
        std::string start = arg.empty() ? "pc" : arg;
        if (start != "pc" && !fs::isDir(start)) start = fs::join(s.userDir(), "Documents");
        path_ = start;
        updateTitle(s);
    }
    ~ExplorerWindow() override { *alive_ = false; }

    bool wantsText() const override { return addrFocus_; }

    bool onBack() override {
        if (!history_.empty()) {
            goBack();
            return true;
        }
        return false;
    }

    void draw(Ui& ui, const Rect& c) override {
        Gfx& g = *ui.g;
        const Theme& th = *ui.th;
        if (version_ != shell->fsVersion()) reload();
        g.rect(c, th.mica);
        bool narrow = c.w < 560;

        Rect addr{c.x, c.y + 2, c.w, 40};
        float bx = c.x + 8;
        if (toolButton(ui, {bx, addr.y + 4, 32, 32}, IcGlyphBack, "", false, false, !history_.empty())) goBack();
        bx += 34;
        if (toolButton(ui, {bx, addr.y + 4, 32, 32}, IcGlyphForward, "", false, false, !forward_.empty())) goForward();
        bx += 34;
        bool canUp = path_ != "pc";
        if (toolButton(ui, {bx, addr.y + 4, 32, 32}, IcGlyphUp, "", false, false, canUp)) {
            std::string up = path_ == shell->root() ? "pc" : fs::parent(path_);
            navigate(up);
        }
        bx += 34;
        if (toolButton(ui, {bx, addr.y + 4, 32, 32}, IcGlyphRefresh, "", false)) reload();
        bx += 40;
        Rect addrBox{bx, addr.y + 4, c.r() - bx - 10, 32};
        if (!addrFocus_) {
            std::string shown = path_ == "pc" ? "This PC" : shell->displayPath(path_);
            addrEdit_.set(shown);
        }
        bool wasFocus = addrFocus_;
        if (ui.lineEdit(addrBox, addrEdit_, addrFocus_, "")) {
            std::string t = addrEdit_.text;
            addrFocus_ = false;
            if (toLower(t) == "this pc" || t.empty()) {
                navigate("pc");
            } else {
                std::string rp = shell->realPath(t, path_ == "pc" ? shell->root() : path_);
                if (fs::isDir(rp))
                    navigate(rp);
                else if (fs::exists(rp))
                    shell->openFile(rp);
                else
                    shell->message("File Explorer", "Windroid can't find '" + t + "'. Check the spelling and try again.");
            }
        }
        if (wasFocus && ui.pressed && !addrBox.contains(ui.x, ui.y)) addrFocus_ = false;

        Rect cmd{c.x, addr.b() + 2, c.w, 42};
        g.rect({cmd.x, cmd.y, cmd.w, 1}, th.border);
        g.rect({cmd.x, cmd.b() - 1, cmd.w, 1}, th.border);
        float cx = c.x + 8;
        bool inFolder = path_ != "pc";
        bool inRecycle = path_ == shell->recycleDir();
        bool hasSel = sel_ >= 0 && sel_ < (int)entries_.size();
        struct Cmd {
            IconId icon;
            const char* label;
            bool enabled;
            int id;
        };
        std::vector<Cmd> cmds;
        if (inRecycle) {
            cmds.push_back({IcGlyphTrash, "Empty Recycle Bin", !entries_.empty(), 10});
            cmds.push_back({IcGlyphTrash, "Delete", hasSel, 4});
        } else {
            cmds.push_back({IcGlyphNewFolder, "New folder", inFolder, 1});
            cmds.push_back({IcGlyphNew, "New file", inFolder, 2});
            cmds.push_back({IcGlyphRename, "Rename", hasSel, 3});
            cmds.push_back({IcGlyphTrash, "Delete", hasSel, 4});
            cmds.push_back({IcTerminal, "Terminal", inFolder, 5});
            bool selFile = hasSel && !entries_[(size_t)sel_].dir;
            if (selFile && fs::extension(entries_[(size_t)sel_].name) == "exe") cmds.push_back({IcGlyphOpen, "Install", true, 6});
            if (selFile && (fs::extension(entries_[(size_t)sel_].name) == "zip" || fs::extension(entries_[(size_t)sel_].name) == "rar"))
                cmds.push_back({IcArchive, "Extract all", true, 8});
            if (selFile) cmds.push_back({IcGlyphBluetooth, "Send", true, 7});
        }
        for (auto& cm : cmds) {
            float w = toolButtonWidth(g, cm.label, !narrow);
            if (cx + w > c.r() - 8) break;
            if (toolButton(ui, {cx, cmd.y + 5, w, 32}, cm.icon, cm.label, !narrow, false, cm.enabled)) command(cm.id);
            cx += w + 4;
        }

        Rect statusR{c.x, c.b() - 26, c.w, 26};
        Rect body{c.x, cmd.b(), c.w, statusR.y - cmd.b()};
        float navW = c.w < 500 ? 0 : 176;
        if (navW > 0) drawNav(ui, {body.x, body.y, navW, body.h});
        Rect main{body.x + navW, body.y, body.w - navW, body.h};
        g.rect(main, th.dark ? rgb(0x1C1C1C) : rgb(0xFFFFFF));
        if (navW > 0) g.rect({main.x, main.y, 1, main.h}, th.border);
        if (path_ == "pc")
            drawPC(ui, main);
        else
            drawList(ui, main);

        g.rect({statusR.x, statusR.y, statusR.w, 1}, th.border);
        std::string st = path_ == "pc" ? "6 items" : std::to_string(entries_.size()) + (entries_.size() == 1 ? " item" : " items");
        if (hasSel) st += "    1 item selected";
        if (hasSel && !entries_[(size_t)sel_].dir) st += "  " + fs::formatSize(entries_[(size_t)sel_].size);
        g.textIn({statusR.x + 12, statusR.y, statusR.w, statusR.h}, 11.5f, st, th.textDim);
    }

private:
    std::shared_ptr<bool> alive_;
    std::string path_;
    std::vector<std::string> history_;
    std::vector<std::string> forward_;
    std::vector<fs::Entry> entries_;
    int version_ = -1;
    int sel_ = -1;
    float scroll_ = 0;
    LineEdit addrEdit_;
    bool addrFocus_ = false;

    void updateTitle(Shell& s) {
        if (path_ == "pc")
            title = "This PC - File Explorer";
        else if (path_ == s.recycleDir())
            title = "Recycle Bin - File Explorer";
        else if (path_ == s.root())
            title = "Local Disk (C:) - File Explorer";
        else
            title = fs::baseName(path_) + " - File Explorer";
    }

    void reload() {
        version_ = shell->fsVersion();
        entries_ = path_ == "pc" ? std::vector<fs::Entry>() : fs::list(path_);
        if (sel_ >= (int)entries_.size()) sel_ = -1;
    }

    void navigate(const std::string& p, bool record = true) {
        if (p == path_) return;
        if (record) {
            history_.push_back(path_);
            forward_.clear();
        }
        path_ = p;
        sel_ = -1;
        scroll_ = 0;
        reload();
        updateTitle(*shell);
    }

    void goBack() {
        if (history_.empty()) return;
        forward_.push_back(path_);
        std::string p = history_.back();
        history_.pop_back();
        navigate(p, false);
    }

    void goForward() {
        if (forward_.empty()) return;
        history_.push_back(path_);
        std::string p = forward_.back();
        forward_.pop_back();
        navigate(p, false);
    }

    void openEntry(int i) {
        const fs::Entry& e = entries_[(size_t)i];
        std::string full = fs::join(path_, e.name);
        if (e.dir)
            navigate(full);
        else
            shell->openFile(full);
    }

    void command(int id) {
        Shell* sh = shell;
        if (id == 1) {
            std::string dir = path_;
            sh->prompt("New folder", "Folder name:", fs::uniqueName(dir, "New folder", ""), [sh, dir](const std::string& n) {
                if (n.empty()) return;
                fs::makeDirs(fs::join(dir, n));
                sh->notifyFs();
            });
        } else if (id == 2) {
            std::string dir = path_;
            sh->prompt("New file", "File name:", fs::uniqueName(dir, "New Text Document", "txt"), [sh, dir](const std::string& n) {
                if (n.empty()) return;
                std::string p = fs::join(dir, n);
                if (!fs::exists(p)) fs::writeText(p, "");
                sh->notifyFs();
            });
        } else if (id == 3 && sel_ >= 0) {
            std::string old = fs::join(path_, entries_[(size_t)sel_].name);
            sh->prompt("Rename", "Enter a new name:", entries_[(size_t)sel_].name, [sh, old](const std::string& n) {
                if (n.empty()) return;
                fs::renamePath(old, fs::join(fs::parent(old), n));
                sh->notifyFs();
            });
        } else if (id == 4 && sel_ >= 0) {
            sh->recycle(fs::join(path_, entries_[(size_t)sel_].name));
            sel_ = -1;
        } else if (id == 5) {
            sh->open(AppKind::Terminal, path_);
        } else if (id == 6 && sel_ >= 0) {
            sh->openProgram(fs::join(path_, entries_[(size_t)sel_].name), false);
        } else if (id == 7 && sel_ >= 0) {
            std::string err = sh->sys("bt.send", fs::join(path_, entries_[(size_t)sel_].name));
            if (!err.empty()) sh->message("Send via Bluetooth", err);
        } else if (id == 8 && sel_ >= 0) {
            sh->extract(fs::join(path_, entries_[(size_t)sel_].name));
        } else if (id == 10) {
            for (auto& e : fs::list(sh->recycleDir())) fs::removeAll(fs::join(sh->recycleDir(), e.name));
            sh->notifyFs();
        }
    }

    void drawNav(Ui& ui, const Rect& r) {
        Gfx& g = *ui.g;
        const Theme& th = *ui.th;
        std::string u = shell->userDir();
        struct N {
            const char* label;
            IconId icon;
            std::string path;
        } items[] = {{"Desktop", IcFolder, fs::join(u, "Desktop")},     {"Documents", IcFolder, fs::join(u, "Documents")},
                     {"Downloads", IcFolder, fs::join(u, "Downloads")}, {"Pictures", IcImage, fs::join(u, "Pictures")},
                     {"Music", IcFolder, fs::join(u, "Music")},         {"This PC", IcPC, "pc"},
                     {"Local Disk (C:)", IcDrive, shell->root()},       {"Recycle Bin", IcBin, shell->recycleDir()}};
        float y = r.y + 8;
        g.pushClip(r);
        for (int i = 0; i < 8; i++) {
            if (i == 5) {
                g.rect({r.x + 12, y + 3, r.w - 24, 1}, th.border);
                y += 8;
            }
            Rect ir{r.x + 6, y, r.w - 12, 30};
            bool selNow = path_ == items[i].path;
            if (selNow) g.rect(ir, th.press, 4);
            if (ui.held(ir)) g.rect(ir, th.press, 4);
            if (selNow) g.rect({ir.x, ir.cy() - 8, 3, 16}, th.accent, 1.5f);
            drawIcon(g, items[i].icon, {ir.x + 12, ir.cy() - 9, 18, 18}, th.text);
            g.textIn({ir.x + 40, ir.y, ir.w - 40, ir.h}, 12, items[i].label, th.text);
            if (ui.clicked(ir)) navigate(items[i].path);
            y += 32;
        }
        g.popClip();
    }

    void drawPC(Ui& ui, const Rect& r) {
        Gfx& g = *ui.g;
        const Theme& th = *ui.th;
        g.pushClip(r);
        float x = r.x + 20, y = r.y + 14;
        g.textBold(x, y, 13, "Folders", th.text);
        y += 28;
        std::string u = shell->userDir();
        const char* names[] = {"Desktop", "Documents", "Downloads", "Pictures", "Music"};
        float tileW = 170;
        int cols = std::max(1, (int)((r.w - 40) / (tileW + 8)));
        for (int i = 0; i < 5; i++) {
            Rect t{x + (i % cols) * (tileW + 8), y + (i / cols) * 56, tileW, 50};
            ui.pressFill(t, 5);
            drawIcon(g, i == 3 ? IcImage : IcFolder, {t.x + 8, t.cy() - 16, 32, 32}, th.text);
            g.text(t.x + 48, t.y + 8, 12.5f, names[i], th.text);
            g.text(t.x + 48, t.y + 27, 11, "Stored locally", th.textDim);
            if (ui.clicked(t)) navigate(fs::join(u, names[i]));
        }
        y += ((5 + cols - 1) / cols) * 56 + 12;
        g.textBold(x, y, 13, "Devices and drives", th.text);
        y += 28;
        Rect d{x, y, std::min(300.f, r.w - 40), 64};
        ui.pressFill(d, 5);
        drawIcon(g, IcDrive, {d.x + 8, d.y + 8, 40, 40}, th.text);
        g.text(d.x + 58, d.y + 6, 12.5f, "Local Disk (C:)", th.text);
        long long total = 0, freeB = 0;
        fs::diskUsage(shell->root(), total, freeB);
        float used = total > 0 ? (float)(total - freeB) / (float)total : 0;
        Rect bar{d.x + 58, d.y + 28, d.w - 70, 12};
        g.rect(bar, th.dark ? rgb(0x3A3A3A) : rgb(0xE6E6E6), 1);
        g.rect({bar.x, bar.y, bar.w * used, bar.h}, used > 0.9f ? th.danger : th.accent, 1);
        g.text(d.x + 58, d.y + 44, 11, fs::formatSize(freeB) + " free of " + fs::formatSize(total), th.textDim);
        if (ui.clicked(d)) navigate(shell->root());
        g.popClip();
    }

    void drawList(Ui& ui, const Rect& r) {
        Gfx& g = *ui.g;
        const Theme& th = *ui.th;
        float rowH = 32;
        Rect head{r.x, r.y, r.w, 30};
        float nameW = std::max(160.f, r.w * 0.42f);
        bool showDate = r.w > 420, showType = r.w > 560, showSize = r.w > 330;
        float colX[4] = {r.x + 14, r.x + nameW, r.x + nameW + 150, r.x + nameW + 290};
        if (!showDate) colX[3] = r.x + nameW;
        g.textIn({colX[0] + 30, head.y, 100, head.h}, 11.5f, "Name", th.textDim);
        if (showDate) g.textIn({colX[1], head.y, 140, head.h}, 11.5f, "Date modified", th.textDim);
        if (showType) g.textIn({colX[2], head.y, 140, head.h}, 11.5f, "Type", th.textDim);
        if (showSize) g.textIn({colX[3], head.y, 100, head.h}, 11.5f, "Size", th.textDim);
        g.rect({r.x, head.b(), r.w, 1}, th.border);

        Rect list{r.x, head.b() + 1, r.w, r.h - head.h - 1};
        float contentH = entries_.size() * rowH + 8;
        if (ui.dragging(list)) scroll_ -= ui.dy();
        scroll_ = clampf(scroll_, 0, std::max(0.f, contentH - list.h));
        g.pushClip(list);
        bool hit = false;
        for (size_t i = 0; i < entries_.size(); i++) {
            float y = list.y + 4 + i * rowH - scroll_;
            if (y + rowH < list.y || y > list.b()) continue;
            Rect row{r.x + 4, y, r.w - 8, rowH - 2};
            const fs::Entry& e = entries_[i];
            ui.pressFill(row, 4, (int)i == sel_);
            std::string ex = fs::extension(e.name);
            IconId ic = e.dir ? IcFolder : (ex == "bmp" ? IcImage : (ex == "zip" || ex == "rar" ? IcArchive : IcFile));
            shell->drawFileIcon(fs::join(path_, e.name), ic, {colX[0], row.cy() - 10, 20, 20}, th.text);
            float nameMax = (showDate ? colX[1] : (showSize ? colX[3] : r.r())) - colX[0] - 40;
            std::string shown = !e.dir && fs::extension(e.name) == "lnk" ? fs::stem(e.name) : e.name;
            g.textIn({colX[0] + 30, row.y, nameMax, row.h}, 12.5f, g.ellipsize(12.5f, shown, nameMax), th.text);
            if (showDate) g.textIn({colX[1], row.y, 140, row.h}, 12, fs::formatTime(e.mtime), th.textDim);
            if (showType) g.textIn({colX[2], row.y, 140, row.h}, 12, g.ellipsize(12, fs::typeName(e), 132), th.textDim);
            if (showSize && !e.dir) g.textIn({colX[3], row.y, 90, row.h}, 12, fs::formatSize(e.size), th.textDim);
            if (ui.clicked(row)) {
                hit = true;
                if (ui.taps >= 2 && sel_ == (int)i) {
                    openEntry((int)i);
                    break;
                }
                sel_ = (int)i;
            }
            if (ui.longPressed(row)) {
                hit = true;
                sel_ = (int)i;
            }
        }
        if (entries_.empty()) g.textIn({list.x, list.y + 30, list.w, 20}, 12.5f, "This folder is empty.", th.textDim, AlignCenter);
        if (!hit && ui.clicked(list)) sel_ = -1;
        g.popClip();
    }
};

std::unique_ptr<AppWindow> makeExplorer(Shell& s, const std::string& arg) { return std::make_unique<ExplorerWindow>(s, arg); }

}
