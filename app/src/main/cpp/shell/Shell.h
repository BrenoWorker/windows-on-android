#pragma once
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "../core/Fs.h"
#include "../core/Gfx.h"
#include "../core/Theme.h"
#include "../core/Ui.h"
#include "Icons.h"
#include "TouchKeyboard.h"

namespace wd {

enum class AppKind { Explorer, Notepad, Calculator, Settings, Terminal, Paint, Minesweeper, Dialog };

struct AppInfo {
    AppKind kind;
    const char* name;
    IconId icon;
    bool pinned;
};

const std::vector<AppInfo>& appCatalog();
const AppInfo& appInfo(AppKind k);

struct Platform {
    std::string dataDir;
    std::vector<std::string> fontPaths;
    std::vector<std::string> monoFontPaths;
    float density = 2.0f;
    std::string model = "Generic device";
    std::string manufacturer = "Unknown";
    std::string osVersion = "";
    std::string abi = "arm64-v8a";
    std::function<void()> quit;
    std::function<float()> batteryLevel;
};

struct Config {
    bool dark = false;
    int accent = 0;
    int wallpaper = 0;
    int scalePct = 100;
    bool taskbarLeft = false;
    bool use24h = true;
    std::string user = "User";
};

class Shell;

class AppWindow {
public:
    virtual ~AppWindow() = default;
    virtual void draw(Ui& ui, const Rect& client) = 0;
    virtual bool wantsText() const { return false; }
    virtual bool onBack() { return false; }

    Shell* shell = nullptr;
    AppKind kind = AppKind::Notepad;
    std::string title;
    IconId icon = IcFile;
    Rect frame;
    Rect restore;
    bool maximized = false;
    bool snapped = false;
    bool minimized = false;
    bool minimizing = false;
    bool closing = false;
    float anim = 0;
    float minW = 300;
    float minH = 200;
    float prefW = 640;
    float prefH = 440;
    bool resizable = true;
};

class Shell {
public:
    explicit Shell(Platform p);
    ~Shell();

    void init();
    void resize(int pw, int ph);
    void frame(double t);
    void pointer(int type, float px, float py);
    void key(int k);
    void character(uint32_t cp);
    bool back();
    void hardwareKeyInput();
    bool wantsContinuousFrames() const { return true; }

    AppWindow* open(AppKind k, const std::string& arg = "");
    void openFile(const std::string& path);
    void prompt(const std::string& title, const std::string& message, const std::string& initial,
                std::function<void(const std::string&)> ok);
    void message(const std::string& title, const std::string& text);
    void focus(AppWindow* w);
    void close(AppWindow* w);
    void minimize(AppWindow* w);
    void toggleMaximize(AppWindow* w);
    AppWindow* focused() const { return focused_; }

    Gfx& gfx() { return gfx_; }
    Theme& theme() { return theme_; }
    Config& config() { return cfg_; }
    const Platform& platform() const { return plat_; }
    void applyConfig();
    void saveConfig();
    void notifyFs() { fsVersion_++; }
    int fsVersion() const { return fsVersion_; }
    std::string root() const;
    std::string userDir() const;
    std::string recycleDir() const;
    std::string displayPath(const std::string& real) const;
    std::string realPath(const std::string& display, const std::string& cwd) const;
    void recycle(const std::string& path);
    Rect workArea() const;
    int monoFont() const { return monoFont_; }
    double now() const { return time_; }
    void restart();
    void shutdown();
    void lock();
    std::string timeString() const;
    std::string dateString() const;
    void drawWallpaper(const Rect& r, int index = -1);
    void setNightLight(bool v) { night_ = v; }
    bool nightLight() const { return night_; }

private:
    enum class State { Boot, Lock, Desktop, Shutdown };
    enum class Owner { None, Desktop, Window, Taskbar, Start, Quick, Calendar, Context, Keyboard };
    enum class Part { None, Client, Title, Resize, Buttons };
    struct Ev {
        int type;
        float x, y;
        uint32_t cp;
        int key;
    };
    struct MenuItem {
        std::string label;
        IconId icon;
        std::function<void()> act;
    };
    struct DeskItem {
        std::string name;
        std::string path;
        IconId icon;
        int special;
    };

    void processInput();
    Owner hitTest(float x, float y, AppWindow** win);
    void ensureFilesystem();
    void loadConfig();
    void drawBoot();
    void drawLock();
    void drawShutdown();
    void drawDesktop(bool interactive);
    void drawSpinner(float cx, float cy, float r, Color c);
    void drawDesktopIcons(bool cap);
    void drawWindow(AppWindow* w, bool cap);
    void drawTaskbar(bool cap);
    void drawStart(bool cap);
    void drawQuick(bool cap);
    void drawCalendar(bool cap);
    void drawContext(bool cap);
    void closeFlyouts();
    void openContext(float x, float y, std::vector<MenuItem> items);
    Rect taskbarRect() const;
    Rect startRect() const;
    Rect quickRect() const;
    Rect calendarRect() const;
    Rect contextRect() const;
    Rect keyboardRect() const;
    float keyboardHeight() const;
    void refreshDesktop();
    void collectRecent();
    std::vector<AppWindow*> windowsOf(AppKind k);
    void taskbarActivate(AppKind k);
    void launchFromStart(AppKind k);
    void ensureVisible(AppWindow* w);

    Platform plat_;
    Gfx gfx_;
    Theme theme_;
    Config cfg_;
    Ui ui_;
    TouchKeyboard kb_;
    State state_ = State::Boot;
    double stateT_ = 0;
    double time_ = 0;
    double lastT_ = -1;
    double downT_ = 0;
    double lastTapT_ = -10;
    float lastTapX_ = 0, lastTapY_ = 0;
    int tapCount_ = 0;
    int pw_ = 1, ph_ = 1;
    int monoFont_ = 0;
    int fsVersion_ = 1;
    int deskVersion_ = 0;
    bool started_ = false;
    std::deque<Ev> queue_;
    std::vector<std::unique_ptr<AppWindow>> windows_;
    AppWindow* focused_ = nullptr;
    Owner cap_ = Owner::None;
    AppWindow* capWin_ = nullptr;
    Part capPart_ = Part::None;
    float grabX_ = 0, grabY_ = 0;
    Rect grabFrame_;

    bool startOpen_ = false;
    bool quickOpen_ = false;
    bool calOpen_ = false;
    bool powerOpen_ = false;
    bool allApps_ = false;
    bool kbOpen_ = false;
    bool hardwareKb_ = false;
    float startAnim_ = 0;
    float quickAnim_ = 0;
    float calAnim_ = 0;
    float startScroll_ = 0;
    LineEdit search_;
    bool searchFocus_ = true;
    int calMonth_ = 0;

    bool ctxOpen_ = false;
    float ctxX_ = 0, ctxY_ = 0;
    std::vector<MenuItem> ctxItems_;

    std::vector<DeskItem> desk_;
    int deskSel_ = -1;
    std::vector<fs::Entry> recent_;
    std::vector<std::string> recentPaths_;

    bool wifi_ = true, bt_ = true, airplane_ = false, saver_ = false, night_ = false;
    float brightness_ = 1.0f, volume_ = 0.6f;
    float lockOffset_ = 0;
    bool unlocking_ = false;
    float battery_ = 1.0f;
    double batteryT_ = -100;
    int openCount_ = 0;
};

}
