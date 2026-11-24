#include "Pe.h"

#include <zlib.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>

#include "Utf8.h"

namespace wd {

namespace {

struct File {
    FILE* f = nullptr;
    long long size = 0;
    explicit File(const std::string& p) {
        f = fopen(p.c_str(), "rb");
        if (f) {
            fseeko(f, 0, SEEK_END);
            size = ftello(f);
        }
    }
    ~File() {
        if (f) fclose(f);
    }
    bool read(long long off, void* dst, size_t n) {
        if (!f || off < 0 || off + (long long)n > size) return false;
        if (fseeko(f, off, SEEK_SET) != 0) return false;
        return fread(dst, 1, n, f) == n;
    }
};

uint16_t u16(const unsigned char* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
uint32_t u32(const unsigned char* p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
uint32_t be32(const unsigned char* p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }

struct Section {
    uint32_t va, vsize, raw, rawSize;
};

struct Res {
    const std::vector<unsigned char>& data;
    uint32_t base;

    bool ok(uint32_t off, uint32_t n) const { return off <= data.size() && n <= data.size() - off; }

    bool entries(uint32_t dir, std::vector<std::pair<uint32_t, uint32_t>>& out, bool named = false) const {
        if (!ok(dir, 16)) return false;
        uint16_t nNamed = u16(&data[dir + 12]), nId = u16(&data[dir + 14]);
        uint32_t count = (uint32_t)nNamed + nId;
        if (count > 4096 || !ok(dir + 16, count * 8)) return false;
        for (uint32_t i = 0; i < count; i++) {
            uint32_t name = u32(&data[dir + 16 + i * 8]);
            uint32_t off = u32(&data[dir + 16 + i * 8 + 4]);
            out.push_back({name, off});
        }
        return true;
    }

    bool leaf(uint32_t off, std::vector<unsigned char>& out, const std::vector<Section>& secs, uint32_t rsrcVa) const {
        int guard = 0;
        while ((off & 0x80000000u) && guard++ < 4) {
            std::vector<std::pair<uint32_t, uint32_t>> e;
            if (!entries(off & 0x7FFFFFFFu, e) || e.empty()) return false;
            off = e[0].second;
        }
        if (off & 0x80000000u) return false;
        if (!ok(off, 16)) return false;
        uint32_t rva = u32(&data[off]), size = u32(&data[off + 4]);
        if (size == 0 || size > 32 * 1024 * 1024) return false;
        uint32_t rel = rva - rsrcVa;
        if (rva < rsrcVa || !ok(rel, size)) return false;
        out.assign(data.begin() + rel, data.begin() + rel + size);
        return true;
    }
};

std::string utf16(const unsigned char* p, size_t maxChars, size_t* used = nullptr) {
    std::string s;
    size_t i = 0;
    for (; i < maxChars; i++) {
        uint32_t c = u16(p + i * 2);
        if (c == 0) {
            i++;
            break;
        }
        if (c >= 0xD800 && c <= 0xDBFF && i + 1 < maxChars) {
            uint32_t d = u16(p + (i + 1) * 2);
            if (d >= 0xDC00 && d <= 0xDFFF) {
                c = 0x10000 + ((c - 0xD800) << 10) + (d - 0xDC00);
                i++;
            }
        }
        utf8Append(s, c);
    }
    if (used) *used = i;
    return s;
}

void versionBlock(const std::vector<unsigned char>& v, size_t off, size_t end, int depth, std::map<std::string, std::string>& out,
                  std::string& fixed) {
    while (off + 6 <= end && depth < 6) {
        size_t len = u16(&v[off]);
        size_t valLen = u16(&v[off + 2]);
        int type = u16(&v[off + 4]);
        if (len < 6 || off + len > end) return;
        size_t keyChars = 0;
        std::string key = utf16(&v[off + 6], (len - 6) / 2, &keyChars);
        size_t valOff = (off + 6 + keyChars * 2 + 3) & ~(size_t)3;
        if (depth == 0 && valLen >= 52 && valOff + 52 <= off + len && u32(&v[valOff]) == 0xFEEF04BD) {
            uint32_t ms = u32(&v[valOff + 8]), ls = u32(&v[valOff + 12]);
            char buf[64];
            snprintf(buf, sizeof buf, "%u.%u.%u.%u", ms >> 16, ms & 0xFFFF, ls >> 16, ls & 0xFFFF);
            fixed = buf;
        }
        if (type == 1 && depth == 3 && valLen > 0 && valOff < off + len) {
            size_t chars = std::min(valLen, (off + len - valOff) / 2);
            std::string val = utf16(&v[valOff], chars);
            while (!val.empty() && (val.back() == ' ' || val.back() == '\0')) val.pop_back();
            if (!val.empty() && !out.count(key)) out[key] = val;
        }
        size_t valBytes = type == 1 ? valLen * 2 : valLen;
        size_t child = (valOff + valBytes + 3) & ~(size_t)3;
        if (depth < 3 && child < off + len) versionBlock(v, child, off + len, depth + 1, out, fixed);
        off = (off + len + 3) & ~(size_t)3;
    }
}

bool decodeDib(const std::vector<unsigned char>& d, std::vector<unsigned char>& rgba, int& w, int& h) {
    if (d.size() < 40 || u32(&d[0]) < 40) return false;
    uint32_t hdr = u32(&d[0]);
    int32_t bw = (int32_t)u32(&d[4]);
    int32_t bh = (int32_t)u32(&d[8]) / 2;
    int bpp = u16(&d[14]);
    uint32_t comp = u32(&d[16]);
    uint32_t used = u32(&d[32]);
    if (bw <= 0 || bh <= 0 || bw > 512 || bh > 512 || comp != 0) return false;
    if (bpp != 32 && bpp != 24 && bpp != 8 && bpp != 4 && bpp != 1) return false;
    size_t palN = bpp <= 8 ? (used ? used : (1u << bpp)) : 0;
    size_t pal = hdr;
    size_t xorOff = pal + palN * 4;
    size_t xorStride = (((size_t)bw * bpp + 31) / 32) * 4;
    size_t andOff = xorOff + xorStride * bh;
    size_t andStride = (((size_t)bw + 31) / 32) * 4;
    if (xorOff + xorStride * bh > d.size()) return false;
    bool hasAnd = andOff + andStride * bh <= d.size();
    w = bw;
    h = bh;
    rgba.assign((size_t)w * h * 4, 0);
    bool anyAlpha = false;
    for (int y = 0; y < h; y++) {
        const unsigned char* row = &d[xorOff + xorStride * (h - 1 - y)];
        for (int x = 0; x < w; x++) {
            unsigned char* px = &rgba[((size_t)y * w + x) * 4];
            if (bpp == 32) {
                px[0] = row[x * 4 + 2];
                px[1] = row[x * 4 + 1];
                px[2] = row[x * 4 + 0];
                px[3] = row[x * 4 + 3];
                if (px[3]) anyAlpha = true;
            } else if (bpp == 24) {
                px[0] = row[x * 3 + 2];
                px[1] = row[x * 3 + 1];
                px[2] = row[x * 3 + 0];
                px[3] = 255;
            } else {
                int idx;
                if (bpp == 8)
                    idx = row[x];
                else if (bpp == 4)
                    idx = (row[x / 2] >> ((x & 1) ? 0 : 4)) & 15;
                else
                    idx = (row[x / 8] >> (7 - (x & 7))) & 1;
                if ((size_t)idx >= palN || pal + idx * 4 + 3 > d.size()) idx = 0;
                const unsigned char* c = &d[pal + idx * 4];
                px[0] = c[2];
                px[1] = c[1];
                px[2] = c[0];
                px[3] = 255;
            }
        }
    }
    if (bpp == 32 && anyAlpha) return true;
    if (!hasAnd) {
        for (size_t i = 3; i < rgba.size(); i += 4) rgba[i] = 255;
        return true;
    }
    for (int y = 0; y < h; y++) {
        const unsigned char* row = &d[andOff + andStride * (h - 1 - y)];
        for (int x = 0; x < w; x++) {
            bool transparent = (row[x / 8] >> (7 - (x & 7))) & 1;
            rgba[((size_t)y * w + x) * 4 + 3] = transparent ? 0 : 255;
        }
    }
    return true;
}

int paeth(int a, int b, int c) {
    int p = a + b - c;
    int pa = std::abs(p - a), pb = std::abs(p - b), pc = std::abs(p - c);
    if (pa <= pb && pa <= pc) return a;
    if (pb <= pc) return b;
    return c;
}

}

bool decodePng(const unsigned char* data, size_t size, std::vector<unsigned char>& rgba, int& w, int& h) {
    static const unsigned char sig[8] = {0x89, 'P', 'N', 'G', 13, 10, 26, 10};
    if (size < 33 || memcmp(data, sig, 8) != 0) return false;
    size_t off = 8;
    int depth = 0, color = 0, interlace = 0;
    std::vector<unsigned char> idat, plte, trns;
    w = h = 0;
    while (off + 12 <= size) {
        uint32_t len = be32(data + off);
        const unsigned char* type = data + off + 4;
        if (len > size - off - 12) return false;
        const unsigned char* body = data + off + 8;
        if (!memcmp(type, "IHDR", 4) && len >= 13) {
            w = (int)be32(body);
            h = (int)be32(body + 4);
            depth = body[8];
            color = body[9];
            interlace = body[12];
        } else if (!memcmp(type, "PLTE", 4)) {
            plte.assign(body, body + len);
        } else if (!memcmp(type, "tRNS", 4)) {
            trns.assign(body, body + len);
        } else if (!memcmp(type, "IDAT", 4)) {
            idat.insert(idat.end(), body, body + len);
        } else if (!memcmp(type, "IEND", 4)) {
            break;
        }
        off += len + 12;
    }
    if (w <= 0 || h <= 0 || w > 1024 || h > 1024 || depth != 8 || interlace != 0) return false;
    int ch;
    switch (color) {
        case 0: ch = 1; break;
        case 2: ch = 3; break;
        case 3: ch = 1; break;
        case 4: ch = 2; break;
        case 6: ch = 4; break;
        default: return false;
    }
    size_t stride = (size_t)w * ch;
    std::vector<unsigned char> raw((stride + 1) * h);
    z_stream zs;
    memset(&zs, 0, sizeof zs);
    if (inflateInit(&zs) != Z_OK) return false;
    zs.next_in = idat.data();
    zs.avail_in = (uInt)idat.size();
    zs.next_out = raw.data();
    zs.avail_out = (uInt)raw.size();
    int r = inflate(&zs, Z_FINISH);
    inflateEnd(&zs);
    if (r != Z_STREAM_END && zs.avail_out != 0) return false;
    std::vector<unsigned char> img(stride * h);
    for (int y = 0; y < h; y++) {
        const unsigned char* src = &raw[(stride + 1) * y];
        int filter = src[0];
        src++;
        unsigned char* dst = &img[stride * y];
        const unsigned char* up = y > 0 ? &img[stride * (y - 1)] : nullptr;
        for (size_t i = 0; i < stride; i++) {
            int a = i >= (size_t)ch ? dst[i - ch] : 0;
            int b = up ? up[i] : 0;
            int c = (up && i >= (size_t)ch) ? up[i - ch] : 0;
            int v = src[i];
            switch (filter) {
                case 1: v += a; break;
                case 2: v += b; break;
                case 3: v += (a + b) / 2; break;
                case 4: v += paeth(a, b, c); break;
                default: break;
            }
            dst[i] = (unsigned char)v;
        }
    }
    rgba.resize((size_t)w * h * 4);
    for (size_t i = 0; i < (size_t)w * h; i++) {
        const unsigned char* s = &img[i * ch];
        unsigned char* d = &rgba[i * 4];
        if (color == 6) {
            memcpy(d, s, 4);
        } else if (color == 2) {
            d[0] = s[0];
            d[1] = s[1];
            d[2] = s[2];
            d[3] = 255;
        } else if (color == 0) {
            d[0] = d[1] = d[2] = s[0];
            d[3] = 255;
        } else if (color == 4) {
            d[0] = d[1] = d[2] = s[0];
            d[3] = s[1];
        } else {
            size_t p = s[0];
            if (p * 3 + 2 < plte.size()) {
                d[0] = plte[p * 3];
                d[1] = plte[p * 3 + 1];
                d[2] = plte[p * 3 + 2];
            } else {
                d[0] = d[1] = d[2] = 0;
            }
            d[3] = p < trns.size() ? trns[p] : 255;
        }
    }
    return true;
}

std::string peMachineName(int m) {
    switch (m) {
        case 0x14C: return "x86 (32-bit)";
        case 0x8664: return "x64 (64-bit)";
        case 0xAA64: return "ARM64";
        case 0x1C4: return "ARM";
        default: return "Unknown";
    }
}

bool peMachineSupported(int m) { return m == 0x8664 || m == 0x14C; }

bool readPe(const std::string& path, PeInfo& out, bool wantIcon) {
    out = PeInfo();
    File f(path);
    if (!f.f) return false;
    unsigned char dos[64];
    if (!f.read(0, dos, 64) || dos[0] != 'M' || dos[1] != 'Z') return false;
    uint32_t peOff = u32(dos + 60);
    unsigned char hdr[24];
    if (!f.read(peOff, hdr, 24) || memcmp(hdr, "PE\0\0", 4) != 0) return false;
    out.machine = u16(hdr + 4);
    int nSec = u16(hdr + 6);
    int optSize = u16(hdr + 20);
    int chars = u16(hdr + 22);
    out.dll = (chars & 0x2000) != 0;
    std::vector<unsigned char> opt(optSize);
    if (optSize < 92 || !f.read(peOff + 24, opt.data(), optSize)) return false;
    bool plus = u16(&opt[0]) == 0x20B;
    out.console = u16(&opt[68]) == 3;
    size_t dirBase = plus ? 112 : 96;
    uint32_t nDirs = u32(&opt[plus ? 108 : 92]);
    uint32_t rsrcRva = 0, rsrcSize = 0;
    if (nDirs > 2 && dirBase + 3 * 8 <= (size_t)optSize) {
        rsrcRva = u32(&opt[dirBase + 16]);
        rsrcSize = u32(&opt[dirBase + 20]);
    }
    if (nDirs > 14 && dirBase + 15 * 8 <= (size_t)optSize) out.dotnet = u32(&opt[dirBase + 14 * 8]) != 0;
    std::vector<Section> secs;
    for (int i = 0; i < nSec && i < 96; i++) {
        unsigned char s[40];
        if (!f.read(peOff + 24 + optSize + i * 40, s, 40)) break;
        secs.push_back({u32(s + 12), u32(s + 8), u32(s + 20), u32(s + 16)});
    }
    out.valid = true;

    unsigned char probe[1 << 16];
    long long scan = std::min<long long>(f.size, 8LL << 20);
    for (long long pos = 0; pos < scan && out.installer.empty(); pos += sizeof(probe) - 32) {
        size_t n = (size_t)std::min<long long>(sizeof(probe), f.size - pos);
        if (!f.read(pos, probe, n)) break;
        std::string chunk((const char*)probe, n);
        if (chunk.find("Inno Setup") != std::string::npos) out.installer = "Inno Setup";
        else if (chunk.find("NullsoftInst") != std::string::npos || chunk.find("Nullsoft.NSIS") != std::string::npos) out.installer = "NSIS";
        else if (chunk.find("InstallShield") != std::string::npos) out.installer = "InstallShield";
        else if (chunk.find("WiX Toolset") != std::string::npos || chunk.find("WixBurn") != std::string::npos) out.installer = "WiX";
    }

    if (!rsrcRva || !rsrcSize || rsrcSize > 48u * 1024 * 1024) return true;
    const Section* rs = nullptr;
    for (auto& s : secs)
        if (rsrcRva >= s.va && rsrcRva < s.va + std::max(s.vsize, s.rawSize)) rs = &s;
    if (!rs) return true;
    uint32_t fileOff = rs->raw + (rsrcRva - rs->va);
    uint32_t avail = std::min<uint32_t>(rsrcSize, (uint32_t)std::max<long long>(0, f.size - fileOff));
    std::vector<unsigned char> data(avail);
    if (avail == 0 || !f.read(fileOff, data.data(), avail)) return true;
    Res res{data, rsrcRva};

    std::vector<std::pair<uint32_t, uint32_t>> types;
    if (!res.entries(0, types)) return true;
    std::map<uint32_t, uint32_t> iconById;
    uint32_t groupDir = 0, versionDir = 0, iconDir = 0;
    for (auto& t : types) {
        if (!(t.second & 0x80000000u)) continue;
        if (t.first == 3) iconDir = t.second & 0x7FFFFFFFu;
        if (t.first == 14) groupDir = t.second & 0x7FFFFFFFu;
        if (t.first == 16) versionDir = t.second & 0x7FFFFFFFu;
    }

    if (versionDir) {
        std::vector<std::pair<uint32_t, uint32_t>> names;
        std::vector<unsigned char> v;
        if (res.entries(versionDir, names) && !names.empty() && res.leaf(names[0].second, v, secs, rsrcRva) && v.size() > 6) {
            std::map<std::string, std::string> strings;
            std::string fixed;
            versionBlock(v, 0, v.size(), 0, strings, fixed);
            out.description = strings["FileDescription"];
            out.product = strings["ProductName"];
            out.company = strings["CompanyName"];
            out.copyright = strings["LegalCopyright"];
            out.version = strings.count("ProductVersion") ? strings["ProductVersion"] : strings["FileVersion"];
            if (out.version.empty()) out.version = fixed;
        }
    }

    if (!wantIcon || !groupDir || !iconDir) return true;
    std::vector<std::pair<uint32_t, uint32_t>> icons;
    if (res.entries(iconDir, icons))
        for (auto& e : icons)
            if (!(e.first & 0x80000000u)) iconById[e.first] = e.second;
    std::vector<std::pair<uint32_t, uint32_t>> groups;
    std::vector<unsigned char> grp;
    if (!res.entries(groupDir, groups) || groups.empty() || !res.leaf(groups[0].second, grp, secs, rsrcRva) || grp.size() < 6) return true;
    int count = u16(&grp[4]);
    struct Cand {
        int size, bpp, id;
    };
    std::vector<Cand> cands;
    for (int i = 0; i < count && 6 + (size_t)(i + 1) * 14 <= grp.size(); i++) {
        const unsigned char* e = &grp[6 + i * 14];
        int sz = e[0] ? e[0] : 256;
        cands.push_back({sz, u16(e + 6), u16(e + 12)});
    }
    std::sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) {
        auto score = [](const Cand& c) { return (c.size <= 128 ? c.size * 4 : 512 - c.size) + c.bpp; };
        return score(a) > score(b);
    });
    for (auto& c : cands) {
        auto it = iconById.find((uint32_t)c.id);
        if (it == iconById.end()) continue;
        std::vector<unsigned char> img;
        if (!res.leaf(it->second, img, secs, rsrcRva)) continue;
        int w = 0, h = 0;
        std::vector<unsigned char> px;
        bool ok = img.size() > 8 && img[0] == 0x89 ? decodePng(img.data(), img.size(), px, w, h) : decodeDib(img, px, w, h);
        if (ok) {
            out.icon.swap(px);
            out.iconW = w;
            out.iconH = h;
            break;
        }
    }
    return true;
}

}
