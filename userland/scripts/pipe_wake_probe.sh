# pipe_wake_probe.sh -- reproducer probe for open-issues item 7d,
# the lost command-substitution wake.
#
# A command substitution x=$(cmd) forks, wires a pipe, execs cmd,
# reads the pipe to EOF, and wait4()s the child.  If any wake in
# that chain is lost, the shell blocks forever.
#
# This script is the smallest shape of that chain: one fork+exec+
# pipe+wait4 per iteration, nothing else.  It prints the iteration
# marker BEFORE the substitution so that a hang names its own
# iteration in the capture, the same discipline test.sh uses with
# its [N] run lines.
#
# Run to completion prints "loopdone".  A hang leaves the last
# "[N] seq" line as the last thing on the console.
#
# It is not a canary row: it takes seconds and it can hang.  Run
# it by hand, per open-issues.md item 7d.

i=0
while [ $i -lt 200 ]; do
    echo "[$i] seq"
    x=$(seq 1 3)
    i=$((i+1))
done
echo loopdone
