#include <stdio.h>
#include <string.h>

int main(void) {
    char line[128];
    printf("bits=%d\n", (int)sizeof(void*) * 8);
    printf("ready\n");
    fflush(stdout);
    if (fgets(line, sizeof line, stdin)) {
        line[strcspn(line, "\r\n")] = 0;
        printf("got:%s\n", line);
    }
    fflush(stdout);
    return 7;
}
