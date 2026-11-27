#include "Programs.h"

#include <algorithm>
#include <cctype>
#include <cstdio>

#include "Shell.h"

namespace wd {

static std::string lower(std::string s) {
    for (auto& c : s) c = (char)tolower((unsigned char)c);
    return s;
}

static std::string safeName(std::string n) {
    std::string out;
    for (char c : n) {
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') continue;
        out += c;
    }
    while (!out.empty() && (out.back() == ' ' || out.back() == '.')) out.pop_back();
    while (!out.empty() && out[0] == ' ') out.erase(0, 1);
    if (out.size() > 64) out.resize(64);
    return out;
}

std::string startMenuDir(const Shell& s) { return fs::join(s.root(), "ProgramData/Microsoft/Windows/Start Menu/Programs"); }

std::string programFilesDir(const Shell& s) { return fs::join(s.root(), "Program Files"); }

bool isShortcut(const std::string& path) { return lower(fs::extension(path)) == "lnk"; }

bool isWindowsProgram(const std::string& path) {
    std::string e = lower(fs::extension(path));
    return e == "exe" || e == "msi" || e == "bat" || e == "cmd";
}

bool isWineTarget(const std::string& path) { return path.compare(0, 5, "wine:") == 0; }

bool readShortcut(const Shell& s, const std::string& lnk, Shortcut& out) {
    std::string text;
    if (!fs::readText(lnk, text)) return false;
    if (text.find("[WinDroid Shortcut]") == std::string::npos) return false;
    out = Shortcut();
    out.path = lnk;
    out.name = fs::stem(fs::baseName(lnk));
    size_t i = 0;
    while (i < text.size()) {
        size_t j = text.find('\n', i);
        if (j == std::string::npos) j = text.size();
        std::string line = text.substr(i, j - i);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t eq = line.find('=');
        if (eq != std::string::npos) {
            std::string k = lower(line.substr(0, eq)), v = line.substr(eq + 1);
            if (k == "target") out.target = isWineTarget(v) ? v : s.realPath(v, s.root());
            if (k == "arguments") out.args = v;
        }
        i = j + 1;
    }
    return !out.target.empty();
}

bool writeShortcut(const Shell& s, const std::string& lnk, const std::string& target, const std::string& args) {
    fs::makeDirs(fs::parent(lnk));
    return fs::writeText(lnk, "[WinDroid Shortcut]\nTarget=" + s.displayPath(target) + "\nArguments=" + args + "\n");
}

std::vector<Shortcut> installedPrograms(const Shell& s) {
    std::vector<Shortcut> out;
    std::string dir = startMenuDir(s);
    for (auto& e : fs::list(dir)) {
        if (e.dir || !isShortcut(e.name)) continue;
        Shortcut sc;
        if (readShortcut(s, fs::join(dir, e.name), sc)) out.push_back(sc);
    }
    std::sort(out.begin(), out.end(), [](const Shortcut& a, const Shortcut& b) { return lower(a.name) < lower(b.name); });
    return out;
}

std::string programName(const PeInfo& pe, const std::string& path) {
    std::string n = !pe.product.empty() ? pe.product : (!pe.description.empty() ? pe.description : fs::stem(fs::baseName(path)));
    n = safeName(n);
    return n.empty() ? "Program" : n;
}

bool isInstalledPath(const Shell& s, const std::string& path) {
    std::string pf = programFilesDir(s) + "/";
    return path.compare(0, pf.size(), pf) == 0;
}

static bool copyFile(const std::string& from, const std::string& to) {
    FILE* in = fopen(from.c_str(), "rb");
    if (!in) return false;
    FILE* out = fopen(to.c_str(), "wb");
    if (!out) {
        fclose(in);
        return false;
    }
    std::vector<char> buf(1 << 16);
    size_t n;
    bool ok = true;
    while ((n = fread(buf.data(), 1, buf.size(), in)) > 0)
        if (fwrite(buf.data(), 1, n, out) != n) {
            ok = false;
            break;
        }
    fclose(in);
    if (fclose(out) != 0) ok = false;
    return ok;
}

static bool copyTree(const std::string& from, const std::string& to, int depth) {
    if (depth > 12 || !fs::makeDirs(to)) return false;
    for (auto& e : fs::list(from)) {
        std::string a = fs::join(from, e.name), b = fs::join(to, e.name);
        if (e.dir ? !copyTree(a, b, depth + 1) : !copyFile(a, b)) return false;
    }
    return true;
}

bool installProgram(Shell& s, const std::string& exe, const PeInfo& pe, bool desktop, std::string& installed, std::string& err) {
    std::string name = programName(pe, exe);
    std::string dir = fs::join(programFilesDir(s), name);
    std::string srcDir = fs::parent(exe);
    if (!fs::makeDirs(dir)) {
        err = "Couldn't create the program folder.";
        return false;
    }
    std::string target = fs::join(dir, fs::baseName(exe));
    if (target != exe && !copyFile(exe, target)) {
        err = "Couldn't copy the program files.";
        return false;
    }
    std::string downloads = fs::join(s.userDir(), "Downloads");
    std::string desk = fs::join(s.userDir(), "Desktop");
    bool ownFolder = srcDir != s.root() && srcDir != s.userDir() && fs::parent(srcDir) != s.userDir() &&
                     srcDir != programFilesDir(s) && (dir + "/").compare(0, srcDir.size() + 1, srcDir + "/") != 0;
    if (ownFolder) {
        if (!copyTree(srcDir, dir, 0)) {
            err = "Couldn't copy the program files.";
            return false;
        }
    } else if (srcDir != downloads && srcDir != desk && srcDir != s.userDir()) {
        for (auto& e : fs::list(srcDir)) {
            std::string ext = lower(fs::extension(e.name));
            if (e.dir || !(ext == "dll" || ext == "dat" || ext == "pak" || ext == "ini" || ext == "cfg")) continue;
            copyFile(fs::join(srcDir, e.name), fs::join(dir, e.name));
        }
    }
    writeShortcut(s, fs::join(startMenuDir(s), name + ".lnk"), target, "");
    if (desktop) writeShortcut(s, fs::join(desk, name + ".lnk"), target, "");
    installed = target;
    s.notifyFs();
    return true;
}

void syncWindowsApps(Shell& s) {
    if (!s.platform().sys) return;
    std::string list = s.sys("wine.apps");
    if (list.compare(0, 3, "ok\n") != 0) return;
    std::vector<std::pair<std::string, std::string>> found;
    size_t i = 3;
    while (i < list.size()) {
        size_t j = list.find('\n', i);
        if (j == std::string::npos) j = list.size();
        std::string line = list.substr(i, j - i);
        size_t tab = line.find('\t');
        if (tab != std::string::npos) {
            std::string name = safeName(line.substr(0, tab)), target = line.substr(tab + 1);
            if (!name.empty() && isWineTarget(target)) found.push_back({name, target});
        }
        i = j + 1;
    }

    std::string menu = startMenuDir(s);
    std::string desk = fs::join(s.userDir(), "Desktop");
    std::vector<std::string> known;
    bool changed = false;
    for (const std::string& dir : {menu, desk}) {
        for (auto& e : fs::list(dir)) {
            if (e.dir || !isShortcut(e.name)) continue;
            Shortcut sc;
            if (!readShortcut(s, fs::join(dir, e.name), sc) || !isWineTarget(sc.target)) continue;
            bool present = false;
            for (auto& f : found)
                if (f.second == sc.target) present = true;
            if (!present) {
                fs::removeAll(sc.path);
                changed = true;
            } else if (dir == menu) {
                known.push_back(sc.target);
            }
        }
    }
    for (auto& f : found) {
        if (std::find(known.begin(), known.end(), f.second) != known.end()) continue;
        std::string name = f.first;
        for (int n = 1; fs::exists(fs::join(menu, name + ".lnk")) && n < 20; n++)
            name = f.first + " (Windows" + (n > 1 ? " " + std::to_string(n) : "") + ")";
        if (fs::exists(fs::join(menu, name + ".lnk"))) continue;
        writeShortcut(s, fs::join(menu, name + ".lnk"), f.second, "");
        changed = true;
    }
    if (changed) s.notifyFs();
}

void uninstallProgram(Shell& s, const Shortcut& sc) {
    if (isWineTarget(sc.target)) {
        s.runWindowsApp("uninstaller");
        return;
    }
    std::string pf = programFilesDir(s) + "/";
    if (sc.target.compare(0, pf.size(), pf) == 0) {
        std::string rest = sc.target.substr(pf.size());
        size_t slash = rest.find('/');
        if (slash != std::string::npos) fs::removeAll(pf + rest.substr(0, slash));
    }
    fs::removeAll(sc.path);
    std::string desk = fs::join(s.userDir(), "Desktop");
    for (auto& e : fs::list(desk)) {
        if (e.dir || !isShortcut(e.name)) continue;
        Shortcut d;
        if (readShortcut(s, fs::join(desk, e.name), d) && d.target == sc.target) fs::removeAll(d.path);
    }
    s.notifyFs();
}

}
