#include "m0ss.h"
#include "m0ss_conf.h"
#include <stddef.h>

#define SCB_ICSR            0xE000ED04U
#define SCB_ICSR_PENDSVSET  (1U << 28)

#define SCB_SHPR3           0xE000ED20U
#define SCB_SHPR3_PRI_14    (0xFFU << 16)
#define SCB_SHPR3_PRI_15    (0xFFU << 24)

#define SYSTICK_CSR             0xE000E010U
#define SYSTICK_CSR_ENABLE      (1U << 0)
#define SYSTICK_CSR_TICKINT     (1U << 1)
#define SYSTICK_CSR_CLKSOURCE   (1U << 2)

#define SYSTICK_RVR         0xE000E014U
#define SYSTICK_RVR_RELOAD  0xFFFFFFU

#define SYSTICK_RVR_RELOAD_VALUE ((M0SS_CONF_CORE_CLK_HZ / M0SS_CONF_TICK_RATE_HZ) - 1U)
_Static_assert((SYSTICK_RVR_RELOAD_VALUE >= 0x00000001U && SYSTICK_RVR_RELOAD_VALUE <= 0x00FFFFFFU), "SysTick RVR RELOAD value is out of range.");

#define SYSTICK_CVR 0xE000E018U

#define THREAD_IDLE_STACK_CAPACITY (1024U)

#define DISABLE_IRQ() asm volatile("CPSID i")
#define ENABLE_IRQ()  asm volatile("CPSIE i")

static m0ss_thread* threads[M0SS_CONF_MAX_THREAD_COUNT];
static uint8_t thread_count;

static m0ss_thread* volatile thread_running;
static m0ss_thread* volatile thread_to_run;

static m0ss_thread thread_idle;
_Alignas(8) static uint8_t thread_idle_stack[THREAD_IDLE_STACK_CAPACITY];

static void thread_idle_func()
{
    while (1) {
        asm volatile("WFI");

        m0ss_schedule();
    }
}

static void m0ss_thread_init(m0ss_thread* thread, m0ss_thread_priority priority, m0ss_thread_func_ptr func_ptr, void* stack_buf, uint32_t stack_buf_capacity)
{
    thread->priority = priority;

    thread->ticks_to_block = 0U;

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
    uint32_t* sp = (uint32_t*)(((uint32_t)stack_buf + stack_buf_capacity) & (~0x07U));

    /* Note: Cortex™-M0+ Devices, Generic User Guide, 2.3.6 Exception entry and return
     * On exception entry, the following registers are pushed onto the stack:
     * xPSR, PC, LR, R12, R3, R2, R1, R0
     */

    /* These registers are pushed by hardware on exception entry (stacking). */
    *(--sp) = 1U << 24; // PSR
    *(--sp) = (uint32_t)func_ptr; // PC (R15)
    *(--sp) = 0x0000000EU; // LR (R14)
    *(--sp) = 0x0000000CU; // R12
    *(--sp) = 0x00000003U; // R3
    *(--sp) = 0x00000002U; // R2
    *(--sp) = 0x00000001U; // R1
    *(--sp) = 0x00000000U; // R0

    /* These registers are NOT pushed by hardware on exception entry. */
    *(--sp) = 0x0000000BU; // R11
    *(--sp) = 0x0000000AU; // R10
    *(--sp) = 0x00000009U; // R9
    *(--sp) = 0x00000008U; // R8
    *(--sp) = 0x00000007U; // R7
    *(--sp) = 0x00000006U; // R6
    *(--sp) = 0x00000005U; // R5
    *(--sp) = 0x00000004U; // R4

    thread->sp = sp;
}

static void m0ss_init()
{
    m0ss_thread_init(&thread_idle, M0SS_THREAD_PRIORITY_LOWEST, thread_idle_func, thread_idle_stack, THREAD_IDLE_STACK_CAPACITY);

    // Set PendSV and SysTick system handler priority to the lowest possible.
    *(volatile uint32_t*)SCB_SHPR3 |= SCB_SHPR3_PRI_14 | SCB_SHPR3_PRI_15;

    *(volatile uint32_t*)SYSTICK_RVR = SYSTICK_RVR_RELOAD_VALUE;
    *(volatile uint32_t*)SYSTICK_CVR = 0U;
    *(volatile uint32_t*)SYSTICK_CSR = SYSTICK_CSR_ENABLE | SYSTICK_CSR_TICKINT | SYSTICK_CSR_CLKSOURCE;
}

static void m0ss_tick()
{
    for (uint8_t thread_index = 0; thread_index < thread_count; ++thread_index) {
        m0ss_thread* thread = threads[thread_index];

        if (thread->ticks_to_block == 0) {
            continue;
        }

        --(thread->ticks_to_block);
    }
}

// Note: Performs the context switch.
__attribute__((naked))
void PendSV_Handler(void)
{
    // Note: This is a critical section which accesses the scheduler state.

    asm volatile (
        // TODO: Use UAL.
        "CPSID i\n\t" // Disable interrupts

        /* if (thread_running) { */
        "LDR r0, =thread_running\n\t" // r0 holds &thread_running
        "LDR r1, [r0]\n\t" // r1 holds thread_running
        "CMP r1, #0\n\t"
        "BEQ thread_not_running\n\t"

        /* Note: Push r4-r11 to the thread's stack */

        "MRS r3, psp\n\t" // r3 holds psp

        "SUB r3, #32\n\t" // Note: Reserve space for {r4-r11}

        "STMIA r3!, {r4-r7}\n\t" // Note: Push {r4-r7}

        "MOV r4, r8\n\t"
        "MOV r5, r9\n\t"
        "MOV r6, r10\n\t"
        "MOV r7, r11\n\t"

        "STMIA r3!, {r4-r7}\n\t" // Note: Push {r8-r11}

        "SUB r3, #32\n\t"

        /* thread_running->sp = psp; */

        "STR r3, [r1, %[offsetof_sp]]\n\t"

        /* } */

        "thread_not_running:\n\t"

        /* thread_running = thread_to_run; */

        "LDR r1, =thread_to_run\n\t" // r1 holds &thread_to_run
        "LDR r2, [r1]\n\t" // r2 holds thread_to_run
        "STR r2, [r0]\n\t"

        /* psp = thread_to_run->sp */

        "LDR r0, [r2, %[offsetof_sp]]\n\t" // r0 holds thread_to_run->sp

        "ADD r0, #16\n\t"

        "LDMIA r0!, {r4-r7}\n\t" // Note: Pop {r8-r11}
        "MOV r8, r4 \n\t"
        "MOV r9, r5 \n\t"
        "MOV r10, r6 \n\t"
        "MOV r11, r7 \n\t"

        "SUB r0, #32\n\t"

        "LDMIA r0!, {r4-r7}\n\t" // Note: Pop {r4-r7}

        "ADD r0, #16\n\t"

        "MSR psp, r0\n\t"

        /* Note: Hardware will pop the remaining registers from the thread's stack using process stack pointer. */

        "CPSIE i\n\t" // Enable interrupts

        /* Note: Return to thread mode, use process stack pointer. */

        "LDR r0, =0xFFFFFFFD\n\t"
        "BX r0"

        : /* outputs */
        : /* inputs */      [offsetof_sp] "i" (offsetof(m0ss_thread, sp))
        : /* clobbers */    "memory" // TODO: Add other clobbers?
    );
}

void SysTick_Handler()
{
    m0ss_tick();

    m0ss_schedule();
}

void m0ss_thread_create(m0ss_thread* thread, m0ss_thread_priority priority, m0ss_thread_func_ptr func_ptr, void* stack_buf, uint32_t stack_buf_capacity)
{
    // TODO: Add safety checks.

    m0ss_thread_init(thread, priority, func_ptr, stack_buf, stack_buf_capacity);

    threads[thread_count++] = thread;
}

void m0ss_start()
{
    m0ss_init();

    m0ss_schedule();
}

// Note: Schedules the execution of the threads.
void m0ss_schedule()
{
    // Note: This is a critical section which accesses the scheduler state.

    DISABLE_IRQ();

    thread_to_run = NULL;

    for (uint8_t thread_index = 0; thread_index < thread_count; ++thread_index) {
        m0ss_thread* thread = threads[thread_index];

        if (thread->ticks_to_block) {
            continue;
        }

        if ((thread_to_run == NULL) || (thread->priority > thread_to_run->priority)) {
            thread_to_run = thread;
        }
    }

    if (thread_to_run == NULL) {
        thread_to_run = &thread_idle;
    }

    if (thread_to_run != thread_running) {
        *(volatile uint32_t*)SCB_ICSR |= SCB_ICSR_PENDSVSET;
    }

    ENABLE_IRQ();
}

void m0ss_delay(uint32_t ticks)
{
    thread_running->ticks_to_block = ticks;

    m0ss_schedule();
}
