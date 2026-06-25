#include "m0ss.h"

#define SCB_ICSR            0xE000ED04
#define SCB_ICSR_PENDSVSET  (1U << 28)

#define DISABLE_IRQ() asm volatile("CPSID i")
#define ENABLE_IRQ()  asm volatile("CPSIE i")

static m0ss_thread* volatile thread_running;
static m0ss_thread* volatile thread_to_run;

// Note: Performs the context switch.
__attribute__((naked))
void PendSV_Handler(void)
{
    // Note: This is a critical section which accesses the scheduler state.

    asm volatile("CPSID i"); // Disable interrupts

    /* if (thread_running) { */
    asm volatile("LDR r0, =thread_running"); // r0 holds &thread_running
    asm volatile("LDR r1, [r0]"); // r1 holds thread_running (a pointer)
    asm volatile("CMP r1, #0");
    asm volatile("BEQ thread_not_running");

    // ARMv6 can't PUSH to high registers directly.
    asm volatile("MOV r2, r11");
    asm volatile("PUSH {r2}");
    asm volatile("MOV r2, r10");
    asm volatile("PUSH {r2}");
    asm volatile("MOV r2, r9");
    asm volatile("PUSH {r2}");
    asm volatile("MOV r2, r8");
    asm volatile("PUSH {r2}");

    asm volatile("PUSH {r4-r7}");

    /* thread_running->stack_ptr = sp; */
    asm volatile("MOV r2, sp"); // r2 = sp
    asm volatile("STR r2, [r1, #0]"); // thread_ruuning->stack_ptr = r2
    /* } */

    asm volatile("thread_not_running:");

    /* thread_running = thread_to_run; */
    asm volatile("LDR r1, =thread_to_run"); // r1 holds &thread_to_run
    asm volatile("LDR r2, [r1]"); // r2 holds thread_to_run (a pointer)
    asm volatile("STR r2, [r0]"); // store thread_to_run to thread_running

    /* sp = thread_to_run->stack_ptr */
    // ARMv6 can't LDR to SP directly.
    asm volatile("LDR r0, [r2, #0]"); // r0 = thread_to_run->stack_ptr
    asm volatile("MOV sp, r0"); // sp = r0

    /* Note: Pop manually pushed registers. */

    asm volatile("POP {r4-r7}"); // load r4-r7

    // ARMv6 can't POP high registers directly.
    asm volatile("POP {r0}");
    asm volatile("MOV r8, r0");
    asm volatile("POP {r0}");
    asm volatile("MOV r9, r0");
    asm volatile("POP {r0}");
    asm volatile("MOV r10, r0");
    asm volatile("POP {r0}");
    asm volatile("MOV r11, r0");

    // Note: Hardware will pop the remaining registers from the stack.

    asm volatile("CPSIE i"); // Enable interrupts

    // Note: LR contains EXC_RETURN value pushed right after stacking on exception entry.
    asm volatile("BX lr");
}

void m0ss_thread_create(m0ss_thread* thread, m0ss_thread_func_ptr func_ptr, void* stack_buf, uint32_t stack_buf_capacity)
{
    /* Note:
     * In ARM Cortex M0/M0+:
     * 1. The stack grows downward (full-descending allocation model)
     * 2. Pushing stores the register value at the newly decremented address (SP - 4)
     */

#ifdef DEBUG
    // Prefill the stack buffer with reset value.
    for (uint8_t* sp = (uint8_t*)stack_buf; sp < (uint8_t*)stack_buf + stack_buf_capacity; ++sp) {
        *sp = 0x5A;
    }
#endif

    // Stack pointer has to be 8 byte-aligned (round down if necessary).
    uint32_t* sp = (uint32_t*)(((uint32_t)stack_buf + stack_buf_capacity) & (~0x07));

    /* Note: Cortex™-M0+ Devices, Generic User Guide, 2.3.6 Exception entry and return
     * On exception entry, the following registers are pushed onto the stack:
     * xPSR, PC, LR, R12, R3, R2, R1, R0
     */

    /* These registers are pushed by hardware on exception entry (stacking). */
    *(--sp) = 1U << 24; // PSR
    *(--sp) = (uint32_t)func_ptr; // PC (R15)
    *(--sp) = 0x0000000E; // LR (R14)
    *(--sp) = 0x0000000C; // R12
    *(--sp) = 0x00000003; // R3
    *(--sp) = 0x00000002; // R2
    *(--sp) = 0x00000001; // R1
    *(--sp) = 0x00000000; // R0

    /* These registers are NOT pushed by hardware on exception entry. */
    *(--sp) = 0x0000000B; // R11
    *(--sp) = 0x0000000A; // R10
    *(--sp) = 0x00000009; // R9
    *(--sp) = 0x00000008; // R8
    *(--sp) = 0x00000007; // R7
    *(--sp) = 0x00000006; // R6
    *(--sp) = 0x00000005; // R5
    *(--sp) = 0x00000004; // R4

    thread->sp = sp;

    // TODO: Implement scheduling.
    thread_to_run = thread;
}

void m0ss_start()
{
    // Note: Thread mode, using MSP.

    m0ss_schedule();
}

// Note: Schedules the execution of the threads.
void m0ss_schedule()
{
    // Note: This is a critical section which accesses the scheduler state.

    DISABLE_IRQ();

    // TODO: Implement scheduling.
    thread_to_run = thread_to_run;

    *(volatile uint32_t*)SCB_ICSR |= SCB_ICSR_PENDSVSET;

    ENABLE_IRQ();
}
