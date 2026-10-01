#include "minimum/msh.h"

#include <stddef.h>

#include "minimum/kprintf.h"
#include "minimum/uart.h"

#define PROMPT "msh> "

#define ASCII_BACKSPACE '\x08'
#define ASCII_DELETE '\x7f'

/* ------------------------------------------------------------------------ */
/* Parsing                                                                   */
/* ------------------------------------------------------------------------ */

/* A word is a [start, start + len) slice of the line buffer (not NUL-terminated). */
struct word {
    const char *start;
    size_t len;
};

/*
 * Find the next space-delimited word at or after *pos in line[0..len).
 * Returns 0 when there are no more words.
 */
static int next_word(const char *line, size_t len, size_t *pos, struct word *out) {
    size_t i = *pos;
    while (i < len && line[i] == ' ') {
        ++i; /* skip any run of spaces */
    }
    if (i == len) {
        *pos = i;
        return 0;
    }
    size_t start = i;
    while (i < len && line[i] != ' ') {
        ++i;
    }
    out->start = &line[start];
    out->len = i - start;
    *pos = i;
    return 1;
}

static int word_equals(const struct word *w, const char *literal) {
    size_t i = 0;
    for (; i < w->len; ++i) {
        if (literal[i] == '\0' || literal[i] != w->start[i]) {
            return 0;
        }
    }
    return literal[i] == '\0'; /* same length, not just a prefix */
}

static void cmd_echo(const char *line, size_t len, size_t pos) {
    struct word w;
    int first = 1;
    while (next_word(line, len, &pos, &w)) {
        if (!first) {
            uart_putc(' '); /* repeated spaces collapse to one separator */
        }
        uart_write(w.start, w.len);
        first = 0;
    }
    uart_putc('\n');
}

static void execute(const char *line, size_t len) {
    size_t pos = 0;
    struct word cmd;

    if (!next_word(line, len, &pos, &cmd)) {
        return; /* empty or all-space line */
    }

    if (word_equals(&cmd, "echo")) {
        cmd_echo(line, len, pos);
    } else {
        uart_puts("command not found: ");
        uart_write(cmd.start, cmd.len);
        uart_putc('\n');
    }
}

/* ------------------------------------------------------------------------ */
/* Line editing                                                              */
/* ------------------------------------------------------------------------ */

void msh_run(void) {
    char line[MSH_LINE_MAX];
    size_t len = 0;
    int overflowed = 0; /* discarding the rest of an overlong line */

    uart_puts(PROMPT);
    for (;;) {
        char c = uart_getc();

        if (c == '\n') {
            if (!overflowed) {
                execute(line, len);
            }
            len = 0;
            overflowed = 0;
            uart_puts(PROMPT);
        } else if (overflowed) {
            /* Swallow everything (including backspaces) until the newline. */
        } else if (c == ASCII_BACKSPACE || c == ASCII_DELETE) {
            if (len > 0) {
                --len;
            }
        } else if (len < MSH_LINE_MAX) {
            line[len++] = c;
        } else {
            overflowed = 1;
        }
    }
}
