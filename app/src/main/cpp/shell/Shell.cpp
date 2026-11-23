#include "Shell.h"

#include <cmath>
#include <cstdio>
#include <ctime>

#include "../apps/Apps.h"
#include "../core/Utf8.h"

namespace wd {

static const float kTaskbarH = 48;
static const float kPi = 3.14159265f;
static const uint32_t kAccents[] = {0x0078D4, 0x8764B8, 0x107C10, 0xCA5010, 0xE3008C, 0x038387, 0xC30052, 0x6B69D6};

const std::vector<AppInfo>& appCatalog() {
    static const std::vector<AppInfo> apps = {
        {AppKind::Explorer, "File Explorer", IcExplorer, true},
        {AppKind::Browser, "Browser", IcBrowser, true},
        {AppKind::Notepad, "Notepad", IcNotepad, true},
        {AppKind::Calculator, "Calculator", IcCalculator, true},
        {AppKind::Settings, "Settings", IcSettings, true},
        {AppKind::Terminal, "Terminal", IcTerminal, true},
        {AppKind::Paint, "Paint", IcPaint, true},
        {AppKind::Minesweeper, "Minesweeper", IcMines, false},
    };
    return apps;
}

const AppInfo& appInfo(AppKind k) {
    static const AppInfo dialog{AppKind::Dialog, "Dialog", IcDialog, false};
    for (auto& a : appCatalog())
        if (a.kind == k) return a;
    return dialog;
}

Shell::Shell(Platform p) : plat_(std::move(p)) {}

Shell::~Shell() { windows_.clear(); }

void Shell::init() {
    gfx_.init();
    gfx_.addFont(plat_.fontPaths);
    int mono = gfx_.addFont(plat_.monoFontPaths);
    monoFont_ = mono >= 0 ? mono : 0;
    ui_.g = &gfx_;
    ui_.th = &theme_;
    loadConfig();
    ensureFilesystem();
    theme_.build(cfg_.dark, rgb(kAccents[cfg_.accent % 8]));
    state_ = State::Boot;
    stateT_ = -1;
    started_ = true;
}

void Shell::resize(int pw, int ph) {
    pw_ = pw;
    ph_ = ph;
    float base = std::min(plat_.density, std::min(pw, ph) / 480.0f);
    float s = base * cfg_.scalePct / 100.0f;
    if (s < 0.75f) s = 0.75f;
    gfx_.resize(pw, ph, s);
    Rect wa = workArea();
    for (auto& w : windows_) {
        if (w->frame.w > wa.w) w->frame.w = wa.w;
        if (w->frame.h > wa.h) w->frame.h = wa.h;
        w->frame.x = clampf(w->frame.x, wa.x - w->frame.w + 80, wa.r() - 80);
        w->frame.y = clampf(w->frame.y, wa.y, wa.b() - w->titleH);
    }
}

void Shell::applyConfig() {
    theme_.build(cfg_.dark, rgb(kAccents[((cfg_.accent % 8) + 8) % 8]));
    resize(pw_, ph_);
}

void Shell::loadConfig() {
    std::string data;
    if (!fs::readText(fs::join(plat_.dataDir, "config.ini"), data)) return;
    size_t pos = 0;
    while (pos < data.size()) {
        size_t nl = data.find('\n', pos);
        if (nl == std::string::npos) nl = data.size();
        std::string line = data.substr(pos, nl - pos);
        pos = nl + 1;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        if (k == "dark") cfg_.dark = v == "1";
        if (k == "accent") cfg_.accent = atoi(v.c_str());
        if (k == "wallpaper") cfg_.wallpaper = atoi(v.c_str());
        if (k == "scale") cfg_.scalePct = std::max(50, std::min(200, atoi(v.c_str())));
        if (k == "taskbarLeft") cfg_.taskbarLeft = v == "1";
        if (k == "user" && !v.empty()) cfg_.user = v;
    }
}

void Shell::saveConfig() {
    std::string s;
    s += "dark=" + std::string(cfg_.dark ? "1" : "0") + "\n";
    s += "accent=" + std::to_string(cfg_.accent) + "\n";
    s += "wallpaper=" + std::to_string(cfg_.wallpaper) + "\n";
    s += "scale=" + std::to_string(cfg_.scalePct) + "\n";
    s += "taskbarLeft=" + std::string(cfg_.taskbarLeft ? "1" : "0") + "\n";
    s += "user=" + cfg_.user + "\n";
    fs::writeText(fs::join(plat_.dataDir, "config.ini"), s);
}

std::string Shell::root() const { return fs::join(plat_.dataDir, "C"); }
std::string Shell::userDir() const { return fs::join(root(), "Users/User"); }
std::string Shell::recycleDir() const { return fs::join(root(), "$Recycle.Bin"); }

void Shell::ensureFilesystem() {
    bool first = !fs::isDir(root());
    for (const char* d : {"Desktop", "Documents", "Downloads", "Pictures", "Music"}) fs::makeDirs(fs::join(userDir(), d));
    fs::makeDirs(fs::join(root(), "Windows/System32"));
    fs::makeDirs(fs::join(root(), "Program Files"));
    fs::makeDirs(recycleDir());
    if (first) {
        fs::writeText(fs::join(userDir(), "Documents/Welcome.txt"),
                      "Welcome to WinDroid!\n\n"
                      "This is a Windows 11 style desktop running natively on Android, written in C++.\n\n"
                      "Tips:\n"
                      "- Drag a window by its title bar. Drop it on the top edge to maximize, or on a side to snap.\n"
                      "- Double tap a title bar to maximize or restore.\n"
                      "- Long press the desktop for more options.\n"
                      "- Tap the clock for the calendar and the tray icons for quick settings.\n"
                      "- Use the keyboard icon on the taskbar to open the touch keyboard.\n");
        fs::writeText(fs::join(userDir(), "Desktop/Notes.txt"), "My notes\n");
        fs::writeText(fs::join(root(), "Windows/System32/drivers.ini"), "[drivers]\ndisplay=gles2\ninput=touch\n");
    }
}

std::string Shell::displayPath(const std::string& real) const {
    std::string r = root();
    if (real.compare(0, r.size(), r) != 0) return real;
    std::string rest = real.substr(r.size());
    for (auto& c : rest)
        if (c == '/') c = '\\';
    if (rest.empty()) rest = "\\";
    return "C:" + rest;
}

std::string Shell::realPath(const std::string& input, const std::string& cwd) const {
    std::string p = input;
    for (auto& c : p)
        if (c == '\\') c = '/';
    std::string base;
    if (p.size() >= 2 && (p[0] == 'C' || p[0] == 'c') && p[1] == ':') {
        base = root();
        p = p.substr(2);
    } else if (!p.empty() && p[0] == '/') {
        base = root();
    } else {
        base = cwd;
    }
    std::vector<std::string> parts;
    std::string r = root();
    std::string rel = base.size() > r.size() ? base.substr(r.size() + 1) : "";
    auto split = [&](const std::string& s) {
        size_t i = 0;
        while (i <= s.size()) {
            size_t j = s.find('/', i);
            if (j == std::string::npos) j = s.size();
            std::string part = s.substr(i, j - i);
            if (part == "..") {
                if (!parts.empty()) parts.pop_back();
            } else if (!part.empty() && part != ".") {
                parts.push_back(part);
            }
            i = j + 1;
        }
    };
    split(rel);
    split(p);
    std::string out = r;
    for (auto& part : parts) out = fs::join(out, part);
    return out;
}

void Shell::recycle(const std::string& path) {
    std::string rb = recycleDir();
    if (path.compare(0, rb.size(), rb) == 0) {
        fs::removeAll(path);
    } else {
        std::string name = fs::baseName(path);
        std::string target = fs::join(rb, fs::uniqueName(rb, fs::stem(name), fs::isDir(path) ? "" : fs::extension(name)));
        if (fs::isDir(path)) target = fs::join(rb, fs::uniqueName(rb, name, ""));
        if (!fs::renamePath(path, target)) fs::removeAll(path);
    }
    notifyFs();
}

Rect Shell::workArea() const {
    float W = gfx_.width(), H = gfx_.height();
    float kh = kbOpen_ ? keyboardHeight() : 0;
    return {insL_, insT_, W - insL_ - insR_, H - kTaskbarH - kh - insT_};
}

void Shell::setInsets(float l, float t, float r, float b) {
    float s = gfx_.scale();
    insL_ = l / s;
    insT_ = t / s;
    insR_ = r / s;
    insB_ = b / s;
}

float Shell::keyboardHeight() const { return std::min(gfx_.height() * 0.44f, 290.0f); }

Rect Shell::keyboardRect() const {
    float kh = keyboardHeight();
    return {0, gfx_.height() - kTaskbarH - kh, gfx_.width(), kh};
}

Rect Shell::taskbarRect() const { return {0, gfx_.height() - kTaskbarH, gfx_.width(), kTaskbarH}; }

Rect Shell::startRect() const {
    float W = gfx_.width(), H = gfx_.height();
    float w = std::min(620.f, W - 16), h = std::min(660.f, H - kTaskbarH - 24);
    float x = cfg_.taskbarLeft ? 12 + insL_ : (W - w) * 0.5f;
    return {x, H - kTaskbarH - 12 - h, w, h};
}

Rect Shell::quickRect() const {
    float W = gfx_.width(), H = gfx_.height();
    float w = std::min(360.f, W - 16), h = std::min(318.f, H - kTaskbarH - 24);
    return {W - w - 12 - insR_, H - kTaskbarH - 12 - h, w, h};
}

Rect Shell::calendarRect() const {
    float W = gfx_.width(), H = gfx_.height();
    float w = std::min(336.f, W - 16), h = std::min(372.f, H - kTaskbarH - 24);
    return {W - w - 12 - insR_, H - kTaskbarH - 12 - h, w, h};
}

Rect Shell::contextRect() const {
    float w = 224, h = ctxItems_.size() * 36.f + 8;
    float x = std::min(ctxX_, gfx_.width() - w - 4), y = std::min(ctxY_, gfx_.height() - kTaskbarH - h - 4);
    return {std::max(4.f, x), std::max(4.f, y), w, h};
}

void Shell::pointer(int type, float px, float py) {
    float s = gfx_.scale();
    queue_.push_back({type, px / s, py / s, 0, 0});
}

void Shell::key(int k) { queue_.push_back({4, 0, 0, 0, k}); }

void Shell::character(uint32_t cp) { queue_.push_back({3, 0, 0, cp, 0}); }

void Shell::hardwareKeyInput() {
    hardwareKb_ = true;
    kbOpen_ = false;
}

bool Shell::back() {
    if (state_ == State::SignIn) {
        state_ = State::Lock;
        stateT_ = time_ - 1;
        lockOffset_ = 0;
        unlocking_ = false;
        signPower_ = false;
        return true;
    }
    if (state_ != State::Desktop) return true;
    if (kbOpen_) {
        kbOpen_ = false;
        return true;
    }
    if (ctxOpen_) {
        ctxOpen_ = false;
        return true;
    }
    if (startOpen_ || quickOpen_ || calOpen_) {
        closeFlyouts();
        return true;
    }
    if (focused_) {
        if (!focused_->onBack()) close(focused_);
        return true;
    }
    return true;
}

void Shell::closeFlyouts() {
    startOpen_ = false;
    quickOpen_ = false;
    calOpen_ = false;
    powerOpen_ = false;
}

void Shell::openContext(float x, float y, std::vector<MenuItem> items) {
    ctxItems_ = std::move(items);
    ctxX_ = x;
    ctxY_ = y;
    ctxOpen_ = true;
}

std::string Shell::timeString() const {
    time_t t = time(nullptr);
    struct tm lt;
    localtime_r(&t, &lt);
    char buf[16];
    strftime(buf, sizeof buf, "%H:%M", &lt);
    return buf;
}

std::string Shell::dateString() const {
    time_t t = time(nullptr);
    struct tm lt;
    localtime_r(&t, &lt);
    char buf[24];
    strftime(buf, sizeof buf, "%d/%m/%Y", &lt);
    return buf;
}

AppWindow* Shell::open(AppKind k, const std::string& arg) {
    std::unique_ptr<AppWindow> w;
    switch (k) {
        case AppKind::Explorer: w = makeExplorer(*this, arg); break;
        case AppKind::Browser: w = makeBrowser(*this, arg); break;
        case AppKind::Notepad: w = makeNotepad(*this, arg); break;
        case AppKind::Calculator: w = makeCalculator(*this); break;
        case AppKind::Settings: w = makeSettings(*this, arg); break;
        case AppKind::Terminal: w = makeTerminal(*this, arg); break;
        case AppKind::Paint: w = makePaint(*this, arg); break;
        case AppKind::Minesweeper: w = makeMinesweeper(*this); break;
        case AppKind::Dialog: return nullptr;
    }
    if (!w) return nullptr;
    w->shell = this;
    w->kind = k;
    if (w->title.empty()) w->title = appInfo(k).name;
    w->icon = appInfo(k).icon;
    Rect wa = workArea();
    float W = std::min(w->prefW, wa.w - 16), H = std::min(w->prefH, wa.h - 16);
    float off = (float)(openCount_++ % 6) * 26 - 52;
    w->frame = {wa.x + (wa.w - W) * 0.5f + off, wa.y + (wa.h - H) * 0.5f + off * 0.6f, W, H};
    w->frame.x = clampf(w->frame.x, wa.x + 4, std::max(wa.x + 4, wa.r() - W - 4));
    w->frame.y = clampf(w->frame.y, wa.y + 4, std::max(wa.y + 4, wa.b() - H - 4));
    w->restore = w->frame;
    w->anim = 0;
    AppWindow* raw = w.get();
    windows_.push_back(std::move(w));
    focus(raw);
    closeFlyouts();
    ctxOpen_ = false;
    return raw;
}

void Shell::openFile(const std::string& path) {
    if (fs::isDir(path)) {
        open(AppKind::Explorer, path);
        return;
    }
    std::string ext = fs::extension(path);
    if (ext == "bmp")
        open(AppKind::Paint, path);
    else
        open(AppKind::Notepad, path);
}

static void placeDialog(Shell& s, AppWindow* w) {
    Rect wa = s.workArea();
    w->frame = {wa.x + (wa.w - w->prefW) * 0.5f, wa.y + (wa.h - w->prefH) * 0.4f, w->prefW, w->prefH};
    w->restore = w->frame;
}

void Shell::prompt(const std::string& title, const std::string& message, const std::string& initial,
                   std::function<void(const std::string&)> ok) {
    auto w = makePrompt(*this, title, message, initial, std::move(ok));
    w->shell = this;
    w->kind = AppKind::Dialog;
    w->icon = IcDialog;
    placeDialog(*this, w.get());
    AppWindow* raw = w.get();
    windows_.push_back(std::move(w));
    focus(raw);
    if (!hardwareKb_) kbOpen_ = true;
}

void Shell::message(const std::string& title, const std::string& text) {
    auto w = makeMessage(*this, title, text);
    w->shell = this;
    w->kind = AppKind::Dialog;
    w->icon = IcDialog;
    placeDialog(*this, w.get());
    AppWindow* raw = w.get();
    windows_.push_back(std::move(w));
    focus(raw);
}

void Shell::focus(AppWindow* w) {
    if (!w) {
        focused_ = nullptr;
        return;
    }
    for (size_t i = 0; i < windows_.size(); i++) {
        if (windows_[i].get() == w) {
            auto p = std::move(windows_[i]);
            windows_.erase(windows_.begin() + (long)i);
            windows_.push_back(std::move(p));
            break;
        }
    }
    w->minimized = false;
    w->minimizing = false;
    focused_ = w;
    if (kbOpen_ && !w->wantsText()) kbOpen_ = false;
}

static AppWindow* topVisible(std::vector<std::unique_ptr<AppWindow>>& ws, AppWindow* except) {
    for (auto it = ws.rbegin(); it != ws.rend(); ++it) {
        AppWindow* w = it->get();
        if (w != except && !w->minimized && !w->minimizing && !w->closing) return w;
    }
    return nullptr;
}

void Shell::close(AppWindow* w) {
    if (!w || w->closing) return;
    w->closing = true;
    if (focused_ == w) focused_ = topVisible(windows_, w);
}

void Shell::minimize(AppWindow* w) {
    if (!w) return;
    w->minimizing = true;
    if (focused_ == w) focused_ = topVisible(windows_, w);
}

void Shell::toggleMaximize(AppWindow* w) {
    if (!w->resizable) return;
    if (w->maximized) {
        w->maximized = false;
        w->frame = w->restore;
    } else {
        if (!w->snapped) w->restore = w->frame;
        w->snapped = false;
        w->maximized = true;
        w->frame = workArea();
    }
}

void Shell::restart() {
    for (auto& w : windows_) {
        w->closing = true;
        w->anim = 0;
    }
    focused_ = nullptr;
    closeFlyouts();
    ctxOpen_ = false;
    kbOpen_ = false;
    state_ = State::Restarting;
    stateT_ = time_;
}

void Shell::shutdown() {
    closeFlyouts();
    ctxOpen_ = false;
    kbOpen_ = false;
    state_ = State::Shutdown;
    stateT_ = time_;
}

void Shell::lock() {
    closeFlyouts();
    ctxOpen_ = false;
    kbOpen_ = false;
    state_ = State::Lock;
    stateT_ = time_;
    lockOffset_ = 0;
    unlocking_ = false;
}

std::vector<AppWindow*> Shell::windowsOf(AppKind k) {
    std::vector<AppWindow*> out;
    for (auto& w : windows_)
        if (w->kind == k && !w->closing) out.push_back(w.get());
    return out;
}

void Shell::processInput() {
    ui_.pressed = ui_.released = ui_.longPress = false;
    ui_.requestKeyboard = false;
    ui_.chars.clear();
    ui_.keys.clear();
    ui_.px = ui_.x;
    ui_.py = ui_.y;
    bool transition = false;
    while (!queue_.empty()) {
        Ev e = queue_.front();
        if (e.type == 3) {
            ui_.chars.push_back(e.cp);
            queue_.pop_front();
            continue;
        }
        if (e.type == 4) {
            ui_.keys.push_back(e.key);
            queue_.pop_front();
            continue;
        }
        if (e.type == 1) {
            ui_.x = e.x;
            ui_.y = e.y;
            if (ui_.down && !ui_.dragged && std::hypot(ui_.x - ui_.downX, ui_.y - ui_.downY) > 9) ui_.dragged = true;
            queue_.pop_front();
            continue;
        }
        if (transition) break;
        if (e.type == 0) {
            ui_.down = true;
            ui_.pressed = true;
            ui_.x = ui_.px = ui_.downX = e.x;
            ui_.y = ui_.py = ui_.downY = e.y;
            ui_.dragged = false;
            ui_.longFired = false;
            downT_ = time_;
        } else if (e.type == 2) {
            ui_.x = e.x;
            ui_.y = e.y;
            if (ui_.down && !ui_.dragged && std::hypot(ui_.x - ui_.downX, ui_.y - ui_.downY) > 9) ui_.dragged = true;
            ui_.down = false;
            ui_.released = true;
            if (!ui_.dragged && !ui_.longFired) {
                if (time_ - lastTapT_ < 0.42 && std::hypot(ui_.x - lastTapX_, ui_.y - lastTapY_) < 26)
                    tapCount_++;
                else
                    tapCount_ = 1;
                lastTapT_ = time_;
                lastTapX_ = ui_.x;
                lastTapY_ = ui_.y;
                ui_.taps = tapCount_;
            } else {
                tapCount_ = 0;
                ui_.taps = 1;
            }
        }
        queue_.pop_front();
        transition = true;
    }
    if (ui_.down && !ui_.dragged && !ui_.longFired && time_ - downT_ > 0.55) {
        ui_.longPress = true;
        ui_.longFired = true;
    }
    if (!ui_.keys.empty() || !ui_.chars.empty()) {
        for (int k : ui_.keys)
            if (k == KeyEscape) back();
    }
}

Shell::Owner Shell::hitTest(float x, float y, AppWindow** win) {
    *win = nullptr;
    if (kbOpen_ && keyboardRect().contains(x, y)) return Owner::Keyboard;
    if (ctxOpen_) {
        if (contextRect().contains(x, y)) return Owner::Context;
        ctxOpen_ = false;
        return Owner::None;
    }
    Rect tb = taskbarRect();
    if (startOpen_) {
        if (startRect().contains(x, y)) return Owner::Start;
        if (!tb.contains(x, y)) {
            closeFlyouts();
            return Owner::None;
        }
    }
    if (quickOpen_) {
        if (quickRect().contains(x, y)) return Owner::Quick;
        if (!tb.contains(x, y)) {
            closeFlyouts();
            return Owner::None;
        }
    }
    if (calOpen_) {
        if (calendarRect().contains(x, y)) return Owner::Calendar;
        if (!tb.contains(x, y)) {
            closeFlyouts();
            return Owner::None;
        }
    }
    if (tb.contains(x, y)) return Owner::Taskbar;
    for (auto it = windows_.rbegin(); it != windows_.rend(); ++it) {
        AppWindow* w = it->get();
        if (w->minimized || w->minimizing || w->closing) continue;
        if (w->frame.contains(x, y)) {
            *win = w;
            return Owner::Window;
        }
    }
    return Owner::Desktop;
}

void Shell::frame(double t) {
    if (lastT_ < 0) lastT_ = t;
    float dt = (float)clampf((float)(t - lastT_), 0, 0.1f);
    lastT_ = t;
    time_ = t;
    if (stateT_ < 0) stateT_ = t;
    ui_.time = t;
    ui_.dt = dt;
    processInput();

    if (time_ - batteryT_ > 30) {
        batteryT_ = time_;
        battery_ = plat_.batteryLevel ? plat_.batteryLevel() : 1.0f;
    }

    if (ui_.pressed && state_ == State::Desktop) {
        AppWindow* w = nullptr;
        cap_ = hitTest(ui_.x, ui_.y, &w);
        capWin_ = w;
        capPart_ = Part::None;
        if (cap_ != Owner::Keyboard && cap_ != Owner::Taskbar && cap_ != Owner::Start && cap_ != Owner::Quick &&
            cap_ != Owner::Calendar && cap_ != Owner::Context)
            ctxOpen_ = false;
        if (cap_ == Owner::Window && w) {
            focus(w);
            Rect f = w->frame;
            Rect btns{f.r() - 138, f.y, 138, w->titleH};
            Rect title{f.x, f.y, f.w, w->titleH};
            Rect corner{f.r() - 22, f.b() - 22, 22, 22};
            if (btns.contains(ui_.x, ui_.y))
                capPart_ = Part::Buttons;
            else if (w->customTitle() && title.contains(ui_.x, ui_.y) &&
                     w->titleHit({f.x, f.y, f.w - 138, w->titleH}, ui_.x, ui_.y))
                capPart_ = Part::TitleApp;
            else if (title.contains(ui_.x, ui_.y))
                capPart_ = Part::Title;
            else if (w->resizable && !w->maximized && corner.contains(ui_.x, ui_.y))
                capPart_ = Part::Resize;
            else
                capPart_ = Part::Client;
            grabX_ = ui_.x - f.x;
            grabY_ = ui_.y - f.y;
            grabFrame_ = f;
        }
        if (cap_ == Owner::Desktop) {
            focused_ = nullptr;
            if (kbOpen_) kbOpen_ = false;
        }
    }

    for (auto& w : windows_) w->drawn = false;
    gfx_.begin({0, 0, 0, 1});
    switch (state_) {
        case State::Boot: drawBoot(); break;
        case State::Lock: drawLock(); break;
        case State::SignIn: drawSignIn(true, false); break;
        case State::Welcome: drawSignIn(false, true); break;
        case State::Desktop:
            drawDesktop(true);
            deskFade_ = approach(deskFade_, 1.f, ui_.dt / 0.45f);
            if (deskFade_ < 1) gfx_.rect({0, 0, gfx_.width(), gfx_.height()}, rgb(0x000000, 1 - easeOut(deskFade_)));
            break;
        case State::Restarting: drawRestarting(); break;
        case State::Shutdown: drawShutdown(); break;
    }
    Rect full{0, 0, gfx_.width(), gfx_.height()};
    if (night_) gfx_.rect(full, rgb(0xFF8A1E, 0.14f));
    if (brightness_ < 1) gfx_.rect(full, rgb(0x000000, (1 - brightness_) * 0.7f));
    gfx_.end();

    for (auto& w : windows_)
        if (!w->drawn) {
            w->exclusive = false;
            w->hidden();
        }
    if (ui_.released) {
        cap_ = Owner::None;
        capWin_ = nullptr;
        capPart_ = Part::None;
    }
    for (size_t i = 0; i < windows_.size();) {
        AppWindow* w = windows_[i].get();
        if (w->closing && w->anim <= 0) {
            if (focused_ == w) focused_ = nullptr;
            windows_.erase(windows_.begin() + (long)i);
        } else {
            i++;
        }
    }
}

void Shell::drawSpinner(float cx, float cy, float r, Color c) {
    for (int i = 0; i < 5; i++) {
        float p = (float)std::fmod(time_ * 0.55 - i * 0.075, 1.0);
        if (p < 0) p += 1;
        float e = p < 0.5f ? 4 * p * p * p : 1 - std::pow(-2 * p + 2, 3.0f) / 2;
        float a = -kPi / 2 + e * 2 * kPi;
        gfx_.circle(cx + std::cos(a) * r, cy + std::sin(a) * r, 2.6f, c);
    }
}

void Shell::drawBoot() {
    float W = gfx_.width(), H = gfx_.height();
    double e = time_ - stateT_;
    gfx_.rect({0, 0, W, H}, rgb(0x000000));
    float a = clampf((float)(e - 0.25) / 0.6f, 0, 1);
    float out = clampf((float)(e - 2.9) / 0.35f, 0, 1);
    float ls = std::min(W, H) * 0.24f;
    gfx_.pushLayer(W / 2, H * 0.42f, 0.96f + 0.04f * easeOut(a), easeOut(a) * (1 - out));
    drawIcon(gfx_, IcLogo, {W / 2 - ls / 2, H * 0.42f - ls / 2, ls, ls}, {1, 1, 1, 1});
    gfx_.popLayer();
    if (e > 0.9 && out < 1) {
        gfx_.pushLayer(W / 2, H * 0.78f, 1, clampf((float)(e - 0.9) / 0.3f, 0, 1) * (1 - out));
        drawSpinner(W / 2, H * 0.78f, 14, rgb(0xFFFFFF));
        gfx_.popLayer();
    }
    if (e > 3.3) {
        state_ = State::Lock;
        lockOffset_ = 0;
        unlocking_ = false;
        stateT_ = time_;
    }
}

void Shell::drawRestarting() {
    float W = gfx_.width(), H = gfx_.height();
    double e = time_ - stateT_;
    gfx_.rect({0, 0, W, H}, rgb(0x0B1F33));
    float a = clampf((float)e / 0.3f, 0, 1) * (1 - clampf((float)(e - 1.9) / 0.3f, 0, 1));
    gfx_.pushLayer(W / 2, H / 2, 1, a);
    float tw = gfx_.textWidth(22, "Restarting");
    float x = W / 2 - (tw + 46) / 2;
    drawSpinner(x + 14, H / 2, 12, rgb(0xFFFFFF));
    gfx_.textIn({x + 46, H / 2 - 18, tw + 4, 36}, 22, "Restarting", rgb(0xFFFFFF));
    gfx_.popLayer();
    if (e > 2.3) {
        state_ = State::Boot;
        stateT_ = time_;
    }
}

void Shell::drawShutdown() {
    float W = gfx_.width(), H = gfx_.height();
    double e = time_ - stateT_;
    gfx_.rect({0, 0, W, H}, rgb(0x0B1F33));
    float tw = gfx_.textWidth(22, "Shutting down");
    float x = W / 2 - (tw + 46) / 2;
    drawSpinner(x + 14, H / 2, 12, rgb(0xFFFFFF));
    gfx_.textIn({x + 46, H / 2 - 18, tw + 4, 36}, 22, "Shutting down", rgb(0xFFFFFF));
    if (e > 1.8) {
        gfx_.rect({0, 0, W, H}, rgb(0x000000));
        if (plat_.quit) plat_.quit();
    }
}

void Shell::drawWallpaper(const Rect& r, int index) {
    bool dk = cfg_.dark;
    int wp = (((index >= 0 ? index : cfg_.wallpaper) % 5) + 5) % 5;
    if (wp == 4) {
        gfx_.rect(r, dk ? rgb(0x1A2633) : rgb(0x2D6A9F));
        return;
    }
    struct Pal {
        uint32_t top, bot, p0, p1, glow;
    };
    Pal light[4] = {{0xDCEBFA, 0xA8CBEF, 0x4EA4F4, 0x0A5CC5, 0xFFFFFF},
                    {0xE9E1F7, 0xC3B2E8, 0xA77BEB, 0x5A2BB2, 0xFFFFFF},
                    {0xFCE9D8, 0xF3C3A0, 0xFF9E5E, 0xD2452A, 0xFFF5E8},
                    {0xD7F2EE, 0x9FD9D1, 0x2FBFAE, 0x0A6E76, 0xFFFFFF}};
    Pal dark[4] = {{0x0B1730, 0x050B18, 0x2A7DE1, 0x0C2E6E, 0x4F8BFF},
                   {0x1A1030, 0x0A0618, 0x8754E0, 0x2E1466, 0xA27CFF},
                   {0x2A1208, 0x120604, 0xE0632E, 0x6B1D0E, 0xFF8A4A},
                   {0x061F22, 0x030D10, 0x1FA898, 0x0A4A50, 0x3FE0CC}};
    Pal p = dk ? dark[wp] : light[wp];
    gfx_.rectV(r, rgb(p.top), rgb(p.bot));
    float cx = r.x + r.w * 0.5f, cy = r.y + r.h * 0.56f;
    float s = std::min(r.w, r.h) * 0.5f;
    gfx_.shadow({cx - s * 0.6f, cy - s * 0.6f, s * 1.2f, s * 1.2f}, s * 0.6f, s * 0.9f, rgb(p.glow, dk ? 0.35f : 0.55f));
    for (int i = 0; i < 6; i++) {
        float a = -kPi / 2 + (i - 2.5f) * 0.42f;
        float len = s * (0.95f - std::fabs(i - 2.5f) * 0.08f);
        float px = cx + std::cos(a) * len * 0.42f, py = cy + std::sin(a) * len * 0.42f;
        Color c0 = rgb(p.p0, 0.92f), c1 = rgb(p.p1, 0.95f);
        gfx_.rotRect(px, py, len * 0.48f, s * 0.15f, a, s * 0.15f, mix(c0, c1, i / 6.0f), c1);
    }
    gfx_.rotRect(cx, cy + s * 0.02f, s * 0.35f, s * 0.16f, 0, s * 0.16f, rgb(p.p1, 0.95f), rgb(p.p1, 0.95f));
    gfx_.shadow({cx - s * 0.3f, cy - s * 0.25f, s * 0.6f, s * 0.3f}, s * 0.15f, s * 0.25f, rgb(p.glow, 0.25f));
}

void Shell::drawLock() {
    float W = gfx_.width(), H = gfx_.height();
    drawSignIn(false, false);
    Ui& ui = ui_;
    ui.active = true;
    Rect full{0, 0, W, H};
    if (!unlocking_) {
        if (ui.down && ui.dragged) lockOffset_ = std::min(0.f, ui.y - ui.downY);
        if (ui.released) {
            if (!ui.dragged || lockOffset_ < -H * 0.16f)
                unlocking_ = true;
            else
                lockOffset_ = 0;
        }
        if (!ui.keys.empty() || !ui.chars.empty()) unlocking_ = true;
    } else {
        lockOffset_ -= ui.dt * H * 4.0f;
        if (lockOffset_ <= -H) {
            state_ = State::SignIn;
            stateT_ = time_;
            unlocking_ = false;
            lockOffset_ = 0;
            signPower_ = false;
            ui.active = false;
            return;
        }
    }
    gfx_.pushLayer(0, 0, 1, 1, 0, lockOffset_);
    drawWallpaper(full);
    gfx_.rectV({0, 0, W, H * 0.5f}, rgb(0x000000, 0.28f), rgb(0x000000, 0.0f));
    gfx_.rect(full, rgb(0x000000, 0.08f));
    float big = std::min(110.f, H * 0.2f);
    std::string ts = timeString();
    float tw = gfx_.textWidth(big, ts);
    gfx_.textBold(W / 2 - tw / 2, H * 0.08f, big, ts, rgb(0xFFFFFF));
    time_t tt = time(nullptr);
    struct tm lt;
    localtime_r(&tt, &lt);
    char buf[64];
    strftime(buf, sizeof buf, "%A, %B %d", &lt);
    gfx_.textIn({0, H * 0.08f + big * 1.12f, W, 30}, 20, buf, rgb(0xFFFFFF), AlignCenter);

    Rect card{24 + insL_, H - 92, 220, 64};
    gfx_.rect(card, rgb(0x000000, 0.28f), 8);
    drawIcon(gfx_, IcGlyphBattery, {card.x + 16, card.cy() - 10, 26, 20}, rgb(0xFFFFFF), battery_);
    gfx_.textBold(card.x + 56, card.y + 12, 15, std::to_string((int)std::lround(battery_ * 100)) + "%", rgb(0xFFFFFF));
    gfx_.text(card.x + 56, card.y + 34, 11.5f, wifi_ ? "Wi-Fi connected" : "Not connected", rgb(0xFFFFFF, 0.8f));

    float bob = (float)std::sin(time_ * 2.6) * 3;
    gfx_.line(W / 2 - 9, H - 28 + bob, W / 2, H - 36 + bob, 1.6f, rgb(0xFFFFFF, 0.8f));
    gfx_.line(W / 2, H - 36 + bob, W / 2 + 9, H - 28 + bob, 1.6f, rgb(0xFFFFFF, 0.8f));
    drawIcon(gfx_, IcGlyphWifi, {W - 76 - insR_, H - 42, 18, 18}, rgb(0xFFFFFF));
    drawIcon(gfx_, IcGlyphBattery, {W - 48 - insR_, H - 42, 22, 18}, rgb(0xFFFFFF), battery_);
    gfx_.popLayer();
    float fadeIn = clampf((float)(time_ - stateT_) / 0.5f, 0, 1);
    if (fadeIn < 1) gfx_.rect(full, rgb(0x000000, 1 - fadeIn));
    ui.active = false;
}

void Shell::drawSignIn(bool interactive, bool welcome) {
    float W = gfx_.width(), H = gfx_.height();
    Rect full{0, 0, W, H};
    Ui& ui = ui_;
    drawWallpaper(full);
    gfx_.rectV(full, rgb(cfg_.dark ? 0x0E1622 : 0x2A4562, 0.82f), rgb(cfg_.dark ? 0x05080E : 0x162638, 0.88f));
    gfx_.shadow({W * 0.25f, H * 0.12f, W * 0.5f, H * 0.55f}, H * 0.3f, H * 0.4f, rgb(0x6FA8E8, 0.10f));
    float r = std::min(H * 0.17f, 86.f);
    float cy = H * 0.36f;
    float appear = welcome ? 1.f : easeOut(clampf((float)(time_ - stateT_) / 0.35f, 0, 1));
    gfx_.pushLayer(W / 2, cy, 0.94f + 0.06f * appear, appear);
    drawIcon(gfx_, IcUser, {W / 2 - r, cy - r, r * 2, r * 2}, rgb(0xFFFFFF));
    float ny = cy + r + 14;
    float nw = gfx_.textWidth(26, cfg_.user);
    gfx_.textBold(W / 2 - nw / 2, ny, 26, cfg_.user, rgb(0xFFFFFF));
    ui.active = interactive;
    if (welcome) {
        float tw = gfx_.textWidth(18, "Welcome");
        float x = W / 2 - (tw + 40) / 2;
        drawSpinner(x + 12, ny + 64, 10, rgb(0xFFFFFF));
        gfx_.textIn({x + 40, ny + 50, tw + 4, 28}, 18, "Welcome", rgb(0xFFFFFF));
        if (time_ - stateT_ > 1.3) {
            state_ = State::Desktop;
            stateT_ = time_;
            deskFade_ = 0;
        }
    } else {
        Rect btn{W / 2 - 110, ny + 52, 220, 38};
        Color bg = ui.held(btn) ? rgb(0xFFFFFF, 0.32f) : rgb(0xFFFFFF, 0.2f);
        gfx_.rect(btn, bg, 5);
        gfx_.stroke(btn, rgb(0xFFFFFF, 0.3f), 5, 1);
        gfx_.textIn(btn, 14, "Sign in", rgb(0xFFFFFF), AlignCenter);
        bool go = ui.clicked(btn);
        if (interactive)
            for (int k : ui.keys)
                if (k == KeyEnter) go = true;
        if (go) {
            state_ = State::Welcome;
            stateT_ = time_;
            signPower_ = false;
        }
    }
    gfx_.popLayer();

    Rect tile{20 + insL_, H - 64, 200, 48};
    gfx_.rect(tile, rgb(0xFFFFFF, 0.12f), 5);
    drawIcon(gfx_, IcUser, {tile.x + 8, tile.y + 8, 32, 32}, rgb(0xFFFFFF));
    gfx_.textIn({tile.x + 50, tile.y, tile.w - 56, tile.h}, 13, cfg_.user, rgb(0xFFFFFF));

    Rect wifiR{W - 112 - insR_, H - 56, 40, 40};
    Rect pwr{W - 64 - insR_, H - 56, 40, 40};
    drawIcon(gfx_, IcGlyphWifi, wifiR.centered(20, 20), rgb(0xFFFFFF));
    if (ui.held(pwr) || signPower_) gfx_.rect(pwr, rgb(0xFFFFFF, 0.18f), 5);
    drawIcon(gfx_, IcGlyphPower, pwr.centered(20, 20), rgb(0xFFFFFF));
    if (interactive && !welcome) {
        if (ui.clicked(pwr)) signPower_ = !signPower_;
        if (signPower_) {
            Rect m{pwr.r() - 170, pwr.y - 8 - 2 * 38 - 8, 170, 2 * 38 + 8};
            gfx_.shadow(m, 8, 16, rgb(0x000000, 0.5f));
            gfx_.rect(m, rgb(0x2C2C2C, 0.97f), 8);
            const char* labels[] = {"Shut down", "Restart"};
            IconId icons[] = {IcGlyphPower, IcGlyphRestart};
            for (int i = 0; i < 2; i++) {
                Rect ir{m.x + 4, m.y + 4 + i * 38, m.w - 8, 36};
                if (ui.held(ir)) gfx_.rect(ir, rgb(0xFFFFFF, 0.1f), 4);
                drawIcon(gfx_, icons[i], {ir.x + 12, ir.cy() - 8, 16, 16}, rgb(0xFFFFFF));
                gfx_.textIn({ir.x + 40, ir.y, ir.w - 40, ir.h}, 13, labels[i], rgb(0xFFFFFF));
                if (ui.clicked(ir)) {
                    signPower_ = false;
                    if (i == 0) shutdown();
                    if (i == 1) restart();
                }
            }
        }
        if (time_ - stateT_ > 30 && !ui.down) {
            state_ = State::Lock;
            stateT_ = time_ - 1;
            lockOffset_ = 0;
            unlocking_ = false;
        }
    }
    ui.active = false;
}

void Shell::refreshDesktop() {
    if (deskVersion_ == fsVersion_) return;
    deskVersion_ = fsVersion_;
    desk_.clear();
    desk_.push_back({"This PC", "", IcPC, 1});
    desk_.push_back({"Recycle Bin", recycleDir(), IcBin, 2});
    std::string dd = fs::join(userDir(), "Desktop");
    for (auto& e : fs::list(dd)) {
        IconId ic = e.dir ? IcFolder : (fs::extension(e.name) == "bmp" ? IcImage : IcFile);
        desk_.push_back({e.name, fs::join(dd, e.name), ic, 0});
    }
    if (deskSel_ >= (int)desk_.size()) deskSel_ = -1;
}

void Shell::drawDesktop(bool interactive) {
    float W = gfx_.width(), H = gfx_.height();
    deskInteractive_ = interactive && state_ == State::Desktop;
    drawWallpaper({0, 0, W, H});
    drawDesktopIcons(interactive && cap_ == Owner::Desktop);
    std::vector<AppWindow*> order;
    for (auto& w : windows_) order.push_back(w.get());
    for (AppWindow* w : order) drawWindow(w, interactive && cap_ == Owner::Window && capWin_ == w);
    drawTaskbar(interactive && cap_ == Owner::Taskbar);
    if (!interactive) return;
    startAnim_ = approach(startAnim_, startOpen_ ? 1.f : 0.f, ui_.dt / 0.16f);
    quickAnim_ = approach(quickAnim_, quickOpen_ ? 1.f : 0.f, ui_.dt / 0.14f);
    calAnim_ = approach(calAnim_, calOpen_ ? 1.f : 0.f, ui_.dt / 0.14f);
    if (startAnim_ > 0) drawStart(cap_ == Owner::Start);
    if (quickAnim_ > 0) drawQuick(cap_ == Owner::Quick);
    if (calAnim_ > 0) drawCalendar(cap_ == Owner::Calendar);
    if (ctxOpen_) drawContext(cap_ == Owner::Context);
    if (kbOpen_) {
        if (focused_ && !startOpen_) ensureVisible(focused_);
        ui_.active = cap_ == Owner::Keyboard;
        bool hide = false;
        kb_.draw(
            ui_, keyboardRect(), [this](uint32_t c) { character(c); }, [this](int k) { key(k); }, hide);
        ui_.active = false;
        if (hide) kbOpen_ = false;
    }
    if (ui_.requestKeyboard && !hardwareKb_) kbOpen_ = true;
}

void Shell::ensureVisible(AppWindow* w) {
    if (w->maximized) return;
    Rect wa = workArea();
    if (w->frame.h > wa.h) w->frame.h = std::max(w->minH, wa.h);
    if (w->frame.b() > wa.b()) w->frame.y = std::max(wa.y, wa.b() - w->frame.h);
}

void Shell::drawDesktopIcons(bool cap) {
    refreshDesktop();
    Rect wa = workArea();
    float cw = 80, ch = 86;
    float x = 6 + wa.x, y = 6 + wa.y;
    bool anyHit = false;
    Ui& ui = ui_;
    ui.active = cap;
    for (size_t i = 0; i < desk_.size(); i++) {
        if (y + ch > wa.b()) {
            y = 6 + wa.y;
            x += cw + 4;
        }
        Rect cell{x, y, cw, ch};
        y += ch + 2;
        DeskItem& d = desk_[i];
        bool sel = (int)i == deskSel_;
        if (sel) {
            gfx_.rect(cell, rgb(0xFFFFFF, 0.22f), 4);
            gfx_.stroke(cell, rgb(0xFFFFFF, 0.35f), 4, 1);
        } else if (ui.held(cell)) {
            gfx_.rect(cell, rgb(0xFFFFFF, 0.14f), 4);
        }
        drawIcon(gfx_, d.icon, {cell.cx() - 22, cell.y + 6, 44, 44}, rgb(0xFFFFFF));
        std::string label = gfx_.ellipsize(11.5f, d.name, cw - 6);
        Rect lr{cell.x, cell.y + 54, cell.w, 22};
        gfx_.textIn({lr.x + 1, lr.y + 1, lr.w, lr.h}, 11.5f, label, rgb(0x000000, 0.65f), AlignCenter);
        gfx_.textIn(lr, 11.5f, label, rgb(0xFFFFFF), AlignCenter);
        if (ui.clicked(cell)) {
            anyHit = true;
            deskSel_ = (int)i;
            if (ui.taps >= 2) {
                if (d.special == 1)
                    open(AppKind::Explorer, "pc");
                else
                    openFile(d.path);
            }
        }
        if (ui.longPressed(cell)) {
            anyHit = true;
            deskSel_ = (int)i;
            std::vector<MenuItem> items;
            DeskItem item = d;
            items.push_back({"Open", IcGlyphOpen, [this, item]() {
                                 if (item.special == 1)
                                     open(AppKind::Explorer, "pc");
                                 else
                                     openFile(item.path);
                             }});
            if (item.special == 0) {
                items.push_back({"Rename", IcGlyphRename, [this, item]() {
                                     prompt("Rename", "Enter a new name:", item.name, [this, item](const std::string& n) {
                                         if (n.empty() || n == item.name) return;
                                         fs::renamePath(item.path, fs::join(fs::parent(item.path), n));
                                         notifyFs();
                                     });
                                 }});
                items.push_back({"Delete", IcGlyphTrash, [this, item]() { recycle(item.path); }});
            }
            if (item.special == 1)
                items.push_back({"Properties", IcGlyphGear, [this]() { open(AppKind::Settings, "about"); }});
            if (item.special == 2)
                items.push_back({"Empty Recycle Bin", IcGlyphTrash, [this]() {
                                     for (auto& e : fs::list(recycleDir())) fs::removeAll(fs::join(recycleDir(), e.name));
                                     notifyFs();
                                 }});
            openContext(ui.x, ui.y, items);
        }
    }
    if (cap && !anyHit) {
        Rect all{0, 0, wa.w, wa.h};
        if (ui.clicked(all)) deskSel_ = -1;
        if (ui.longPressed(all)) {
            deskSel_ = -1;
            std::string dd = fs::join(userDir(), "Desktop");
            std::vector<MenuItem> items;
            items.push_back({"Refresh", IcGlyphRefresh, [this]() { notifyFs(); }});
            items.push_back({"New folder", IcGlyphNewFolder, [this, dd]() {
                                 fs::makeDirs(fs::join(dd, fs::uniqueName(dd, "New folder", "")));
                                 notifyFs();
                             }});
            items.push_back({"New text document", IcGlyphNew, [this, dd]() {
                                 fs::writeText(fs::join(dd, fs::uniqueName(dd, "New Text Document", "txt")), "");
                                 notifyFs();
                             }});
            items.push_back({"Open in Terminal", IcTerminal, [this, dd]() { open(AppKind::Terminal, dd); }});
            items.push_back({"Display settings", IcGlyphGear, [this]() { open(AppKind::Settings, "system"); }});
            items.push_back({"Personalize", IcGlyphBrush, [this]() { open(AppKind::Settings, "personalization"); }});
            openContext(ui.x, ui.y, items);
        }
    }
    ui.active = false;
}

void Shell::drawWindow(AppWindow* w, bool cap) {
    w->exclusive = false;
    float target = (w->closing || w->minimizing) ? 0.f : 1.f;
    w->anim = approach(w->anim, target, ui_.dt / 0.16f);
    if (w->minimizing && w->anim <= 0) {
        w->minimizing = false;
        w->minimized = true;
    }
    if (w->minimized) return;
    if (w->maximized) w->frame = workArea();
    bool interactive = cap && w->anim >= 1;
    Rect wa = workArea();
    Ui& ui = ui_;
    ui.active = interactive;

    if (interactive && capPart_ == Part::Title && ui.down && ui.dragged) {
        if (w->maximized || w->snapped) {
            float rel = grabX_ / std::max(1.f, w->frame.w);
            w->maximized = false;
            w->snapped = false;
            w->frame.w = w->restore.w;
            w->frame.h = w->restore.h;
            grabX_ = rel * w->frame.w;
            grabY_ = std::min(grabY_, w->titleH * 0.5f);
        }
        w->frame.x = ui.x - grabX_;
        w->frame.y = clampf(ui.y - grabY_, wa.y, wa.b() - w->titleH);
    }
    if (interactive && capPart_ == Part::Title && ui.released) {
        if (ui.dragged && w->resizable) {
            if (ui.y <= wa.y + 6) {
                w->restore = w->frame;
                w->maximized = true;
            } else if (ui.x <= 6) {
                w->restore = w->frame;
                w->snapped = true;
                w->frame = {wa.x, wa.y, wa.w * 0.5f, wa.h};
            } else if (ui.x >= wa.r() - 6) {
                w->restore = w->frame;
                w->snapped = true;
                w->frame = {wa.x + wa.w * 0.5f, wa.y, wa.w * 0.5f, wa.h};
            }
        } else if (!ui.dragged && ui.taps >= 2) {
            toggleMaximize(w);
        }
    }
    if (interactive && capPart_ == Part::Resize && ui.down) {
        w->frame.w = clampf(grabFrame_.w + ui.x - ui.downX, w->minW, wa.r() - w->frame.x);
        w->frame.h = clampf(grabFrame_.h + ui.y - ui.downY, w->minH, wa.b() - w->frame.y);
        w->snapped = false;
    }
    if (w->maximized) w->frame = workArea();

    Rect f = w->frame;
    float a = easeOut(w->anim);
    float sc = 0.93f + 0.07f * a;
    float ty = w->minimizing ? (1 - a) * 90 : 0;
    gfx_.pushLayer(f.cx(), f.cy(), sc, a, 0, ty);
    bool foc = w == focused_;
    float rad = (w->maximized || w->snapped) ? 0 : 8;
    if (!w->maximized)
        gfx_.shadow({f.x, f.y + (foc ? 10.f : 5.f), f.w, f.h}, rad, foc ? 26.f : 14.f,
                    alpha(theme_.shadow, foc ? 1.0f : 0.55f));
    float th = w->titleH;
    Color titleBg = w->customTitle() ? w->titleColor(theme_, foc)
                                     : (foc ? theme_.mica : (theme_.dark ? rgb(0x272727) : rgb(0xF9F9F9)));
    gfx_.rect(f, titleBg, rad);

    Color tc = foc ? theme_.text : theme_.textDim;
    if (w->darkChrome) tc = foc ? rgb(0xFFFFFF) : rgb(0xA0A0A0);
    if (w->customTitle()) {
        ui.active = interactive && capPart_ == Part::TitleApp;
        gfx_.pushClip({f.x, f.y, f.w - 138, th});
        w->drawTitle(ui, {f.x, f.y, f.w - 138, th}, foc);
        gfx_.popClip();
    } else {
        drawIcon(gfx_, w->icon, {f.x + 12, f.y + th / 2 - 8, 16, 16}, theme_.text);
        gfx_.textIn({f.x + 38, f.y, f.w - 38 - 140, th}, 12, gfx_.ellipsize(12, w->title, f.w - 190), tc);
    }

    ui.active = interactive && capPart_ == Part::Buttons;
    Rect bClose{f.r() - 46, f.y, 46, th};
    Rect bMax{f.r() - 92, f.y, 46, th};
    Rect bMin{f.r() - 138, f.y, 46, th};
    Color pressC = w->darkChrome ? rgb(0xFFFFFF, 0.1f) : theme_.press;
    if (ui.held(bClose)) gfx_.rect(bClose, rgb(0xC42B1C));
    if (ui.held(bMax)) gfx_.rect(bMax, pressC);
    if (ui.held(bMin)) gfx_.rect(bMin, pressC);
    Color gc = ui.held(bClose) ? rgb(0xFFFFFF) : tc;
    gfx_.line(bClose.cx() - 5, bClose.cy() - 5, bClose.cx() + 5, bClose.cy() + 5, 1.1f, gc);
    gfx_.line(bClose.cx() + 5, bClose.cy() - 5, bClose.cx() - 5, bClose.cy() + 5, 1.1f, gc);
    if (w->resizable) {
        if (w->maximized) {
            gfx_.stroke({bMax.cx() - 5, bMax.cy() - 3, 8, 8}, tc, 1.5f, 1);
            gfx_.line(bMax.cx() - 3, bMax.cy() - 5, bMax.cx() + 5, bMax.cy() - 5, 1, tc);
            gfx_.line(bMax.cx() + 5, bMax.cy() - 5, bMax.cx() + 5, bMax.cy() + 3, 1, tc);
        } else {
            gfx_.stroke({bMax.cx() - 5, bMax.cy() - 5, 10, 10}, tc, 1.5f, 1);
        }
    }
    gfx_.line(bMin.cx() - 5, bMin.cy(), bMin.cx() + 5, bMin.cy(), 1, tc);
    if (ui.clicked(bClose)) close(w);
    if (ui.clicked(bMax) && w->resizable) toggleMaximize(w);
    if (ui.clicked(bMin)) minimize(w);

    Rect client{f.x, f.y + th, f.w, f.h - th};
    bool manipulating = cap && ui_.down && capPart_ == Part::Resize;
    w->exclusive = deskInteractive_ && w == focused_ && w->anim >= 1 && !w->closing && !manipulating && !startOpen_ &&
                   !quickOpen_ && !calOpen_ && !ctxOpen_ && startAnim_ <= 0 && quickAnim_ <= 0 && calAnim_ <= 0 &&
                   topVisible(windows_, nullptr) == w;
    w->drawn = true;
    gfx_.pushClip(client);
    ui.active = interactive && capPart_ == Part::Client;
    ui.focus = foc && !startOpen_;
    w->draw(ui, client);
    ui.focus = false;
    gfx_.popClip();
    if (w->resizable && !w->maximized) {
        Color rc = alpha(theme_.textDim, 0.6f);
        gfx_.line(f.r() - 4, f.b() - 12, f.r() - 12, f.b() - 4, 1, rc);
        gfx_.line(f.r() - 4, f.b() - 8, f.r() - 8, f.b() - 4, 1, rc);
    }
    gfx_.stroke(f, alpha(theme_.text, theme_.dark ? 0.14f : 0.12f), rad, 1);
    gfx_.popLayer();
    ui.active = false;
}

void Shell::taskbarActivate(AppKind k) {
    auto ws = windowsOf(k);
    closeFlyouts();
    if (ws.empty()) {
        open(k);
        return;
    }
    AppWindow* top = nullptr;
    for (auto it = windows_.rbegin(); it != windows_.rend(); ++it)
        if ((*it)->kind == k && !(*it)->closing) {
            top = it->get();
            break;
        }
    if (top == focused_ && !top->minimized && !top->minimizing)
        minimize(top);
    else
        focus(top);
}

void Shell::drawTaskbar(bool cap) {
    Rect tb = taskbarRect();
    Ui& ui = ui_;
    ui.active = cap;
    gfx_.rect(tb, theme_.taskbar);
    gfx_.rect({tb.x, tb.y, tb.w, 1}, alpha(theme_.text, theme_.dark ? 0.08f : 0.06f));

    struct Item {
        int kind;
        AppKind app;
        IconId icon;
    };
    std::vector<Item> items;
    items.push_back({0, AppKind::Explorer, IcLogo});
    items.push_back({1, AppKind::Explorer, IcGlyphSearch});
    for (auto& a : appCatalog())
        if (a.pinned) items.push_back({2, a.kind, a.icon});
    for (auto& a : appCatalog())
        if (!a.pinned && !windowsOf(a.kind).empty()) items.push_back({2, a.kind, a.icon});
    float slot = 44;
    float total = items.size() * slot - 4;
    float x0 = cfg_.taskbarLeft ? 10 + insL_ : (tb.w - total) * 0.5f;
    for (size_t i = 0; i < items.size(); i++) {
        Item& it = items[i];
        Rect r{x0 + i * slot, tb.y + 4, 40, 40};
        auto ws = it.kind == 2 ? windowsOf(it.app) : std::vector<AppWindow*>();
        bool isFocused = false;
        for (auto* w : ws)
            if (w == focused_) isFocused = true;
        bool on = (it.kind == 0 && startOpen_) || isFocused;
        if (on) {
            gfx_.rect(r, theme_.dark ? rgb(0xFFFFFF, 0.08f) : rgb(0xFFFFFF, 0.7f), 5);
            gfx_.stroke(r, theme_.dark ? rgb(0xFFFFFF, 0.06f) : rgb(0x000000, 0.06f), 5, 1);
        }
        if (ui.held(r)) gfx_.rect(r, theme_.press, 5);
        float press = ui.held(r) ? 0.88f : 1.0f;
        float is = 24 * press;
        Color glyph = theme_.text;
        drawIcon(gfx_, it.icon, {r.cx() - is / 2, r.cy() - is / 2, is, is}, glyph);
        if (!ws.empty()) {
            bool anyVisible = false;
            for (auto* w : ws)
                if (!w->minimized) anyVisible = true;
            float iw = isFocused ? 16 : 6;
            Color ic = isFocused ? theme_.accent : (anyVisible ? theme_.textDim : alpha(theme_.textDim, 0.7f));
            gfx_.rect({r.cx() - iw / 2, tb.b() - 6, iw, 3}, ic, 1.5f);
        }
        if (ui.clicked(r)) {
            if (it.kind == 0) {
                bool o = !startOpen_;
                closeFlyouts();
                startOpen_ = o;
                if (o) {
                    search_.set("");
                    allApps_ = false;
                    startScroll_ = 0;
                    collectRecent();
                }
            } else if (it.kind == 1) {
                closeFlyouts();
                startOpen_ = true;
                search_.set("");
                allApps_ = false;
                collectRecent();
                if (!hardwareKb_) kbOpen_ = true;
            } else {
                taskbarActivate(it.app);
            }
        }
        if (it.kind == 2 && ui.longPressed(r)) {
            AppKind k = it.app;
            std::vector<MenuItem> m;
            m.push_back({std::string(appInfo(k).name), appInfo(k).icon, [this, k]() { open(k); }});
            if (!ws.empty())
                m.push_back({ws.size() > 1 ? "Close all windows" : "Close window", IcGlyphClose, [this, k]() {
                                 for (auto* w : windowsOf(k)) close(w);
                             }});
            openContext(r.x, tb.y - (m.size() * 36.f + 16), m);
        }
    }

    float rx = tb.r() - 10 - insR_;
    Rect peek{tb.r() - 8 - insR_, tb.y, 8 + insR_, tb.h};
    gfx_.rect({peek.x, tb.y + 14, 1, tb.h - 28}, alpha(theme_.text, 0.2f));
    if (ui.clicked(peek)) {
        bool anyOpen = false;
        for (auto& w : windows_)
            if (!w->minimized && !w->closing) anyOpen = true;
        for (auto& w : windows_) {
            if (anyOpen) {
                if (!w->minimized) w->minimizing = true;
            } else {
                w->minimized = false;
            }
        }
        focused_ = anyOpen ? nullptr : topVisible(windows_, nullptr);
    }
    rx -= 4;
    std::string ts = timeString(), ds = dateString();
    float cw = std::max(gfx_.textWidth(12, ts), gfx_.textWidth(12, ds)) + 20;
    Rect clock{rx - cw, tb.y + 4, cw, 40};
    if (calOpen_) gfx_.rect(clock, theme_.hover, 5);
    if (ui.held(clock)) gfx_.rect(clock, theme_.press, 5);
    gfx_.textIn({clock.x, clock.y + 3, clock.w - 8, 17}, 12, ts, theme_.text, AlignRight);
    gfx_.textIn({clock.x, clock.y + 20, clock.w - 8, 17}, 12, ds, theme_.text, AlignRight);
    if (ui.clicked(clock)) {
        bool o = !calOpen_;
        closeFlyouts();
        calOpen_ = o;
        calMonth_ = 0;
    }
    rx = clock.x - 4;
    Rect tray{rx - 84, tb.y + 6, 84, 36};
    if (quickOpen_) gfx_.rect(tray, theme_.hover, 5);
    if (ui.held(tray)) gfx_.rect(tray, theme_.press, 5);
    drawIcon(gfx_, airplane_ ? IcGlyphPlane : IcGlyphWifi, {tray.x + 10, tray.cy() - 8, 16, 16},
             wifi_ || airplane_ ? theme_.text : theme_.textDim);
    drawIcon(gfx_, IcGlyphVolume, {tray.x + 34, tray.cy() - 8, 16, 16}, theme_.text);
    drawIcon(gfx_, IcGlyphBattery, {tray.x + 58, tray.cy() - 8, 18, 16}, theme_.text, battery_);
    if (ui.clicked(tray)) {
        bool o = !quickOpen_;
        closeFlyouts();
        quickOpen_ = o;
    }
    Rect kbBtn{tray.x - 38, tb.y + 6, 34, 36};
    if (kbOpen_) gfx_.rect(kbBtn, theme_.hover, 5);
    if (ui.held(kbBtn)) gfx_.rect(kbBtn, theme_.press, 5);
    drawIcon(gfx_, IcGlyphKeyboard, kbBtn.centered(18, 18), theme_.text);
    if (ui.clicked(kbBtn)) {
        kbOpen_ = !kbOpen_;
        hardwareKb_ = false;
    }
    ui.active = false;
}

void Shell::collectRecent() {
    recent_.clear();
    recentPaths_.clear();
    std::vector<std::pair<fs::Entry, std::string>> all;
    for (const char* d : {"Desktop", "Documents", "Downloads", "Pictures", "Music"}) {
        std::string dir = fs::join(userDir(), d);
        for (auto& e : fs::list(dir))
            if (!e.dir) all.push_back({e, fs::join(dir, e.name)});
    }
    std::sort(all.begin(), all.end(), [](const std::pair<fs::Entry, std::string>& a, const std::pair<fs::Entry, std::string>& b) {
        return a.first.mtime > b.first.mtime;
    });
    for (auto& p : all) {
        recent_.push_back(p.first);
        recentPaths_.push_back(p.second);
    }
}

void Shell::launchFromStart(AppKind k) {
    closeFlyouts();
    open(k);
}

static std::string relTime(time_t t) {
    double d = difftime(time(nullptr), t);
    if (d < 60) return "Just now";
    if (d < 3600) return std::to_string((int)(d / 60)) + " min ago";
    if (d < 86400) return std::to_string((int)(d / 3600)) + "h ago";
    return fs::formatTime(t).substr(0, 10);
}

void Shell::drawStart(bool cap) {
    Rect s = startRect();
    float a = easeOut(startAnim_);
    gfx_.pushLayer(s.cx(), s.b(), 1, a, 0, (1 - a) * 40);
    Ui& ui = ui_;
    ui.active = cap && startAnim_ >= 1;
    ui.focus = startOpen_;
    gfx_.shadow({s.x, s.y + 8, s.w, s.h}, 8, 28, theme_.shadow);
    gfx_.rect(s, theme_.flyout, 8);
    gfx_.stroke(s, alpha(theme_.text, 0.1f), 8, 1);

    float pad = s.w > 500 ? 28 : 16;
    Rect sb{s.x + pad, s.y + 20, s.w - pad * 2, 36};
    bool enter = ui.lineEdit(sb, search_, searchFocus_, "Search for apps, settings, and documents", 13, 24);
    drawIcon(gfx_, IcGlyphSearch, {sb.x + 12, sb.cy() - 7, 14, 14}, theme_.textDim);

    Rect bottom{s.x, s.b() - 62, s.w, 62};
    Rect body{s.x + pad, sb.b() + 16, s.w - pad * 2, bottom.y - sb.b() - 20};
    std::string q = toLower(search_.text);

    if (!q.empty()) {
        gfx_.textBold(body.x, body.y, 13, "Best match", theme_.text);
        float y = body.y + 26;
        struct Res {
            std::string name, sub, path;
            IconId icon;
            int app;
        };
        std::vector<Res> res;
        for (auto& ap : appCatalog())
            if (toLower(ap.name).find(q) != std::string::npos) res.push_back({ap.name, "App", "", ap.icon, (int)ap.kind});
        const char* settingsKeys[] = {"settings", "theme", "dark", "wallpaper", "display", "accent", "about", "personal"};
        for (auto* sk : settingsKeys)
            if (std::string(sk).find(q) != std::string::npos || q.find(sk) != std::string::npos) {
                res.push_back({"Personalization settings", "Settings", "personalization", IcSettings, -2});
                break;
            }
        for (size_t i = 0; i < recent_.size(); i++)
            if (toLower(recent_[i].name).find(q) != std::string::npos)
                res.push_back({recent_[i].name, displayPath(fs::parent(recentPaths_[i])), recentPaths_[i], IcFile, -1});
        gfx_.pushClip(body);
        for (size_t i = 0; i < res.size() && y < body.b(); i++) {
            Rect r{body.x, y, body.w, 44};
            ui.pressFill(r, 5, i == 0);
            IconId ic = res[i].icon;
            if (res[i].app == -1 && fs::extension(res[i].name) == "bmp") ic = IcImage;
            drawIcon(gfx_, ic, {r.x + 10, r.cy() - 12, 24, 24}, theme_.text);
            gfx_.text(r.x + 46, r.y + 5, 13, res[i].name, theme_.text);
            gfx_.text(r.x + 46, r.y + 24, 11, gfx_.ellipsize(11, res[i].sub, r.w - 60), theme_.textDim);
            bool go = ui.clicked(r) || (enter && i == 0);
            if (go) {
                Res rr = res[i];
                closeFlyouts();
                kbOpen_ = false;
                if (rr.app >= 0)
                    open((AppKind)rr.app);
                else if (rr.app == -2)
                    open(AppKind::Settings, rr.path);
                else
                    openFile(rr.path);
                break;
            }
            y += 46;
        }
        if (res.empty()) gfx_.text(body.x, y, 13, "No results for \"" + search_.text + "\"", theme_.textDim);
        gfx_.popClip();
    } else if (allApps_) {
        gfx_.textBold(body.x + 8, body.y + 2, 14, "All apps", theme_.text);
        Rect back{body.r() - 86, body.y - 4, 86, 28};
        if (ui.button(back, "\xE2\x86\x90 Back")) allApps_ = false;
        Rect list{body.x, body.y + 34, body.w, body.h - 34};
        std::vector<AppInfo> sorted = appCatalog();
        std::sort(sorted.begin(), sorted.end(), [](const AppInfo& x, const AppInfo& y) { return std::string(x.name) < y.name; });
        float contentH = sorted.size() * 42.f + 30 * 4;
        if (ui.dragging(list)) startScroll_ = clampf(startScroll_ - ui.dy(), 0, std::max(0.f, contentH - list.h));
        gfx_.pushClip(list);
        float y = list.y - startScroll_;
        char last = 0;
        for (auto& ap : sorted) {
            char c = ap.name[0];
            if (c != last) {
                gfx_.text(list.x + 12, y + 6, 12, std::string(1, c), theme_.accent);
                y += 28;
                last = c;
            }
            Rect r{list.x, y, list.w, 40};
            ui.pressFill(r, 5);
            drawIcon(gfx_, ap.icon, {r.x + 12, r.cy() - 12, 24, 24}, theme_.text);
            gfx_.textIn({r.x + 48, r.y, r.w - 48, r.h}, 13, ap.name, theme_.text);
            if (ui.clicked(r)) {
                launchFromStart(ap.kind);
                break;
            }
            y += 42;
        }
        gfx_.popClip();
    } else {
        gfx_.textBold(body.x + 8, body.y + 2, 14, "Pinned", theme_.text);
        Rect allBtn{body.r() - 96, body.y - 4, 96, 28};
        if (ui.button(allBtn, "All apps  \xE2\x80\xBA")) {
            allApps_ = true;
            startScroll_ = 0;
        }
        int cols = std::max(3, (int)(body.w / 96));
        float cw = body.w / cols, chh = 80;
        float gy = body.y + 36;
        const auto& apps = appCatalog();
        for (size_t i = 0; i < apps.size(); i++) {
            int cx = (int)i % cols, cy = (int)i / cols;
            Rect r{body.x + cx * cw, gy + cy * chh, cw, chh - 4};
            ui.pressFill(r, 5);
            float ic = ui.held(r) ? 30 : 34;
            drawIcon(gfx_, apps[i].icon, {r.cx() - ic / 2, r.y + 10 + (34 - ic) / 2, ic, ic}, theme_.text);
            gfx_.textIn({r.x + 2, r.y + 50, r.w - 4, 18}, 11.5f, gfx_.ellipsize(11.5f, apps[i].name, r.w - 6), theme_.text,
                        AlignCenter);
            if (ui.clicked(r)) {
                launchFromStart(apps[i].kind);
                break;
            }
        }
        int rows = ((int)apps.size() + cols - 1) / cols;
        float ry = gy + rows * chh + 10;
        if (ry + 70 < body.b()) {
            gfx_.textBold(body.x + 8, ry, 14, "Recommended", theme_.text);
            float iy = ry + 30;
            float colW = body.w / 2;
            int maxRows = (int)((body.b() - iy) / 52);
            int shown = std::min((int)recent_.size(), maxRows * 2);
            for (int i = 0; i < shown; i++) {
                Rect r{body.x + (i % 2) * colW, iy + (i / 2) * 52, colW - 6, 48};
                ui.pressFill(r, 5);
                IconId ic = fs::extension(recent_[i].name) == "bmp" ? IcImage : IcFile;
                drawIcon(gfx_, ic, {r.x + 8, r.cy() - 14, 28, 28}, theme_.text);
                gfx_.text(r.x + 44, r.y + 7, 12.5f, gfx_.ellipsize(12.5f, recent_[i].name, r.w - 52), theme_.text);
                gfx_.text(r.x + 44, r.y + 26, 11, relTime(recent_[i].mtime), theme_.textDim);
                if (ui.clicked(r)) {
                    std::string p = recentPaths_[(size_t)i];
                    closeFlyouts();
                    openFile(p);
                    break;
                }
            }
            if (recent_.empty()) gfx_.text(body.x + 8, iy, 12, "Your recent files will show up here.", theme_.textDim);
        }
    }

    gfx_.pushClip(bottom);
    gfx_.rect(bottom, theme_.dark ? rgb(0x000000, 0.12f) : rgb(0x000000, 0.03f), 8);
    gfx_.popClip();
    gfx_.rect({s.x, bottom.y, s.w, 1}, alpha(theme_.text, 0.08f));
    Rect user{s.x + pad - 8, bottom.y + 11, 200, 40};
    ui.pressFill(user, 5);
    drawIcon(gfx_, IcUser, {user.x + 8, user.cy() - 14, 28, 28}, theme_.text);
    gfx_.textIn({user.x + 46, user.y, 150, user.h}, 12.5f, cfg_.user, theme_.text);
    if (ui.clicked(user)) {
        closeFlyouts();
        open(AppKind::Settings, "accounts");
    }
    Rect pwr{s.r() - pad - 32, bottom.y + 13, 36, 36};
    ui.pressFill(pwr, 5, powerOpen_);
    drawIcon(gfx_, IcGlyphPower, pwr.centered(16, 16), theme_.text);
    if (ui.clicked(pwr)) powerOpen_ = !powerOpen_;
    if (powerOpen_) {
        Rect pm{pwr.r() - 180, pwr.y - 8 - 4 * 38 - 8, 180, 4 * 38 + 8};
        gfx_.shadow(pm, 8, 16, theme_.shadow);
        gfx_.rect(pm, theme_.dark ? rgb(0x2C2C2C) : rgb(0xFAFAFA), 8);
        gfx_.stroke(pm, alpha(theme_.text, 0.1f), 8, 1);
        struct P {
            const char* label;
            IconId icon;
            int act;
        } items[] = {{"Lock", IcGlyphLock, 0}, {"Sleep", IcGlyphSleep, 1}, {"Shut down", IcGlyphPower, 2}, {"Restart", IcGlyphRestart, 3}};
        for (int i = 0; i < 4; i++) {
            Rect r{pm.x + 4, pm.y + 4 + i * 38, pm.w - 8, 36};
            ui.pressFill(r, 4);
            drawIcon(gfx_, items[i].icon, {r.x + 12, r.cy() - 8, 16, 16}, theme_.text);
            gfx_.textIn({r.x + 40, r.y, r.w - 40, r.h}, 13, items[i].label, theme_.text);
            if (ui.clicked(r)) {
                int act = items[i].act;
                powerOpen_ = false;
                if (act == 0 || act == 1) lock();
                if (act == 2) shutdown();
                if (act == 3) restart();
            }
        }
    }
    gfx_.popLayer();
    ui.active = false;
    ui.focus = false;
}

void Shell::drawQuick(bool cap) {
    Rect q = quickRect();
    float a = easeOut(quickAnim_);
    gfx_.pushLayer(q.cx(), q.b(), 1, a, 0, (1 - a) * 30);
    Ui& ui = ui_;
    ui.active = cap && quickAnim_ >= 1;
    gfx_.shadow({q.x, q.y + 8, q.w, q.h}, 8, 26, theme_.shadow);
    gfx_.rect(q, theme_.flyout, 8);
    gfx_.stroke(q, alpha(theme_.text, 0.1f), 8, 1);
    float pad = 18;
    struct T {
        const char* label;
        IconId icon;
        bool* v;
        int special;
    };
    bool darkV = cfg_.dark;
    T tiles[] = {{"Wi-Fi", IcGlyphWifi, &wifi_, 0},       {"Bluetooth", IcGlyphBluetooth, &bt_, 0},
                 {"Airplane mode", IcGlyphPlane, &airplane_, 0}, {"Battery saver", IcGlyphLeaf, &saver_, 0},
                 {"Dark mode", IcGlyphMoon, &darkV, 1},    {"Night light", IcGlyphSun, &night_, 0}};
    float tw = (q.w - pad * 2 - 16) / 3;
    for (int i = 0; i < 6; i++) {
        float x = q.x + pad + (i % 3) * (tw + 8);
        float y = q.y + pad + (i / 3) * 76;
        Rect btn{x, y, tw, 44};
        bool on = *tiles[i].v;
        Color bg = on ? theme_.accent : (theme_.dark ? rgb(0x373737) : rgb(0xFBFBFB));
        if (ui.held(btn)) bg = on ? darken(bg, 0.15f) : mix(bg, theme_.text, 0.08f);
        gfx_.rect(btn, bg, 5);
        if (!on) gfx_.stroke(btn, alpha(theme_.text, 0.12f), 5, 1);
        drawIcon(gfx_, tiles[i].icon, btn.centered(18, 18), on ? theme_.accentText : theme_.text);
        gfx_.textIn({x - 6, y + 48, tw + 12, 18}, 11, gfx_.ellipsize(11, tiles[i].label, tw + 12), theme_.text, AlignCenter);
        if (ui.clicked(btn)) {
            *tiles[i].v = !on;
            if (tiles[i].special == 1) {
                cfg_.dark = darkV;
                applyConfig();
                saveConfig();
            }
            if (tiles[i].v == &airplane_ && airplane_) {
                wifi_ = false;
                bt_ = false;
            }
        }
    }
    float sy = q.y + pad + 2 * 76 + 6;
    drawIcon(gfx_, IcGlyphSun, {q.x + pad, sy + 6, 18, 18}, theme_.text);
    ui.slider({q.x + pad + 34, sy, q.w - pad * 2 - 40, 30}, brightness_);
    if (brightness_ < 0.15f) brightness_ = 0.15f;
    drawIcon(gfx_, IcGlyphVolume, {q.x + pad, sy + 46, 18, 18}, theme_.text);
    ui.slider({q.x + pad + 34, sy + 40, q.w - pad * 2 - 40, 30}, volume_);
    Rect bar{q.x, q.b() - 48, q.w, 48};
    gfx_.pushClip(bar);
    gfx_.rect(q, theme_.dark ? rgb(0x000000, 0.12f) : rgb(0x000000, 0.03f), 8);
    gfx_.popClip();
    gfx_.rect({q.x, bar.y, q.w, 1}, alpha(theme_.text, 0.08f));
    drawIcon(gfx_, IcGlyphBattery, {q.x + pad, bar.cy() - 8, 20, 16}, theme_.text, battery_);
    gfx_.textIn({q.x + pad + 28, bar.y, 80, bar.h}, 12, std::to_string((int)std::lround(battery_ * 100)) + "%", theme_.text);
    Rect gear{q.r() - pad - 32, bar.y + 8, 32, 32};
    ui.pressFill(gear, 4);
    drawIcon(gfx_, IcGlyphGear, gear.centered(16, 16), theme_.text);
    if (ui.clicked(gear)) {
        closeFlyouts();
        open(AppKind::Settings);
    }
    gfx_.popLayer();
    ui.active = false;
}

void Shell::drawCalendar(bool cap) {
    Rect c = calendarRect();
    float a = easeOut(calAnim_);
    gfx_.pushLayer(c.cx(), c.b(), 1, a, 0, (1 - a) * 30);
    Ui& ui = ui_;
    ui.active = cap && calAnim_ >= 1;
    gfx_.shadow({c.x, c.y + 8, c.w, c.h}, 8, 26, theme_.shadow);
    gfx_.rect(c, theme_.flyout, 8);
    gfx_.stroke(c, alpha(theme_.text, 0.1f), 8, 1);

    time_t now = time(nullptr);
    struct tm lt;
    localtime_r(&now, &lt);
    char buf[64];
    strftime(buf, sizeof buf, "%A, %B %d", &lt);
    gfx_.textBold(c.x + 16, c.y + 14, 13, buf, theme_.text);
    gfx_.rect({c.x, c.y + 44, c.w, 1}, alpha(theme_.text, 0.08f));

    struct tm m = lt;
    m.tm_mday = 1;
    m.tm_mon += calMonth_;
    m.tm_hour = 12;
    mktime(&m);
    strftime(buf, sizeof buf, "%B %Y", &m);
    gfx_.textBold(c.x + 16, c.y + 58, 13, buf, theme_.text);
    Rect up{c.r() - 76, c.y + 52, 30, 30}, dn{c.r() - 42, c.y + 52, 30, 30};
    ui.pressFill(up, 4);
    ui.pressFill(dn, 4);
    gfx_.line(up.cx() - 4, up.cy() + 2, up.cx(), up.cy() - 2, 1.2f, theme_.text);
    gfx_.line(up.cx(), up.cy() - 2, up.cx() + 4, up.cy() + 2, 1.2f, theme_.text);
    gfx_.line(dn.cx() - 4, dn.cy() - 2, dn.cx(), dn.cy() + 2, 1.2f, theme_.text);
    gfx_.line(dn.cx(), dn.cy() + 2, dn.cx() + 4, dn.cy() - 2, 1.2f, theme_.text);
    if (ui.clicked(up)) calMonth_--;
    if (ui.clicked(dn)) calMonth_++;

    const char* wd[] = {"Su", "Mo", "Tu", "We", "Th", "Fr", "Sa"};
    float gx = c.x + 12, gw = (c.w - 24) / 7;
    float gy = c.y + 96;
    for (int i = 0; i < 7; i++) gfx_.textIn({gx + i * gw, gy, gw, 20}, 11.5f, wd[i], theme_.text, AlignCenter);
    int startDow = m.tm_wday;
    int month = m.tm_mon, year = m.tm_year;
    struct tm d = m;
    float cellH = std::min(38.f, (c.b() - gy - 30) / 6);
    for (int cell = 0; cell < 42; cell++) {
        d = m;
        d.tm_mday = 1 + cell - startDow;
        mktime(&d);
        float x = gx + (cell % 7) * gw, y = gy + 24 + (cell / 7) * cellH;
        bool inMonth = d.tm_mon == month && d.tm_year == year;
        bool today = d.tm_mday == lt.tm_mday && d.tm_mon == lt.tm_mon && d.tm_year == lt.tm_year;
        Rect r{x, y, gw, cellH};
        if (today) gfx_.circle(r.cx(), r.cy(), std::min(gw, cellH) * 0.42f, theme_.accent);
        gfx_.textIn(r, 12, std::to_string(d.tm_mday), today ? theme_.accentText : (inMonth ? theme_.text : theme_.textDim),
                    AlignCenter);
    }
    gfx_.popLayer();
    ui.active = false;
}

void Shell::drawContext(bool cap) {
    Rect r = contextRect();
    Ui& ui = ui_;
    ui.active = cap;
    gfx_.shadow({r.x, r.y + 6, r.w, r.h}, 8, 18, theme_.shadow);
    gfx_.rect(r, theme_.dark ? rgb(0x2C2C2C) : rgb(0xFAFAFA), 8);
    gfx_.stroke(r, alpha(theme_.text, 0.1f), 8, 1);
    for (size_t i = 0; i < ctxItems_.size(); i++) {
        Rect ir{r.x + 4, r.y + 4 + i * 36.f, r.w - 8, 34};
        ui.pressFill(ir, 4);
        drawIcon(gfx_, ctxItems_[i].icon, {ir.x + 10, ir.cy() - 8, 16, 16}, theme_.text);
        gfx_.textIn({ir.x + 38, ir.y, ir.w - 40, ir.h}, 12.5f, ctxItems_[i].label, theme_.text);
        if (ui.clicked(ir)) {
            auto act = ctxItems_[i].act;
            ctxOpen_ = false;
            if (act) act();
            break;
        }
    }
    ui.active = false;
}

}
