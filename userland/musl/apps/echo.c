#include <unistd.h>

int main(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        if (i > 1) write(1, " ", 1);
        const char *s = argv[i];
        size_t len = 0;
        while (s[len]) len++;
        write(1, s, len);
    }
    write(1, "\n", 1);
    return 0;
}
