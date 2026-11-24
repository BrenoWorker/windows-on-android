#include <cmath>
#include <cstdio>

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

        bool visible = exclusive && clipped.w > 8 && clipped.h > 8;
        setLayout(page, clipped, visible);

        if (!visible) {
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

        if (st_.editable && !lastEditable_) {
            addrFocus_ = false;
            ui.requestKeyboard = true;
        }
        lastEditable_ = st_.editable;

        if (ui.focus && !addrFocus_) {
            std::string typed;
            for (uint32_t ch : ui.chars)
                if (ch >= 32) utf8Append(typed, ch);
            if (!typed.empty()) web_->text(id_, typed);
            for (int k : ui.keys) web_->key(id_, k);
        }
    }

private:
    bool fsOn_ = false;
    bool fsMaximized_ = false;

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
        Rect deskBtn{right - 32, bar.y + 7, 32, 32};
        right -= 34;
        Rect padBtn{right - 32, bar.y + 7, 32, 32};
        right -= 38;
        if (toolButton(ui, chromeBtn, IcGlyphExternal, "", false, false, hasWeb)) web_->action(id_, WebOpenInChrome);
        if (toolButton(ui, deskBtn, IcGlyphMonitor, "", false, st_.desktop, hasWeb)) web_->action(id_, WebToggleDesktop);
        if (toolButton(ui, padBtn, IcGlyphGamepad, "", false, st_.controls, hasWeb)) web_->action(id_, WebToggleControls);

        Rect addr{x + 4, bar.y + 7, right - x - 4, 32};
        if (!addrFocus_) addrEdit_.set(displayUrl(st_.url));
        bool wasFocus = addrFocus_;
        bool secure = st_.url.rfind("https://", 0) == 0;
        if (ui.lineEdit(addr, addrEdit_, addrFocus_, "Search Google or type a URL", 13, 22)) {
            if (hasWeb) web_->load(id_, normalizeUrl(addrEdit_.text));
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

std::unique_ptr<AppWindow> makeBrowser(Shell& s, const std::string& arg) { return std::make_unique<BrowserWindow>(s, arg); }

}
