/*
 * First entry into USR mode.
 * Adapted from examples/user-mode-hello/kernel/user.S (minimum-template).
 */
.syntax unified
.cpu cortex-a9
.arm

.global loader_enter_user
.type loader_enter_user, %function

/* r0 = A32 entry VA, r1 = aligned top of the user stack. Does not return. */
loader_enter_user:
    cpsid   if                      @ no interrupts while banked regs are half-set
    mov     r2, r0

    /* USR sp/lr are banked: set them with a "^" user-register transfer. */
    sub     sp, sp, #8
    str     r1, [sp]                @ user sp = stack top
    mov     r3, #0
    str     r3, [sp, #4]            @ user lr = 0 (returning from main faults)
    mov     r3, sp
    ldmia   r3, {sp, lr}^
    add     sp, sp, #8

    mov     r3, #0x10               @ SPSR = USR mode, A32, IRQ/FIQ unmasked
    msr     spsr_cxsf, r3
    mov     lr, r2
    mov     r0, #0                  @ don't leak kernel values into user regs
    mov     r1, #0
    mov     r2, #0
    mov     r3, #0
    movs    pc, lr                  @ pc = entry, CPSR = SPSR -> now in USR
