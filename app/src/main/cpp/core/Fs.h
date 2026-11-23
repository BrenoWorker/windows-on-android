#pragma once
#include <ctime>
#include <string>
#include <vector>

namespace wd {
namespace fs {

struct Entry {
    std::string name;
    bool dir = false;
    long long size = 0;
    time_t mtime = 0;
};

std::string join(const std::string& a, const std::string& b);
std::string parent(const std::string& p);
std::string baseName(const std::string& p);
std::string extension(const std::string& name);
std::string stem(const std::string& name);
bool exists(const std::string& p);
bool isDir(const std::string& p);
bool makeDirs(const std::string& p);
bool removeAll(const std::string& p);
bool renamePath(const std::string& from, const std::string& to);
bool readText(const std::string& p, std::string& out);
bool writeText(const std::string& p, const std::string& data);
bool readBinary(const std::string& p, std::vector<unsigned char>& out);
bool writeBinary(const std::string& p, const std::vector<unsigned char>& data);
std::vector<Entry> list(const std::string& dir);
std::string uniqueName(const std::string& dir, const std::string& base, const std::string& ext);
long long dirSize(const std::string& p);
bool diskUsage(const std::string& p, long long& total, long long& free);
std::string formatSize(long long bytes);
std::string formatTime(time_t t);
std::string typeName(const Entry& e);

}
}
