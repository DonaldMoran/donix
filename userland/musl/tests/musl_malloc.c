#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void)
{
    /* Large allocation: usually mmap() backed */
    char *p = malloc(256 * 1024);

    if (!p) {
        printf("MALLOC-FAIL\n");
        return 1;
    }

    strcpy(p, "MALLOC-OK");
    printf("%s\n", p);
    free(p);

    /* Small allocation: usually brk() backed */
    char *q = malloc(1024);

    if (!q) {
        printf("SMALL-FAIL\n");
        return 1;
    }

    strcpy(q, "SMALL-OK");
    printf("%s\n", q);
    free(q);

    return 0;
}
