/* sigterm_ignore.c -- does SIG_IGN through rt_sigaction survive?
 *
 * Installs SIG_IGN for SIGTERM, raises SIGTERM against itself,
 * and prints whether it is still running.
 */
#include <signal.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

int main(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = SIG_IGN;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    if (sigaction(SIGTERM, &sa, NULL) != 0) {
        printf("SIGTERM-IGNORE: sigaction failed: %s\n", strerror(errno));
        return 2;
    }

    printf("SIGTERM-IGNORE: installed SIG_IGN for SIGTERM\n");
    fflush(stdout);

    if (kill(getpid(), SIGTERM) != 0) {
        printf("SIGTERM-IGNORE: kill failed: %s\n", strerror(errno));
        return 2;
    }

    printf("SIGTERM-IGNORE: survived kill(SIGTERM)\n");
    return 0;
}
