#include <cstdlib>
#include <ctime>

#include "Apps.h"
#include "Common.h"

namespace wd {

class MinesweeperWindow : public AppWindow {
public:
    MinesweeperWindow() {
        prefW = 340;
        prefH = 450;
        minW = 300;
        minH = 380;
        srand((unsigned)time(nullptr));
        newGame();
    }

    void draw(Ui& ui, const Rect& c) override {
        Gfx& g = *ui.g;
        const Theme& th = *ui.th;
        g.rect(c, th.mica);
        if (state_ == 0 && started_) elapsed_ += ui.dt;

        Rect top{c.x + 12, c.y + 10, c.w - 24, 44};
        card(g, th, top);
        int flags = 0;
        for (auto& cl : cells_)
            if (cl.flag) flags++;
        g.rect({top.x + 8, top.y + 8, 56, 28}, rgb(0x1A1A1A), 4);
        g.textIn({top.x + 8, top.y + 8, 56, 28}, 16, std::to_string(kMines - flags), rgb(0xFF4B3A), AlignCenter);
        g.rect({top.r() - 64, top.y + 8, 56, 28}, rgb(0x1A1A1A), 4);
        g.textIn({top.r() - 64, top.y + 8, 56, 28}, 16, std::to_string(std::min(999, (int)elapsed_)), rgb(0xFF4B3A), AlignCenter);
        Rect face{top.cx() - 18, top.y + 4, 36, 36};
        ui.pressFill(face, 6);
        g.circle(face.cx(), face.cy(), 13, rgb(0xFFD43B));
        g.ring(face.cx(), face.cy(), 13, 1, rgb(0x8A6D00));
        if (state_ == 2) {
            g.line(face.cx() - 7, face.cy() - 6, face.cx() - 3, face.cy() - 2, 1.4f, rgb(0x222222));
            g.line(face.cx() - 3, face.cy() - 6, face.cx() - 7, face.cy() - 2, 1.4f, rgb(0x222222));
            g.line(face.cx() + 3, face.cy() - 6, face.cx() + 7, face.cy() - 2, 1.4f, rgb(0x222222));
            g.line(face.cx() + 7, face.cy() - 6, face.cx() + 3, face.cy() - 2, 1.4f, rgb(0x222222));
            g.arc(face.cx(), face.cy() + 9, 5, 3.6f, 5.8f, 1.4f, rgb(0x222222));
        } else {
            if (state_ == 1) {
                g.rect({face.cx() - 9, face.cy() - 6, 7, 4}, rgb(0x111111), 1.5f);
                g.rect({face.cx() + 2, face.cy() - 6, 7, 4}, rgb(0x111111), 1.5f);
            } else {
                g.circle(face.cx() - 4.5f, face.cy() - 3, 1.6f, rgb(0x222222));
                g.circle(face.cx() + 4.5f, face.cy() - 3, 1.6f, rgb(0x222222));
            }
            g.arc(face.cx(), face.cy() + 1, 6, 0.5f, 2.64f, 1.4f, rgb(0x222222));
        }
        if (ui.clicked(face)) newGame();

        Rect modeR{c.x + 12, c.b() - 44, c.w - 24, 34};
        if (ui.button(modeR, flagMode_ ? "Mode: Flag (tap to switch)" : "Mode: Reveal (tap to switch)", flagMode_)) flagMode_ = !flagMode_;

        Rect board{c.x + 12, top.b() + 10, c.w - 24, modeR.y - top.b() - 20};
        float cs = std::min(board.w / kW, board.h / kH);
        Rect grid{board.cx() - cs * kW / 2, board.cy() - cs * kH / 2, cs * kW, cs * kH};
        const uint32_t numCol[] = {0, 0x0078D4, 0x107C10, 0xE81123, 0x00188F, 0x8B2500, 0x008B8B, 0x111111, 0x777777};
        for (int y = 0; y < kH; y++)
            for (int x = 0; x < kW; x++) {
                Cell& cl = cells_[(size_t)(y * kW + x)];
                Rect r{grid.x + x * cs + 1, grid.y + y * cs + 1, cs - 2, cs - 2};
                if (cl.open) {
                    g.rect(r, th.dark ? rgb(0x2E2E2E) : rgb(0xE9EDF1), 3);
                    if (cl.mine) {
                        g.rect(r, cl.boom ? rgb(0xE81123) : (th.dark ? rgb(0x2E2E2E) : rgb(0xE9EDF1)), 3);
                        drawIcon(g, IcMines, r.inset(r.w * 0.18f), th.text);
                    } else if (cl.n > 0) {
                        g.textIn(r, cs * 0.5f, std::to_string(cl.n), th.dark ? lighten(rgb(numCol[cl.n]), 0.35f) : rgb(numCol[cl.n]), AlignCenter);
                    }
                } else {
                    Color base = th.dark ? rgb(0x4A5563) : rgb(0x9FB2C6);
                    if (ui.held(r) && state_ == 0) base = darken(base, 0.12f);
                    g.rectV(r, lighten(base, 0.12f), base, 3);
                    if (cl.flag) drawIcon(g, IcGlyphFlag, r.inset(r.w * 0.2f), rgb(0x111111));
                }
                if (state_ != 0) continue;
                bool tap = ui.clicked(r);
                bool lp = ui.longPressed(r);
                if ((tap && flagMode_) || lp) {
                    if (!cl.open) cl.flag = !cl.flag;
                } else if (tap && !cl.flag) {
                    reveal(x, y);
                }
            }
        if (state_ == 1) g.textIn({grid.x, grid.b() + 2, grid.w, 16}, 12, "You win!", th.accent, AlignCenter);
        if (state_ == 2) g.textIn({grid.x, grid.b() + 2, grid.w, 16}, 12, "Game over. Tap the face to retry.", th.danger, AlignCenter);
    }

private:
    static const int kW = 9, kH = 9, kMines = 10;
    struct Cell {
        bool mine = false, open = false, flag = false, boom = false;
        int n = 0;
    };
    std::vector<Cell> cells_;
    int state_ = 0;
    bool started_ = false;
    bool flagMode_ = false;
    float elapsed_ = 0;

    void newGame() {
        cells_.assign(kW * kH, Cell{});
        state_ = 0;
        started_ = false;
        elapsed_ = 0;
    }

    void place(int sx, int sy) {
        int placed = 0;
        while (placed < kMines) {
            int x = rand() % kW, y = rand() % kH;
            if (std::abs(x - sx) <= 1 && std::abs(y - sy) <= 1) continue;
            Cell& c = cells_[(size_t)(y * kW + x)];
            if (c.mine) continue;
            c.mine = true;
            placed++;
        }
        for (int y = 0; y < kH; y++)
            for (int x = 0; x < kW; x++) {
                int n = 0;
                for (int dy = -1; dy <= 1; dy++)
                    for (int dx = -1; dx <= 1; dx++) {
                        int nx = x + dx, ny = y + dy;
                        if (nx >= 0 && ny >= 0 && nx < kW && ny < kH && cells_[(size_t)(ny * kW + nx)].mine) n++;
                    }
                cells_[(size_t)(y * kW + x)].n = n;
            }
    }

    void reveal(int x, int y) {
        if (!started_) {
            place(x, y);
            started_ = true;
        }
        Cell& c = cells_[(size_t)(y * kW + x)];
        if (c.mine) {
            c.boom = true;
            for (auto& cl : cells_)
                if (cl.mine) cl.open = true;
            state_ = 2;
            return;
        }
        std::vector<std::pair<int, int>> st{{x, y}};
        while (!st.empty()) {
            auto [cx, cy] = st.back();
            st.pop_back();
            Cell& cl = cells_[(size_t)(cy * kW + cx)];
            if (cl.open || cl.flag) continue;
            cl.open = true;
            if (cl.n == 0)
                for (int dy = -1; dy <= 1; dy++)
                    for (int dx = -1; dx <= 1; dx++) {
                        int nx = cx + dx, ny = cy + dy;
                        if (nx >= 0 && ny >= 0 && nx < kW && ny < kH) st.push_back({nx, ny});
                    }
        }
        int closed = 0;
        for (auto& cl : cells_)
            if (!cl.open) closed++;
        if (closed == kMines) state_ = 1;
    }
};

std::unique_ptr<AppWindow> makeMinesweeper(Shell&) { return std::make_unique<MinesweeperWindow>(); }

}
