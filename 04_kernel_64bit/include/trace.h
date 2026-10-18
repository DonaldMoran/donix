#ifndef TRACE_H
#define TRACE_H

/*
 * Kernel trace toggles.  Flip to 1 to enable, 0 to disable.
 *
 * All are checked with #if, so a disabled trace compiles out
 * entirely -- no instructions, no string literals in the binary.
 * The source stays across both configurations; you never lose
 * the diagnostic code, and the toggle is a one-line visible
 * edit in git diff.
 *
 * Defaults are 0 (production): a scripted boot has no debug
 * noise.  For a debugging session, set the ones you need to 1
 * and rebuild.  To capture to a file while still seeing live
 * output, use -serial stdio piped through tee; the toggle only
 * controls whether the print is compiled in.
 *
 * This file is created in piece 1 of the G1 re-derivation.  It
 * is NOT included anywhere yet; pieces 2-8 add the includes and
 * the #if/#endif gates one group at a time.  Until then, this
 * file is inert.
 */

/* sys_execve: successful-exec entry line (pid, entry, argc,
 * rsp).  Fires on every execve that finds a valid ELF.  Noisy on
 * a boot that runs a lot of commands; useful when tracing what
 * the shell is launching. */
#define TRACE_EXECVE_ENTRY   0

/* sys_execve: the "not an ELF file" family -- the diagnostics
 * that fire when execve is handed a shell script, a text file,
 * or anything else that is not an ELF binary.  These are the
 * NORMAL result for scripts (execve returns -ENOEXEC and the
 * shell falls back to running the file through an interpreter),
 * so they are off by default.  Turn on to see what the shell
 * is trying and failing to exec directly. */
#define TRACE_EXECVE_ENOEXEC 0

/* sys_execve: real errors -- I/O failed, out of memory, ELF
 * corrupt, arg/argv layout overflow.  These indicate a genuine
 * problem, not a script being exec'd.  On by default. */
#define TRACE_EXECVE_ERR     1

/* open_resolved: the f_open failure diagnostic (path, flags,
 * mode, FRESULT). */
#define TRACE_OPEN     0

/* process_wake_parent_if_waiting: the WW: child=... drop=X and
 * wake=ok lines. */
#define TRACE_WAKE     0

/* sys_wait4: the WW: wait4 self=... blocking / yielding lines. */
#define TRACE_WAIT     0

/* scheduler: the EXIT: pid=... qhead=... line. */
#define TRACE_EXIT     0

/* The kernel boot banner and "press k" prompt. */
#define TRACE_BOOT     0

#endif /* TRACE_H */
