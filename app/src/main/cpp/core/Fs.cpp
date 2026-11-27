#include "Fs.h"

#include <dirent.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>

#include "Utf8.h"

namespace wd {
namespace fs {

std::string join(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    if (a.back() == '/') return a + b;
    return a + "/" + b;
}

std::string parent(const std::string& p) {
    size_t s = p.find_last_of('/');
    if (s == std::string::npos || s == 0) return "/";
    return p.substr(0, s);
}

std::string baseName(const std::string& p) {
    size_t s = p.find_last_of('/');
    return s == std::string::npos ? p : p.substr(s + 1);
}

std::string extension(const std::string& name) {
    size_t d = name.find_last_of('.');
    if (d == std::string::npos || d == 0) return "";
    return toLower(name.substr(d + 1));
}

std::string stem(const std::string& name) {
    size_t d = name.find_last_of('.');
    if (d == std::string::npos || d == 0) return name;
    return name.substr(0, d);
}

bool exists(const std::string& p) {
    struct stat st;
    return stat(p.c_str(), &st) == 0;
}

bool isDir(const std::string& p) {
    struct stat st;
    return stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

bool makeDirs(const std::string& p) {
    if (p.empty() || isDir(p)) return true;
    std::string par = parent(p);
    if (par != p && par != "/" && !isDir(par)) makeDirs(par);
    return mkdir(p.c_str(), 0755) == 0 || isDir(p);
}

bool removeAll(const std::string& p) {
    if (isDir(p)) {
        DIR* d = opendir(p.c_str());
        if (d) {
            while (dirent* e = readdir(d)) {
                std::string n = e->d_name;
                if (n == "." || n == "..") continue;
                removeAll(join(p, n));
            }
            closedir(d);
        }
        return rmdir(p.c_str()) == 0;
    }
    return unlink(p.c_str()) == 0;
}

bool renamePath(const std::string& from, const std::string& to) { return rename(from.c_str(), to.c_str()) == 0; }

bool readBinary(const std::string& p, std::vector<unsigned char>& out) {
    FILE* f = fopen(p.c_str(), "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 0) {
        fclose(f);
        return false;
    }
    out.resize((size_t)n);
    size_t got = n > 0 ? fread(out.data(), 1, (size_t)n, f) : 0;
    fclose(f);
    return got == (size_t)n;
}

bool writeBinary(const std::string& p, const std::vector<unsigned char>& data) {
    FILE* f = fopen(p.c_str(), "wb");
    if (!f) return false;
    size_t put = data.empty() ? 0 : fwrite(data.data(), 1, data.size(), f);
    fclose(f);
    return put == data.size();
}

bool readText(const std::string& p, std::string& out) {
    std::vector<unsigned char> b;
    if (!readBinary(p, b)) return false;
    out.assign(b.begin(), b.end());
    std::string clean;
    clean.reserve(out.size());
    for (char c : out)
        if (c != '\r') clean += c;
    out.swap(clean);
    return true;
}

bool writeText(const std::string& p, const std::string& data) {
    return writeBinary(p, std::vector<unsigned char>(data.begin(), data.end()));
}

std::vector<Entry> list(const std::string& dir) {
    std::vector<Entry> out;
    DIR* d = opendir(dir.c_str());
    if (!d) return out;
    while (dirent* e = readdir(d)) {
        std::string n = e->d_name;
        if (n == "." || n == "..") continue;
        Entry en;
        en.name = n;
        struct stat st;
        if (stat(join(dir, n).c_str(), &st) == 0) {
            en.dir = S_ISDIR(st.st_mode);
            en.size = st.st_size;
            en.mtime = st.st_mtime;
        }
        out.push_back(en);
    }
    closedir(d);
    std::sort(out.begin(), out.end(), [](const Entry& a, const Entry& b) {
        if (a.dir != b.dir) return a.dir;
        return toLower(a.name) < toLower(b.name);
    });
    return out;
}

std::string uniqueName(const std::string& dir, const std::string& base, const std::string& ext) {
    std::string suffix = ext.empty() ? "" : "." + ext;
    std::string n = base + suffix;
    int i = 2;
    while (exists(join(dir, n))) n = base + " (" + std::to_string(i++) + ")" + suffix;
    return n;
}

long long dirSize(const std::string& p) {
    if (!isDir(p)) {
        struct stat st;
        return stat(p.c_str(), &st) == 0 ? (long long)st.st_size : 0;
    }
    long long t = 0;
    for (auto& e : list(p)) t += dirSize(join(p, e.name));
    return t;
}

bool diskUsage(const std::string& p, long long& total, long long& freeBytes) {
    struct statvfs v;
    if (statvfs(p.c_str(), &v) != 0) return false;
    total = (long long)v.f_blocks * (long long)v.f_frsize;
    freeBytes = (long long)v.f_bavail * (long long)v.f_frsize;
    return true;
}

std::string formatSize(long long b) {
    char buf[32];
    if (b < 1024) {
        snprintf(buf, sizeof buf, "%lld bytes", b);
    } else if (b < 1024LL * 1024) {
        snprintf(buf, sizeof buf, "%lld KB", (b + 1023) / 1024);
    } else if (b < 1024LL * 1024 * 1024) {
        snprintf(buf, sizeof buf, "%.1f MB", b / (1024.0 * 1024.0));
    } else {
        snprintf(buf, sizeof buf, "%.1f GB", b / (1024.0 * 1024.0 * 1024.0));
    }
    return buf;
}

std::string formatTime(time_t t) {
    struct tm lt;
    localtime_r(&t, &lt);
    char buf[32];
    strftime(buf, sizeof buf, "%d/%m/%Y %H:%M", &lt);
    return buf;
}

std::string typeName(const Entry& e) {
    if (e.dir) return "File folder";
    std::string x = extension(e.name);
    if (x == "txt") return "Text Document";
    if (x == "bmp") return "Bitmap image";
    if (x == "ini" || x == "cfg") return "Configuration settings";
    if (x == "log") return "Text log";
    if (x == "md") return "Markdown file";
    if (x == "exe") return "Application";
    if (x == "zip") return "Compressed (zipped) Folder";
    if (x == "rar") return "WinRAR archive";
    if (x == "lnk") return "Shortcut";
    if (x == "dll") return "Application extension";
    if (x == "msi") return "Windows Installer Package";
    if (x == "bat") return "Windows Batch File";
    if (x == "cmd") return "Windows Command Script";
    if (x.empty()) return "File";
    std::string u = x;
    for (auto& c : u) c = (char)toupper((unsigned char)c);
    return u + " File";
}

}
}
