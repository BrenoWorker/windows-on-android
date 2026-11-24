#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace wd {

class XClient {
public:
    ~XClient();
    bool connect(const std::string& socketPath);
    void close();
    bool connected() const { return fd_ >= 0; }
    int width() const { return width_; }
    int height() const { return height_; }
    void motion(int x, int y);
    void button(int b, bool down);
    void key(uint32_t keysym, bool down);
    void type(uint32_t codepoint);
    void flush();

private:
    int fd_ = -1;
    uint32_t root_ = 0;
    uint8_t xtest_ = 0;
    int width_ = 0, height_ = 0;
    int minKey_ = 8, maxKey_ = 255;
    uint16_t seq_ = 0;
    std::map<uint32_t, std::pair<int, bool>> keys_;
    int shift_ = 0;
    std::vector<unsigned char> out_;

    bool writeAll(const unsigned char* p, size_t n);
    bool readAll(unsigned char* p, size_t n, int timeoutMs);
    bool readReply(std::vector<unsigned char>& reply, int timeoutMs);
    void fake(int type, int detail, int x, int y);
    bool loadKeymap();
};

struct XFrame {
    int width = 0, height = 0;
    bool read(const std::string& path, std::vector<unsigned char>& rgba);
};

}
