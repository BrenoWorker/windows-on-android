#pragma once
#include <string>
#include <vector>

namespace wd {

struct PeInfo {
    bool valid = false;
    int machine = 0;
    bool console = false;
    bool dll = false;
    bool dotnet = false;
    std::string description;
    std::string product;
    std::string company;
    std::string version;
    std::string copyright;
    std::string installer;
    int iconW = 0;
    int iconH = 0;
    std::vector<unsigned char> icon;
};

bool readPe(const std::string& path, PeInfo& out, bool wantIcon = true);
std::string peMachineName(int machine);
bool peMachineSupported(int machine);
bool decodePng(const unsigned char* data, size_t size, std::vector<unsigned char>& rgba, int& w, int& h);

}
