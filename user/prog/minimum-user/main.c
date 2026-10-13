/*
 * msh: the minimum shell, now a user-space program (HW2 Task 2).
 *
 * Ported from the HW1 kernel shell (kernel/src/core/msh.c). The parser is
 * unchanged; only the I/O layer moved: every byte in or out now goes through
 * the ioctl syscall instead of touching the UART registers, which this
 * program can no longer reach (MMIO pages are mapped supervisor-only).
 */
#include <stddef.h>
#include <stdint.h>

#include "minemu/user_abi.h"

#define PROMPT "msh> "
#define MSH_LINE_MAX 128U /* tests/hw2 send 25-byte lines; spec says "up to 20" */

#define ASCII_BACKSPACE '\x08'
#define ASCII_DELETE '\x7f'

/* ------------------------------------------------------------------------ */
/* I/O over ioctl                                                           */
/* ------------------------------------------------------------------------ */

static void out_write(const char *buf, size_t len) {
    while (len != 0) {
        int32_t n = ioctl(MINEMU_FD_UART_OUT, MINEMU_IOCTL_UART_WRITE, (void *)buf, (uint32_t)len);
        if (n <= 0) {
            return; /* nothing sensible to do on a failed console write */
        }
        buf += n;
        len -= (size_t)n;
    }
}

static void out_puts(const char *s) {
    size_t n = 0;
    while (s[n] != '\0') {
        ++n;
    }
    out_write(s, n);
}

static void out_putc(char c) {
    out_write(&c, 1);
}

/*
 * uart_read is non-blocking ("all data available, up to n"), so batch reads
 * into a small buffer and poll when it's empty. One syscall per keystroke
 * burst instead of one per byte.
 */
static char in_buf[32];
static size_t in_len;
static size_t in_pos;

static char in_getc(void) {
    while (in_pos == in_len) {
        int32_t n = ioctl(MINEMU_FD_UART_IN, MINEMU_IOCTL_UART_READ, (void *)in_buf,
                          (uint32_t)sizeof(in_buf));
        in_pos = 0;
        in_len = n > 0 ? (size_t)n : 0;
    }
    return in_buf[in_pos++];
}

/* ------------------------------------------------------------------------ */
/* Parsing (unchanged from HW1)                                             */
/* ------------------------------------------------------------------------ */

struct word {
    const char *start;
    size_t len;
};

static int next_word(const char *line, size_t len, size_t *pos, struct word *out) {
    size_t i = *pos;
    while (i < len && line[i] == ' ') {
        ++i;
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
    return literal[i] == '\0';
}

static void cmd_echo(const char *line, size_t len, size_t pos) {
    struct word w;
    int first = 1;
    while (next_word(line, len, &pos, &w)) {
        if (!first) {
            out_putc(' ');
        }
        out_write(w.start, w.len);
        first = 0;
    }
    out_putc('\n');
}

static void execute(const char *line, size_t len) {
    size_t pos = 0;
    struct word cmd;

    if (!next_word(line, len, &pos, &cmd)) {
        return;
    }
    if (word_equals(&cmd, "echo")) {
        cmd_echo(line, len, pos);
    } else {
        out_puts("command not found: ");
        out_write(cmd.start, cmd.len);
        out_putc('\n');
    }
}

/* ------------------------------------------------------------------------ */
/* Line editing (unchanged from HW1)                                        */
/* ------------------------------------------------------------------------ */

void minemu_user_main(void) {
    char line[MSH_LINE_MAX];
    size_t len = 0;
    int overflowed = 0;

    out_puts(PROMPT);
    for (;;) {
        char c = in_getc();

        if (c == '\n') {
            if (!overflowed) {
                execute(line, len);
            }
            len = 0;
            overflowed = 0;
            out_puts(PROMPT);
        } else if (overflowed) {
            /* discard until newline */
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
