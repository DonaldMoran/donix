# donix strategy and rules

Reference material for the live state in `handoff.md`.  Read once
when you need the background; not required reading every session.

## What donix is

(fork of dons-os; Linux x86_64 syscall ABI; static musl; newlib
retired; userland at `userland/musl/`)

## Baseline facts

(dons-os `dev` is the recovery point; the previous migration
session's stdio failure was self-inflicted by unnecessary changes
to `arc2/crt0.S`, `user_newlib_linker.ld`, `arc2/syscalls.c`
semantics -- all now removed at A5 step 7)

## Strategy: two phases

### Phase A -- Linux syscall ABI, then static musl binaries
(complete at `v0.6.0`; newlib userland was the passive regression
canary during the migration)

### Phase B -- busybox / coreutils against musl
(underway; see `handoff.md` for current state)

## The one critical rule

**One change at a time. Test. Commit. Revert on failure.**

## Files that must not be touched

Unless a specific tested problem requires it:
- `process.c` -- especially `process_copy_kernel_frame`
- `scheduler.c`
- `context_switch.asm`
- `interrupts.c`
- `kmain.c`

(The prebuilt newlib `.a` files, `arc2/crt0.S`, `arc2/reent.c`,
and `user_newlib_linker.ld` were removed at A5 step 7.)

## A6 -- musl userland source tree (complete at `v0.6.0`)

(sources at `userland/musl/{apps,tests}/`; `Makefile` builds each
`.c` into `build/*.elf` with `-static -no-pie -O2 -mcmodel=large`;
`05_boot_kernel64/Makefile` invokes `make -C ../userland/musl` and
stages onto the FAT; no `/tmp` staging)

Full A1-A6 narrative: `docs/migration-history.md`.

## Testing harness

- **Kernel shell:** `k` at boot prompt (`proclist`, `schstat`,
  `heapstat`, `selftest`, `fatls`, `elfload`).
- **User shell:** `musl_sh`, launched automatically from the FAT
  as `0:/MUSL_SH.ELF`.  Do not press `k`.  A FAT read failure is
  a serial PANIC.
- **QEMU:** `make runkernel64-kvm-single` (fast),
  `make runkernel64-single` (TCG), `make logkernel64` (debug).
  `./run` does a clean single-drive TCG rebuild, tees to
  `capture.txt`, and does **not** set `-d in_asm,cpu`.
- **musl userland build:** `make -C userland/musl`, sources under
  `userland/musl/{apps,tests}/`, ELFs to `userland/musl/build/`,
  copied to FAT as `::/*.ELF`.  Missing ELF is a hard failure.
- **Busybox config:** tracked at `configs/busybox.config`; the
  Makefile installs it to `third_party/busybox/.config`.  Edit
  only the tracked copy.
- **Canary:** see `handoff.md` for the current focused/full lists.

## Tagging convention

Tags through `20260926Z` used a single-letter suffix; that scheme
is exhausted.  From the next commit onward:

    YYYYMMDD-NN

`NN` is a two-digit sequence starting at `01`, incrementing per
commit within a day; a new day restarts at `01`.  Zero-padding is
required so lexical order matches chronological order.  Do not
renumber or retag existing tags.

**Working tags** are local-only, one per commit, **kept in the
local repo** -- do not delete them.  `git show 20260927-19` must
always resolve; that is what makes the handoff's tag-only
references work.

**Milestone tags** (`v0.5.5`, `v0.6.0`, `v0.6.1`, `v0.6.2`, ...)
are the only tags pushed to the remote.  Do not push working tags.

## Git hygiene

- **Do not use `git add -A`.**  Stage explicit files for each
  commit; verify with `git diff --cached --stat` before
  committing.  (Learned 2026-09-24: `git add -A` on the halt-fix
  commit swept in an unrelated rename.)
- **A file with only a diagnostic print in it should be
  reverted, not committed.**
- **After every patch, verify the edit landed in the tree you are
  building.**  `git status` shows the file as modified; a targeted
  `grep` on the changed function confirms it.  If either is
  unexpected, stop.
- **Commits are recorded in `docs/session-log.md` before the
  session ends.**  An unrecorded commit on `dev` is worse than no
  entry.

## Recovery

dons-os `dev` is the recovery point.  donix is a clone; if it goes
bad, `git restore .` or re-clone.  Commit after every successful
milestone.

## Summary for any session

1. Confirm donix baseline: boot to `musl_sh`, run the focused
   canary.
2. One logical change at a time.  Test.  Commit.  Tag.
3. Revert with `git restore .` on any failure.
4. Explicit `git add <file>...`; verify `git diff --cached --stat`.
