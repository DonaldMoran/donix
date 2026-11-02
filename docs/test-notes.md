# Test notes

Run-by-hand tests and their prerequisites. These do not belong in
`docs/open-issues.md` and do not belong in `handoff.md`; they live
here so an open-issues read stays short.

- **The old `musl_sh` ash-only caveats are gone.**  Redirection
  and pipeline tests are valid from `donix>` as well as ash.
- **Framebuffer / `vi` tests are one-offs, not canary rows.**
  `vi` mutates the disk and takes over the screen.
- **Pipe regression suite is not a canary.**  `pipe_step1` ...
  `pipe_step3b` fork and take seconds.
- **`at_step2` is not a canary either.**  It mutates the disk.
  `at_step1` is read-only.
- **`readlink_errno` is read-only and fast**, run by hand.
- **`proc_status` and `proc_fd` are read-only and fast**, run by
  hand.
- **`proc_dir`, `proc_stat`, `proc_walk`, `proc_walk_fds`,
  `mmap_stress`, and `exec_churn`** are run by hand.
  `exec_churn` needs its helper staged as
  `/usr/bin/CHURN_HELPER`, and is the test that exercises
  `process_free_clone` on the process-exit path.
- **`test.sh`** is the ~58-row harness at `/root/scripts/test.sh`.
  Run it by hand when changing the applet config or the
  pipe/wait paths.
- **`mmap_nx`** proves `sys_mmap`'s `PT_NX` gate is enforced.
  Run it when changing `sys_mmap`, the `PROT_*` handling, or the
  leaf-PTE construction.
- **`sigsegv_probe`** installs a `SIGSEGV` handler, faults, and
  edits `ucontext.gregs[16]` so `rt_sigreturn` resumes past the
  fault.  It is the acceptance check for slice 4.
- **`sigterm_ignore`** installs `SIG_IGN` for SIGTERM, raises
  SIGTERM, and prints whether it survived.  It is the acceptance
  check for the `SIG_IGN` disposition (`20261023-sigign`).
- **`nanosleep_intr_probe`** blocks in `nanosleep`, has a peer
  signal it, and asserts `got=1` and `rc=-1/errno=EINTR`.  It
  is the acceptance check for `nanosleep` interruption and the
  `-EINTR` return on a blocking syscall.
- **`at_step1` sections**, as of session 41: sections 1-7
  `resolve_at`; 8-9 `faccessat`; 11 `utimensat`.  (There is no
  section 10.)
- **The canary is now `canary`, a program.**  `canary` (read-only)
  and `canary --full` (also the mutating rows).  15/15 and 28/28.
- **`exec_churn` performance.**  On `873be27`, `prnt` was 0 on
  most rounds and 70-140 ms on some; `total` swung 30-170 ms
  between otherwise identical rounds.  Stage `CHURN_HELPER` as
  `/usr/bin/CHURN_HELPER`; the test exercises
  `process_free_clone` on the process-exit path.  The variance is
  the top item in `handoff.md`'s NEXT.
