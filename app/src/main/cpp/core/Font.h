#pragma once
#include <GLES2/gl2.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "../third_party/stb_truetype.h"

namespace wd {

struct FontAtlas {
    GLuint tex = 0;
    int px = 0;
    int width = 0;
    int height = 0;
    float ascent = 0;
    float descent = 0;
    float lineGap = 0;
    std::vector<stbtt_packedchar> glyphs;
    std::vector<std::pair<uint32_t, int>> ranges;
    std::vector<int> offsets;
    int find(uint32_t cp) const;
};

class Font {
public:
    ~Font();
    bool loadFirst(const std::vector<std::string>& paths);
    bool valid() const { return loaded_; }
    FontAtlas* atlas(int px);

private:
    std::vector<unsigned char> data_;
    stbtt_fontinfo info_{};
    bool loaded_ = false;
    std::map<int, std::unique_ptr<FontAtlas>> cache_;
};

}
