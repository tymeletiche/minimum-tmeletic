#ifndef MINIMUM_KPRINTF_H
#define MINIMUM_KPRINTF_H

#include <stdarg.h>

/*
 * Minimal kernel printf over the console UART.
 *
 * Supported conversions: %s %c %d %i %u %x %p %%
 * No width, precision, or length modifiers. An unknown conversion is
 * printed literally (e.g. "%q") so mistakes are visible, not silent.
 */
void kprintf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void kvprintf(const char *fmt, va_list args);

#endif
