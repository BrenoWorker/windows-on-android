#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>

#include "Apps.h"
#include "Common.h"

namespace wd {

static const char* kHome = "https://www.google.com/";

static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\n");
    return s.substr(a, b - a + 1);
}

static std::string urlEncode(const std::string& s) {
    std::string out;
    char buf[4];
    for (unsigned char c : s) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out += (char)c;
        } else if (c == ' ') {
            out += '+';
        } else {
            snprintf(buf, sizeof buf, "%%%02X", c);
            out += buf;
        }
    }
    return out;
}

static std::string normalizeUrl(const std::string& input) {
    std::string t = trim(input);
    if (t.empty()) return kHome;
    if (t.find("://") != std::string::npos || t.rfind("about:", 0) == 0 || t.rfind("data:", 0) == 0) return t;
    bool hasSpace = t.find(' ') != std::string::npos;
    bool looksHost = t.find('.') != std::string::npos || t.rfind("localhost", 0) == 0;
    if (!hasSpace && looksHost) return "https://" + t;
    return "https://www.google.com/search?q=" + urlEncode(t);
}

static std::string cwsId(const std::string& url) {
    static const char* prefixes[] = {"chromewebstore.google.com/detail/", "chrome.google.com/webstore/detail/"};
    for (const char* pre : prefixes) {
        size_t p = url.find(pre);
        if (p == std::string::npos) continue;
        std::string rest = url.substr(p + strlen(pre));
        size_t cut = rest.find_first_of("?#");
        if (cut != std::string::npos) rest = rest.substr(0, cut);
        size_t i = 0;
        while (i <= rest.size()) {
            size_t j = rest.find('/', i);
            if (j == std::string::npos) j = rest.size();
            std::string seg = rest.substr(i, j - i);
            bool ok = seg.size() == 32;
            for (char ch : seg)
                if (ch < 'a' || ch > 'p') ok = false;
            if (ok) return seg;
            i = j + 1;
        }
    }
    return "";
}

static std::vector<unsigned char> base64Decode(const std::string& in) {
    std::vector<unsigned char> out;
    int val = 0, bits = -8;
    for (unsigned char ch : in) {
        int d;
        if (ch >= 'A' && ch <= 'Z') d = ch - 'A';
        else if (ch >= 'a' && ch <= 'z') d = ch - 'a' + 26;
        else if (ch >= '0' && ch <= '9') d = ch - '0' + 52;
        else if (ch == '+') d = 62;
        else if (ch == '/') d = 63;
        else continue;
        val = (val << 6) | d;
        bits += 6;
        if (bits >= 0) {
            out.push_back((unsigned char)((val >> bits) & 0xFF));
            bits -= 8;
        }
    }
    return out;
}

static std::string firstGlyph(const std::string& s) {
    if (s.empty()) return "?";
    unsigned char c = (unsigned char)s[0];
    size_t n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 1;
    std::string g = s.substr(0, std::min(n, s.size()));
    if (n == 1 && c >= 'a' && c <= 'z') g[0] = (char)(c - 32);
    return g;
}

static std::string displayUrl(const std::string& url) {
    std::string u = url;
    if (u.rfind("https://", 0) == 0) u = u.substr(8);
    if (u.rfind("www.", 0) == 0) u = u.substr(4);
    if (!u.empty() && u.back() == '/' && u.find('/') == u.size() - 1) u.pop_back();
    return u;
}

class BrowserWindow : public AppWindow {
public:
    BrowserWindow(Shell& s, const std::string& arg) : web_(s.platform().web), start_(arg) {
        prefW = 900;
        prefH = 560;
        minW = 360;
        minH = 260;
        title = "New tab - Browser";
    }

    ~BrowserWindow() override {
        if (web_ && id_ >= 0) web_->destroy(id_);
        if (tex_ && shell) shell->gfx().deleteTexture(tex_);
    }

    bool wantsText() const override { return true; }

    bool onBack() override {
        if (web_ && id_ >= 0 && st_.fullscreen) {
            web_->action(id_, WebExitFullscreen);
            return true;
        }
        if (web_ && id_ >= 0 && st_.canBack) {
            web_->action(id_, WebBack);
            return true;
        }
        return false;
    }

    bool gamepad(int button, bool down) override {
        if (!web_ || id_ < 0 || addrFocus_) return false;
        if (button == PadB && !st_.fullscreen && !st_.controls) return false;
        web_->gamepad(id_, button, down);
        return true;
    }

    void hidden() override { setLayout({0, 0, 0, 0}, {0, 0, 0, 0}, false); }

    void draw(Ui& ui, const Rect& c) override {
        Gfx& g = *ui.g;
        const Theme& th = *ui.th;
        if (web_ && id_ < 0) {
            id_ = web_->create();
            web_->load(id_, normalizeUrl(start_));
        }
        if (web_ && id_ >= 0) st_ = web_->state(id_);
        std::string pageTitle = st_.title.empty() ? (st_.url.empty() ? "New tab" : displayUrl(st_.url)) : st_.title;
        title = pageTitle + " - Browser";

        syncFullscreen();
        g.rect(c, th.mica);
        Rect bar{c.x, c.y, c.w, fsOn_ ? 0.f : 46.f};
        bool hasWeb = web_ && id_ >= 0;
        if (!fsOn_) drawToolbar(ui, bar, hasWeb);

        Rect page{c.x, bar.b(), c.w, c.h - bar.h};
        if (fsOn_) g.rect(page, rgb(0x000000));
        Rect clipped = page.intersect(shell->workArea());
        g.rect(page, rgb(0xFFFFFF));

        if (!hasWeb) {
            drawIcon(g, IcBrowser, {page.cx() - 32, page.cy() - 70, 64, 64}, th.text);
            g.textIn({page.x, page.cy() + 6, page.w, 24}, 15, "The browser engine is only available on Android.", rgb(0x333333),
                     AlignCenter);
            g.textIn({page.x, page.cy() + 32, page.w, 20}, 12, "It uses Android System WebView, powered by Chromium.",
                     rgb(0x777777), AlignCenter);
            return;
        }

        pollExt(ui);
        bool modal = extModal();
        bool visible = exclusive && !dlOpen_ && !extOpen_ && !modal && clipped.w > 8 && clipped.h > 8;
        setLayout(page, clipped, visible);

        if (!visible) {
            double every = st_.loading ? 1.0 : 2.5;
            if (ui.time - refreshT_ > every && laidOut_ && !lvis_) {
                refreshT_ = ui.time;
                web_->action(id_, WebCapture);
            }
            std::vector<unsigned char> px;
            int w = 0, h = 0;
            if (web_->snapshot(id_, px, w, h) && w > 0 && h > 0) {
                if (tex_ && (texW_ != w || texH_ != h)) {
                    g.deleteTexture(tex_);
                    tex_ = 0;
                }
                if (!tex_)
                    tex_ = g.createTexture(w, h, px.data());
                else
                    g.updateTexture(tex_, w, h, px.data());
                texW_ = w;
                texH_ = h;
                float s = g.scale();
                if (pendingClip_ && w == lw_ && h == lh_) {
                    snapW_ = visPage_.w;
                    snapH_ = visPage_.h;
                    snapX_ = visClip_.x - visPage_.x;
                    snapY_ = visClip_.y - visPage_.y;
                } else {
                    snapW_ = w / s;
                    snapH_ = h / s;
                    snapX_ = 0;
                    snapY_ = 0;
                }
                pendingClip_ = false;
            }
            g.pushClip(page);
            if (tex_ && snapW_ > 0 && snapH_ > 0) {
                float s = g.scale();
                float k = std::max(page.w / snapW_, page.h / snapH_);
                g.image(tex_, {page.x + snapX_ * k, page.y + snapY_ * k, texW_ / s * k, texH_ / s * k});
            } else {
                drawPlaceholder(g, th, page);
            }
            g.popClip();
        }

        pollDownloads(ui);
        if (dlOpen_ && !fsOn_) drawDownloads(ui, c, bar);
        if (extOpen_ && !fsOn_ && !modal) drawExtensions(ui, c, bar);
        if (modal) drawExtModal(ui, page);

        if (st_.editable && !lastEditable_) {
            addrFocus_ = false;
            ui.requestKeyboard = true;
        }
        lastEditable_ = st_.editable;

        if (ui.focus && !addrFocus_ && !dlOpen_ && !extOpen_ && !modal) {
            std::string typed;
            for (uint32_t ch : ui.chars)
                if (ch >= 32) utf8Append(typed, ch);
            if (!typed.empty()) web_->text(id_, typed);
            for (int k : ui.keys) web_->key(id_, k);
        }
    }

private:
    struct Dl {
        int id = 0;
        std::string name, path, state, error;
        long long done = 0, total = -1, speed = 0;
    };
    static int lastSeenDl_;
    std::vector<Dl> dls_;
    double dlPollT_ = -10;
    double dlToggleT_ = -10;
    bool dlOpen_ = false;
    bool dlUnseen_ = false;
    float dlScroll_ = 0;
    std::map<int, std::string> dlStates_;

    static std::vector<std::string> splitTab(const std::string& s) {
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

    void pollDownloads(Ui& ui) {
        if (!shell->platform().sys || ui.time - dlPollT_ < (dlOpen_ ? 0.25 : 0.6)) return;
        dlPollT_ = ui.time;
        std::string text = shell->sys("dl.list");
        std::vector<Dl> list;
        size_t i = 0;
        while (i < text.size()) {
            size_t j = text.find('\n', i);
            if (j == std::string::npos) j = text.size();
            auto f = splitTab(text.substr(i, j - i));
            i = j + 1;
            if (f.size() < 8) continue;
            Dl d;
            d.id = atoi(f[0].c_str());
            d.name = f[1];
            d.path = f[2];
            d.state = f[3];
            d.done = atoll(f[4].c_str());
            d.total = atoll(f[5].c_str());
            d.speed = atoll(f[6].c_str());
            d.error = f[7];
            list.push_back(d);
        }
        bool changed = false;
        int newest = 0;
        for (auto& d : list) {
            newest = std::max(newest, d.id);
            auto it = dlStates_.find(d.id);
            if (it != dlStates_.end() && it->second != d.state && d.state == "done") changed = true;
            if (it == dlStates_.end() && d.state == "done") changed = true;
            dlStates_[d.id] = d.state;
        }
        if (changed) shell->notifyFs();
        if (newest > lastSeenDl_) {
            lastSeenDl_ = newest;
            if (ui.focus || exclusive) {
                dlOpen_ = true;
                dlScroll_ = 0;
            } else {
                dlUnseen_ = true;
            }
        }
        if (dlOpen_) dlUnseen_ = false;
        dls_ = list;
    }

    static std::string eta(long long secs) {
        if (secs < 60) return std::to_string(std::max(1LL, secs)) + " s left";
        if (secs < 3600) return std::to_string(secs / 60) + " min left";
        return std::to_string(secs / 3600) + " h left";
    }

    void drawDownloads(Ui& ui, const Rect& c, const Rect& bar) {
        Gfx& g = *ui.g;
        const Theme& th = *ui.th;
        float w = std::min(380.f, c.w - 16);
        float rowH = 66;
        int visibleRows = std::max(1, std::min((int)dls_.size(), 5));
        float listH = dls_.empty() ? 70.f : visibleRows * rowH;
        float h = std::min(52 + listH + 8, c.b() - bar.b() - 12);
        Rect panel{c.r() - w - 8, bar.b() + 4, w, h};
        if (ui.pressed && !panel.contains(ui.x, ui.y) && ui.time - dlToggleT_ > 0.05 && !bar.contains(ui.x, ui.y)) {
            dlOpen_ = false;
            return;
        }
        g.shadow({panel.x, panel.y + 6, panel.w, panel.h}, 8, 22, th.shadow);
        g.rect(panel, th.flyout, 8);
        g.stroke(panel, alpha(th.text, 0.1f), 8, 1);
        g.textBold(panel.x + 16, panel.y + 15, 14, "Downloads", th.text);
        Rect closeB{panel.r() - 40, panel.y + 8, 32, 32};
        Rect folderB{closeB.x - 34, panel.y + 8, 32, 32};
        if (toolButton(ui, closeB, IcGlyphClose, "", false)) {
            dlOpen_ = false;
            return;
        }
        if (toolButton(ui, folderB, IcFolder, "", false)) {
            shell->open(AppKind::Explorer, fs::join(shell->userDir(), "Downloads"));
            dlOpen_ = false;
            return;
        }
        bool anyFinished = false;
        for (auto& d : dls_)
            if (d.state != "running") anyFinished = true;
        if (anyFinished) {
            float cw = g.textWidth(12, "Clear") + 20;
            if (ui.flatButton({folderB.x - cw - 4, panel.y + 10, cw, 28}, "Clear", 12)) shell->sys("dl.clear");
        }
        g.rect({panel.x, panel.y + 48, panel.w, 1}, alpha(th.text, 0.08f));
        Rect list{panel.x, panel.y + 50, panel.w, panel.h - 54};
        if (dls_.empty()) {
            g.textIn({list.x, list.y + 18, list.w, 24}, 12.5f, "No recent downloads", th.textDim, AlignCenter);
            return;
        }
        float contentH = dls_.size() * rowH;
        if (ui.dragging(list)) dlScroll_ -= ui.dy();
        dlScroll_ = clampf(dlScroll_, 0, std::max(0.f, contentH - list.h));
        g.pushClip(list);
        for (size_t i = 0; i < dls_.size(); i++) {
            const Dl& d = dls_[i];
            Rect row{list.x + 6, list.y + i * rowH - dlScroll_ + 2, list.w - 12, rowH - 4};
            if (row.b() < list.y || row.y > list.b()) continue;
            bool done = d.state == "done";
            if (done) ui.pressFill(row, 5);
            Rect ic{row.x + 10, row.y + 15, 32, 32};
            shell->drawFileIcon(d.path, IcFile, ic, th.text);
            float tx = row.x + 52;
            Rect act{row.r() - 38, row.cy() - 16, 32, 32};
            float tw = act.x - tx - 6;
            Color nameC = (d.state == "failed" || d.state == "cancelled" || d.state == "deleted") ? th.textDim : th.text;
            g.text(tx, row.y + 12, 12.5f, g.ellipsize(12.5f, d.name, tw), nameC);
            std::string status;
            if (d.state == "running") {
                status = d.total > 0 ? fs::formatSize(d.done) + " of " + fs::formatSize(d.total) : fs::formatSize(d.done);
                if (d.speed > 0) {
                    status += ", " + fs::formatSize(d.speed) + "/s";
                    if (d.total > 0) status += ", " + eta((d.total - d.done) / std::max(1LL, d.speed));
                }
                Rect pb{tx, row.y + 50, tw, 4};
                g.rect(pb, alpha(th.text, 0.12f), 2);
                if (d.total > 0) {
                    g.rect({pb.x, pb.y, pb.w * clampf((float)d.done / d.total, 0, 1), pb.h}, th.accent, 2);
                } else {
                    float t = (float)fmod(ui.time * 0.8, 1.0);
                    g.rect({pb.x + pb.w * t * 0.75f, pb.y, pb.w * 0.25f, pb.h}, th.accent, 2);
                }
                if (toolButton(ui, act, IcGlyphClose, "", false)) shell->sys("dl.cancel", std::to_string(d.id));
            } else if (done) {
                status = fs::formatSize(d.total > 0 ? d.total : d.done) + "  \xC2\xB7  Open file";
                if (toolButton(ui, act, IcFolder, "", false)) {
                    shell->open(AppKind::Explorer, fs::parent(d.path));
                    dlOpen_ = false;
                }
                if (ui.clicked(row) && !act.contains(ui.x, ui.y)) {
                    dlOpen_ = false;
                    shell->openFile(d.path);
                }
            } else {
                if (d.state == "cancelled") status = "Canceled";
                else if (d.state == "deleted") status = "Deleted";
                else status = "Failed" + (d.error.empty() ? std::string() : " - " + d.error);
                if (d.state != "deleted" && toolButton(ui, act, IcGlyphRefresh, "", false)) shell->sys("dl.retry", std::to_string(d.id));
            }
            g.text(tx, row.y + 31, 11.5f, g.ellipsize(11.5f, status, tw), d.state == "failed" ? rgb(0xD13438) : th.textDim);
        }
        g.popClip();
    }

    struct ExtItem {
        std::string id, name, version, badge, badgeColor, desc;
        bool enabled = true;
        bool popup = false;
        bool options = false;
        int stamp = 0;
    };
    struct IconTex {
        GLuint tex = 0;
        int stamp = -1;
        bool none = false;
    };
    static std::map<std::string, IconTex> extIcons_;
    std::vector<ExtItem> exts_;
    std::vector<std::string> extState_;
    double extPollT_ = -10;
    double extStateT_ = -10;
    double extTakeT_ = -10;
    double extToggleT_ = -10;
    bool extOpen_ = false;
    float extScroll_ = 0;
    std::string removeArm_;
    double removeArmT_ = 0;
    Rect extBtn_;

    bool extModal() const {
        if (!exclusive || extState_.empty()) return false;
        const std::string& k = extState_[0];
        return k == "ready" || k == "downloading" || k == "failed" || k == "installed";
    }

    void openExtPanel(Ui& ui) {
        extOpen_ = true;
        dlOpen_ = false;
        extToggleT_ = ui.time;
        extScroll_ = 0;
        extPollT_ = -10;
    }

    void pollExt(Ui& ui) {
        if (!shell->platform().sys) return;
        if (ui.time - extStateT_ > 0.35) {
            extStateT_ = ui.time;
            extState_ = splitTab(shell->sys("ext.state"));
        }
        if (ui.time - extPollT_ > (extOpen_ ? 0.4 : 1.5)) {
            extPollT_ = ui.time;
            std::string text = shell->sys("ext.list");
            std::vector<ExtItem> list;
            size_t i = 0;
            while (i < text.size()) {
                size_t j = text.find('\n', i);
                if (j == std::string::npos) j = text.size();
                auto f = splitTab(text.substr(i, j - i));
                i = j + 1;
                if (f.size() < 10) continue;
                ExtItem e;
                e.id = f[0];
                e.name = f[1];
                e.version = f[2];
                e.enabled = f[3] == "1";
                e.popup = f[4] == "1";
                e.options = f[5] == "1";
                e.badge = f[6];
                e.badgeColor = f[7];
                e.stamp = atoi(f[8].c_str());
                e.desc = f[9];
                list.push_back(e);
            }
            exts_ = list;
        }
        if (ui.time - extTakeT_ > 0.5) {
            extTakeT_ = ui.time;
            std::string text = shell->sys("ext.take", std::to_string(id_));
            size_t i = 0;
            bool closeMe = false;
            while (i < text.size()) {
                size_t j = text.find('\n', i);
                if (j == std::string::npos) j = text.size();
                auto f = splitTab(text.substr(i, j - i));
                i = j + 1;
                if (f.size() >= 2 && f[0] == "open" && !f[1].empty()) shell->open(AppKind::Browser, f[1]);
                else if (!f.empty() && f[0] == "close") closeMe = true;
            }
            if (closeMe) shell->close(this);
        }
    }

    static Color hexColor(const std::string& s, Color fallback) {
        if (s.size() != 7 || s[0] != '#') return fallback;
        return rgb((unsigned)strtoul(s.c_str() + 1, nullptr, 16));
    }

    void drawExtIcon(Gfx& g, const ExtItem& e, const Rect& r) {
        IconTex& ic = extIcons_[e.id];
        if (ic.stamp != e.stamp) {
            ic.stamp = e.stamp;
            ic.none = true;
            auto f = splitTab(shell->sys("ext.icon", e.id));
            if (f.size() >= 3) {
                int w = atoi(f[0].c_str()), h = atoi(f[1].c_str());
                std::vector<unsigned char> px = base64Decode(f[2]);
                if (w > 0 && h > 0 && px.size() == (size_t)w * h * 4) {
                    if (ic.tex) g.deleteTexture(ic.tex);
                    ic.tex = g.createTexture(w, h, px.data());
                    ic.none = ic.tex == 0;
                }
            }
        }
        if (ic.tex && !ic.none) {
            g.image(ic.tex, r, e.enabled ? Color{1, 1, 1, 1} : Color{1, 1, 1, 0.45f});
            return;
        }
        static const unsigned palette[] = {0x0B57D0, 0x188038, 0xC5221F, 0xE37400, 0x8430CE, 0x007B83};
        unsigned hsh = 0;
        for (char ch : e.id) hsh = hsh * 31 + (unsigned char)ch;
        Color col = rgb(palette[hsh % 6], e.enabled ? 1.f : 0.45f);
        g.rect(r, col, r.w * 0.25f);
        g.textIn(r, r.h * 0.55f, firstGlyph(e.name), rgb(0xFFFFFF), AlignCenter);
    }

    void drawExtensions(Ui& ui, const Rect& c, const Rect& bar) {
        Gfx& g = *ui.g;
        const Theme& th = *ui.th;
        std::string store = cwsId(st_.url);
        bool storeInstalled = false;
        for (auto& e : exts_)
            if (e.id == store) storeInstalled = true;
        float w = std::min(400.f, c.w - 16);
        float rowH = 54;
        float cardH = store.empty() ? 0.f : 64.f;
        int rows = std::max(1, std::min((int)exts_.size(), 6));
        float listH = exts_.empty() ? 84.f : rows * rowH;
        float h = std::min(52 + cardH + listH + 52, c.b() - bar.b() - 12);
        float px = clampf(extBtn_.r() - w + 40, c.x + 8, c.r() - w - 8);
        Rect panel{px, bar.b() + 4, w, h};
        if (ui.pressed && !panel.contains(ui.x, ui.y) && ui.time - extToggleT_ > 0.05 && !bar.contains(ui.x, ui.y)) {
            extOpen_ = false;
            return;
        }
        g.shadow({panel.x, panel.y + 6, panel.w, panel.h}, 8, 22, th.shadow);
        g.rect(panel, th.flyout, 8);
        g.stroke(panel, alpha(th.text, 0.1f), 8, 1);
        g.textBold(panel.x + 16, panel.y + 15, 14, "Extensions", th.text);
        Rect closeB{panel.r() - 40, panel.y + 8, 32, 32};
        if (toolButton(ui, closeB, IcGlyphClose, "", false)) {
            extOpen_ = false;
            return;
        }
        g.rect({panel.x, panel.y + 48, panel.w, 1}, alpha(th.text, 0.08f));
        float y = panel.y + 50;
        if (!store.empty()) {
            Rect card{panel.x + 10, y + 8, panel.w - 20, cardH - 14};
            g.rect(card, alpha(th.accent, th.dark ? 0.22f : 0.1f), 6);
            if (storeInstalled) {
                g.textIn({card.x + 14, card.y, card.w - 28, card.h}, 12.5f, "This extension is installed in WinDroid", th.text);
            } else {
                float bw = g.textWidth(13, "Add to WinDroid") + 28;
                Rect addB{card.r() - bw - 9, card.cy() - 15, bw, 30};
                g.textIn({card.x + 14, card.y, addB.x - card.x - 20, card.h}, 12.5f,
                         g.ellipsize(12.5f, "Get this extension", addB.x - card.x - 20), th.text);
                if (ui.button(addB, "Add to WinDroid", true)) shell->sys("ext.install", st_.url);
            }
            y += cardH;
        }
        Rect list{panel.x, y, panel.w, panel.b() - 52 - y};
        Rect foot{panel.x, panel.b() - 50, panel.w, 50};
        g.rect({foot.x, foot.y, foot.w, 1}, alpha(th.text, 0.08f));
        float sw = g.textWidth(12.5f, "Chrome Web Store") + 44;
        Rect storeB{foot.x + 8, foot.y + 9, sw, 32};
        if (toolButton(ui, storeB, IcGlyphGlobe, "Chrome Web Store", true)) {
            web_->load(id_, "https://chromewebstore.google.com/category/extensions");
            extOpen_ = false;
            return;
        }
        if (exts_.empty()) {
            g.textIn({list.x, list.y + 14, list.w, 22}, 13, "No extensions yet", th.text, AlignCenter);
            g.textIn({list.x + 16, list.y + 38, list.w - 32, 20}, 11.5f, "Open an extension in the Chrome Web Store and tap Add to WinDroid",
                     th.textDim, AlignCenter);
            return;
        }
        if (!removeArm_.empty() && ui.time - removeArmT_ > 4) removeArm_.clear();
        float contentH = exts_.size() * rowH;
        if (ui.dragging(list)) extScroll_ -= ui.dy();
        extScroll_ = clampf(extScroll_, 0, std::max(0.f, contentH - list.h));
        float scale = g.scale();
        g.pushClip(list);
        for (size_t i = 0; i < exts_.size(); i++) {
            const ExtItem& e = exts_[i];
            Rect row{list.x + 6, list.y + i * rowH - extScroll_ + 2, list.w - 12, rowH - 4};
            if (row.b() < list.y || row.y > list.b()) continue;
            Rect trash{row.r() - 36, row.cy() - 16, 32, 32};
            Rect gear{trash.x - 34, trash.y, 32, 32};
            Rect tog{(e.options ? gear.x : trash.x) - 50, row.cy() - 14, 46, 28};
            Rect controls{tog.x, row.y, row.r() - tog.x, row.h};
            bool armed = removeArm_ == e.id;
            if (e.enabled && !controls.contains(ui.x, ui.y)) ui.pressFill(row, 5);
            drawExtIcon(g, e, {row.x + 10, row.cy() - 12, 24, 24});
            float tx = row.x + 44;
            float tw = tog.x - tx - 8;
            std::string name = g.ellipsize(13, e.name, tw);
            Color nameC = e.enabled ? th.text : th.textDim;
            g.text(tx, row.y + 9, 13, name, nameC);
            if (!e.badge.empty() && e.enabled) {
                float bx = tx + g.textWidth(13, name) + 6;
                float bwid = std::max(16.f, g.textWidth(10, e.badge) + 8);
                if (bx + bwid <= tog.x - 4) {
                    Rect pill{bx, row.y + 10, bwid, 15};
                    g.rect(pill, hexColor(e.badgeColor, th.danger), 7.5f);
                    g.textIn(pill, 10, e.badge, rgb(0xFFFFFF), AlignCenter);
                }
            }
            std::string sub = armed ? "Tap the trash again to remove" : (e.desc.empty() ? "Version " + e.version : e.desc);
            g.text(tx, row.y + 29, 11, g.ellipsize(11, sub, tw), armed ? th.danger : th.textDim);
            bool on = e.enabled;
            if (ui.toggle(tog, on)) shell->sys("ext.enable", e.id + "\t" + (on ? "1" : "0"));
            if (e.options && toolButton(ui, gear, IcGlyphGear, "", false, false, e.enabled)) {
                shell->sys("ext.options", e.id);
                extOpen_ = false;
            }
            if (toolButton(ui, trash, IcGlyphTrash, "", false, armed)) {
                if (armed) {
                    shell->sys("ext.remove", e.id);
                    removeArm_.clear();
                } else {
                    removeArm_ = e.id;
                    removeArmT_ = ui.time;
                }
            }
            if (e.enabled && ui.clicked(row) && !controls.contains(ui.x, ui.y)) {
                int right = (int)std::lround(extBtn_.r() * scale);
                int top = (int)std::lround((bar.b() + 2) * scale);
                shell->sys("ext.popup", e.id + "\t" + std::to_string(id_) + "\t" + std::to_string(right) + "\t" + std::to_string(top));
                extOpen_ = false;
            }
        }
        g.popClip();
    }

    void drawExtModal(Ui& ui, const Rect& page) {
        Gfx& g = *ui.g;
        const Theme& th = *ui.th;
        const std::string& k = extState_[0];
        auto field = [&](size_t i) { return i < extState_.size() ? extState_[i] : std::string(); };
        std::string q1 = "\xE2\x80\x9C", q2 = "\xE2\x80\x9D";
        std::string title, body;
        std::vector<std::string> perms;
        if (k == "ready") {
            title = "Add " + q1 + field(2) + q2 + "?";
            std::string p = field(4);
            size_t i = 0;
            while (i < p.size()) {
                size_t j = p.find('|', i);
                if (j == std::string::npos) j = p.size();
                if (j > i) perms.push_back(p.substr(i, j - i));
                i = j + 1;
            }
            body = perms.empty() ? "It doesn't need any special permissions." : "It can:";
        } else if (k == "downloading") {
            title = "Adding extension";
            body = "Downloading it from the Chrome Web Store\xE2\x80\xA6";
        } else if (k == "failed") {
            title = "Couldn't add the extension";
            body = field(2).empty() ? "Something went wrong." : field(2);
        } else {
            title = q1 + field(2) + q2 + " has been added";
            body = "Open it from the extensions button in the toolbar.";
        }
        g.rect(page, rgb(0x000000, 0.35f));
        float w = std::min(420.f, page.w - 32);
        float inner = w - 48;
        auto tl = wrapText(g, 16, title, inner);
        auto bl = wrapText(g, 12.5f, body, inner);
        std::vector<std::pair<std::string, bool>> pl;
        for (auto& p : perms) {
            bool warn = !p.empty() && p[0] == '!';
            for (auto& line : wrapText(g, 12.5f, warn ? p.substr(1) : p, inner - 18)) pl.push_back({line, warn});
        }
        float h = 24 + tl.size() * 22 + 8 + bl.size() * 19 + (pl.empty() ? 0 : 6 + pl.size() * 19) + 28 + 32 + 24;
        if (k == "downloading") h += 26;
        h = std::min(h, page.h - 16);
        Rect card{page.cx() - w / 2, page.cy() - h / 2, w, h};
        g.shadow({card.x, card.y + 8, card.w, card.h}, 10, 30, th.shadow);
        g.rect(card, th.flyout, 8);
        g.stroke(card, alpha(th.text, 0.1f), 8, 1);
        float y = card.y + 24;
        float x = card.x + 24;
        for (auto& l : tl) {
            g.textBold(x, y, 16, l, th.text);
            y += 22;
        }
        y += 8;
        for (auto& l : bl) {
            g.text(x, y, 12.5f, l, th.textDim);
            y += 19;
        }
        if (!pl.empty()) {
            y += 6;
            for (auto& l : pl) {
                g.circle(x + 5, y + 8, 2, l.second ? rgb(0xE37400) : th.text);
                g.text(x + 18, y, 12.5f, l.first, l.second ? rgb(0xE37400) : th.text);
                y += 19;
            }
        }
        if (k == "downloading") {
            float t = (float)fmod(ui.time * 1.4, 1.0) * 6.2832f;
            g.arc(x + 10, y + 14, 9, t, t + 4.2f, 2.5f, th.accent);
            y += 26;
        }
        float by = card.b() - 24 - 32;
        if (k == "ready") {
            float aw = g.textWidth(13, "Add extension") + 32;
            Rect addB{card.r() - 24 - aw, by, aw, 32};
            Rect cancelB{addB.x - 8 - 96, by, 96, 32};
            if (ui.button(addB, "Add extension", true)) shell->sys("ext.confirm");
            if (ui.button(cancelB, "Cancel")) shell->sys("ext.cancel");
        } else if (k == "downloading") {
            Rect cancelB{card.r() - 24 - 96, by, 96, 32};
            if (ui.button(cancelB, "Cancel")) shell->sys("ext.cancel");
        } else {
            Rect okB{card.r() - 24 - 96, by, 96, 32};
            if (ui.button(okB, "OK", true)) {
                shell->sys("ext.dismiss");
                extState_.clear();
                extStateT_ = ui.time;
                if (k == "installed") openExtPanel(ui);
            }
        }
    }

    bool fsOn_ = false;
    bool fsMaximized_ = false;
    double refreshT_ = 0;

    void syncFullscreen() {
        if (st_.fullscreen == fsOn_) return;
        fsOn_ = st_.fullscreen;
        if (fsOn_) {
            addrFocus_ = false;
            fsMaximized_ = !maximized;
            if (fsMaximized_) shell->toggleMaximize(this);
        } else {
            if (fsMaximized_ && maximized) shell->toggleMaximize(this);
            fsMaximized_ = false;
        }
    }

    void drawToolbar(Ui& ui, const Rect& bar, bool hasWeb) {
        Gfx& g = *ui.g;
        const Theme& th = *ui.th;
        float x = bar.x + 6;
        if (toolButton(ui, {x, bar.y + 7, 32, 32}, IcGlyphBack, "", false, false, hasWeb && st_.canBack)) web_->action(id_, WebBack);
        x += 34;
        if (toolButton(ui, {x, bar.y + 7, 32, 32}, IcGlyphForward, "", false, false, hasWeb && st_.canForward))
            web_->action(id_, WebForward);
        x += 34;
        if (st_.loading) {
            if (toolButton(ui, {x, bar.y + 7, 32, 32}, IcGlyphClose, "", false, false, hasWeb)) web_->action(id_, WebStop);
        } else {
            if (toolButton(ui, {x, bar.y + 7, 32, 32}, IcGlyphRefresh, "", false, false, hasWeb)) web_->action(id_, WebReload);
        }
        x += 34;
        if (bar.w > 520) {
            if (toolButton(ui, {x, bar.y + 7, 32, 32}, IcGlyphHome, "", false, false, hasWeb)) web_->load(id_, kHome);
            x += 34;
        }
        float right = bar.r() - 6;
        Rect chromeBtn{right - 32, bar.y + 7, 32, 32};
        right -= 34;
        if (!dls_.empty() || dlOpen_) {
            Rect dlBtn{right - 32, bar.y + 7, 32, 32};
            right -= 34;
            if (toolButton(ui, dlBtn, IcGlyphDown, "", false, dlOpen_, true)) {
                dlOpen_ = !dlOpen_;
                if (dlOpen_) extOpen_ = false;
                dlToggleT_ = ui.time;
                dlScroll_ = 0;
            }
            long long done = 0, total = 0;
            int running = 0;
            for (auto& d : dls_)
                if (d.state == "running") {
                    running++;
                    done += d.done;
                    total += d.total > 0 ? d.total : d.done + 1;
                }
            if (running > 0) {
                float f = total > 0 ? (float)done / total : 0.f;
                g.ring(dlBtn.cx(), dlBtn.cy(), 14, 1.5f, alpha(th.text, 0.15f));
                g.arc(dlBtn.cx(), dlBtn.cy(), 14, -1.5708f, -1.5708f + 6.2832f * std::max(0.04f, f), 2, th.accent);
            } else if (dlUnseen_) {
                g.circle(dlBtn.r() - 6, dlBtn.y + 7, 4, th.accent);
            }
        }
        Rect deskBtn{right - 32, bar.y + 7, 32, 32};
        right -= 34;
        extBtn_ = Rect{right - 32, bar.y + 7, 32, 32};
        right -= 34;
        bool showPad = bar.w >= 480;
        Rect padBtn{right - 32, bar.y + 7, 32, 32};
        if (showPad) right -= 34;
        right -= 4;
        if (toolButton(ui, chromeBtn, IcGlyphExternal, "", false, false, hasWeb)) web_->action(id_, WebOpenInChrome);
        if (toolButton(ui, deskBtn, IcGlyphMonitor, "", false, st_.desktop, hasWeb)) web_->action(id_, WebToggleDesktop);
        if (toolButton(ui, extBtn_, IcGlyphPuzzle, "", false, extOpen_, hasWeb)) {
            if (extOpen_) extOpen_ = false;
            else openExtPanel(ui);
        }
        bool anyBadge = false;
        for (auto& e : exts_)
            if (e.enabled && !e.badge.empty()) anyBadge = true;
        if (anyBadge && !extOpen_) g.circle(extBtn_.r() - 6, extBtn_.y + 7, 4, th.accent);
        if (showPad && toolButton(ui, padBtn, IcGlyphGamepad, "", false, st_.controls, hasWeb)) web_->action(id_, WebToggleControls);

        Rect addr{x + 4, bar.y + 7, right - x - 4, 32};
        if (!addrFocus_) addrEdit_.set(displayUrl(st_.url));
        bool wasFocus = addrFocus_;
        bool secure = st_.url.rfind("https://", 0) == 0;
        if (ui.lineEdit(addr, addrEdit_, addrFocus_, "Search Google or type a URL", 13, 22)) {
            std::string typed = trim(addrEdit_.text);
            if (typed == "chrome://extensions" || typed == "windroid://extensions" || typed == "chrome://extensions/") {
                openExtPanel(ui);
            } else if (hasWeb) {
                web_->load(id_, normalizeUrl(addrEdit_.text));
            }
            addrFocus_ = false;
        }
        if (!wasFocus && addrFocus_) {
            addrEdit_.set(st_.url);
        }
        if (addrFocus_ && ui.pressed && !addr.contains(ui.x, ui.y)) addrFocus_ = false;
        drawIcon(g, secure ? IcGlyphLock : IcGlyphGlobe, {addr.x + 9, addr.cy() - 6.5f, 13, 13}, th.textDim);

        g.rect({bar.x, bar.b() - 1, bar.w, 1}, th.border);
        if (st_.loading && st_.progress < 100) {
            float p = std::max(0.05f, st_.progress / 100.0f);
            g.rect({bar.x, bar.b() - 2, bar.w * p, 2}, th.accent);
        }
    }

    std::shared_ptr<WebBridge> web_;
    std::string start_;
    int id_ = -1;
    WebState st_;
    LineEdit addrEdit_;
    bool addrFocus_ = false;
    bool lastEditable_ = false;
    GLuint tex_ = 0;
    int texW_ = 0, texH_ = 0;
    int lx_ = -1, ly_ = -1, lw_ = -1, lh_ = -1;
    int hw_ = 0, hh_ = 0;
    bool lvis_ = false;
    bool laidOut_ = false;
    bool pendingClip_ = false;
    Rect visPage_, visClip_;
    float snapX_ = 0, snapY_ = 0, snapW_ = 0, snapH_ = 0;

    void drawPlaceholder(Gfx& g, const Theme& th, const Rect& page) {
        g.rect(page, rgb(0xFFFFFF));
        Color bar = rgb(0xEDEDED);
        Color soft = rgb(0xF5F5F5);
        float pad = std::min(32.f, page.w * 0.05f);
        Rect head{page.x, page.y, page.w, std::min(56.f, page.h * 0.12f)};
        g.rect(head, rgb(0xFAFAFA));
        g.rect({head.x, head.b() - 1, head.w, 1}, rgb(0xE6E6E6));
        drawIcon(g, IcGlyphGlobe, {head.x + pad, head.cy() - 8, 16, 16}, rgb(0x9A9A9A));
        std::string host = st_.url.empty() ? "New tab" : displayUrl(st_.url);
        g.textIn({head.x + pad + 26, head.y, head.w - pad * 2 - 26, head.h}, 13, g.ellipsize(13, host, head.w - pad * 2 - 26),
                 rgb(0x707070));
        float y = head.b() + pad;
        float cw = page.w - pad * 2;
        std::string heading = st_.title.empty() ? host : st_.title;
        if (y + 30 < page.b()) {
            g.textIn({page.x + pad, y, cw, 30}, 20, g.ellipsize(20, heading, cw), rgb(0x2A2A2A));
            y += 44;
        }
        if (page.w > 560 && y + 140 < page.b()) {
            g.rect({page.x + pad, y, cw, std::min(180.f, (page.b() - y) * 0.4f)}, soft, 8);
            y += std::min(180.f, (page.b() - y) * 0.4f) + pad;
        }
        static const float widths[] = {0.92f, 0.86f, 0.95f, 0.6f, 0.0f, 0.9f, 0.82f, 0.94f, 0.7f, 0.0f};
        int i = 0;
        while (y + 12 < page.b() - pad * 0.5f) {
            float f = widths[i % 10];
            if (f > 0) g.rect({page.x + pad, y, cw * f, 12}, bar, 6);
            y += f > 0 ? 24 : 18;
            i++;
        }
        if (st_.loading && st_.progress < 100) {
            float p = std::max(0.05f, st_.progress / 100.0f);
            g.rect({page.x, page.y, page.w * p, 2}, th.accent);
        }
    }

    void setLayout(const Rect& page, const Rect& r, bool visible) {
        if (!web_ || id_ < 0) return;
        float s = shell->gfx().scale();
        if (visible) {
            int x = (int)std::lround(r.x * s), y = (int)std::lround(r.y * s);
            int w = std::max(1, (int)std::lround(r.w * s)), h = std::max(1, (int)std::lround(r.h * s));
            visPage_ = page;
            visClip_ = r;
            if (laidOut_ && lvis_ && x == lx_ && y == ly_ && w == lw_ && h == lh_) return;
            laidOut_ = true;
            lvis_ = true;
            lx_ = x;
            ly_ = y;
            lw_ = w;
            lh_ = h;
            web_->layout(id_, x, y, w, h, true);
            return;
        }
        int w = hw_, h = hh_;
        if (page.w >= 8 && page.h >= 8) {
            w = (int)std::lround(page.w * s);
            h = (int)std::lround(page.h * s);
        }
        if (w <= 0 || h <= 0) {
            if (!laidOut_) return;
            w = std::max(1, lw_);
            h = std::max(1, lh_);
        }
        if (laidOut_ && !lvis_ && w == hw_ && h == hh_) return;
        if (lvis_) pendingClip_ = true;
        laidOut_ = true;
        lvis_ = false;
        hw_ = w;
        hh_ = h;
        web_->layout(id_, lx_, ly_, w, h, false);
    }
};

int BrowserWindow::lastSeenDl_ = 0;
std::map<std::string, BrowserWindow::IconTex> BrowserWindow::extIcons_;

std::unique_ptr<AppWindow> makeBrowser(Shell& s, const std::string& arg) { return std::make_unique<BrowserWindow>(s, arg); }

}
