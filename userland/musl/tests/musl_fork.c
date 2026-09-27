#include <unistd.h>
#include <stdio.h>

int main(void) {
    write(1, "A\n", 2);

    int pid = fork();

    if (pid == 0) {
        write(1, "C\n", 2);
        _exit(0);
    } else if (pid > 0) {
        write(1, "P\n", 2);
        return 0;
    } else {
        write(1, "F\n", 2);
        return 1;
    }
}
