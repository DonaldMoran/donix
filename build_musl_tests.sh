#!/usr/bin/env bash

set -euo pipefail

echo "=================================================================="
echo "Building musl test binaries"
echo "=================================================================="

# -------------------------------------------------------------------
# Test 1: Minimal write()
# -------------------------------------------------------------------

cat > /tmp/musl_min.c <<'EOF'
#include <unistd.h>

int main(void) {
    write(1, "MUSL-START\n", 11);
    return 0;
}
EOF

echo "[BUILD] musl_min"

musl-gcc \
    -static \
    -O2 \
    -o /tmp/musl_min \
    /tmp/musl_min.c

# -------------------------------------------------------------------
# Test 2: printf()
# -------------------------------------------------------------------

cat > /tmp/musl_printf.c <<'EOF'
#include <stdio.h>
int main(void) {
    printf("MUSL-PRINTF\n");
    return 0;
}
EOF

echo "[BUILD] musl_printf"

musl-gcc \
    -static \
    -no-pie \
    -O2 \
    -mcmodel=large \
    -o /tmp/musl_printf \
    /tmp/musl_printf.c

# -------------------------------------------------------------------
# Test 3: malloc()
# -------------------------------------------------------------------

cat > /tmp/musl_malloc.c <<'EOF'
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
EOF

echo "[BUILD] musl_malloc"

musl-gcc \
    -static \
    -no-pie \
    -O2 \
    -mcmodel=large \
    -o /tmp/musl_malloc \
    /tmp/musl_malloc.c

# -------------------------------------------------------------------
# Test 4: MUSL_TWOMMAP()
# -------------------------------------------------------------------

cat > /tmp/musl_twommap.c<<EOF
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    char* a = malloc(256 * 1024);
    char* b = malloc(256 * 1024);
    if (!a || !b) { printf("ALLOC-FAIL\n"); return 1; }
    if (a == b)   { printf("SAME-PTR\n");   return 1; }

    strcpy(a, "AAA");
    strcpy(b, "BBB");
    if (strcmp(a, "AAA") != 0 || strcmp(b, "BBB") != 0) {
        printf("CLOBBER\n"); return 1;
    }
    printf("TWO-MMAP-OK a=%p b=%p\n", (void*)a, (void*)b);

    free(a);
    free(b);
    printf("FREE-OK\n");
    return 0;
}
EOF

musl-gcc -static -no-pie -O2 -mcmodel=large -o /tmp/musl_twommap /tmp/musl_twommap.c

# -------------------------------------------------------------------
# Test 4: MUSL_TWOMMAP()
# -------------------------------------------------------------------

cat > /tmp/musl_twommap2.c<<EOF
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(void) {
    char* a = malloc(256 * 1024);
    char* b = malloc(256 * 1024);
    printf("a=%p b=%p\n", (void*)a, (void*)b);
    //if (a) strcpy(a, "AAA");
    //if (b) strcpy(b, "BBB");
    printf("wrote both\n");
    //free(a);
    //printf("freed a\n");
    //free(b);
    //printf("freed b\n");
    return 0;
}
EOF

musl-gcc -static -no-pie -O2 -mcmodel=large -o /tmp/musl_twommap2 /tmp/musl_twommap2.c

# -------------------------------------------------------------------
# Test 5: PRINTNUM()
# -------------------------------------------------------------------

cat > /tmp/printnum.c<<EOF
#include <stdio.h>
int main(void) {
    //setvbuf(stdout, NULL, _IONBF, 0);
    printf("x=%d\n", 42);
    return 0;
}
EOF

musl-gcc -static -no-pie -O2 -mcmodel=large -o /tmp/printnum /tmp/printnum.c

# -------------------------------------------------------------------
# Test 6: BRK_VERIFY()
# -------------------------------------------------------------------

cat > /tmp/brk_verify.c <<'EOF'
#include <unistd.h>
#include <stdlib.h>

static void puthex(unsigned long v) {
    char buf[32];
    int n = 0;
    buf[n++] = 'p'; buf[n++] = '='; buf[n++] = '0'; buf[n++] = 'x';
    for (int i = 15; i >= 0; i--) {
        unsigned d = (v >> (i * 4)) & 0xf;
        buf[n++] = d < 10 ? ('0' + d) : ('a' + d - 10);
    }
    buf[n++] = '\n';
    write(1, buf, n);
}

int main(void) {
    char *p = malloc(4096);
    if (!p) {
        write(2, "malloc-null\n", 12);
        return 1;
    }
    puthex((unsigned long)p);

    for (int i = 0; i < 4096; i++) p[i] = (char)(i & 0xff);

    for (int i = 0; i < 4096; i++) {
        if (p[i] != (char)(i & 0xff)) {
            write(1, "VERIFY-FAIL\n", 12);
            return 2;
        }
    }
    write(1, "VERIFY-OK\n", 10);
    return 0;
}
EOF

musl-gcc -static -no-pie -O2 -mcmodel=large -o /tmp/brk_verify /tmp/brk_verify.c

# -------------------------------------------------------------------
# Test 7: BRKRAW()
# -------------------------------------------------------------------
cat > /tmp/brkraw.c <<'EOF'
#include <unistd.h>
#include <stdint.h>

static void puthex64(uint64_t v) {
    char b[20]; int n = 0;
    b[n++]='0'; b[n++]='x';
    for (int i=15;i>=0;i--){unsigned d=(v>>(i*4))&0xf; b[n++]= d<10?('0'+d):('a'+d-10);}
    b[n++]='\n';
    write(1, b, n);
}

int main(void) {
    /* read fsbase — if musl's init worked, this is somewhere in the TLS region */
    uint64_t fs;
    __asm__ volatile ("rdfsbase %0" : "=r"(fs));
    write(1, "FS=", 3); puthex64(fs);

    /* ask for current break via the raw syscall, bypassing libc wrappers */
    long cur;
    __asm__ volatile ("syscall" : "=a"(cur) : "a"(12L), "D"(0L) : "rcx","r11","memory");
    write(1, "BRK0=", 5); puthex64((uint64_t)cur);

    /* try a small extension */
    long want = cur + 4096;
    long got;
    __asm__ volatile ("syscall" : "=a"(got) : "a"(12L), "D"(want) : "rcx","r11","memory");
    write(1, "BRKN=", 5); puthex64((uint64_t)got);
    write(1, "WANT=", 5); puthex64((uint64_t)want);

    return 0;
}
EOF
musl-gcc -static -no-pie -O2 -mcmodel=large -o /tmp/brkraw /tmp/brkraw.c

# -------------------------------------------------------------------
# Test 7: BRKRAW()
# -------------------------------------------------------------------
cat > /tmp/brkgrow.c <<'EOF'
#include <unistd.h>
#include <stdint.h>

static void puthex64(uint64_t v) {
    char b[20]; int n = 0;
    b[n++]='0'; b[n++]='x';
    for (int i=15;i>=0;i--){unsigned d=(v>>(i*4))&0xf; b[n++]= d<10?('0'+d):('a'+d-10);}
    b[n++]='\n';
    write(1, b, n);
}

static long brk_sys(long addr) {
    long ret;
    __asm__ volatile ("syscall" : "=a"(ret) : "a"(12L), "D"(addr) : "rcx","r11","memory");
    return ret;
}

int main(void) {
    long cur = brk_sys(0);
    write(1, "start=", 6); puthex64((uint64_t)cur);

    /* try 64 KB */
    long want1 = cur + 0x10000;
    long got1 = brk_sys(want1);
    write(1, "64K want=", 9); puthex64((uint64_t)want1);
    write(1, "64K got =", 9); puthex64((uint64_t)got1);

    /* try 1 MB more */
    long want2 = got1 + 0x100000;
    long got2 = brk_sys(want2);
    write(1, "1M want =", 9); puthex64((uint64_t)want2);
    write(1, "1M got  =", 9); puthex64((uint64_t)got2);

    return 0;
}
EOF
musl-gcc -static -no-pie -O2 -mcmodel=large -o /tmp/brkgrow /tmp/brkgrow.c

# -------------------------------------------------------------------
# Verify outputs
# -------------------------------------------------------------------

echo
echo "=================================================================="
echo "Build results"
echo "=================================================================="

for f in \
    /tmp/musl_min \
    /tmp/musl_printf \
    /tmp/musl_malloc \
    /tmp/musl_twommap \
    /tmp/printnum \
    /tmp/brk_verify \
    /tmp/brkraw \
    /tmp/brkgrow
do
    if [[ -f "$f" ]]; then
        echo "[OK] $f"
        file "$f"
    else
        echo "[FAIL] $f"
        exit 1
    fi
done

echo
echo "Done."
