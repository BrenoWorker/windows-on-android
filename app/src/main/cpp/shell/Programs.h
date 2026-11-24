#pragma once
#include <string>
#include <vector>

#include "../core/Pe.h"

namespace wd {

class Shell;

struct Shortcut {
    std::string name;
    std::string path;
    std::string target;
    std::string args;
};

std::string startMenuDir(const Shell& s);
std::string programFilesDir(const Shell& s);
bool isShortcut(const std::string& path);
bool readShortcut(const Shell& s, const std::string& lnk, Shortcut& out);
bool writeShortcut(const Shell& s, const std::string& lnk, const std::string& target, const std::string& args);
std::vector<Shortcut> installedPrograms(const Shell& s);
std::string programName(const PeInfo& pe, const std::string& path);
bool isInstalledPath(const Shell& s, const std::string& path);
bool installProgram(Shell& s, const std::string& exe, const PeInfo& pe, bool desktop, std::string& installed, std::string& err);
void uninstallProgram(Shell& s, const Shortcut& sc);

}
