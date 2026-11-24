#pragma once
#include <functional>
#include <memory>
#include <string>

#include "../shell/Shell.h"

namespace wd {

std::unique_ptr<AppWindow> makeExplorer(Shell& s, const std::string& arg);
std::unique_ptr<AppWindow> makeBrowser(Shell& s, const std::string& arg);
std::unique_ptr<AppWindow> makeNotepad(Shell& s, const std::string& arg);
std::unique_ptr<AppWindow> makeCalculator(Shell& s);
std::unique_ptr<AppWindow> makeSettings(Shell& s, const std::string& arg);
std::unique_ptr<AppWindow> makeTerminal(Shell& s, const std::string& arg);
std::unique_ptr<AppWindow> makePaint(Shell& s, const std::string& arg);
std::unique_ptr<AppWindow> makeMinesweeper(Shell& s);
std::unique_ptr<AppWindow> makeWindowsDisplay(Shell& s);
std::unique_ptr<AppWindow> makeProgram(Shell& s, const std::string& exe, bool run, const std::string& args);
std::unique_ptr<AppWindow> makePrompt(Shell& s, const std::string& title, const std::string& message,
                                      const std::string& initial, std::function<void(const std::string&)> ok);
std::unique_ptr<AppWindow> makeMessage(Shell& s, const std::string& title, const std::string& text);

}
