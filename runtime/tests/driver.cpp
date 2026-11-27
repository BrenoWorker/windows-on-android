#include <unistd.h>

#include <cstdio>
#include <vector>

#include "../../app/src/main/cpp/core/XClient.h"

int main(int argc, char** argv) {
    if (argc < 3) return 2;
    wd::XFrame frame;
    std::vector<unsigned char> pixels;
    double share = 0;
    for (int attempt = 0; attempt < 120 && share < 0.5; attempt++) {
        if (frame.read(argv[2], pixels)) {
            size_t red = 0, count = (size_t)frame.width * frame.height;
            for (size_t i = 0; i < count; i++)
                if (pixels[i * 4] > 200 && pixels[i * 4 + 1] < 40 && pixels[i * 4 + 2] < 40) red++;
            share = (double)red / count;
        }
        if (share < 0.5) usleep(500000);
    }
    printf("screen %dx%d, red share %.2f\n", frame.width, frame.height, share);
    if (share < 0.5) return 1;

    wd::XClient client;
    if (!client.connect(argv[1])) {
        printf("couldn't connect to the X server\n");
        return 1;
    }
    client.motion(frame.width / 2, frame.height / 2);
    client.button(1, true);
    client.button(1, false);
    client.flush();
    sleep(2);
    for (uint32_t ch : {(uint32_t)'o', (uint32_t)0xE7, (uint32_t)0xE3, (uint32_t)0xC9}) {
        client.type(ch);
        client.flush();
        usleep(700000);
    }
    sleep(2);
    return 0;
}
