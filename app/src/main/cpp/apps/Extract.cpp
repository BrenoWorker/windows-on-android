#include <algorithm>
#include <cmath>

#include "../core/Archive.h"
#include "Apps.h"
#include "Common.h"

namespace wd {

class ExtractWindow : public AppWindow {
public:
    ExtractWindow(Shell& s, const std::string& archive) : archive_(archive) {
        std::string name = fs::baseName(archive);
        title = "Extract - " + name;
        prefW = 520;
        prefH = 380;
        minW = 360;
        minH = 300;
        resizable = false;
        icon = IcArchive;
        archiveInfo(archive, "", info_);
        std::string dest = fs::join(fs::parent(archive), fs::stem(name));
        dest_.set(s.displayPath(dest));
        if (info_.encrypted) passFocus_ = true;
    }

    ~ExtractWindow() override {
        if (job_ && job_->state == ArchiveState::Running) job_->cancel = true;
    }

    bool wantsText() const override { return phase_ == Phase::Setup; }

    void draw(Ui& ui, const Rect& c) override {
        Gfx& g = *ui.g;
        const Theme& th = *ui.th;
        g.rect(c, th.mica);
        if (phase_ == Phase::Progress) poll();
        if (phase_ == Phase::Setup)
            drawSetup(ui, c);
        else
            drawProgress(ui, c);
    }

private:
    enum class Phase { Setup, Progress };
    std::string archive_;
    ArchiveInfo info_;
    LineEdit dest_;
    LineEdit pass_;
    bool destFocus_ = false;
    bool passFocus_ = false;
    bool showAfter_ = true;
    Phase phase_ = Phase::Setup;
    std::shared_ptr<ArchiveJob> job_;
    std::string destReal_;
    std::string note_;
    double startT_ = 0;

    void start() {
        destReal_ = shell->realPath(dest_.text, shell->root());
        if (destReal_.empty() || destReal_ == shell->root()) destReal_ = fs::join(fs::parent(archive_), fs::stem(fs::baseName(archive_)));
        note_.clear();
        job_ = extractArchive(archive_, destReal_, pass_.text);
        phase_ = Phase::Progress;
        startT_ = shell->now();
    }

    void poll() {
        if (!job_) return;
        ArchiveState st = job_->state;
        if (st == ArchiveState::Running) return;
        shell->notifyFs();
        if (st == ArchiveState::Done) {
            shell->playSound("notify");
            if (showAfter_) shell->open(AppKind::Explorer, destReal_);
            shell->close(this);
        } else if (st == ArchiveState::NeedPassword || st == ArchiveState::WrongPassword) {
            info_.encrypted = true;
            note_ = st == ArchiveState::WrongPassword ? "The password is incorrect. Try again." : "This archive is password protected.";
            pass_.set("");
            passFocus_ = true;
            phase_ = Phase::Setup;
            shell->playSound("error");
        } else if (st == ArchiveState::Cancelled) {
            phase_ = Phase::Setup;
            note_ = "Extraction was canceled.";
        } else {
            std::string err = job_->error();
            shell->message("Extract", err.empty() ? "The archive couldn't be extracted." : err);
            phase_ = Phase::Setup;
            note_ = err;
        }
        job_.reset();
    }

    void drawSetup(Ui& ui, const Rect& c) {
        Gfx& g = *ui.g;
        const Theme& th = *ui.th;
        float x = c.x + 22, w = c.w - 44, y = c.y + 16;
        drawIcon(g, IcArchive, {x, y, 40, 40}, th.text);
        g.textBold(x + 54, y + 2, 15, "Select a Destination and Extract Files", th.text);
        std::string sub;
        if (!info_.error.empty()) {
            sub = info_.error;
        } else {
            sub = (info_.format.empty() ? std::string("Archive") : info_.format + " archive");
            if (!info_.encryptedNames || info_.files > 0) sub += "  \xC2\xB7  " + std::to_string(info_.files) + (info_.files == 1 ? " file" : " files");
            if (info_.size > 0) sub += "  \xC2\xB7  " + fs::formatSize(info_.size);
            if (info_.encrypted) sub += "  \xC2\xB7  Encrypted";
        }
        g.text(x + 54, y + 24, 12, g.ellipsize(12, sub, w - 54), info_.error.empty() ? th.textDim : rgb(0xD13438));
        y += 62;
        g.text(x, y, 12.5f, "Files will be extracted to this folder:", th.text);
        y += 22;
        if (ui.lineEdit({x, y, w, 32}, dest_, destFocus_, "", 12.5f) && !info_.encrypted) start();
        if (destFocus_) passFocus_ = false;
        y += 42;
        if (info_.encrypted) {
            g.text(x, y, 12.5f, "Password:", th.text);
            y += 22;
            LineEdit shown = pass_;
            std::string dots;
            for (size_t i = 0; i < utf8Length(pass_.text); i++) dots += "\xE2\x97\x8F";
            shown.text = dots;
            shown.caret = dots.size();
            bool wasFocus = passFocus_;
            bool enter = ui.lineEdit({x, y, w, 32}, shown, passFocus_, "Enter the archive password", 12.5f);
            if (wasFocus && ui.focus) pass_.apply(ui.chars, ui.keys);
            if (passFocus_) destFocus_ = false;
            if (enter) start();
            y += 42;
        }
        Rect tr{x - 2, y, w, 28};
        ui.toggle({tr.x, tr.y + 4, 48, 20}, showAfter_);
        g.textIn({tr.x + 56, tr.y, tr.w - 56, tr.h}, 12.5f, "Show extracted files when complete", th.text);
        y += 34;
        if (!note_.empty()) g.text(x, y, 12, g.ellipsize(12, note_, w), note_.find("canceled") != std::string::npos ? th.textDim : rgb(0xD13438));

        Rect bar{c.x, c.b() - 64, c.w, 64};
        g.rect(bar, th.dark ? rgb(0x202020) : rgb(0xF0F0F0));
        g.rect({bar.x, bar.y, bar.w, 1}, th.border);
        Rect cancel{c.r() - 130, bar.y + 16, 110, 32};
        Rect go{cancel.x - 120, bar.y + 16, 110, 32};
        bool can = info_.error.empty() && (!info_.encrypted || !pass_.text.empty());
        if (ui.button(go, "Extract", true, can)) start();
        if (ui.button(cancel, "Cancel")) shell->close(this);
    }

    void drawProgress(Ui& ui, const Rect& c) {
        Gfx& g = *ui.g;
        const Theme& th = *ui.th;
        if (!job_) return;
        float x = c.x + 22, w = c.w - 44, y = c.y + 20;
        long long done = job_->done, total = job_->total;
        float f = total > 0 ? std::min(1.f, (float)done / total) : 0.f;
        g.textBold(x, y, 15, std::to_string((int)(f * 100)) + "% complete", th.text);
        y += 32;
        std::string from = fs::baseName(archive_), to = fs::baseName(destReal_);
        g.text(x, y, 12, g.ellipsize(12, "Extracting from " + from + " to " + to, w), th.textDim);
        y += 30;
        Rect pb{x, y, w, 8};
        g.rect(pb, alpha(th.text, 0.12f), 4);
        if (total > 0) {
            g.rect({pb.x, pb.y, pb.w * f, pb.h}, th.accent, 4);
        } else {
            float t = (float)fmod(ui.time * 0.7, 1.0);
            g.rect({pb.x + pb.w * t * 0.75f, pb.y, pb.w * 0.25f, pb.h}, th.accent, 4);
        }
        y += 24;
        std::string cur = job_->current();
        g.text(x, y, 12, g.ellipsize(12, "Name: " + cur, w), th.text);
        y += 22;
        double el = std::max(0.1, shell->now() - startT_);
        std::string items = std::to_string(job_->files.load()) + " items";
        if (total > 0) items += "  \xC2\xB7  " + fs::formatSize(done) + " of " + fs::formatSize(total);
        if (el > 1 && done > 0) items += "  \xC2\xB7  " + fs::formatSize((long long)(done / el)) + "/s";
        g.text(x, y, 12, g.ellipsize(12, items, w), th.textDim);
        Rect bar{c.x, c.b() - 64, c.w, 64};
        g.rect(bar, th.dark ? rgb(0x202020) : rgb(0xF0F0F0));
        g.rect({bar.x, bar.y, bar.w, 1}, th.border);
        if (ui.button({c.r() - 130, bar.y + 16, 110, 32}, "Cancel")) job_->cancel = true;
    }
};

std::unique_ptr<AppWindow> makeExtract(Shell& s, const std::string& archive) { return std::make_unique<ExtractWindow>(s, archive); }

}
