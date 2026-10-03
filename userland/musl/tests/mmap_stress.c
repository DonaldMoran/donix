/*
 * mmap_stress.c -- map, unmap, and re-map, to exercise the PMM's
 * free-behind-the-cursor case.
 *
 * The bug this targets: pmm_alloc_page's zone scan starts at a
 * cursor and moves in one direction.  A page freed after the cursor
 * passed it is found only if the free's rewind moves the cursor
 * back to it; a page that was free the whole time and sits on the
 * far side of the cursor is never found by a linear scan.  That
 * made a fresh boot after adding userland ELFs fail to allocate a
 * page-table page, because the extra early allocations pushed the
 * cursor past pages that had been free from boot.
 *
 * munmap is the userspace path that frees a run of pages, so a
 * map/unmap/map cycle puts free pages behind the cursor and then
 * asks for them again.  Under the old one-directional scan the
 * second map eventually fails; under the wrapping scan it succeeds.
 *
 * This is the direct syscall, not malloc: musl's malloc would use
 * brk for small sizes and its own mmap for large ones, and either
 * would interpose chunking between the test and the kernel's
 * allocator.  We want the kernel's allocator.
 *
 * Read-only on the disk (mmap is anonymous, munmap frees), fast.
 * Not a canary row.
 */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <errno.h>

#define CHUNK   (2 * 1024 * 1024)   /* 2 MB, 512 pages */
#define ROUNDS  16

static int fails = 0;

int main(void) {
    printf("=== mmap_stress ===\n");

    for (int i = 0; i < ROUNDS; i++) {
        errno = 0;
        void* p = mmap(NULL, CHUNK, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (p == MAP_FAILED) {
            printf("FAIL round %d: mmap(%d bytes) failed: %s\n",
                   i, CHUNK, strerror(errno));
            fails++;
            break;
        }
        printf("ok   round %d: mmap at %p\n", i, p);

        /*
         * Touch every page, so the kernel actually allocates and maps
         * them.  A lazy mmap would not exercise the allocator.
         */
        volatile char* q = (volatile char*)p;
        for (size_t off = 0; off < CHUNK; off += 4096) {
            q[off] = (char)(i & 0x7F);
        }
        /* And read one back, so a mapping that is present-but-bad is
         * caught here rather than silently. */
        if (q[0] != (char)(i & 0x7F)) {
            printf("FAIL round %d: wrote %d, read %d\n",
                   i, (int)(i & 0x7F), (int)q[0]);
            fails++;
        }

        errno = 0;
        if (munmap(p, CHUNK) != 0) {
            printf("FAIL round %d: munmap failed: %s\n", i, strerror(errno));
            fails++;
            break;
        }
        /* Do not print the unmap; the point is the cycle. */
    }

    /*
     * One more map after the last unmap: the pages the last unmap
     * freed are behind the cursor now, and this is the allocation
     * that a one-directional scan cannot serve.
     */
    errno = 0;
    void* last = mmap(NULL, CHUNK, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (last == MAP_FAILED) {
        printf("FAIL final: mmap after %d cycles failed: %s\n",
               ROUNDS, strerror(errno));
        fails++;
    } else {
        volatile char* q = (volatile char*)last;
        q[0] = 'x';
        q[CHUNK - 1] = 'y';
        printf("ok   final: mmap after %d cycles, %d bytes at %p\n",
               ROUNDS, CHUNK, last);
        munmap(last, CHUNK);
    }

    if (fails == 0) {
        printf("MMAP_STRESS-ALL-PASS\n");
        return 0;
    }
    printf("MMAP_STRESS: %d failed\n", fails);
    return 1;
}
