#include <unistd.h>

int main(void) {
    write(1, "MUSL-START\n", 11);
    return 0;
}
