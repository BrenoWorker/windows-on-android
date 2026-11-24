#include "XClient.h"

#include <fcntl.h>
#include <poll.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

namespace wd {

namespace {

uint16_t le16(const unsigned char* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
uint32_t le32(const unsigned char* p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
uint32_t be32(const unsigned char* p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }

void put16(std::vector<unsigned char>& v, uint16_t x) {
    v.push_back(x & 0xFF);
    v.push_back(x >> 8);
}

void put32(std::vector<unsigned char>& v, uint32_t x) {
    for (int i = 0; i < 4; i++) v.push_back((x >> (i * 8)) & 0xFF);
}

const uint32_t kShiftL = 0xFFE1;

}

XClient::~XClient() { close(); }

void XClient::close() {
    if (fd_ >= 0) ::close(fd_);
    fd_ = -1;
    keys_.clear();
    out_.clear();
}

bool XClient::writeAll(const unsigned char* p, size_t n) {
    while (n > 0) {
        ssize_t w = ::send(fd_, p, n, MSG_NOSIGNAL);
        if (w < 0 && errno == EINTR) continue;
        if (w < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            pollfd pf{fd_, POLLOUT, 0};
            if (poll(&pf, 1, 1000) <= 0) return false;
            continue;
        }
        if (w <= 0) return false;
        p += w;
        n -= (size_t)w;
    }
    return true;
}

bool XClient::readAll(unsigned char* p, size_t n, int timeoutMs) {
    while (n > 0) {
        pollfd pf{fd_, POLLIN, 0};
        if (poll(&pf, 1, timeoutMs) <= 0) return false;
        ssize_t r = ::recv(fd_, p, n, 0);
        if (r < 0 && (errno == EINTR || errno == EAGAIN)) continue;
        if (r <= 0) return false;
        p += r;
        n -= (size_t)r;
    }
    return true;
}

bool XClient::readReply(std::vector<unsigned char>& reply, int timeoutMs) {
    while (true) {
        unsigned char head[32];
        if (!readAll(head, 32, timeoutMs)) return false;
        if (head[0] == 0) return false;
        if (head[0] != 1) continue;
        uint32_t extra = le32(head + 4) * 4;
        reply.assign(head, head + 32);
        if (extra > 0) {
            if (extra > (64u << 20)) return false;
            reply.resize(32 + extra);
            if (!readAll(reply.data() + 32, extra, timeoutMs)) return false;
        }
        return true;
    }
}

bool XClient::connect(const std::string& socketPath) {
    close();
    fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd_ < 0) return false;
    sockaddr_un addr;
    memset(&addr, 0, sizeof addr);
    addr.sun_family = AF_UNIX;
    if (socketPath.size() >= sizeof addr.sun_path) {
        close();
        return false;
    }
    memcpy(addr.sun_path, socketPath.c_str(), socketPath.size());
    if (::connect(fd_, (sockaddr*)&addr, sizeof addr) != 0) {
        close();
        return false;
    }
    std::vector<unsigned char> req = {0x6C, 0};
    put16(req, 11);
    put16(req, 0);
    put16(req, 0);
    put16(req, 0);
    put16(req, 0);
    unsigned char head[8];
    if (!writeAll(req.data(), req.size()) || !readAll(head, 8, 3000) || head[0] != 1) {
        close();
        return false;
    }
    size_t extra = (size_t)le16(head + 6) * 4;
    std::vector<unsigned char> d(extra);
    if (extra < 40 || !readAll(d.data(), extra, 3000)) {
        close();
        return false;
    }
    size_t vendorLen = le16(&d[16]);
    int nFormats = d[21];
    minKey_ = d[26];
    maxKey_ = d[27];
    size_t screen = 32 + ((vendorLen + 3) & ~(size_t)3) + 8 * (size_t)nFormats;
    if (screen + 40 > d.size()) {
        close();
        return false;
    }
    root_ = le32(&d[screen]);
    width_ = le16(&d[screen + 20]);
    height_ = le16(&d[screen + 22]);
    seq_ = 0;

    const char* name = "XTEST";
    size_t nl = strlen(name);
    std::vector<unsigned char> q = {98, 0};
    put16(q, (uint16_t)(2 + (nl + 3) / 4));
    put16(q, (uint16_t)nl);
    put16(q, 0);
    q.insert(q.end(), name, name + nl);
    while (q.size() % 4) q.push_back(0);
    seq_++;
    std::vector<unsigned char> reply;
    if (!writeAll(q.data(), q.size()) || !readReply(reply, 3000) || !reply[8]) {
        close();
        return false;
    }
    xtest_ = reply[9];
    if (!loadKeymap()) {
        close();
        return false;
    }
    int fl = fcntl(fd_, F_GETFL);
    fcntl(fd_, F_SETFL, fl | O_NONBLOCK);
    return true;
}

bool XClient::loadKeymap() {
    int count = maxKey_ - minKey_ + 1;
    if (count <= 0) return false;
    std::vector<unsigned char> q = {101, 0};
    put16(q, 2);
    q.push_back((unsigned char)minKey_);
    q.push_back((unsigned char)count);
    put16(q, 0);
    seq_++;
    std::vector<unsigned char> r;
    if (!writeAll(q.data(), q.size()) || !readReply(r, 3000)) return false;
    int per = r[1];
    if (per <= 0) return false;
    keys_.clear();
    for (int i = 0; i < count; i++) {
        for (int j = 0; j < per && j < 2; j++) {
            size_t off = 32 + ((size_t)i * per + j) * 4;
            if (off + 4 > r.size()) break;
            uint32_t sym = le32(&r[off]);
            if (!sym) continue;
            if (!keys_.count(sym)) keys_[sym] = {minKey_ + i, j == 1};
        }
    }
    auto it = keys_.find(kShiftL);
    shift_ = it != keys_.end() ? it->second.first : 0;
    return true;
}

void XClient::fake(int type, int detail, int x, int y) {
    if (fd_ < 0) return;
    out_.push_back(xtest_);
    out_.push_back(2);
    put16(out_, 9);
    out_.push_back((unsigned char)type);
    out_.push_back((unsigned char)detail);
    put16(out_, 0);
    put32(out_, 0);
    put32(out_, type == 6 ? root_ : 0);
    put32(out_, 0);
    put32(out_, 0);
    put16(out_, (uint16_t)(int16_t)x);
    put16(out_, (uint16_t)(int16_t)y);
    put32(out_, 0);
    put16(out_, 0);
    out_.push_back(0);
    out_.push_back(0);
    seq_++;
    if (out_.size() > 4096) flush();
}

void XClient::motion(int x, int y) { fake(6, 0, x, y); }

void XClient::button(int b, bool down) { fake(down ? 4 : 5, b, 0, 0); }

void XClient::key(uint32_t keysym, bool down) {
    auto it = keys_.find(keysym);
    if (it == keys_.end()) return;
    fake(down ? 2 : 3, it->second.first, 0, 0);
}

void XClient::type(uint32_t cp) {
    uint32_t sym = cp;
    if (cp == '\n' || cp == '\r') sym = 0xFF0D;
    else if (cp == '\t') sym = 0xFF09;
    else if (cp == 8) sym = 0xFF08;
    else if (cp >= 0x100) sym = 0x01000000 | cp;
    auto it = keys_.find(sym);
    if (it == keys_.end()) return;
    bool shifted = it->second.second && shift_;
    if (shifted) fake(2, shift_, 0, 0);
    fake(2, it->second.first, 0, 0);
    fake(3, it->second.first, 0, 0);
    if (shifted) fake(3, shift_, 0, 0);
}

void XClient::flush() {
    if (fd_ < 0) return;
    if (!out_.empty()) {
        if (!writeAll(out_.data(), out_.size())) {
            close();
            return;
        }
        out_.clear();
    }
    unsigned char buf[1024];
    while (true) {
        ssize_t r = ::recv(fd_, buf, sizeof buf, MSG_DONTWAIT);
        if (r > 0) continue;
        if (r == 0) close();
        break;
    }
}

bool XFrame::read(const std::string& path, std::vector<unsigned char>& rgba) {
    int fd = open(path.c_str(), O_RDONLY);
    if (fd < 0) return false;
    struct stat st;
    if (fstat(fd, &st) != 0 || st.st_size < 100) {
        ::close(fd);
        return false;
    }
    size_t size = (size_t)st.st_size;
    void* map = mmap(nullptr, size, PROT_READ, MAP_SHARED, fd, 0);
    ::close(fd);
    if (map == MAP_FAILED) return false;
    const unsigned char* p = (const unsigned char*)map;
    uint32_t headerSize = be32(p);
    uint32_t version = be32(p + 4);
    int w = (int)be32(p + 16), h = (int)be32(p + 20);
    uint32_t byteOrder = be32(p + 28);
    uint32_t bpp = be32(p + 44);
    uint32_t stride = be32(p + 48);
    uint32_t redMask = be32(p + 56);
    uint32_t ncolors = be32(p + 76);
    size_t data = (size_t)headerSize + (size_t)ncolors * 12;
    bool ok = version == 7 && bpp == 32 && w > 0 && h > 0 && w <= 8192 && h <= 8192 && stride >= (uint32_t)w * 4 &&
              data + (size_t)stride * h <= size;
    if (ok) {
        width = w;
        height = h;
        rgba.resize((size_t)w * h * 4);
        bool redHigh = redMask == 0xFF0000;
        for (int y = 0; y < h; y++) {
            const unsigned char* s = p + data + (size_t)stride * y;
            unsigned char* d = &rgba[(size_t)y * w * 4];
            for (int x = 0; x < w; x++, s += 4, d += 4) {
                uint32_t v = byteOrder == 0 ? le32(s) : be32(s);
                uint32_t r = redHigh ? (v >> 16) & 0xFF : v & 0xFF;
                uint32_t b = redHigh ? v & 0xFF : (v >> 16) & 0xFF;
                d[0] = (unsigned char)r;
                d[1] = (unsigned char)((v >> 8) & 0xFF);
                d[2] = (unsigned char)b;
                d[3] = 255;
            }
        }
    }
    munmap(map, size);
    return ok;
}

}
