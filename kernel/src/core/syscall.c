/*
 * HW2: SVC dispatch and the ioctl syscall.
 *
 * Split of work (see report / chat log):
 *   - Tyler: minemu_svc_dispatch validation chain, user_range_ok().
 *   - Claude (claude-opus-5-5): copy helpers, ioctl device handlers.
 * Device semantics follow minemu/user_abi.h and tests/hw2 (the autograded
 * ABI), which differs from the fd/op table on the course website.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "minemu/mmu.h"
#include "minemu/platform.h"
#include "minemu/runtime.h"
#include "minemu/syscall.h"
#include "minemu/trace.h"
#include "minemu/trap.h"
#include "minemu/user_abi.h"
#include "minimum/kprintf.h"
#include "minimum/loader.h"
#include "minimum/uart.h"

#define CPSR_MODE_MASK UINT32_C(0x1f)
#define CPSR_MODE_USR UINT32_C(0x10)
#define CPSR_THUMB UINT32_C(0x20)
#define A32_SVC_ZERO UINT32_C(0xef000000)

/* ------------------------------------------------------------------------ */
/* User-pointer validation (Tyler)                                          */
/* ------------------------------------------------------------------------ */

/*
 * Return true iff every byte of [addr, addr + len) is mapped for USER access
 * with READ permission, and also WRITE permission when `write` is true.
 *
 * Use loader_user_pte(page_vaddr): it returns the PTE for a user address, or
 * 0 if the address is outside the user window or unmapped.
 * Flags: MINEMU_PTE_VALID, MINEMU_PTE_USER, MINEMU_PTE_READABLE,
 *        MINEMU_PTE_WRITABLE (minemu/mmu.h). Page = addr & MINEMU_PTE_PAGE_MASK.
 *
 * Think about: len == 0, addr + len wrapping past 0xffffffff, a range that
 * starts mid-page and ends mid-page in the next one. No / or %.
 */
static bool user_range_ok(uint32_t addr, uint32_t len, bool write)
{
    // zero len ranges are always okay
    if (len == 0)
    {
        return true;
    }

    // guard against integer overflow wrapping past 0xFFFFFFFF
    if (addr + len - 1 < addr)
    {
        return false;
    }

    // construct the required bitmask based on access requirements
    uint32_t req_bits = MINEMU_PTE_VALID | MINEMU_PTE_USER | MINEMU_PTE_READABLE;
    if (write)
    {
        req_bits |= MINEMU_PTE_WRITABLE;
    }

    // isolate the starting and ending page boundaries
    uint32_t start_page = addr & MINEMU_PTE_PAGE_MASK;
    uint32_t end_page = (addr + len - 1) & MINEMU_PTE_PAGE_MASK;

    // scan each page without using div or modulo
    for (uint32_t curr_page = start_page;; curr_page += MINEMU_PAGE_SIZE)
    {
        uint32_t pte = loader_user_pte(curr_page);

        // make sure all permission and verification flags are matched
        if ((pte & req_bits) != req_bits)
        {
            return false;
        }

        // safe loop break that protects against wrapping
        if (curr_page == end_page)
        {
            break;
        }
    }

    return true;
}

/* ------------------------------------------------------------------------ */
/* Copy helpers: only call after user_range_ok() has passed for the range.  */
/* ------------------------------------------------------------------------ */

static uint8_t user_load_byte(uint32_t addr)
{
    return *loader_user_kaddr(addr);
}

static void user_store_byte(uint32_t addr, uint8_t value)
{
    *loader_user_kaddr(addr) = value;
}

static void user_store_word(uint32_t addr, uint32_t value)
{
    for (uint32_t i = 0; i < 4U; ++i)
    {
        user_store_byte(addr + i, (uint8_t)(value >> (i << 3)));
    }
}

/* ------------------------------------------------------------------------ */
/* ioctl device handlers                                                    */
/* ------------------------------------------------------------------------ */

static int32_t ioctl_uart_read(uint32_t dst, uint32_t n)
{
    if ((int32_t)n < 0 || !user_range_ok(dst, n, true))
    {
        return MINEMU_IOCTL_ERROR;
    }
    /* Range is valid, so popping input now can't strand bytes on failure. */
    char chunk[32];
    uint32_t total = 0;
    while (total < n)
    {
        uint32_t want = n - total;
        if (want > sizeof(chunk))
        {
            want = sizeof(chunk);
        }
        size_t got = uart_read_available(chunk, want);
        for (size_t i = 0; i < got; ++i)
        {
            user_store_byte(dst + total + (uint32_t)i, (uint8_t)chunk[i]);
        }
        total += (uint32_t)got;
        if (got < want)
        {
            break; /* buffer drained: return what we have (non-blocking) */
        }
    }
    return (int32_t)total;
}

static int32_t ioctl_uart_write(int fd, uint32_t src, uint32_t n)
{
    if ((int32_t)n < 0 || !user_range_ok(src, n, false))
    {
        return MINEMU_IOCTL_ERROR;
    }
    for (uint32_t i = 0; i < n; ++i)
    {
        char c = (char)user_load_byte(src + i);
        if (fd == MINEMU_FD_UART_DIAG)
        {
            uart1_write(&c, 1);
        }
        else
        {
            uart_putc(c);
        }
    }
    return (int32_t)n;
}

static int32_t ioctl_uart_status(int fd)
{
    volatile struct minemu_uart_regs *uart = fd == MINEMU_FD_UART_DIAG ? MINEMU_UART1 : MINEMU_UART0;
    uint32_t status = uart->status;
    int32_t result = 0;
    if ((status & MINEMU_UART_STATUS_RX_READY) != 0)
    {
        result |= MINEMU_IOCTL_UART_RX_READY;
    }
    if ((status & MINEMU_UART_STATUS_TX_READY) != 0)
    {
        result |= MINEMU_IOCTL_UART_TX_READY;
    }
    return result;
}

static int32_t ioctl_uart(int fd, uint32_t request, uint32_t arg0, uint32_t arg1)
{
    switch (request)
    {
    case MINEMU_IOCTL_UART_READ:
        /* Only UART0 has an RX path (the HW1 IRQ ring buffer). */
        return fd == MINEMU_FD_UART_IN ? ioctl_uart_read(arg0, arg1) : MINEMU_IOCTL_ERROR;
    case MINEMU_IOCTL_UART_WRITE:
        return fd == MINEMU_FD_UART_IN ? MINEMU_IOCTL_ERROR : ioctl_uart_write(fd, arg0, arg1);
    case MINEMU_IOCTL_UART_STATUS:
        return ioctl_uart_status(fd);
    default:
        return MINEMU_IOCTL_ERROR;
    }
}

static int32_t ioctl_rng(uint32_t request, uint32_t arg0)
{
    switch (request)
    {
    case MINEMU_IOCTL_RNG_SEED:
        MINEMU_RNG->seed = arg0;
        return 0;
    case MINEMU_IOCTL_RNG_NEXT:
    case MINEMU_IOCTL_RNG_STATE:
        /* Validate first: reading DATA advances the generator. */
        if ((arg0 & 3U) != 0 || !user_range_ok(arg0, 4U, true))
        {
            return MINEMU_IOCTL_ERROR;
        }
        user_store_word(arg0, request == MINEMU_IOCTL_RNG_NEXT ? MINEMU_RNG->data : MINEMU_RNG->state);
        return 0;
    default:
        return MINEMU_IOCTL_ERROR;
    }
}

static int32_t ioctl_dispatch(int fd, uint32_t request, uint32_t arg0, uint32_t arg1)
{
    switch (fd)
    {
    case MINEMU_FD_UART_IN:
    case MINEMU_FD_UART_OUT:
    case MINEMU_FD_UART_DIAG:
        return ioctl_uart(fd, request, arg0, arg1);
    case MINEMU_FD_RNG:
        return ioctl_rng(request, arg0);
    case MINEMU_FD_TRACE:
        if (request != MINEMU_IOCTL_TRACE_EVENT)
        {
            return MINEMU_IOCTL_ERROR;
        }
        minemu_trace_event(arg0);
        return 0;
    default:
        return MINEMU_IOCTL_ERROR;
    }
}

/* ------------------------------------------------------------------------ */
/* SVC dispatcher (Tyler)                                                   */
/* ------------------------------------------------------------------------ */

/*
 * Called by minemu_svc_trampoline (svc.S) in SVC mode, IRQs masked, with the
 * caller's state saved in *frame. Return the frame to resume.
 *
 * TODO(Tyler): before trusting anything in the frame, reject (r0 = -1) unless
 *   1. frame->exception_id == MINEMU_EXCEPTION_SVC
 *   2. the caller was in USR mode          (frame->spsr, CPSR_MODE_*)
 *   3. the caller was A32, not Thumb       (CPSR_THUMB)
 *   4. the word at frame->fault_pc is A32_SVC_ZERO
 *        - fault_pc is a USER address: check it with user_range_ok first,
 *          then read it with user_load_byte (4 bytes, little-endian)
 * Then route on frame->r[7]: MINEMU_SYSCALL_IOCTL -> ioctl_dispatch(
 *   (int)r[0], r[1], r[2], r[3]); anything else -> -1.
 * The result goes in frame->r[0].
 */
struct minemu_trap_frame *minemu_svc_dispatch(struct minemu_trap_frame *frame)
{
    // 1. confirm the handler was invoked due to an actual SVC exception
    if (frame->exception_id != MINEMU_EXCEPTION_SVC)
    {
        goto reject;
    }

    // 2 conf the exception originated from USR
    if ((frame->spsr & CPSR_MODE_MASK) != CPSR_MODE_USR)
    {
        goto reject;
    }

    // 3. conf the application was running in A32 ARM mode and not thumb mode
    if ((frame->spsr & CPSR_THUMB) != 0)
    {
        goto reject;
    }

    // 4. conf the 4-byte instruction space at fault_pc is mapped and readable
    if (!user_range_ok(frame->fault_pc, 4, false))
    {
        goto reject;
    }

    // 5. decode the little endian instruction (located at fault_[c])
    uint32_t svc_word = (uint32_t)user_load_byte(frame->fault_pc) |
                        ((uint32_t)user_load_byte(frame->fault_pc + 1) << 8) |
                        ((uint32_t)user_load_byte(frame->fault_pc + 2) << 16) |
                        ((uint32_t)user_load_byte(frame->fault_pc + 3) << 24);

    // 6. enforce that the actual triggering operation matches A32_SVC_ZERO
    if (svc_word != A32_SVC_ZERO)
    {
        goto reject;
    }

    // 7 inspect r7 to dispatch the registered system call identifier
    if (frame->r[7] == MINEMU_SYSCALL_IOCTL)
    {
        frame->r[0] = (uint32_t)ioctl_dispatch(
            (int)frame->r[0],
            frame->r[1],
            frame->r[2],
            frame->r[3]);
    }
    else
    {
        goto reject;
    }

    return frame;

reject:
    // safe error path for invalid application requests
    frame->r[0] = (uint32_t)-1;
    return frame;
}

/* ------------------------------------------------------------------------ */
/* Faults: a user program that touches memory it shouldn't is stopped.      */
/* ------------------------------------------------------------------------ */

void minemu_undefined_dispatch(struct minemu_trap_frame *frame)
{
    kprintf("undefined instruction at %p\n", (void *)(uintptr_t)frame->fault_pc);
    minemu_fail_stop();
}

void minemu_abort_dispatch(struct minemu_trap_frame *frame)
{
    kprintf("abort at pc %p addr %p status %x\n", (void *)(uintptr_t)frame->fault_pc,
            (void *)(uintptr_t)frame->dfar, (unsigned)frame->dfsr);
    minemu_fail_stop();
}
