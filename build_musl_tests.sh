#!/usr/bin/env bash

set -euo pipefail

# -------------------------------------------------------------------
# Compiler: project-local musl-gcc by default.
#
# The wrapper at toolchain/musl-gcc.sh points gcc at the musl 1.2.5
# tree built by toolchain/install_musl.sh into
# third_party/musl-install/.  If you want to fall back to Fedora's
# system musl-gcc for a comparison run, override:
#
#     MUSL_GCC=/usr/bin/musl-gcc ./build_musl_tests.sh
# -------------------------------------------------------------------
MUSL_GCC="${MUSL_GCC:-./toolchain/musl-gcc.sh}"
if [ ! -x "$MUSL_GCC" ]; then
    echo "ERROR: $MUSL_GCC not found or not executable." >&2
    echo "Run ./toolchain/install_musl.sh first, or set MUSL_GCC." >&2
    exit 1
fi

echo "=================================================================="
echo "Building musl test binaries with: $MUSL_GCC"
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

"$MUSL_GCC" \
    -static \
    -O2 \
    -o /tmp/musl_min \
    /tmp/musl_min.c


# -------------------------------------------------------------------
# Test: HELLO_MUSL — A4 first item.  Copy of the newlib hello.c
# source, linked against musl.  Parallel to the newlib HELLO.ELF,
# not a replacement: the canary suite still needs HELLO.ELF.
# -------------------------------------------------------------------

cat > /tmp/hello_musl.c <<'EOF'
#include <stdio.h>

int main(void) {
    printf("hello from donix (musl)\n");
    return 0;
}
EOF

echo "[BUILD] hello_musl"

"$MUSL_GCC" \
    -static \
    -no-pie \
    -O2 \
    -mcmodel=large \
    -o /tmp/hello_musl \
    /tmp/hello_musl.c


# -------------------------------------------------------------------
# Test: ECHO_MUSL — A4 item 2.  Copy of the newlib echo.c source,
# linked against musl.  Parallel to the newlib ECHO.ELF, not a
# replacement: the canary suite still needs ECHO.ELF.
#
# Uses write(2) directly, not printf, so this test does not depend
# on musl's stdio.  musl_printf already covers that path.
# -------------------------------------------------------------------

cat > /tmp/echo_musl.c <<'EOF'
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
EOF

echo "[BUILD] echo_musl"

"$MUSL_GCC" \
    -static \
    -no-pie \
    -O2 \
    -mcmodel=large \
    -o /tmp/echo_musl \
    /tmp/echo_musl.c


# -------------------------------------------------------------------
# Test: CAT_MUSL — A4 item 3.  Copy of the newlib cat.c source,
# linked against musl.  Parallel to the newlib CAT.ELF, not a
# replacement: the canary suite still needs CAT.ELF.
#
# Uses open/read/write/close (POSIX, musl-native).  Prepends "0:/"
# to argv[1] itself, matching the newlib convention, so
# `cat_musl hello-world.txt` and `cat hello-world.txt` should
# produce byte-identical output.
# -------------------------------------------------------------------

cat > /tmp/cat_musl.c <<'EOF'
#include <unistd.h>
#include <fcntl.h>

int main(int argc, char **argv) {
    if (argc < 2) {
        write(2, "usage: cat FILE\n", 16);
        return 1;
    }

    char path[64];
    {
        int i = 0;
        path[i++] = '0';
        path[i++] = ':';
        path[i++] = '/';
        for (const char* p = argv[1]; *p; p++) {
            if (i >= (int)sizeof(path) - 1) {
                write(2, "cat: path too long\n", 19);
                return 1;
            }
            path[i++] = *p;
        }
        path[i] = 0;
    }

    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        write(2, "cat: cannot open\n", 17);
        return 2;
    }

    char buf[512];
    for (;;) {
        int r = read(fd, buf, sizeof(buf));
        if (r < 0) {
            close(fd);
            write(2, "cat: read error\n", 16);
            return 3;
        }
        if (r == 0) break;
        int written = 0;
        while (written < r) {
            int w = write(1, buf + written, r - written);
            if (w <= 0) {
                close(fd);
                return 3;
            }
            written += w;
        }
    }

    close(fd);
    return 0;
}
EOF

echo "[BUILD] cat_musl"

"$MUSL_GCC" \
    -static \
    -no-pie \
    -O2 \
    -mcmodel=large \
    -o /tmp/cat_musl \
    /tmp/cat_musl.c

# -------------------------------------------------------------------
# Test: MUSL_STAT — exercises fstat(2) at syscall 5.
#
# Opens 0:/HELLO-WORLD.TXT (known to be 180 bytes from the image
# build's mdir listing) and calls fstat().  Verifies st_size == 180
# and that st_mode reports a regular file.
#
# Uses raw write(1, ...) rather than printf so that a failure in
# musl's stdio cannot mask a failure in sys_fstat.  musl_printf
# already covers stdio.
# -------------------------------------------------------------------

cat > /tmp/musl_stat.c <<'EOF'
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <stdio.h>

int main(void) {
    int fd = open("0:/HELLO-WORLD.TXT", O_RDONLY);
    if (fd < 0) { printf("STAT-OPEN-FAIL\n"); return 1; }

    struct stat st;
    long fr = fstat(fd, &st);
    close(fd);

    printf("STAT-FR %ld\n", fr);
    printf("STAT-SIZE %ld\n", (long)st.st_size);
    printf("STAT-MODE 0x%lx\n", (unsigned long)st.st_mode);

    if (fr != 0) {
        printf("STAT-FR-FAIL\n");
        return 1;
    }
    if (st.st_size != 180) {
        printf("STAT-SIZE-FAIL\n");
        return 1;
    }
    if ((st.st_mode & 0170000) != 0100000) {   /* S_IFREG */
        printf("STAT-MODE-FAIL\n");
        return 1;
    }
    printf("STAT-OK\n");
    return 0;
}
EOF

echo "[BUILD] musl_stat"

"$MUSL_GCC" \
    -static \
    -no-pie \
    -O2 \
    -mcmodel=large \
    -o /tmp/musl_stat \
    /tmp/musl_stat.c

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

"$MUSL_GCC" \
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

"$MUSL_GCC" \
    -static \
    -no-pie \
    -O2 \
    -mcmodel=large \
    -o /tmp/musl_malloc \
    /tmp/musl_malloc.c

# -------------------------------------------------------------------
# Test: fork()
# -------------------------------------------------------------------

cat > /tmp/musl_fork.c <<'EOF'
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
EOF

echo "[BUILD] musl_fork"

"$MUSL_GCC" \
    -static \
    -no-pie \
    -O2 \
    -mcmodel=large \
    -o /tmp/musl_fork \
    /tmp/musl_fork.c

# -------------------------------------------------------------------
# Test: fork via raw syscalls (bypasses musl's fork wrapper)
# -------------------------------------------------------------------

cat > /tmp/musl_fork_raw.c <<'EOF'
#include <unistd.h>

static long raw_syscall(long nr, long a0, long a1, long a2) {
    long ret;
    __asm__ volatile("syscall" : "=a"(ret)
                     : "a"(nr), "D"(a0), "S"(a1), "d"(a2)
                     : "rcx", "r11", "memory");
    return ret;
}

int main(void) {
    raw_syscall(1, 1, (long)"A\n", 2);

    long pid = raw_syscall(57, 0, 0, 0);

    if (pid == 0) {
        raw_syscall(1, 1, (long)"C\n", 2);
        raw_syscall(60, 0, 0, 0);   /* SYS_exit */
    } else {
        raw_syscall(1, 1, (long)"P\n", 2);
    }
    return 0;
}
EOF

echo "[BUILD] musl_fork_raw"

"$MUSL_GCC" \
    -static \
    -no-pie \
    -O2 \
    -mcmodel=large \
    -o /tmp/musl_fork_raw \
    /tmp/musl_fork_raw.c

# -------------------------------------------------------------------
# Test: execve() — fork a child, child execve()s MUSL_MIN.ELF,
# parent wait4()s and prints a completion marker.
#
# Exercises:
#   - fork(2)  from musl (already green as musl_fork)
#   - execve(2) at SYS_EXECVE (59) — in-place, copy-then-swap
#   - wait4(2) at SYS_WAIT4  (61) — reap the child
#
# Expected output (order of MUSL-START/MUSL-END is fixed; the parent's
# EXEC-PARENT-DONE always comes last because wait4 blocks):
#
#   EXEC-PARENT-START
#   MUSL-START
#   EXEC-PARENT-DONE
#
# If execve fails, the child falls through to EXEC-FAILED and _exit(127),
# which the parent then reports as a nonzero wait status.  A missing
# EXEC-PARENT-DONE means wait4 blocked forever.
# -------------------------------------------------------------------

cat > /tmp/musl_exec.c <<'EOF'
#include <unistd.h>
#include <sys/wait.h>

static void puts_raw(const char* s, unsigned long n) {
    /* write(2) directly, no stdio buffering — keeps the ordering
       between parent and child bytes deterministic. */
    __asm__ volatile("syscall"
                     :
                     : "a"(1L), "D"(1L), "S"(s), "d"(n)
                     : "rcx", "r11", "memory");
}

int main(void) {
    puts_raw("EXEC-PARENT-START\n", 18);

    long pid;
    __asm__ volatile("syscall" : "=a"(pid)
                     : "a"(57L), "D"(0L), "S"(0L), "d"(0L)
                     : "rcx", "r11", "memory");

    if (pid < 0) {
        puts_raw("FORK-FAILED\n", 12);
        return 1;
    }

    if (pid == 0) {
        /* Child: execve MUSL_MIN.ELF with argv = {"MUSL_MIN.ELF", NULL}. */
        static char path[]  = "0:/MUSL_MIN.ELF";
        static char arg0[]  = "MUSL_MIN.ELF";
        char* argv[2];
        argv[0] = arg0;
        argv[1] = (char*)0;

        /* execve(2): nr=59, rdi=path, rsi=argv, rdx=envp */
        __asm__ volatile("syscall"
                         :
                         : "a"(59L), "D"(path), "S"(argv), "d"(0L)
                         : "rcx", "r11", "memory");

        /* Only reached if execve returned (i.e. failed). */
        puts_raw("EXEC-FAILED\n", 12);
        __asm__ volatile("syscall"
                         :
                         : "a"(60L), "D"(127L)
                         : "rcx", "r11", "memory");
        __builtin_unreachable();
    }

    /* Parent: wait4(pid, &status, 0). */
    int status = 0;
    long reaped;
    __asm__ volatile("syscall" : "=a"(reaped)
                     : "a"(61L), "D"(pid), "S"(&status), "d"(0L)
                     : "rcx", "r11", "memory");

    if (reaped == pid) {
        puts_raw("EXEC-PARENT-DONE\n", 17);
        return 0;
    } else {
        puts_raw("WAIT-FAILED\n", 12);
        return 1;
    }
}
EOF

echo "[BUILD] musl_exec"

"$MUSL_GCC" \
    -static \
    -no-pie \
    -O2 \
    -mcmodel=large \
    -o /tmp/musl_exec \
    /tmp/musl_exec.c

# -------------------------------------------------------------------
# Test: wait4() — status propagation, WNOHANG, wait4(-1)
#
# Exercises:
#   - Non-zero exit status propagation (WEXITSTATUS)
#   - WNOHANG on a still-running child returns 0 immediately
#   - wait4(-1, ...) reaps any child
#
# Raw syscalls throughout (no libc wrappers) so that a failure in
# musl's stdio cannot mask a failure in wait4's plumbing.
# -------------------------------------------------------------------

cat > /tmp/musl_wait.c <<'EOF'
#include <unistd.h>
#include <sys/wait.h>

static void puts_raw(const char* s, unsigned long n) {
    __asm__ volatile("syscall"
                     :
                     : "a"(1L), "D"(1L), "S"(s), "d"(n)
                     : "rcx", "r11", "memory");
}

static void put_dec(long v) {
    char b[24]; int n = 0;
    if (v < 0) { b[n++] = '-'; v = -v; }
    char tmp[24]; int t = 0;
    if (v == 0) tmp[t++] = '0';
    while (v > 0) { tmp[t++] = '0' + (v % 10); v /= 10; }
    while (t > 0) b[n++] = tmp[--t];
    b[n++] = '\n';
    puts_raw(b, n);
}

static long raw_fork(void) {
    long r;
    __asm__ volatile("syscall" : "=a"(r)
                     : "a"(57L), "D"(0L), "S"(0L), "d"(0L)
                     : "rcx", "r11", "memory");
    return r;
}

static void raw_exit(int code) {
    __asm__ volatile("syscall"
                     :
                     : "a"(60L), "D"((long)code)
                     : "rcx", "r11", "memory");
    __builtin_unreachable();
}

static long raw_wait4(long pid, int* status, int options) {
    long r;
    __asm__ volatile("syscall" : "=a"(r), "+m"(*status)
                     : "a"(61L), "D"(pid), "S"(status), "d"((long)options)
                     : "rcx", "r11", "memory");
    return r;
}

int main(void) {
    /* ---- Part 1: exit status propagation ---- */
    long pid = raw_fork();
    if (pid == 0) {
        puts_raw("WAIT-CHILD-42\n", 14);
        raw_exit(42);
    }

    int status = -1;
    long reaped = raw_wait4(pid, &status, 0);
    if (reaped != pid) {
        puts_raw("WAIT-REAP-FAIL\n", 15);
        return 1;
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 42) {
        puts_raw("WAIT-STATUS-FAIL raw=", 22);
        put_dec(status);
        return 1;
    }
    puts_raw("WAIT-STATUS-OK ", 15);
    put_dec(WEXITSTATUS(status));

    /* ---- Part 2: WNOHANG on a running child returns 0 ---- */
    pid = raw_fork();
    if (pid == 0) {
        for (volatile long i = 0; i < 100000; i++) {
            puts_raw(".", 1);
        }
        raw_exit(0);
    }

    status = -1;
    long wn = raw_wait4(pid, &status, WNOHANG);
    if (wn != 0) {
        puts_raw("WNOHANG-FAIL ret=", 17);
        put_dec(wn);
    } else {
        puts_raw("WAIT-WNOHANG-OK\n", 16);
    }
    reaped = raw_wait4(pid, &status, 0);
    if (reaped != pid) {
        puts_raw("WNOHANG-REAP-FAIL\n", 18);
        return 1;
    }

    /* ---- Part 3: wait4(-1, ...) reaps any child ---- */
    long p1 = raw_fork();
    if (p1 == 0) { raw_exit(11); }

    long p2 = raw_fork();
    if (p2 == 0) { raw_exit(22); }

    status = -1;
    long got1 = raw_wait4(-1, &status, 0);
    long r1 = status;
    long s1 = WEXITSTATUS(status);

    status = -1;
    long got2 = raw_wait4(-1, &status, 0);
    long r2 = status;
    long s2 = WEXITSTATUS(status);

    if (got1 <= 0 || got2 <= 0 || got1 == got2) {
        puts_raw("WAIT-ANY-FAIL\n", 14);
        return 1;
    }
    puts_raw("WAIT-ANY-1 got=", 15); put_dec(got1);
    puts_raw("WAIT-ANY-1 r=",   13); put_dec(r1);
    puts_raw("WAIT-ANY-1 s=",   13); put_dec(s1);
    puts_raw("WAIT-ANY-2 got=", 15); put_dec(got2);
    puts_raw("WAIT-ANY-2 r=",   13); put_dec(r2);
    puts_raw("WAIT-ANY-2 s=",   13); put_dec(s2);

    puts_raw("WAIT-ALL-OK\n", 12);
    return 0;
}
EOF

echo "[BUILD] musl_wait"

"$MUSL_GCC" \
    -static \
    -no-pie \
    -O2 \
    -mcmodel=large \
    -o /tmp/musl_wait \
    /tmp/musl_wait.c

# -------------------------------------------------------------------
# Test: readdir() — musl's opendir/readdir/closedir against getdents64
#
# Uses raw write(1, ...) for output rather than printf, because
# musl_printf is currently red (musl-internal __stdio_write bug).
# The entry names and count print reliably via write.
#
# Expected output: one NAME per line, then "READDIR-DONE count=N".
# -------------------------------------------------------------------

cat > /tmp/musl_readdir.c <<'EOF'
#include <dirent.h>
#include <unistd.h>

static void puts_raw(const char* s) {
    size_t n = 0;
    while (s[n]) n++;
    __asm__ volatile("syscall"
                     :
                     : "a"(1L), "D"(1L), "S"(s), "d"(n)
                     : "rcx", "r11", "memory");
}

static void put_dec(long v) {
    char b[24]; int n = 0;
    if (v < 0) { b[n++] = '-'; v = -v; }
    char tmp[24]; int t = 0;
    if (v == 0) tmp[t++] = '0';
    while (v > 0) { tmp[t++] = '0' + (v % 10); v /= 10; }
    while (t > 0) b[n++] = tmp[--t];
    b[n++] = '\n';
    __asm__ volatile("syscall"
                     :
                     : "a"(1L), "D"(1L), "S"(b), "d"((long)n)
                     : "rcx", "r11", "memory");
}

int main(void) {
    DIR* d = opendir("0:/");
    if (!d) { puts_raw("OPENDIR-FAIL\n"); return 1; }

    long count = 0;
    struct dirent* e;
    while ((e = readdir(d)) != 0) {
        puts_raw(e->d_name);
        puts_raw("\n");
        count++;
    }
    closedir(d);

    puts_raw("READDIR-DONE count=");
    put_dec(count);
    return 0;
}
EOF

echo "[BUILD] musl_readdir"

"$MUSL_GCC" \
    -static \
    -no-pie \
    -O2 \
    -mcmodel=large \
    -o /tmp/musl_readdir \
    /tmp/musl_readdir.c

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

"$MUSL_GCC" -static -no-pie -O2 -mcmodel=large -o /tmp/musl_twommap /tmp/musl_twommap.c

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

"$MUSL_GCC" -static -no-pie -O2 -mcmodel=large -o /tmp/musl_twommap2 /tmp/musl_twommap2.c

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

"$MUSL_GCC" -static -no-pie -O2 -mcmodel=large -o /tmp/printnum /tmp/printnum.c

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

"$MUSL_GCC" -static -no-pie -O2 -mcmodel=large -o /tmp/brk_verify /tmp/brk_verify.c

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
"$MUSL_GCC" -static -no-pie -O2 -mcmodel=large -o /tmp/brkraw /tmp/brkraw.c

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
"$MUSL_GCC" -static -no-pie -O2 -mcmodel=large -o /tmp/brkgrow /tmp/brkgrow.c
# -------------------------------------------------------------------
# Test: R10 PROBE — is %r10 preserved across a writev syscall?
#
# The Linux x86_64 syscall ABI clobbers only %rax, %rcx, %r11.
# Every other GPR must survive a syscall.  musl's __stdio_write
# builds its iovec array in a way that can leave a live value in
# %r10 across the raw writev syscall.  If the kernel's return path
# clobbers %r10, musl stores a bad pointer into iov[1] and printf
# emits garbage.
#
# This test sets %r10 to a recognisable sentinel, issues a raw
# writev(2), and prints the value of %r10 afterwards.
#
# Expected (correct kernel): R10-AFTER=0xdeadbeefcafebabe
# Broken kernel:             R10-AFTER=0x0000008xxxxxxxxx  (a user RSP)
# -------------------------------------------------------------------

cat > /tmp/musl_r10probe.c <<'EOF'
#include <unistd.h>
#include <stdint.h>
#include <sys/uio.h>

static void puthex64(uint64_t v) {
    char b[32]; int n = 0;
    b[n++]='R'; b[n++]='1'; b[n++]='0'; b[n++]='-';
    b[n++]='A'; b[n++]='F'; b[n++]='T'; b[n++]='E'; b[n++]='R'; b[n++]='=';
    b[n++]='0'; b[n++]='x';
    for (int i = 15; i >= 0; i--) {
        unsigned d = (v >> (i * 4)) & 0xf;
        b[n++] = d < 10 ? ('0' + d) : ('a' + d - 10);
    }
    b[n++] = '\n';
    write(1, b, n);
}

int main(void) {
    struct iovec iov[2];
    iov[0].iov_base = (void*)"ABC";
    iov[0].iov_len  = 3;
    iov[1].iov_base = (void*)"DEF\n";
    iov[1].iov_len  = 4;

    long ret;
    register uint64_t r10_reg asm("r10") = 0xDEADBEEFCAFEBABEULL;

    __asm__ volatile (
        "syscall"
        : "=a"(ret), "+r"(r10_reg)
        : "a"(20L), "D"(1L), "S"(iov), "d"(2L)
        : "rcx", "r11", "memory"
    );

    puthex64(r10_reg);
    return (ret == 7) ? 0 : 1;
}
EOF

echo "[BUILD] musl_r10probe"

"$MUSL_GCC" \
    -static \
    -no-pie \
    -O2 \
    -mcmodel=large \
    -o /tmp/musl_r10probe \
    /tmp/musl_r10probe.c
    
# -------------------------------------------------------------------
# Test: MUSL SH — minimal shell (fork + execve + wait4)
#
# Reads a line from stdin, forks, child execve()s the named binary,
# parent wait4()s the child before reading the next line.  The
# wait4 is the whole point: without it, parent and child console
# output interleaves byte-by-byte, which is what the newlib shell
# currently does (see capture.txt around the `echo` and `printnum`
# lines in the 20260924L session).
#
# Convention (matches the newlib shell, user_shell.c:run_external):
#   - The user types a bare command name (`cat`, `hello`), optionally
#     followed by whitespace-separated arguments.
#   - argv[0] is normalized to `0:/NAME.ELF`.  If argv[0] already
#     contains `:/`, it is used as-is (so `0:/musl_r10probe.elf`
#     still works).
#   - argv[1..n] are passed verbatim.  The newlib cat/echo expect
#     bare filenames and prepend `0:/` themselves.
#
# Empty line = ignore.  `exit` quits.
#
# Uses raw write() for the prompt so that musl's stdio is not on
# the critical path of the first A3 milestone.
# -------------------------------------------------------------------

cat > /tmp/musl_sh.c <<'EOF'
#include <unistd.h>
#include <string.h>
#include <sys/wait.h>

static void puts_raw(const char* s, unsigned long n) {
    __asm__ volatile("syscall"
                     :
                     : "a"(1L), "D"(1L), "S"(s), "d"(n)
                     : "rcx", "r11", "memory");
}

int main(void) {
    char line[256];

    for (;;) {
        puts_raw("donix> ", 7);

        /*
         * Read one byte at a time until newline.
         *
         * sys_read on fd 0 now returns on the first available byte
         * (POSIX short read), so a single read() is not a line.
         * Loop here, like the newlib shell does.  Byte-at-a-time
         * also lets us echo each character immediately, which is
         * what a terminal user expects.
         */
        int n = 0;
        for (;;) {
            char c;
            int r = read(0, &c, 1);
            if (r <= 0) continue;
            if (c == '\r' || c == '\n') {
                puts_raw("\n", 1);
                break;
            }
            if (c == '\b' || c == 0x7f) {
                if (n > 0) {
                    n--;
                    puts_raw("\b \b", 3);
                }
                continue;
            }
            if (c < 0x20 || c > 0x7e) continue;
            if (n >= (int)sizeof(line) - 1) continue;
            line[n++] = c;
            puts_raw(&c, 1);
        }
        line[n] = 0;

        if (n == 0) continue;

        if (n == 4 && memcmp(line, "exit", 4) == 0) {
            puts_raw("bye\n", 4);
            return 0;
        }

        pid_t pid = fork();
        if (pid < 0) {
            puts_raw("FORK-FAILED\n", 12);
            continue;
        }

        if (pid == 0) {
            /*
             * Tokenize `line` in place on whitespace.  `line` is a
             * local in the child's copy of the address space after
             * fork, so mutating it is safe.
             *
             * MAX_ARGS is a compile-time cap; a longer command line
             * is truncated to the first MAX_ARGS-1 tokens, with the
             * final slot left NULL-terminated.
             */
            enum { MAX_ARGS = 16 };
            char* argv[MAX_ARGS];
            int argc = 0;

            char* p = line;
            while (*p && argc < MAX_ARGS - 1) {
                while (*p == ' ' || *p == '\t') p++;
                if (!*p) break;
                argv[argc++] = p;
                while (*p && *p != ' ' && *p != '\t') p++;
                if (*p) *p++ = 0;
            }
            argv[argc] = (char*)0;

            if (argc == 0) _exit(0);   /* should not happen: n>0 checked above */

            /*
             * Resolve argv[0] to a full path, matching the newlib
             * shell's convention (user_shell.c:run_external):
             *
             *     snprintf(path, sizeof(path), "0:/%s.ELF", argv[0]);
             *
             * The user types a bare command name (`cat`, `hello`);
             * the shell turns it into `0:/NAME.ELF`.  If the token
             * already contains `:/` (the user typed a full path
             * like `0:/musl_r10probe.elf`), leave it alone.
             *
             * argv[1..n] are passed verbatim, exactly as the newlib
             * shell does.  The newlib cat/echo expect bare
             * filenames and prepend `0:/` themselves.
             */
            char path[128];
            {
                const char* tok = argv[0];
                int has_prefix = 0;
                for (const char* q = tok; *q; q++) {
                    if (q[0] == ':' && q[1] == '/') { has_prefix = 1; break; }
                }
                if (has_prefix) {
                    int i = 0;
                    while (tok[i] && i < (int)sizeof(path) - 1) {
                        path[i] = tok[i];
                        i++;
                    }
                    path[i] = 0;
                } else {
                    int i = 0;
                    path[i++] = '0';
                    path[i++] = ':';
                    path[i++] = '/';
                    for (const char* q = tok; *q && i < (int)sizeof(path) - 1; q++) {
                        path[i++] = *q;
                    }
                    if (i < (int)sizeof(path) - 5) {
                        path[i++] = '.';
                        path[i++] = 'E';
                        path[i++] = 'L';
                        path[i++] = 'F';
                    }
                    path[i] = 0;
                }
            }

            execve(path, argv, (char**)0);
            puts_raw("EXEC-FAILED\n", 12);
            _exit(127);
        }

        int status = 0;
        wait4(pid, &status, 0, (void*)0);
    }
}
EOF

echo "[BUILD] musl_sh"

"$MUSL_GCC" \
    -static \
    -no-pie \
    -O2 \
    -mcmodel=large \
    -o /tmp/musl_sh \
    /tmp/musl_sh.c
# -------------------------------------------------------------------
# Verify outputs
# -------------------------------------------------------------------

echo
echo "=================================================================="
echo "Build results"
echo "=================================================================="

for f in \
    /tmp/hello_musl \
    /tmp/echo_musl \
    /tmp/cat_musl \
    /tmp/musl_stat \
    /tmp/musl_min \
    /tmp/musl_printf \
    /tmp/musl_malloc \
    /tmp/musl_fork \
    /tmp/musl_fork_raw \
    /tmp/musl_exec \
    /tmp/musl_wait \
    /tmp/musl_readdir \
    /tmp/musl_twommap \
    /tmp/printnum \
    /tmp/brk_verify \
    /tmp/brkraw \
    /tmp/brkgrow \
    /tmp/musl_r10probe \
    /tmp/musl_sh
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
