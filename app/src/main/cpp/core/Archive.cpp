#include "Archive.h"

#include <sys/stat.h>
#include <zlib.h>

#include <cstdint>
#include <cstdio>
#include <clocale>
#include <cstring>
#include <vector>

#include "Fs.h"

#ifdef HAVE_UNRAR
#ifndef _UNIX
#define _UNIX
#endif
#include "dll.hpp"
#endif

namespace wd {

namespace {

uint16_t r16(const unsigned char* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
uint32_t r32(const unsigned char* p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
uint64_t r64(const unsigned char* p) { return (uint64_t)r32(p) | ((uint64_t)r32(p + 4) << 32); }

std::string lower(std::string s) {
    for (auto& c : s) c = (char)tolower((unsigned char)c);
    return s;
}

struct ZipEntry {
    std::string name;
    uint16_t flags = 0, method = 0;
    uint32_t crc = 0;
    uint64_t csize = 0, usize = 0, local = 0;
    bool dir = false;
};

struct Reader {
    FILE* f = nullptr;
    long long size = 0;
    explicit Reader(const std::string& p) {
        f = fopen(p.c_str(), "rb");
        if (f) {
            fseeko(f, 0, SEEK_END);
            size = ftello(f);
        }
    }
    ~Reader() {
        if (f) fclose(f);
    }
    bool at(long long off, void* dst, size_t n) {
        if (!f || off < 0 || off + (long long)n > size) return false;
        if (fseeko(f, off, SEEK_SET) != 0) return false;
        return fread(dst, 1, n, f) == n;
    }
};

bool zipDirectory(Reader& r, std::vector<ZipEntry>& out, std::string& err) {
    long long scan = std::min<long long>(r.size, 65557);
    std::vector<unsigned char> tail((size_t)scan);
    if (scan < 22 || !r.at(r.size - scan, tail.data(), (size_t)scan)) {
        err = "This file isn't a valid ZIP archive.";
        return false;
    }
    long long eocd = -1;
    for (long long i = scan - 22; i >= 0; i--)
        if (r32(&tail[(size_t)i]) == 0x06054b50) {
            eocd = i;
            break;
        }
    if (eocd < 0) {
        err = "This file isn't a valid ZIP archive.";
        return false;
    }
    const unsigned char* e = &tail[(size_t)eocd];
    uint64_t count = r16(e + 10);
    uint64_t cdSize = r32(e + 12);
    uint64_t cdOff = r32(e + 16);
    long long eocdAbs = r.size - scan + eocd;
    if (count == 0xFFFF || cdOff == 0xFFFFFFFFu || cdSize == 0xFFFFFFFFu) {
        unsigned char loc[20];
        if (eocdAbs >= 20 && r.at(eocdAbs - 20, loc, 20) && r32(loc) == 0x07064b50) {
            unsigned char z[56];
            if (r.at((long long)r64(loc + 8), z, 56) && r32(z) == 0x06064b50) {
                count = r64(z + 32);
                cdSize = r64(z + 40);
                cdOff = r64(z + 48);
            }
        }
    }
    if (cdSize > (uint64_t)r.size || cdSize > (256u << 20) || cdOff + cdSize > (uint64_t)r.size) {
        err = "The ZIP archive is damaged.";
        return false;
    }
    std::vector<unsigned char> cd((size_t)cdSize);
    if (!r.at((long long)cdOff, cd.data(), cd.size())) {
        err = "The ZIP archive is damaged.";
        return false;
    }
    size_t p = 0;
    for (uint64_t i = 0; i < count && p + 46 <= cd.size(); i++) {
        if (r32(&cd[p]) != 0x02014b50) break;
        ZipEntry z;
        z.flags = r16(&cd[p + 8]);
        z.method = r16(&cd[p + 10]);
        z.crc = r32(&cd[p + 16]);
        z.csize = r32(&cd[p + 20]);
        z.usize = r32(&cd[p + 24]);
        size_t nl = r16(&cd[p + 28]), xl = r16(&cd[p + 30]), cl = r16(&cd[p + 32]);
        z.local = r32(&cd[p + 42]);
        if (p + 46 + nl + xl + cl > cd.size()) break;
        z.name.assign((const char*)&cd[p + 46], nl);
        size_t x = p + 46 + nl, xe = x + xl;
        while (x + 4 <= xe) {
            uint16_t id = r16(&cd[x]), sz = r16(&cd[x + 2]);
            if (id == 1) {
                size_t q = x + 4;
                if (z.usize == 0xFFFFFFFFu && q + 8 <= x + 4 + sz) {
                    z.usize = r64(&cd[q]);
                    q += 8;
                }
                if (z.csize == 0xFFFFFFFFu && q + 8 <= x + 4 + sz) {
                    z.csize = r64(&cd[q]);
                    q += 8;
                }
                if (z.local == 0xFFFFFFFFu && q + 8 <= x + 4 + sz) z.local = r64(&cd[q]);
            }
            x += 4 + sz;
        }
        for (auto& c : z.name)
            if (c == '\\') c = '/';
        z.dir = !z.name.empty() && z.name.back() == '/';
        out.push_back(z);
        p += 46 + nl + xl + cl;
    }
    return true;
}

bool safeRelative(const std::string& name, std::string& out) {
    std::string clean;
    size_t i = 0;
    while (i <= name.size()) {
        size_t j = name.find('/', i);
        if (j == std::string::npos) j = name.size();
        std::string part = name.substr(i, j - i);
        i = j + 1;
        if (part.empty() || part == ".") continue;
        if (part == ".." || part.find(':') != std::string::npos) return false;
        if (!clean.empty()) clean += '/';
        clean += part;
    }
    out = clean;
    return !clean.empty();
}

struct ZipCrypto {
    uint32_t k0 = 0x12345678, k1 = 0x23456789, k2 = 0x34567890;
    void update(unsigned char c) {
        k0 = crcByte(k0, c);
        k1 = (k1 + (k0 & 0xFF)) * 134775813u + 1;
        unsigned char b = (unsigned char)(k1 >> 24);
        k2 = crcByte(k2, b);
    }
    static uint32_t crcByte(uint32_t crc, unsigned char b) {
        static uint32_t table[256];
        static bool init = false;
        if (!init) {
            for (uint32_t n = 0; n < 256; n++) {
                uint32_t c = n;
                for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
                table[n] = c;
            }
            init = true;
        }
        return table[(crc ^ b) & 0xFF] ^ (crc >> 8);
    }
    unsigned char decrypt(unsigned char c) {
        uint16_t t = (uint16_t)(k2 | 2);
        unsigned char p = c ^ (unsigned char)((t * (t ^ 1)) >> 8);
        update(p);
        return p;
    }
    void init(const std::string& pw) {
        k0 = 0x12345678;
        k1 = 0x23456789;
        k2 = 0x34567890;
        for (unsigned char c : pw) update(c);
    }
};

bool extractZip(const std::string& path, const std::string& dest, const std::string& password, ArchiveJob& job) {
    Reader r(path);
    if (!r.f) {
        job.setError("Couldn't open the archive.");
        job.state = ArchiveState::Failed;
        return false;
    }
    std::vector<ZipEntry> entries;
    std::string err;
    if (!zipDirectory(r, entries, err)) {
        job.setError(err);
        job.state = ArchiveState::Failed;
        return false;
    }
    long long total = 0;
    bool needPw = false;
    for (auto& z : entries) {
        total += (long long)z.usize;
        if (z.flags & 1) needPw = true;
    }
    job.total = total;
    if (needPw && password.empty()) {
        job.state = ArchiveState::NeedPassword;
        return false;
    }
    fs::makeDirs(dest);
    std::vector<unsigned char> in(1 << 16), out(1 << 16);
    for (auto& z : entries) {
        if (job.cancel) {
            job.state = ArchiveState::Cancelled;
            return false;
        }
        std::string rel;
        if (!safeRelative(z.name, rel)) continue;
        std::string target = fs::join(dest, rel);
        job.setCurrent(rel);
        if (z.dir) {
            fs::makeDirs(target);
            continue;
        }
        if (z.method == 99) {
            job.setError("This ZIP uses AES encryption, which isn't supported yet.");
            job.state = ArchiveState::Failed;
            return false;
        }
        if (z.method != 0 && z.method != 8) {
            job.setError("'" + rel + "' uses a compression method that isn't supported.");
            job.state = ArchiveState::Failed;
            return false;
        }
        unsigned char lh[30];
        if (!r.at((long long)z.local, lh, 30) || r32(lh) != 0x04034b50) {
            job.setError("The ZIP archive is damaged.");
            job.state = ArchiveState::Failed;
            return false;
        }
        long long data = (long long)z.local + 30 + r16(lh + 26) + r16(lh + 28);
        uint64_t left = z.csize;
        ZipCrypto zc;
        if (z.flags & 1) {
            zc.init(password);
            unsigned char hdr[12];
            if (left < 12 || !r.at(data, hdr, 12)) {
                job.setError("The ZIP archive is damaged.");
                job.state = ArchiveState::Failed;
                return false;
            }
            unsigned char last = 0;
            for (int i = 0; i < 12; i++) last = zc.decrypt(hdr[i]);
            unsigned char check = (z.flags & 8) ? (unsigned char)(r16(lh + 10) >> 8) : (unsigned char)(z.crc >> 24);
            if (last != check) {
                job.state = ArchiveState::WrongPassword;
                return false;
            }
            data += 12;
            left -= 12;
        }
        fs::makeDirs(fs::parent(target));
        FILE* o = fopen(target.c_str(), "wb");
        if (!o) {
            job.setError("Couldn't create '" + rel + "'.");
            job.state = ArchiveState::Failed;
            return false;
        }
        z_stream zs;
        memset(&zs, 0, sizeof zs);
        if (z.method == 8) inflateInit2(&zs, -MAX_WBITS);
        uLong crc = crc32(0, nullptr, 0);
        bool ok = true;
        long long pos = data;
        bool finished = false;
        while ((left > 0 || (z.method == 8 && zs.avail_in > 0)) && ok && !finished) {
            if (job.cancel) {
                ok = false;
                break;
            }
            size_t n = (size_t)std::min<uint64_t>(left, in.size());
            if (n > 0) {
                if (!r.at(pos, in.data(), n)) {
                    ok = false;
                    break;
                }
                if (z.flags & 1)
                    for (size_t i = 0; i < n; i++) in[i] = zc.decrypt(in[i]);
                pos += (long long)n;
                left -= n;
            }
            if (z.method == 0) {
                if (fwrite(in.data(), 1, n, o) != n) ok = false;
                crc = crc32(crc, in.data(), (uInt)n);
                job.done += (long long)n;
                continue;
            }
            zs.next_in = in.data();
            zs.avail_in = (uInt)n;
            do {
                zs.next_out = out.data();
                zs.avail_out = (uInt)out.size();
                int ret = inflate(&zs, Z_NO_FLUSH);
                if (ret != Z_OK && ret != Z_STREAM_END && ret != Z_BUF_ERROR) {
                    ok = false;
                    break;
                }
                size_t produced = out.size() - zs.avail_out;
                if (produced && fwrite(out.data(), 1, produced, o) != produced) ok = false;
                crc = crc32(crc, out.data(), (uInt)produced);
                job.done += (long long)produced;
                if (ret == Z_STREAM_END) {
                    finished = true;
                    break;
                }
                if (ret == Z_BUF_ERROR && zs.avail_in == 0) break;
            } while (zs.avail_out == 0 && ok);
        }
        if (z.method == 8) inflateEnd(&zs);
        fclose(o);
        if (job.cancel) {
            remove(target.c_str());
            job.state = ArchiveState::Cancelled;
            return false;
        }
        if (!ok || crc != z.crc) {
            remove(target.c_str());
            if ((z.flags & 1) && crc != z.crc) {
                job.state = ArchiveState::WrongPassword;
                return false;
            }
            job.setError("'" + rel + "' is damaged (CRC error).");
            job.state = ArchiveState::Failed;
            return false;
        }
        job.files++;
    }
    job.state = ArchiveState::Done;
    return true;
}

#ifdef HAVE_UNRAR
struct RarCtx {
    ArchiveJob* job;
    std::string password;
    bool askedPassword = false;
};

int CALLBACK rarCallback(UINT msg, LPARAM user, LPARAM p1, LPARAM p2) {
    RarCtx* ctx = (RarCtx*)user;
    switch (msg) {
        case UCM_PROCESSDATA:
            if (ctx->job) ctx->job->done += (long long)p2;
            return ctx->job && ctx->job->cancel ? -1 : 1;
        case UCM_NEEDPASSWORD:
            ctx->askedPassword = true;
            if (ctx->password.empty()) return -1;
            strncpy((char*)p1, ctx->password.c_str(), (size_t)p2 - 1);
            ((char*)p1)[p2 - 1] = 0;
            return 1;
        case UCM_NEEDPASSWORDW: {
            ctx->askedPassword = true;
            if (ctx->password.empty()) return -1;
            wchar_t* w = (wchar_t*)p1;
            size_t n = 0;
            const std::string& s = ctx->password;
            for (size_t i = 0; i < s.size() && n + 1 < (size_t)p2;) {
                unsigned char c = (unsigned char)s[i];
                uint32_t cp = c;
                int len = 1;
                if (c >= 0xF0 && i + 3 < s.size()) {
                    cp = ((c & 7) << 18) | ((s[i + 1] & 0x3F) << 12) | ((s[i + 2] & 0x3F) << 6) | (s[i + 3] & 0x3F);
                    len = 4;
                } else if (c >= 0xE0 && i + 2 < s.size()) {
                    cp = ((c & 15) << 12) | ((s[i + 1] & 0x3F) << 6) | (s[i + 2] & 0x3F);
                    len = 3;
                } else if (c >= 0xC0 && i + 1 < s.size()) {
                    cp = ((c & 31) << 6) | (s[i + 1] & 0x3F);
                    len = 2;
                }
                w[n++] = (wchar_t)cp;
                i += len;
            }
            w[n] = 0;
            return 1;
        }
        case UCM_CHANGEVOLUME:
        case UCM_CHANGEVOLUMEW:
            return p2 == RAR_VOL_ASK ? -1 : 1;
        case UCM_LARGEDICT:
            return 1;
        default:
            return 0;
    }
}

int rarOpen(const std::string& path, unsigned mode, RarCtx& ctx, HANDLE& h, unsigned* flags = nullptr) {
    RAROpenArchiveDataEx d;
    memset(&d, 0, sizeof d);
    std::vector<char> name(path.begin(), path.end());
    name.push_back(0);
    d.ArcName = name.data();
    d.OpenMode = mode;
    d.Callback = rarCallback;
    d.UserData = (LPARAM)&ctx;
    h = RAROpenArchiveEx(&d);
    if (flags) *flags = d.Flags;
    if (!ctx.password.empty() && h) RARSetPassword(h, (char*)ctx.password.c_str());
    return (int)d.OpenResult;
}

std::string rarError(int code) {
    switch (code) {
        case ERAR_NO_MEMORY: return "Not enough memory to extract this archive.";
        case ERAR_BAD_DATA: return "The archive is damaged (CRC error).";
        case ERAR_BAD_ARCHIVE: return "This file isn't a valid RAR archive.";
        case ERAR_UNKNOWN_FORMAT: return "Unknown archive format.";
        case ERAR_EOPEN: return "Couldn't open the archive or one of its volumes.";
        case ERAR_ECREATE: return "Couldn't create a file in the destination folder.";
        case ERAR_ECLOSE: return "Couldn't close a file.";
        case ERAR_EREAD: return "Couldn't read the archive.";
        case ERAR_EWRITE: return "Couldn't write to the destination folder.";
        case ERAR_UNKNOWN: return "The archive contains unsafe or unsupported items and was not extracted.";
        case ERAR_LARGE_DICT: return "This archive needs more memory than is available.";
        default: return "The archive couldn't be extracted (error " + std::to_string(code) + ").";
    }
}

bool extractRar(const std::string& path, const std::string& dest, const std::string& password, ArchiveJob& job) {
    static bool localeSet = false;
    if (!localeSet) {
        localeSet = true;
        if (!setlocale(LC_CTYPE, "C.UTF-8")) setlocale(LC_CTYPE, "en_US.UTF-8");
    }
    RarCtx ctx{&job, password};
    HANDLE h = nullptr;
    long long total = 0;
    int rc = rarOpen(path, RAR_OM_LIST, ctx, h);
    if (rc == ERAR_SUCCESS && h) {
        RARHeaderDataEx hd;
        memset(&hd, 0, sizeof hd);
        int r;
        while ((r = RARReadHeaderEx(h, &hd)) == ERAR_SUCCESS) {
            total += ((long long)hd.UnpSizeHigh << 32) | hd.UnpSize;
            if (RARProcessFile(h, RAR_SKIP, nullptr, nullptr) != ERAR_SUCCESS) break;
        }
        RARCloseArchive(h);
        if (r == ERAR_MISSING_PASSWORD || (r == ERAR_BAD_PASSWORD && password.empty())) {
            job.state = ArchiveState::NeedPassword;
            return false;
        }
        if (r == ERAR_BAD_PASSWORD) {
            job.state = ArchiveState::WrongPassword;
            return false;
        }
    } else if (rc == ERAR_MISSING_PASSWORD) {
        job.state = ArchiveState::NeedPassword;
        return false;
    } else if (rc == ERAR_BAD_PASSWORD) {
        job.state = ArchiveState::WrongPassword;
        return false;
    }
    job.total = total;
    ctx.askedPassword = false;
    rc = rarOpen(path, RAR_OM_EXTRACT, ctx, h);
    if (rc != ERAR_SUCCESS || !h) {
        if (rc == ERAR_MISSING_PASSWORD) job.state = password.empty() ? ArchiveState::NeedPassword : ArchiveState::WrongPassword;
        else if (rc == ERAR_BAD_PASSWORD) job.state = ArchiveState::WrongPassword;
        else {
            job.setError(rarError(rc));
            job.state = ArchiveState::Failed;
        }
        return false;
    }
    fs::makeDirs(dest);
    std::vector<wchar_t> destBuf;
    for (size_t i = 0; i < dest.size();) {
        unsigned char c = (unsigned char)dest[i];
        uint32_t cp = c;
        int len = 1;
        if (c >= 0xF0 && i + 3 < dest.size()) {
            cp = ((c & 7) << 18) | ((dest[i + 1] & 0x3F) << 12) | ((dest[i + 2] & 0x3F) << 6) | (dest[i + 3] & 0x3F);
            len = 4;
        } else if (c >= 0xE0 && i + 2 < dest.size()) {
            cp = ((c & 15) << 12) | ((dest[i + 1] & 0x3F) << 6) | (dest[i + 2] & 0x3F);
            len = 3;
        } else if (c >= 0xC0 && i + 1 < dest.size()) {
            cp = ((c & 31) << 6) | (dest[i + 1] & 0x3F);
            len = 2;
        }
        destBuf.push_back((wchar_t)cp);
        i += len;
    }
    destBuf.push_back(0);
    RARHeaderDataEx hd;
    memset(&hd, 0, sizeof hd);
    int r;
    bool ok = true;
    while ((r = RARReadHeaderEx(h, &hd)) == ERAR_SUCCESS) {
        job.setCurrent(hd.FileName);
        int pr = RARProcessFileW(h, RAR_EXTRACT, destBuf.data(), nullptr);
        if (pr != ERAR_SUCCESS) {
            r = pr;
            ok = false;
            break;
        }
        if (!(hd.Flags & RHDF_DIRECTORY)) job.files++;
        if (job.cancel) break;
    }
    RARCloseArchive(h);
    if (job.cancel) {
        job.state = ArchiveState::Cancelled;
        return false;
    }
    if (!ok || (r != ERAR_END_ARCHIVE && r != ERAR_SUCCESS)) {
        if (r == ERAR_MISSING_PASSWORD) {
            job.state = password.empty() ? ArchiveState::NeedPassword : ArchiveState::WrongPassword;
        } else if (r == ERAR_BAD_PASSWORD || (r == ERAR_BAD_DATA && !password.empty() && ctx.askedPassword)) {
            job.state = ArchiveState::WrongPassword;
        } else if (r == ERAR_EOPEN) {
            job.setError("A volume of this multi-part archive is missing.");
            job.state = ArchiveState::Failed;
        } else {
            job.setError(rarError(r));
            job.state = ArchiveState::Failed;
        }
        return false;
    }
    job.state = ArchiveState::Done;
    return true;
}
#endif

}

bool isArchive(const std::string& path) {
    std::string e = lower(fs::extension(path));
    return e == "zip" || e == "rar" || e == "cbz" || e == "cbr" || e == "jar" || e == "apk";
}

bool rarSupported() {
#ifdef HAVE_UNRAR
    return true;
#else
    return false;
#endif
}

static bool isRar(const std::string& path) {
    unsigned char sig[7] = {0};
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    size_t n = fread(sig, 1, 7, f);
    fclose(f);
    return n >= 6 && memcmp(sig, "Rar!\x1a\x07", 6) == 0;
}

bool archiveInfo(const std::string& path, const std::string& password, ArchiveInfo& out) {
    out = ArchiveInfo();
    if (isRar(path)) {
        out.format = "RAR";
#ifdef HAVE_UNRAR
        RarCtx ctx{nullptr, password};
        HANDLE h = nullptr;
        unsigned flags = 0;
        int rc = rarOpen(path, RAR_OM_LIST, ctx, h, &flags);
        if (rc != ERAR_SUCCESS || !h) {
            if (rc == ERAR_MISSING_PASSWORD || rc == ERAR_BAD_PASSWORD) {
                out.encrypted = out.encryptedNames = true;
                return true;
            }
            out.error = rarError(rc);
            return false;
        }
        if (flags & ROADF_ENCHEADERS) out.encryptedNames = out.encrypted = true;
        RARHeaderDataEx hd;
        memset(&hd, 0, sizeof hd);
        int r;
        while ((r = RARReadHeaderEx(h, &hd)) == ERAR_SUCCESS) {
            if (!(hd.Flags & RHDF_DIRECTORY)) out.files++;
            out.size += ((long long)hd.UnpSizeHigh << 32) | hd.UnpSize;
            if (hd.Flags & RHDF_ENCRYPTED) out.encrypted = true;
            if (RARProcessFile(h, RAR_SKIP, nullptr, nullptr) != ERAR_SUCCESS) break;
        }
        RARCloseArchive(h);
        if (r == ERAR_MISSING_PASSWORD || r == ERAR_BAD_PASSWORD) out.encrypted = out.encryptedNames = true;
        return true;
#else
        out.error = "RAR support isn't included in this build.";
        return false;
#endif
    }
    out.format = "ZIP";
    Reader r(path);
    std::vector<ZipEntry> entries;
    if (!r.f || !zipDirectory(r, entries, out.error)) return false;
    for (auto& z : entries) {
        if (!z.dir) out.files++;
        out.size += (long long)z.usize;
        if (z.flags & 1) out.encrypted = true;
    }
    return true;
}

bool extractArchiveSync(const std::string& path, const std::string& dest, const std::string& password, ArchiveJob& job) {
    if (isRar(path)) {
#ifdef HAVE_UNRAR
        return extractRar(path, dest, password, job);
#else
        job.setError("RAR support isn't included in this build.");
        job.state = ArchiveState::Failed;
        return false;
#endif
    }
    return extractZip(path, dest, password, job);
}

std::shared_ptr<ArchiveJob> extractArchive(const std::string& path, const std::string& dest, const std::string& password) {
    auto job = std::make_shared<ArchiveJob>();
    std::thread([job, path, dest, password]() { extractArchiveSync(path, dest, password, *job); }).detach();
    return job;
}

}
