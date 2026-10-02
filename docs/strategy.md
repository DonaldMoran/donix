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

## When a feature may force architecture

A feature is allowed to force architectural change only when the
current architecture **cannot express it at all** -- not when a
new architecture would merely be cleaner.  This is the rule that
keeps the one-change-at-a-time discipline from collapsing into
either of its two failure modes: speculative layers with no
callers, and special cases scattered across syscalls.

Worked examples from the current tree:

- **`realpath` does not force anything.**  It is config-only; it
  routes through musl's `realpath()`, which uses `lstat`,
  `readlink`, and `getcwd` -- all present.
- **A `readlink` errno fix does not force anything.**  It is a
  correctness fix inside one function.
- **Symlinks do not force anything yet**, because they are
  deferrable.  They become a *consumer* of the pathname layer when
  it exists; they are not the reason to build it.
- **`/proc` forces the pathname dispatch seam.**  `resolve_against_cwd`
  plus `fat_lookup` literally cannot express `/proc/self/status`:
  there is no FAT entry and never will be.  This is the test --
  the current machinery cannot produce the feature at all.

When a feature does force change:

- **Build exactly the seam that feature needs -- no more, no less.**
  Not "a VFS"; the dispatch seam plus the smallest open-file
  representation the feature's *smallest* case requires.  For
  `/proc`, that is one `/proc` file's worth.
- **Scope the seam against a named minimal customer**, then build
  the customer to prove the sizing.  "Design the seam" in the
  abstract is the speculative trap; "the seam `/proc/self/status`
  needs" is designable.
- **Let the customer prove the seam.**  If the customer does not
  fit, the seam is wrong; the customer is not negotiable.
- **Do not special-case in individual syscalls.**  The failure mode
  donix is *most* at risk of is not over-abstraction -- it is
  `strncmp(path, "/proc/", 6)` appearing first in `sys_open`, then
  in `sys_stat`, then in `sys_access`.  `resolve_against_cwd` and
  `sys_execve`'s shims are the existing instance of this pattern;
  `/proc` is where it either gets generalized or gets three more
  copies.

The dispatch seam is the **VFS front door**, whatever it is called.
Design it knowing `/proc` and `/dev` are coming, but do not build
the VFS they are commonly assumed to need.  See `ROADMAP.md`,
"Make `/proc` possible."

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

**Working tags** name a single commit and are local-only:

    YYYYMMDD-<slug>

The slug is a short lowercase description of the commit
(`20261001-envp`, `20261002-handoff`).  **Working tags are annotated**
-- the annotation is the fuller per-commit summary, and at a
milestone bump the annotations seed the final milestone narrative.
This is why each step gets a tag even when no `v*` tag is imminent.

**Working tags are dropped at the milestone bump.**  They exist
while a milestone is being developed as local restore points; once
the milestone is tagged `v*` and pushed, the scratch tags are
deleted (`git tag -d <tag>...`).  A dropped scratch tag resolves to
nothing; do not cite one as if it were a stable reference.  If a
commit row needs a durable name after the bump, use its subject
line, not its old scratch tag.

**Milestone tags** (`v0.5.5`, `v0.6.0`, ... `v0.6.9`) are the only
tags pushed to the remote and are permanent.  Do not push working
tags.

(Historical note: tags through `20260926Z` used a single-letter
suffix, exhausted at `Z`; those were kept rather than dropped.
The slug scheme above replaced the `YYYYMMDD-NN` sequence, which
was never actually used and is superseded.)

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
- **A commit message is a claim, not a fact.**  Read the diff.
  `95c6337 handoff: rewrite fresh for the v0.6.9 bump` did not
  rewrite the handoff body; the mismatch was found a session
  later.

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
