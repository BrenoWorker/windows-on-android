#include <ctime>
#include <memory>
#include <unordered_map>

#include "Apps.h"
#include "Common.h"

namespace wd {

class NotepadWindow : public AppWindow {
public:
    NotepadWindow(Shell& s, const std::string& path) : alive_(std::make_shared<bool>(true)) {
        prefW = 660;
        prefH = 440;
        minW = 300;
        minH = 220;
        if (!path.empty()) {
            std::string data;
            if (fs::readText(path, data)) {
                text_ = data;
                path_ = path;
            }
        }
        (void)s;
        updateTitle();
    }
    ~NotepadWindow() override { *alive_ = false; }

    bool wantsText() const override { return true; }

    bool onBack() override {
        if (menu_ >= 0) {
            menu_ = -1;
            return true;
        }
        return false;
    }

    void draw(Ui& ui, const Rect& c) override {
        Gfx& g = *ui.g;
        const Theme& th = *ui.th;
        g.rect(c, th.mica);

        Rect menuBar{c.x, c.y, c.w, 34};
        const char* menus[] = {"File", "Edit", "View"};
        float mx = c.x + 6;
        Rect menuRects[3];
        for (int i = 0; i < 3; i++) {
            float w = g.textWidth(12.5f, menus[i]) + 22;
            menuRects[i] = {mx, c.y + 3, w, 28};
            mx += w + 2;
            ui.pressFill(menuRects[i], 4, menu_ == i);
            g.textIn(menuRects[i], 12.5f, menus[i], th.text, AlignCenter);
            if (ui.clicked(menuRects[i])) menu_ = menu_ == i ? -1 : i;
        }

        Rect status{c.x, c.b() - 26, c.w, 26};
        Rect area{c.x, menuBar.b(), c.w, status.y - menuBar.b()};
        g.rect(area, th.dark ? rgb(0x1E1E1E) : rgb(0xFFFFFF));
        g.rect({area.x, area.y, area.w, 1}, th.border);
        Rect textR = area.inset(12, 8);
        float size = 14 * zoom_ / 100.0f;
        float lh = g.lineHeight(size) * 1.2f;

        layout(g, size, textR.w);

        bool menuOpen = menu_ >= 0;
        bool moved = false;
        if (ui.focus && !menuOpen) moved = handleKeys(ui, g, size);

        if (!menuOpen) {
            if (ui.dragging(area)) {
                scroll_ -= ui.dy();
            }
            if (ui.clicked(area)) {
                int li = (int)((ui.y - textR.y + scroll_) / lh);
                li = std::max(0, std::min((int)lines_.size() - 1, li));
                caret_ = indexAt(g, size, lines_[(size_t)li], ui.x - textR.x);
                ui.requestKeyboard = true;
                desiredX_ = -1;
            }
        }
        float contentH = lines_.size() * lh;
        int cl = caretLine();
        float caretY = cl * lh;
        if (moved) {
            if (caretY - scroll_ < 0) scroll_ = caretY;
            if (caretY + lh - scroll_ > textR.h) scroll_ = caretY + lh - textR.h;
        }
        scroll_ = clampf(scroll_, 0, std::max(0.f, contentH - textR.h + lh * 0.5f));

        g.pushClip(area);
        int first = std::max(0, (int)(scroll_ / lh));
        for (int i = first; i < (int)lines_.size(); i++) {
            float y = textR.y + i * lh - scroll_;
            if (y > area.b()) break;
            const Line& L = lines_[(size_t)i];
            g.text(textR.x, y + (lh - g.lineHeight(size)) * 0.5f, size, text_.substr(L.start, L.end - L.start), th.text);
        }
        if (ui.focus && std::fmod(ui.time, 1.0) < 0.6) {
            const Line& L = lines_[(size_t)cl];
            float cx = textR.x + widthOf(g, size, L.start, caret_);
            g.rect({cx, textR.y + caretY - scroll_ + 2, 1.5f, lh - 4}, th.text);
        }
        if (contentH > textR.h) {
            float barH = std::max(30.f, area.h * textR.h / contentH);
            float barY = area.y + (area.h - barH) * (scroll_ / std::max(1.f, contentH - textR.h));
            g.rect({area.r() - 5, barY, 3, barH}, alpha(th.textDim, 0.5f), 1.5f);
        }
        g.popClip();

        g.rect(status, th.mica);
        g.rect({status.x, status.y, status.w, 1}, th.border);
        int col = (int)utf8Length(text_.substr(paraStart(caret_), caret_ - paraStart(caret_))) + 1;
        int ln = 1;
        for (size_t i = 0; i < caret_ && i < text_.size(); i++)
            if (text_[i] == '\n') ln++;
        std::string st = "Ln " + std::to_string(ln) + ", Col " + std::to_string(col);
        g.textIn({status.x + 12, status.y, 200, status.h}, 11.5f, st, th.textDim);
        std::string right = std::to_string(utf8Length(text_)) + " characters    " + std::to_string(zoom_) + "%    UTF-8";
        g.textIn({status.x, status.y, status.w - 14, status.h}, 11.5f, right, th.textDim, AlignRight);

        if (menuOpen) drawMenu(ui, menuRects[menu_], menuBar);
    }

private:
    struct Line {
        size_t start, end;
    };
    std::shared_ptr<bool> alive_;
    std::string text_;
    std::string path_;
    size_t caret_ = 0;
    float scroll_ = 0;
    int zoom_ = 100;
    int menu_ = -1;
    bool dirty_ = false;
    float desiredX_ = -1;
    std::vector<Line> lines_;
    std::unordered_map<uint32_t, float> adv_;
    float advKey_ = -1;
    float layoutW_ = 100;

    void updateTitle() {
        std::string name = path_.empty() ? "Untitled" : fs::baseName(path_);
        title = (dirty_ ? "*" : "") + name + " - Notepad";
    }

    float adv(Gfx& g, float size, uint32_t cp) {
        float key = size * g.scale();
        if (key != advKey_) {
            adv_.clear();
            advKey_ = key;
        }
        auto it = adv_.find(cp);
        if (it != adv_.end()) return it->second;
        float w = g.textWidth(size, utf8From(cp == '\t' ? ' ' : cp));
        adv_[cp] = w;
        return w;
    }

    float widthOf(Gfx& g, float size, size_t a, size_t b) {
        float w = 0;
        size_t i = a;
        while (i < b && i < text_.size()) w += adv(g, size, utf8Decode(text_, i));
        return w;
    }

    size_t indexAt(Gfx& g, float size, const Line& L, float x) {
        float w = 0;
        size_t i = L.start;
        while (i < L.end) {
            size_t ni = i;
            float a = adv(g, size, utf8Decode(text_, ni));
            if (x < w + a * 0.5f) return i;
            w += a;
            i = ni;
        }
        return L.end;
    }

    void layout(Gfx& g, float size, float maxW) {
        layoutW_ = maxW;
        lines_.clear();
        size_t p = 0;
        while (true) {
            size_t pe = text_.find('\n', p);
            if (pe == std::string::npos) pe = text_.size();
            size_t ls = p, i = p, lastBreak = std::string::npos;
            float w = 0;
            while (i < pe) {
                size_t ni = i;
                uint32_t cp = utf8Decode(text_, ni);
                float a = adv(g, size, cp);
                if (w + a > maxW && i > ls) {
                    if (lastBreak != std::string::npos && lastBreak > ls && lastBreak <= i) {
                        lines_.push_back({ls, lastBreak});
                        ls = lastBreak;
                        w = widthOf(g, size, ls, i);
                    } else {
                        lines_.push_back({ls, i});
                        ls = i;
                        w = 0;
                    }
                    lastBreak = std::string::npos;
                    continue;
                }
                w += a;
                if (cp == ' ') lastBreak = ni;
                i = ni;
            }
            lines_.push_back({ls, pe});
            if (pe >= text_.size()) break;
            p = pe + 1;
        }
    }

    int caretLine() const {
        int best = 0;
        for (size_t i = 0; i < lines_.size(); i++)
            if (lines_[i].start <= caret_) best = (int)i;
        return best;
    }

    size_t paraStart(size_t pos) const {
        size_t p = text_.rfind('\n', pos == 0 ? 0 : pos - 1);
        if (pos == 0 || p == std::string::npos) return 0;
        return p + 1;
    }

    void insert(const std::string& s) {
        text_.insert(caret_, s);
        caret_ += s.size();
        touch();
    }

    void touch() {
        if (!dirty_) {
            dirty_ = true;
            updateTitle();
        }
    }

    bool handleKeys(Ui& ui, Gfx& g, float size) {
        bool moved = false;
        for (uint32_t ch : ui.chars) {
            if (ch == '\r' || ch == '\n') {
                insert("\n");
            } else if (ch == '\t') {
                insert("    ");
            } else if (ch >= 32 && ch != 127) {
                insert(utf8From(ch));
            }
            moved = true;
            desiredX_ = -1;
        }
        if (!ui.chars.empty()) layout(g, size, layoutW_);
        for (int k : ui.keys) {
            moved = true;
            switch (k) {
                case KeyEnter: insert("\n"); break;
                case KeyTab: insert("    "); break;
                case KeyBackspace:
                    if (caret_ > 0) {
                        size_t p = utf8Prev(text_, caret_);
                        text_.erase(p, caret_ - p);
                        caret_ = p;
                        touch();
                    }
                    break;
                case KeyDelete:
                    if (caret_ < text_.size()) {
                        text_.erase(caret_, utf8Next(text_, caret_) - caret_);
                        touch();
                    }
                    break;
                case KeyLeft: caret_ = utf8Prev(text_, caret_); break;
                case KeyRight: caret_ = utf8Next(text_, caret_); break;
                case KeyHome: caret_ = lines_[(size_t)caretLine()].start; break;
                case KeyEnd: caret_ = lines_[(size_t)caretLine()].end; break;
                case KeyUp:
                case KeyDown: {
                    int cl = caretLine();
                    const Line& L = lines_[(size_t)cl];
                    if (desiredX_ < 0) desiredX_ = widthOf(g, size, L.start, caret_);
                    int nl = cl + (k == KeyUp ? -1 : 1);
                    if (nl >= 0 && nl < (int)lines_.size()) caret_ = indexAt(g, size, lines_[(size_t)nl], desiredX_);
                    continue;
                }
                default: break;
            }
            if (k != KeyUp && k != KeyDown) desiredX_ = -1;
            if (k == KeyEnter || k == KeyBackspace || k == KeyDelete || k == KeyTab) layout(g, size, layoutW_);
        }
        return moved;
    }

    void save(bool as) {
        if (path_.empty() || as) {
            std::shared_ptr<bool> alive = alive_;
            std::string docs = fs::join(shell->userDir(), "Documents");
            std::string init = path_.empty() ? fs::uniqueName(docs, "Untitled", "txt") : fs::baseName(path_);
            shell->prompt("Save as", "File name (saved in Documents):", init, [this, alive, docs](const std::string& name) {
                if (!*alive || name.empty()) return;
                std::string n = name;
                if (fs::extension(n).empty()) n += ".txt";
                path_ = fs::join(docs, n);
                writeOut();
            });
            return;
        }
        writeOut();
    }

    void writeOut() {
        if (fs::writeText(path_, text_)) {
            dirty_ = false;
            updateTitle();
            shell->notifyFs();
        } else {
            shell->message("Notepad", "Could not save the file.");
        }
    }

    void drawMenu(Ui& ui, const Rect& anchor, const Rect& bar) {
        Gfx& g = *ui.g;
        const Theme& th = *ui.th;
        std::vector<std::string> items;
        if (menu_ == 0) items = {"New", "Open...", "Save", "Save as..."};
        if (menu_ == 1) items = {"Time/Date", "Select all to clipboard", "Clear all"};
        if (menu_ == 2) items = {"Zoom in", "Zoom out", "Restore default zoom"};
        Rect m{anchor.x, anchor.b() + 2, 210, items.size() * 34.f + 8};
        g.shadow(m, 8, 14, th.shadow);
        g.rect(m, th.dark ? rgb(0x2C2C2C) : rgb(0xFAFAFA), 8);
        g.stroke(m, alpha(th.text, 0.1f), 8, 1);
        int chosen = -1;
        for (size_t i = 0; i < items.size(); i++) {
            Rect r{m.x + 4, m.y + 4 + i * 34.f, m.w - 8, 32};
            ui.pressFill(r, 4);
            g.textIn({r.x + 12, r.y, r.w - 12, r.h}, 12.5f, items[i], th.text);
            if (ui.clicked(r)) chosen = (int)i;
        }
        bool clickedOutside = ui.released && !ui.dragged && !m.contains(ui.x, ui.y) && !bar.contains(ui.downX, ui.downY);
        if (chosen >= 0) {
            int which = menu_;
            menu_ = -1;
            if (which == 0) {
                if (chosen == 0) shell->open(AppKind::Notepad);
                if (chosen == 1) shell->open(AppKind::Explorer, fs::join(shell->userDir(), "Documents"));
                if (chosen == 2) save(false);
                if (chosen == 3) save(true);
            } else if (which == 1) {
                if (chosen == 0) {
                    time_t t = time(nullptr);
                    struct tm lt;
                    localtime_r(&t, &lt);
                    char buf[32];
                    strftime(buf, sizeof buf, "%H:%M %d/%m/%Y", &lt);
                    insert(buf);
                }
                if (chosen == 1) shell->message("Notepad", "Clipboard is not available on this device yet.");
                if (chosen == 2) {
                    text_.clear();
                    caret_ = 0;
                    touch();
                }
            } else {
                if (chosen == 0) zoom_ = std::min(300, zoom_ + 10);
                if (chosen == 1) zoom_ = std::max(50, zoom_ - 10);
                if (chosen == 2) zoom_ = 100;
            }
        } else if (clickedOutside && ui.active) {
            menu_ = -1;
        }
    }
};

std::unique_ptr<AppWindow> makeNotepad(Shell& s, const std::string& arg) { return std::make_unique<NotepadWindow>(s, arg); }

}
