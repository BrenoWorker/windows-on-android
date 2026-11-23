#define STB_TRUETYPE_IMPLEMENTATION
#include "Font.h"

#include <cstdio>

namespace wd {

static const std::pair<uint32_t, int> kRanges[] = {
    {32, 224}, {0x2013, 2}, {0x2018, 4}, {0x201C, 4}, {0x2022, 1}, {0x2026, 1}, {0x2039, 2},
    {0x20AC, 1}, {0x2190, 4}, {0x2212, 1}, {0x221A, 1}, {0x2261, 1}, {0x25B2, 1}, {0x25BC, 1},
};

int FontAtlas::find(uint32_t cp) const {
    for (size_t i = 0; i < ranges.size(); i++) {
        if (cp >= ranges[i].first && cp < ranges[i].first + (uint32_t)ranges[i].second)
            return offsets[i] + (int)(cp - ranges[i].first);
    }
    return -1;
}

Font::~Font() {
    for (auto& kv : cache_)
        if (kv.second->tex) glDeleteTextures(1, &kv.second->tex);
}

bool Font::loadFirst(const std::vector<std::string>& paths) {
    for (const auto& p : paths) {
        FILE* f = fopen(p.c_str(), "rb");
        if (!f) continue;
        fseek(f, 0, SEEK_END);
        long n = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (n <= 0) {
            fclose(f);
            continue;
        }
        data_.resize((size_t)n);
        size_t got = fread(data_.data(), 1, (size_t)n, f);
        fclose(f);
        if (got != (size_t)n) continue;
        int off = stbtt_GetFontOffsetForIndex(data_.data(), 0);
        if (off < 0) continue;
        if (!stbtt_InitFont(&info_, data_.data(), off)) continue;
        loaded_ = true;
        return true;
    }
    return false;
}

FontAtlas* Font::atlas(int px) {
    if (!loaded_) return nullptr;
    if (px < 4) px = 4;
    if (px > 160) px = 160;
    auto it = cache_.find(px);
    if (it != cache_.end()) return it->second.get();

    auto a = std::make_unique<FontAtlas>();
    a->px = px;
    int total = 0;
    for (auto& r : kRanges) {
        a->ranges.push_back(r);
        a->offsets.push_back(total);
        total += r.second;
    }
    a->glyphs.resize((size_t)total);

    int side = px <= 28 ? 512 : px <= 56 ? 1024 : 2048;
    std::vector<unsigned char> bitmap;
    bool ok = false;
    while (!ok && side <= 4096) {
        bitmap.assign((size_t)side * side, 0);
        stbtt_pack_context pc;
        if (!stbtt_PackBegin(&pc, bitmap.data(), side, side, 0, 1, nullptr)) break;
        stbtt_PackSetOversampling(&pc, px < 40 ? 2 : 1, 1);
        std::vector<stbtt_pack_range> pr;
        for (size_t i = 0; i < a->ranges.size(); i++) {
            stbtt_pack_range r{};
            r.font_size = (float)px;
            r.first_unicode_codepoint_in_range = (int)a->ranges[i].first;
            r.num_chars = a->ranges[i].second;
            r.chardata_for_range = a->glyphs.data() + a->offsets[i];
            pr.push_back(r);
        }
        ok = stbtt_PackFontRanges(&pc, data_.data(), stbtt_GetFontOffsetForIndex(data_.data(), 0), pr.data(),
                                  (int)pr.size()) != 0;
        stbtt_PackEnd(&pc);
        if (!ok) side *= 2;
    }
    a->width = side;
    a->height = side;

    float scale = stbtt_ScaleForPixelHeight(&info_, (float)px);
    int asc, desc, gap;
    stbtt_GetFontVMetrics(&info_, &asc, &desc, &gap);
    a->ascent = asc * scale;
    a->descent = desc * scale;
    a->lineGap = gap * scale;

    GLint prev = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prev);
    glGenTextures(1, &a->tex);
    glBindTexture(GL_TEXTURE_2D, a->tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, side, side, 0, GL_ALPHA, GL_UNSIGNED_BYTE, bitmap.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, (GLuint)prev);

    FontAtlas* raw = a.get();
    cache_[px] = std::move(a);
    return raw;
}

}
