#ifndef MINIMUM_MSH_H
#define MINIMUM_MSH_H

/*
 * msh: the minimum kernel shell.
 *
 * Line discipline:
 *   - '\n' is the only terminator.
 *   - 0x08 (BS) and 0x7f (DEL) erase the last byte; ignored on an empty line.
 *   - A line holds at most MSH_LINE_MAX bytes, not counting the '\n'. When a
 *     line overflows, the rest of it is discarded up to its '\n' and the
 *     shell re-prompts without running anything.
 *   - Input is not echoed: the TUI console mirrors typed keys itself.
 *
 * Commands (words are separated by one or more ASCII spaces):
 *   echo [WORD...]  print the words joined by single spaces, then '\n'
 *   anything else   "command not found: WORD\n" (first word)
 *   empty/blank     re-prompt
 */
#define MSH_LINE_MAX 20

/* Run the shell forever. Requires UART RX interrupts and CPU IRQs enabled. */
void msh_run(void) __attribute__((noreturn));

#endif
