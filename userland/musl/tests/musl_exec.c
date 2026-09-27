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
