/*
 * HW2: SVC (syscall) entry trampoline -- written by Tyler.
 *
 * Same 88-byte struct minemu_trap_frame as irq.S. Start from your irq.S and
 * work out what differs for an SVC exception:
 *   - what goes in frame->exception_id  (hint: minemu/trap.h)
 *   - what LR_svc points at, so what frame->fault_pc should hold
 *   - how to return (LR_svc already points at the right instruction)
 */
.syntax unified
.cpu cortex-a9
.arm

.global minemu_svc_trampoline
.type minemu_svc_trampoline, %function
.extern minemu_svc_dispatch

@ TODO(Tyler): minemu_svc_trampoline
