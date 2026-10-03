/*
 * churn_helper -- the execve'd side of the exec_churn test.
 *
 * Its entire contract is "load and exit 0".  It is the smallest
 * possible program that exercises elf_load_into_process: the
 * kernel must read the ELF, map its segments (which is where the
 * bootloader's huge-page split runs), set up the stack, and enter
 * it, and then the process must be able to return from main and
 * exit.
 *
 * It is deliberately separate from envp_helper: envp_helper exits
 * 2 when its test variable is absent, which would make a churn
 * run red for a reason that has nothing to do with churn.  A
 * helper whose only behavior is "exit 0" keeps exec_churn's
 * failures meaningful.
 *
 * The churn test checks the child's exit STATUS, not its output,
 * because a parent cannot capture a child's stdout on donix.
 */
int main(void) {
    return 0;
}
