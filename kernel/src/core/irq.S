/*
 * minemu_irq_trampoline: IRQ entry and exit.  (HW1 Task 2 -- YOU write this.)
 *
 * The vector table (src/startup/vectors.S) branches here on every IRQ. At
 * that point the CPU has already:
 *   - copied CPSR of the interrupted code into SPSR_irq
 *   - switched to IRQ mode (so sp and lr are now the *IRQ-banked* copies;
 *     sp points at the IRQ stack that boot.S set up)
 *   - masked further IRQs (CPSR.I = 1)
 *   - set lr_irq = (address of the interrupted instruction) + 4
 * It has NOT saved r0-r12, built a frame, called C, or returned. We do that.
 *
 * struct minemu_trap_frame (minemu/trap.h) is 88 bytes:
 *     offset  0  r[0..12]      (13 words)
 *     offset 52  return_lr
 *     offset 56  spsr
 *     offset 60  exception_id
 *     offset 64  fault_pc
 *     offset 68  dfsr          (0 for IRQs)
 *     offset 72  dfar          (0 for IRQs)
 *     offset 76  user_sp
 *     offset 80  user_lr
 *     offset 84  reserved      (0)
 *
 * Reference: examples/irq-context-switch/irq.S, and the three exception
 * entries in src/startup/vectors.S (same shape, different adjustments).
 *
 * Useful instructions:
 *   sub/add sp, sp, #N          allocate/free stack space
 *   stmia rX, {r0-r12}          store 13 regs to [rX], [rX+4], ...
 *   ldmia rX, {r0-r12}          load them back
 *   str/ldr rA, [rB, #off]      one word at an offset
 *   mrs rX, spsr / msr spsr_cxsf, rX    read/write SPSR
 *   stmia rX, {sp, lr}^         the ^ means "the USER-mode sp/lr", not ours
 *   ldmia rX, {sp, lr}^         (same, for loading)
 *   bl func                     call C; first argument goes in r0
 *   subs pc, lr, #4             exception return: pc = lr - 4 AND cpsr = spsr
 *
 * Questions to be able to answer afterwards (the TA may ask):
 *   Q1. Why may we freely use r1 as scratch after step 2 but not before?
 *   Q2. Why is the frame 88 bytes and not 60? Why does 88 keep sp 8-aligned,
 *       and why does the C calling convention (AAPCS) care?
 *   Q3. Why does the return use "subs pc, lr, #4" and not "mov pc, lr"?
 *       What does the "s" do, and why "- 4"?
 *   Q4. Why reload sp from the dispatcher's return value (r0) before restoring?
 *   Q5. What would break if the trampoline forgot to save r12?
 */

.syntax unified
.cpu cortex-a9
.arm

.global minemu_irq_trampoline
.type minemu_irq_trampoline, %function
.extern minemu_irq_dispatch

minemu_irq_trampoline:
    /* 1. Allocate one trap frame (88 bytes) on the IRQ stack. */
    sub     sp, sp, #88             @ stack grows down; sp now = &frame

    /* 2. Save r0-r12 at offset 0. */
    stmia   sp, {r0-r12}            @ frame->r[0..12]; r0-r12 are now free to use

    /* 3. Save lr_irq at return_lr (52) and SPSR at spsr (56). */
    str     lr, [sp, #52]           @ frame->return_lr = lr_irq (resume addr + 4)
    mrs     r1, spsr                @ SPSR can't be stored directly: copy to r1
    str     r1, [sp, #56]           @ frame->spsr = interrupted code's CPSR

    /* 4. Read the interrupt controller's CLAIM register (0x10000000 + 8)
     *    and store the source id at exception_id (60). */
    ldr     r1, =0x10000000         @ r1 = interrupt controller base ADDRESS
    ldr     r1, [r1, #8]            @ r1 = VALUE of CLAIM (base + 8) = source id
    str     r1, [sp, #60]           @ frame->exception_id = source id

    /* 5. Record the interrupted instruction's address at fault_pc (64):
     *    lr_irq - 4. Zero dfsr (68) and dfar (72). */
    sub     r1, lr, #4              @ lr_irq is 4 past the interrupted instruction
    str     r1, [sp, #64]           @ frame->fault_pc = where we will resume
    mov     r1, #0
    str     r1, [sp, #68]           @ frame->dfsr = 0 (only used by data aborts)
    str     r1, [sp, #72]           @ frame->dfar = 0

    /* 6. Save the user-mode banked sp and lr at user_sp (76) / user_lr (80),
     *    and zero reserved (84). */
    add     r1, sp, #76             @ r1 = &frame->user_sp
    stmia   r1, {sp, lr}^           @ ^ = USER-mode sp/lr -> user_sp, user_lr
    mov     r1, #0
    str     r1, [sp, #84]           @ frame->reserved = 0

    /* 7. Call minemu_irq_dispatch(frame): the frame's address is sp. */
    mov     r0, sp                  @ arg 0 = &frame (88 = 8*11 keeps sp 8-aligned)
    bl      minemu_irq_dispatch     @ C handler; returns the frame to resume in r0

    /* 8. Restore from the frame the dispatcher RETURNED (r0), in reverse:
     *    sp = returned frame, lr_irq, SPSR, user sp/lr, r0-r12, free the
     *    frame, then do the exception return. */
    mov     sp, r0                  @ resume from the RETURNED frame (scheduler hook)
    ldr     lr, [sp, #52]           @ lr_irq = frame->return_lr
    ldr     r1, [sp, #56]
    msr     spsr_cxsf, r1           @ SPSR = frame->spsr (all fields)
    add     r1, sp, #76
    ldmia   r1, {sp, lr}^           @ user-mode sp/lr = frame->user_sp/user_lr
    ldmia   sp, {r0-r12}            @ r0-r12 back LAST: r1 was scratch until here
    add     sp, sp, #88             @ free the frame
    subs    pc, lr, #4              @ pc = lr - 4 AND cpsr = spsr: back to the
                                    @ interrupted code, old mode + IRQ mask restored
