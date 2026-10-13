/*
 * HW2 extra test (Tyler + claude-opus-5-5): hostile ioctl arguments.
 * Every bad call must return a negative value without side effects; each
 * result is traced so the manifest can check them in order.
 */
#include <stdint.h>

#include "minemu/user_abi.h"

#define TRACE(v) (void)ioctl(MINEMU_FD_TRACE, MINEMU_IOCTL_TRACE_EVENT, (uint32_t)(v))

static int32_t raw_svc(uint32_t number) {
    register uint32_t r0 __asm__("r0") = 0;
    register uint32_t r7 __asm__("r7") = number;
    __asm__ volatile("svc #0" : "+r"(r0) : "r"(r7) : "memory", "cc");
    return (int32_t)r0;
}

void minemu_user_main(void) {
    static char msg[4] = "ok\n";
    uint32_t value = 0;

    /* kernel, MMIO, unmapped, wrapping, and page-straddling source buffers */
    TRACE(ioctl(MINEMU_FD_UART_OUT, MINEMU_IOCTL_UART_WRITE, (void *)0xc0030000u, 4u));
    TRACE(ioctl(MINEMU_FD_UART_OUT, MINEMU_IOCTL_UART_WRITE, (void *)0x10004000u, 4u));
    TRACE(ioctl(MINEMU_FD_UART_OUT, MINEMU_IOCTL_UART_WRITE, (void *)0x00700000u, 4u));
    TRACE(ioctl(MINEMU_FD_UART_OUT, MINEMU_IOCTL_UART_WRITE, (void *)0xfffffff0u, 0x20u));
    TRACE(ioctl(MINEMU_FD_UART_OUT, MINEMU_IOCTL_UART_WRITE, (void *)0x007ffffeu, 4u));
    /*
     * WRITE-path check: destination straddles the top of the user stack.
     * (A read-only code page would be the natural target, but user/common/
     * user.ld links .text and .data into one RWE segment, so code pages are
     * mapped writable -- noted in the report.)
     */
    TRACE(ioctl(MINEMU_FD_UART_IN, MINEMU_IOCTL_UART_READ, (void *)0x007ffffeu, 4u));
    /* bad fd, bad request, wrong direction, unknown syscall number */
    TRACE(ioctl(99, MINEMU_IOCTL_UART_WRITE, (void *)msg, 1u));
    TRACE(ioctl(MINEMU_FD_UART_OUT, 0x999u, (void *)msg, 1u));
    TRACE(ioctl(MINEMU_FD_UART_IN, MINEMU_IOCTL_UART_WRITE, (void *)msg, 1u));
    TRACE(raw_svc(42));
    /* RNG_NEXT to an unmapped pointer must NOT advance the generator */
    TRACE(ioctl(MINEMU_FD_RNG, MINEMU_IOCTL_RNG_NEXT, (uint32_t *)0x00700000u));
    (void)ioctl(MINEMU_FD_RNG, MINEMU_IOCTL_RNG_STATE, &value);
    TRACE(value);
    /* zero-length write is a successful no-op */
    TRACE(ioctl(MINEMU_FD_UART_OUT, MINEMU_IOCTL_UART_WRITE, (void *)msg, 0u));
    /* and a good call still works afterwards */
    TRACE(ioctl(MINEMU_FD_UART_OUT, MINEMU_IOCTL_UART_WRITE, (void *)msg, 3u));

    __asm__ volatile("bkpt #0");
    for (;;) {
    }
}
