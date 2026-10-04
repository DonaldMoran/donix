#!/bin/sh
#
# test.sh -- applet smoke test for donix.
#
# Runs every enabled busybox applet that can be exercised
# non-interactively, checks known answers where there is one, and
# reports pass/fail.  Interactive applets (vi, ash line editing)
# and terminal-dependent ones (stty printing, tab completion) are
# not covered here; they are manual.
#
# Run:  sh /root/scripts/test.sh
#
# TRACE DISCIPLINE.  Every row prints
#
#     [N] run  <name>
#
# on its own line BEFORE it executes, where N is a row counter.
# If the script stops -- hang, fault, killed -- the LAST line in
# the capture is the row that stopped it.  This has caught a
# different hang row on every run, which is how the lost-wakeup
# race was found.
#
# KNOWN BUGS this script currently reports (session 51):
#
#   a mid-script hang -- a chain of command substitutions can lose
#                      a wake; the shell blocks and the scheduler
#                      falls to idle.  Racy: the same image runs to
#                      row 54 on one boot and hangs at row 29 on
#                      the next.  Not attributable to a single row.
#
#   sleep 0.1       -- NOT a bug.  FEATURE_FANCY_SLEEP is not
#                      enabled, so busybox sleep accepts integers
#                      only.  The row below uses `sleep 1`.

pass=0
fail=0
row=0

ok()  { pass=$((pass+1)); echo "ok   $1"; }
bad() { fail=$((fail+1)); echo "FAIL $1"; }

# trace NAME -- print the row marker.  Called at the top of every
# row helper, before the command runs.
trace() {
    row=$((row+1))
    printf '[%d] run  %s\n' "$row" "$1"
}

# check NAME WANT CMD...
#
# Runs CMD, compares its stdout to WANT (exact string), reports
# pass/fail.  Uses `case`, not `[`, because ash's `[` builtin may
# be disabled in this build -- `case` is a shell keyword and is
# always present.
#
# Input uses `printf`, not `echo -n`: inside `sh -c`, `echo` may be
# ash's builtin, whose -n handling differs from the applet.
check() {
    name="$1"; want="$2"; shift 2
    trace "$name"
    got=$("$@" 2>/dev/null)
    case "$got" in
        "$want") ok "$name" ;;
        *) bad "$name"; echo "       want: [$want]"; echo "       got:  [$got]" ;;
    esac
}

# ran NAME CMD...
#
# Runs CMD, reports pass/fail on its exit status alone.  On failure
# the exit code and the command's output are printed.
#
# Session 52: the failure path used to indent with
#     echo "$out" | while read -r line; do echo "       $line"; done
# That pipeline forks a subshell, and the subshell's exit can lose
# its wake -- the shell blocks in wait4 forever with no runnable
# process, and the scheduler falls to idle.  This is item 7d in a
# second location.  The failure path below does not fork.
ran() {
    name="$1"; shift
    trace "$name"
    out=$("$@" 2>&1)
    st=$?
    if [ $st -eq 0 ]; then
        ok "$name"
    else
        bad "$name (exit $st)"
        printf '%s\n' "$out"
    fi
}

# ran_fail NAME CMD...
#
# The inverse of ran: passes when CMD exits NON-ZERO.
ran_fail() {
    name="$1"; shift
    trace "$name (expect fail)"
    "$@" >/dev/null 2>&1
    st=$?
    if [ $st -ne 0 ]; then
        ok "$name (expected exit $st)"
    else
        bad "$name (expected failure, got 0)"
    fi
}

echo "=== donix applet test ==="

# ------------------------------------------------------------------
# Checksums: known answers.  printf feeds the bytes.
#
# Session 52: sha512sum is CORRECT.  The previous expected value
# was the SHA-512 of lowercase 'abc' -- the FIPS test vector for
# the lowercase letters -- not of the three bytes 0x41 0x42 0x43
# that the row feeds it.  The value is corrected below.
# ------------------------------------------------------------------
check "md5sum ABC" "902fbdd2b1df0c4f70b4a5d23525e932  -" \
    sh -c 'printf ABC | md5sum'
check "sha1sum ABC" "3c01bdbb26f358bab27f267924aa2c9a03fcfdb8  -" \
    sh -c 'printf ABC | sha1sum'
check "sha256sum ABC" "b5d4045c3f466fa91fe2cc6abe79232a1a57cdf104f7a26e716e0a1e2789df78  -" \
    sh -c 'printf ABC | sha256sum'
check "sha512sum ABC" "397118fdac8d83ad98813c50759c85b8c47565d8268bf10da483153b747a74743a58a90e85aa9f705ce6984ffc128db567489817e4092d050d8a1cc596ddc119  -" \
    sh -c 'printf ABC | sha512sum'

# ------------------------------------------------------------------
# Encoders: known answers
# ------------------------------------------------------------------
check "base64 hello" "aGVsbG8=" sh -c 'printf hello | base64'
check "base32 hello" "NBSWY3DP" sh -c 'printf hello | base32'

# ------------------------------------------------------------------
# Text transforms: known answers
# ------------------------------------------------------------------
check "rev"    "olleh"  sh -c 'echo hello | rev'
check "tac"    "c
b
a" sh -c 'printf "a\nb\nc\n" | tac'
check "fold"   "12
34" sh -c 'printf "1234\n" | fold -w 2'
check "tr a-z A-Z" "ABC" sh -c 'echo abc | tr a-z A-Z'
check "cut -d: -f2" "b" sh -c 'echo a:b:c | cut -d: -f2'
check "uniq"   "a
b" sh -c 'printf "a\na\nb\n" | uniq'
check "seq"    "1
2
3" seq 1 3

# ------------------------------------------------------------------
# Feature flags on existing applets
# ------------------------------------------------------------------
check "echo -e" "a
b" sh -c 'echo -e "a\nb"'
check "echo -n" "hi" sh -c 'echo -n hi'
check "head -n 2" "1
2" sh -c 'printf "1\n2\n3\n" | head -n 2'
check "tail -n 1" "3" sh -c 'printf "1\n2\n3\n" | tail -n 1'
check "sort -n" "2
10" sh -c 'printf "10\n2\n" | sort -n'
check "grep -A1" "a
b" sh -c 'printf "a\nb\nc\n" | grep -A1 a'
check "sed s///" "world" sh -c 'echo hello | sed "s/hello/world/"'

# ------------------------------------------------------------------
# Shell features
# ------------------------------------------------------------------
check "math"   "14" sh -c 'echo $((2 + 3 * 4))'
check "math64" "1099511627776" sh -c 'echo $((1 << 40))'
check "test -a" "yes" sh -c '[ -n x -a -n y ] && echo yes'

# ------------------------------------------------------------------
# Identity: resolves through /etc/passwd and /etc/group
# ------------------------------------------------------------------
check "id -u"  "0"    id -u
check "id -g"  "0"    id -g
check "id -un" "root" id -un
check "id -gn" "root" id -gn

# ------------------------------------------------------------------
# Checksums: run only
# ------------------------------------------------------------------
ran "cksum"    sh -c 'printf ABC | cksum'
ran "crc32"    sh -c 'printf ABC | crc32'
ran "sum"      sh -c 'printf ABC | sum'
ran "sha3sum"  sh -c 'printf ABC | sha3sum'
ran "hexdump"  sh -c 'printf ABC | hexdump -C'
ran "od"       sh -c 'printf ABCD | od -c'
ran "split"    sh -c 'echo 1234567890 > /sp; split -b 5 /sp; rm -f xaa xab xac /sp'
ran "strings"  sh -c 'strings /bin/busybox | head -3'
ran "tree"     tree /bin
ran "which"    which busybox
ran "pidof"    pidof busybox
ran "hostid"   hostid
ran "nohup"    sh -c 'nohup echo hi'
ran "ascii"    ascii
ran "shuf"     sh -c 'printf "a\nb\nc\n" | shuf'
ran "id"       id
ran "groups"   groups
ran "whoami"   whoami
ran "basename" basename /a/b/c
ran "dirname"  dirname /a/b/c
ran "arch"     arch
ran "mktemp"   mktemp
ran "ttysize"  ttysize
ran "clear"    clear
ran "true"     true
ran "usleep"   usleep 1000

# sleep: integer argument.  FEATURE_FANCY_SLEEP is not enabled, so
# `sleep 0.1` correctly fails with "invalid number".  The row uses
# an integer, which is what this build supports.
ran "sleep"    sleep 1

# ------------------------------------------------------------------
# find flags
#
# -not is a GNU spelling, and busybox gates it behind
# ENABLE_DESKTOP in findutils/find.c -- it sits inside an
# `#if ENABLE_DESKTOP` block alongside -and, -or, and -wholename.
# This build sets CONFIG_DESKTOP=n, so -not is not compiled in even
# though CONFIG_FEATURE_FIND_NOT=y.  FEATURE_FIND_NOT controls the
# POSIX `!` operator, which is what the row uses.  Session 52.
# ------------------------------------------------------------------
ran "find -maxdepth" find / -maxdepth 1 -type d
ran "find -not"      find / -maxdepth 1 -name busybox ! -path /tmp
ran "find -empty"    find / -maxdepth 1 -empty -type d

echo "---"
echo "$pass passed, $fail failed"
echo "DONE ($row rows)"
