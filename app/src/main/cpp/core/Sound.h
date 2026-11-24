#pragma once
#include <string>
#include <vector>

namespace wd {

const std::vector<std::string>& soundNames();
bool writeSounds(const std::string& dir);
std::vector<float> renderSound(const std::string& name, int rate);

}
