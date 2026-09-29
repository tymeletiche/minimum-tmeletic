#include "minimum/kprintf.h"

#include <stdint.h>

#include "minimum/uart.h"

/*
 * Number formatting deliberately avoids the / and % operators.
 *
 * The Cortex-A9 has no hardware divide instruction, so GCC lowers a
 * runtime division to a libgcc helper (__aeabi_uidiv). Under minemu 0.2.3
 * that helper hangs for larger dividends (e.g. 100000 / 10 never returns),
 * so we format numbers with only subtraction and shifts instead.
 */

/* Decimal: count how many times each power of ten fits, largest first. */
static void print_decimal(uint32_t value) {
    static const uint32_t powers[] = {
        1000000000u, 100000000u, 10000000u, 1000000u, 100000u,
        10000u, 1000u, 100u, 10u, 1u,
    };
    int started = 0; /* suppress leading zeros */

    for (unsigned i = 0; i < sizeof(powers) / sizeof(powers[0]); ++i) {
        char digit = '0';
        while (value >= powers[i]) {
            value -= powers[i];
            ++digit; /* at most 9 iterations per place (4 for 10^9) */
        }
        if (digit != '0' || started || powers[i] == 1u) {
            uart_putc(digit);
            started = 1;
        }
    }
}

/* Hex: each nibble is 4 bits, so shift instead of dividing by 16. */
static void print_hex(uint32_t value) {
    static const char digits[] = "0123456789abcdef";
    int started = 0;

    for (int shift = 28; shift >= 0; shift -= 4) {
        uint32_t nibble = (value >> shift) & 0xfu;
        if (nibble != 0 || started || shift == 0) {
            uart_putc(digits[nibble]);
            started = 1;
        }
    }
}

static void print_signed(int32_t value) {
    if (value < 0) {
        uart_putc('-');
        /*
         * Negate in unsigned arithmetic: -INT32_MIN overflows int32_t,
         * but 0u - (uint32_t)INT32_MIN == 2147483648u is well defined.
         */
        print_decimal(0u - (uint32_t)value);
    } else {
        print_decimal((uint32_t)value);
    }
}

void kvprintf(const char *fmt, va_list args) {
    for (const char *p = fmt; *p != '\0'; ++p) {
        if (*p != '%') {
            uart_putc(*p);
            continue;
        }

        ++p; /* look at the conversion character */
        switch (*p) {
        case 's': {
            const char *s = va_arg(args, const char *);
            uart_puts(s != NULL ? s : "(null)");
            break;
        }
        case 'c':
            /* char is promoted to int when passed through "...". */
            uart_putc((char)va_arg(args, int));
            break;
        case 'd':
        case 'i':
            print_signed(va_arg(args, int32_t));
            break;
        case 'u':
            print_decimal(va_arg(args, uint32_t));
            break;
        case 'x':
            print_hex(va_arg(args, uint32_t));
            break;
        case 'p':
            uart_puts("0x");
            print_hex((uint32_t)(uintptr_t)va_arg(args, void *));
            break;
        case '%':
            uart_putc('%');
            break;
        case '\0':
            /* Format string ended with a lone '%': print it and stop. */
            uart_putc('%');
            return;
        default:
            /* Unknown conversion: echo it so the bug is visible. */
            uart_putc('%');
            uart_putc(*p);
            break;
        }
    }
}

void kprintf(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    kvprintf(fmt, args);
    va_end(args);
}
