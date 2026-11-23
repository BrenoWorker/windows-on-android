#include <cmath>
#include <cstring>
#include <memory>

#include "Apps.h"
#include "Common.h"

namespace wd {

static void put16(std::vector<unsigned char>& b, uint32_t v) {
    b.push_back(v & 255);
    b.push_back((v >> 8) & 255);
}

static void put32(std::vector<unsigned char>& b, uint32_t v) {
    for (int i = 0; i < 4; i++) b.push_back((v >> (8 * i)) & 255);
}

static uint32_t rd32(const std::vector<unsigned char>& b, size_t o) {
    return b[o] | (b[o + 1] << 8) | (b[o + 2] << 16) | ((uint32_t)b[o + 3] << 24);
}

static uint16_t rd16(const std::vector<unsigned char>& b, size_t o) { return (uint16_t)(b[o] | (b[o + 1] << 8)); }

class PaintWindow : public AppWindow {
public:
    PaintWindow(const std::string& path) : alive_(std::make_shared<bool>(true)) {
        prefW = 760;
        prefH = 500;
        minW = 340;
        minH = 280;
        reset(800, 500);
        if (!path.empty() && load(path)) path_ = path;
        updateTitle();
    }
    ~PaintWindow() override {
        *alive_ = false;
        if (tex_ && shell) shell->gfx().deleteTexture(tex_);
    }

    void draw(Ui& ui, const Rect& c) override {
        Gfx& g = *ui.g;
        const Theme& th = *ui.th;
        g.rect(c, th.mica);
        if (!tex_ || texW_ != w_ || texH_ != h_) {
            if (tex_) g.deleteTexture(tex_);
            tex_ = g.createTexture(w_, h_, px_.data());
            texW_ = w_;
            texH_ = h_;
            dirty_ = false;
        }

        Rect bar{c.x, c.y, c.w, 52};
        g.rect({bar.x, bar.b() - 1, bar.w, 1}, th.border);
        float x = c.x + 8;
        bool compact = c.w < 640;
        if (toolButton(ui, {x, bar.y + 10, 32, 32}, IcGlyphNew, "", false)) {
            reset(800, 500);
            path_.clear();
            modified_ = false;
            updateTitle();
        }
        x += 34;
        if (toolButton(ui, {x, bar.y + 10, 32, 32}, IcGlyphOpen, "", false))
            shell->open(AppKind::Explorer, fs::join(shell->userDir(), "Pictures"));
        x += 34;
        if (toolButton(ui, {x, bar.y + 10, 32, 32}, IcGlyphSave, "", false)) save();
        x += 40;
        g.rect({x - 4, bar.y + 12, 1, 28}, th.border);
        IconId tools[] = {IcGlyphBrush, IcGlyphEraser, IcGlyphDown};
        for (int i = 0; i < 3; i++) {
            Rect r{x, bar.y + 10, 32, 32};
            if (i == 2) {
                if (tool_ == 2) g.rect(r, th.press, 4);
                if (ui.held(r)) g.rect(r, th.press, 4);
                g.rect({r.x + 9, r.y + 12, 14, 12}, th.text, 2);
                g.rect({r.x + 11, r.y + 14, 10, 8}, color_, 1);
                g.line(r.x + 23, r.y + 12, r.x + 25, r.y + 20, 1.5f, th.text);
                if (ui.clicked(r)) tool_ = 2;
            } else if (toolButton(ui, r, tools[i], "", false, tool_ == i)) {
                tool_ = i;
            }
            x += 34;
        }
        x += 6;
        g.rect({x - 4, bar.y + 12, 1, 28}, th.border);
        float sizes[] = {3, 8, 18};
        for (int i = 0; i < 3; i++) {
            Rect r{x, bar.y + 10, 32, 32};
            ui.pressFill(r, 4, brush_ == sizes[i]);
            g.circle(r.cx(), r.cy(), 2 + i * 2.6f, th.text);
            if (ui.clicked(r)) brush_ = sizes[i];
            x += 34;
        }
        x += 6;
        g.rect({x - 4, bar.y + 12, 1, 28}, th.border);
        uint32_t pal[] = {0x000000, 0x7F7F7F, 0xFFFFFF, 0xE81123, 0xFF8C00, 0xFFD400, 0x16C60C, 0x00B7C3, 0x0078D4, 0x8764B8, 0xE3008C, 0x8E562E};
        int pc = compact ? 6 : 12;
        g.circle(x + 16, bar.cy(), 13, th.border);
        g.circle(x + 16, bar.cy(), 12, color_);
        x += 38;
        for (int i = 0; i < pc; i++) {
            float sx = x + (i % (compact ? 6 : 6)) * 22;
            float sy = bar.y + (i < 6 ? 6 : 27);
            if (compact) sy = bar.y + 15;
            Rect r{sx, sy, 20, 19};
            if (r.r() > c.r() - 4) break;
            g.rect(r, th.border, 10);
            g.rect(r.inset(1), rgb(pal[i]), 9);
            if (ui.clicked(r)) {
                color_ = rgb(pal[i]);
                if (tool_ == 1) tool_ = 0;
            }
        }

        Rect area{c.x, bar.b(), c.w, c.h - bar.h - 24};
        g.rect(area, th.dark ? rgb(0x161616) : rgb(0xDCE3EB));
        float sc = std::min((area.w - 32) / w_, (area.h - 32) / h_);
        if (sc <= 0) sc = 0.1f;
        Rect view{area.cx() - w_ * sc / 2, area.cy() - h_ * sc / 2, w_ * sc, h_ * sc};

        if (ui.active && ui.down && view.contains(ui.downX, ui.downY) && tool_ != 2) {
            float cx = (ui.x - view.x) / sc, cy = (ui.y - view.y) / sc;
            if (!stroking_) {
                stroking_ = true;
                lx_ = cx;
                ly_ = cy;
                pushUndo();
            }
            Color col = tool_ == 1 ? Color{1, 1, 1, 1} : color_;
            segment(lx_, ly_, cx, cy, brush_ / 2, col);
            lx_ = cx;
            ly_ = cy;
            modified_ = true;
        }
        if (!ui.down) stroking_ = false;
        if (tool_ == 2 && ui.clicked(view)) {
            pushUndo();
            fill((int)((ui.x - view.x) / sc), (int)((ui.y - view.y) / sc), color_);
            modified_ = true;
        }
        if (dirty_) {
            g.updateTexture(tex_, w_, h_, px_.data());
            dirty_ = false;
        }
        g.pushClip(area);
        g.shadow(view, 0, 10, th.shadow);
        g.image(tex_, view);
        g.popClip();

        Rect status{c.x, c.b() - 24, c.w, 24};
        g.rect(status, th.mica);
        g.rect({status.x, status.y, status.w, 1}, th.border);
        g.textIn({status.x + 12, status.y, 300, status.h}, 11.5f,
                 std::to_string(w_) + " x " + std::to_string(h_) + "px    " + std::to_string((int)std::lround(sc * 100)) + "%",
                 th.textDim);
        Rect undo{status.r() - 70, status.y + 1, 66, 22};
        if (!undo_.empty()) {
            ui.pressFill(undo, 3);
            g.textIn(undo, 11.5f, "Undo", th.text, AlignCenter);
            if (ui.clicked(undo)) {
                px_ = undo_.back();
                undo_.pop_back();
                dirty_ = true;
            }
        }
        if (modified_ != titleMod_) updateTitle();
    }

private:
    std::shared_ptr<bool> alive_;
    std::vector<unsigned char> px_;
    std::vector<std::vector<unsigned char>> undo_;
    int w_ = 0, h_ = 0;
    GLuint tex_ = 0;
    int texW_ = 0, texH_ = 0;
    bool dirty_ = false;
    int tool_ = 0;
    float brush_ = 8;
    Color color_ = rgb(0x000000);
    bool stroking_ = false;
    float lx_ = 0, ly_ = 0;
    std::string path_;
    bool modified_ = false;
    bool titleMod_ = false;

    void updateTitle() {
        titleMod_ = modified_;
        title = std::string(modified_ ? "*" : "") + (path_.empty() ? "Untitled" : fs::baseName(path_)) + " - Paint";
    }

    void reset(int w, int h) {
        w_ = w;
        h_ = h;
        px_.assign((size_t)w * h * 4, 255);
        undo_.clear();
        dirty_ = true;
    }

    void pushUndo() {
        undo_.push_back(px_);
        if (undo_.size() > 12) undo_.erase(undo_.begin());
    }

    void stamp(float cx, float cy, float r, Color c) {
        int x0 = std::max(0, (int)std::floor(cx - r - 1)), x1 = std::min(w_ - 1, (int)std::ceil(cx + r + 1));
        int y0 = std::max(0, (int)std::floor(cy - r - 1)), y1 = std::min(h_ - 1, (int)std::ceil(cy + r + 1));
        unsigned char cr = (unsigned char)(c.r * 255), cg = (unsigned char)(c.g * 255), cb = (unsigned char)(c.b * 255);
        for (int y = y0; y <= y1; y++)
            for (int x = x0; x <= x1; x++) {
                float d = std::hypot(x + 0.5f - cx, y + 0.5f - cy);
                float a = clampf(r + 0.5f - d, 0, 1);
                if (a <= 0) continue;
                unsigned char* p = &px_[((size_t)y * w_ + x) * 4];
                p[0] = (unsigned char)(p[0] + (cr - p[0]) * a);
                p[1] = (unsigned char)(p[1] + (cg - p[1]) * a);
                p[2] = (unsigned char)(p[2] + (cb - p[2]) * a);
                p[3] = 255;
            }
        dirty_ = true;
    }

    void segment(float x0, float y0, float x1, float y1, float r, Color c) {
        float d = std::hypot(x1 - x0, y1 - y0);
        int steps = std::max(1, (int)(d / std::max(0.5f, r * 0.4f)));
        for (int i = 0; i <= steps; i++) {
            float t = (float)i / steps;
            stamp(x0 + (x1 - x0) * t, y0 + (y1 - y0) * t, r, c);
        }
    }

    void fill(int sx, int sy, Color c) {
        if (sx < 0 || sy < 0 || sx >= w_ || sy >= h_) return;
        unsigned char nr = (unsigned char)(c.r * 255), ng = (unsigned char)(c.g * 255), nb = (unsigned char)(c.b * 255);
        unsigned char* s = &px_[((size_t)sy * w_ + sx) * 4];
        unsigned char tr = s[0], tg = s[1], tb = s[2];
        if (tr == nr && tg == ng && tb == nb) return;
        auto match = [&](int x, int y) {
            unsigned char* p = &px_[((size_t)y * w_ + x) * 4];
            return std::abs(p[0] - tr) < 40 && std::abs(p[1] - tg) < 40 && std::abs(p[2] - tb) < 40;
        };
        std::vector<std::pair<int, int>> stack{{sx, sy}};
        std::vector<unsigned char> seen((size_t)w_ * h_, 0);
        while (!stack.empty()) {
            auto [x, y] = stack.back();
            stack.pop_back();
            int l = x;
            while (l > 0 && !seen[(size_t)y * w_ + l - 1] && match(l - 1, y)) l--;
            int r = x;
            while (r < w_ - 1 && !seen[(size_t)y * w_ + r + 1] && match(r + 1, y)) r++;
            for (int i = l; i <= r; i++) {
                size_t idx = (size_t)y * w_ + i;
                if (seen[idx]) continue;
                seen[idx] = 1;
                unsigned char* p = &px_[idx * 4];
                p[0] = nr;
                p[1] = ng;
                p[2] = nb;
                if (y > 0 && !seen[idx - w_] && match(i, y - 1)) stack.push_back({i, y - 1});
                if (y < h_ - 1 && !seen[idx + w_] && match(i, y + 1)) stack.push_back({i, y + 1});
            }
        }
        dirty_ = true;
    }

    bool load(const std::string& path) {
        std::vector<unsigned char> b;
        if (!fs::readBinary(path, b) || b.size() < 54 || b[0] != 'B' || b[1] != 'M') return false;
        uint32_t off = rd32(b, 10);
        int32_t w = (int32_t)rd32(b, 18), h = (int32_t)rd32(b, 22);
        uint16_t bpp = rd16(b, 28);
        uint32_t comp = rd32(b, 30);
        if ((bpp != 24 && bpp != 32) || (comp != 0 && comp != 3) || w <= 0 || h == 0 || w > 4096 || std::abs(h) > 4096) return false;
        bool topDown = h < 0;
        int ah = std::abs(h);
        size_t stride = ((size_t)w * (bpp / 8) + 3) & ~(size_t)3;
        if (off + stride * ah > b.size()) return false;
        reset(w, ah);
        for (int y = 0; y < ah; y++) {
            int sy = topDown ? y : ah - 1 - y;
            const unsigned char* row = &b[off + stride * sy];
            for (int x = 0; x < w; x++) {
                const unsigned char* s = row + x * (bpp / 8);
                unsigned char* d = &px_[((size_t)y * w + x) * 4];
                d[0] = s[2];
                d[1] = s[1];
                d[2] = s[0];
                d[3] = 255;
            }
        }
        return true;
    }

    bool writeBmp(const std::string& path) {
        size_t stride = ((size_t)w_ * 3 + 3) & ~(size_t)3;
        std::vector<unsigned char> b;
        b.reserve(54 + stride * h_);
        b.push_back('B');
        b.push_back('M');
        put32(b, (uint32_t)(54 + stride * h_));
        put32(b, 0);
        put32(b, 54);
        put32(b, 40);
        put32(b, (uint32_t)w_);
        put32(b, (uint32_t)h_);
        put16(b, 1);
        put16(b, 24);
        put32(b, 0);
        put32(b, (uint32_t)(stride * h_));
        put32(b, 2835);
        put32(b, 2835);
        put32(b, 0);
        put32(b, 0);
        for (int y = h_ - 1; y >= 0; y--) {
            size_t start = b.size();
            for (int x = 0; x < w_; x++) {
                const unsigned char* p = &px_[((size_t)y * w_ + x) * 4];
                b.push_back(p[2]);
                b.push_back(p[1]);
                b.push_back(p[0]);
            }
            while (b.size() - start < stride) b.push_back(0);
        }
        return fs::writeBinary(path, b);
    }

    void save() {
        if (!path_.empty()) {
            if (writeBmp(path_)) {
                modified_ = false;
                shell->notifyFs();
            }
            return;
        }
        std::shared_ptr<bool> alive = alive_;
        std::string dir = fs::join(shell->userDir(), "Pictures");
        shell->prompt("Save as", "File name (saved in Pictures):", fs::uniqueName(dir, "Untitled", "bmp"),
                      [this, alive, dir](const std::string& n) {
                          if (!*alive || n.empty()) return;
                          std::string name = n;
                          if (fs::extension(name) != "bmp") name += ".bmp";
                          path_ = fs::join(dir, name);
                          if (writeBmp(path_)) {
                              modified_ = false;
                              shell->notifyFs();
                          }
                          updateTitle();
                      });
    }
};

std::unique_ptr<AppWindow> makePaint(Shell&, const std::string& arg) { return std::make_unique<PaintWindow>(arg); }

}
