#include "sp0re.h"
#include "sp0re_conf.h"
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

#define SYSTICK_RVR_RELOAD_VALUE ((SP0RE_CONF_CORE_CLK_HZ / SP0RE_CONF_TICK_RATE_HZ) - 1U)
_Static_assert((SYSTICK_RVR_RELOAD_VALUE >= 0x00000001U && SYSTICK_RVR_RELOAD_VALUE <= 0x00FFFFFFU), "SysTick RVR RELOAD value is out of range.");

#define SYSTICK_CVR 0xE000E018U

#define THREAD_IDLE_STACK_CAPACITY (1024U)

static volatile sp0re_tick g_tick = 0U;

static sp0re_thread* threads[SP0RE_CONF_MAX_THREAD_COUNT];
static uint8_t thread_count;

static sp0re_thread* volatile thread_running;
static sp0re_thread* volatile thread_to_run;

static sp0re_thread thread_idle;
_Alignas(8) static uint8_t thread_idle_stack[THREAD_IDLE_STACK_CAPACITY];

static void thread_idle_func()
{
    while (1) {
        asm volatile("WFI");

        sp0re_reschedule();
    }
}

static void sp0re_thread_init(sp0re_thread* thread, sp0re_thread_priority priority, sp0re_thread_func_ptr func_ptr, void* stack_buf, uint32_t stack_buf_capacity)
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

    thread->priority = priority;

    thread->tick_to_wake_at = 0U;

    thread->blocking_object = NULL;
}

static void sp0re_init()
{
    sp0re_thread_init(&thread_idle, SP0RE_THREAD_PRIORITY_LOWEST, thread_idle_func, thread_idle_stack, THREAD_IDLE_STACK_CAPACITY);

    // Set PendSV and SysTick system handler priority to the lowest possible.
    *(volatile uint32_t*)SCB_SHPR3 |= SCB_SHPR3_PRI_14 | SCB_SHPR3_PRI_15;

    *(volatile uint32_t*)SYSTICK_RVR = SYSTICK_RVR_RELOAD_VALUE;
    *(volatile uint32_t*)SYSTICK_CVR = 0U;
    *(volatile uint32_t*)SYSTICK_CSR = SYSTICK_CSR_ENABLE | SYSTICK_CSR_TICKINT | SYSTICK_CSR_CLKSOURCE;
}

/* Note: Selects the thread to run.
 * This function is meant to be called inside PendSV, not before it.
 * The main purpose of enforcing this behavior reducing the number
 * of times the schedulee algorithm runs in situations where multiple
 * higher-priority interrupts use APIs which need to trigger the reschedule.
 */
__attribute__((used))
static void sp0re_schedule()
{
    // Note: This section accesses scheduler shared state.

    thread_to_run = NULL;

    for (uint8_t thread_index = 0; thread_index < thread_count; ++thread_index) {
        sp0re_thread* thread = threads[thread_index];

        if (thread->tick_to_wake_at > g_tick) {
            // Note: Thread shoud sleep.
            continue;
        }

        if ((thread_to_run == NULL) || (thread->priority > thread_to_run->priority)) {
            thread_to_run = thread;
        }
    }

    if (thread_to_run == NULL) {
        thread_to_run = &thread_idle;
    }
}

// Note: Performs the context switch.
__attribute__((naked))
void PendSV_Handler(void)
{
    // Note: This is a critical section which accesses the scheduler state.

    asm volatile (
        // TODO: Use UAL.
        // TODO: Modify the sequence so that only necessary parts of the context switch are inside critical section (does saving/restoring have to be?)
        // TODO: Early exit on thread_to_run = thread_running.
        "CPSID i\n\t" // Disable interrupts

        "BL sp0re_schedule\n\t" // Select the thread to run.

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
        : /* inputs */      [offsetof_sp] "i" (offsetof(sp0re_thread, sp))
        : /* clobbers */    "memory" // TODO: Add other clobbers?
    );
}

void SysTick_Handler()
{
    SP0RE_DISABLE_IRQ();

    // Note: Access is not atomic
    ++g_tick;

    // Note: Handle timeouts.
    for (uint8_t thread_index = 0; thread_index < thread_count; ++thread_index) {
        sp0re_thread* thread = threads[thread_index];

        if (thread->tick_to_wake_at > g_tick) {
            continue;
        }

        if (thread->blocking_object != NULL) {
            thread->blocking_object = NULL;
        }
    }

    SP0RE_ENABLE_IRQ();

    // TODO: Track the smallest tick_to_wake_at to reduce unnecessary reschedules.
    sp0re_reschedule();
}

void sp0re_thread_create(sp0re_thread* thread, sp0re_thread_priority priority, sp0re_thread_func_ptr func_ptr, void* stack_buf, uint32_t stack_buf_capacity)
{
    // TODO: Add safety checks.

    sp0re_thread_init(thread, priority, func_ptr, stack_buf, stack_buf_capacity);

    threads[thread_count++] = thread;
}

void sp0re_start()
{
    sp0re_init();

    sp0re_reschedule();
}

// Note: Triggers PendSV to schedule the execution of the threads.
void sp0re_reschedule()
{
    *(volatile uint32_t*)SCB_ICSR |= SCB_ICSR_PENDSVSET;
}

sp0re_tick sp0re_get_tick()
{
    uint32_t primask;
    SP0RE_ENTER_CRITICAL(primask);

    // Note: Access is not atomic.
    sp0re_tick tick = g_tick;

    SP0RE_EXIT_CRITICAL(primask);

    return tick;
}

void sp0re_sleep(sp0re_tick ticks)
{
    sp0re_sleep_until(sp0re_get_tick() + ticks);
}

void sp0re_sleep_until(sp0re_tick tick)
{
    uint32_t primask;
    SP0RE_ENTER_CRITICAL(primask);

    // Note: Access is not atomic.
    thread_running->tick_to_wake_at = tick;

    SP0RE_EXIT_CRITICAL(primask);

    // Note: Schedule another thread to run
    sp0re_reschedule();
}

// TODO: What should be done if thread is blocked waiting for an object?
void sp0re_wake(sp0re_thread* thread)
{
    uint32_t primask;
    SP0RE_ENTER_CRITICAL(primask);

    // Note: Access is not atomic.
    thread->tick_to_wake_at = g_tick;

    SP0RE_EXIT_CRITICAL(primask);

    // Note: If the waken thread has higher priority, reschedule
    if (thread->priority > thread_running->priority) {
        sp0re_reschedule();
    }
}

void sp0re_semaphore_create(sp0re_semaphore* semaphore, uint8_t max_count)
{
    semaphore->count = 0U;
    semaphore->count_max = max_count;
}

sp0re_error sp0re_semaphore_wait(sp0re_semaphore* semaphore, sp0re_tick ticks)
{
    uint32_t primask;
    SP0RE_ENTER_CRITICAL(primask);

    if (semaphore->count) {
        --semaphore->count;

        SP0RE_EXIT_CRITICAL(primask);

        return SP0RE_OK;
    }

    if (ticks != 0) {
        thread_running->tick_to_wake_at = g_tick + ticks;
        thread_running->blocking_object = semaphore;
    }

    thread_running->blocking_object_acquired = false;

    SP0RE_EXIT_CRITICAL(primask);

    if (ticks != 0) {
        // Note: Wait for the signal or timeout.
        sp0re_reschedule();
    }

    return thread_running->blocking_object_acquired ? SP0RE_OK : SP0RE_ERROR_TIMEOUT;
}

void sp0re_semaphore_signal(sp0re_semaphore* semaphore)
{
    uint32_t primask;
    SP0RE_ENTER_CRITICAL(primask);

    sp0re_thread* thread_to_wake = NULL;

    for (uint8_t thread_index = 0U; thread_index < thread_count; ++thread_index) {
        sp0re_thread* thread = threads[thread_index];

        if (thread->blocking_object != semaphore) {
            // Note: The thread is not sleeping on the semaphore.
            continue;
        }

        if ((thread_to_wake == NULL) || (thread->priority > thread_to_wake->priority)) {
            thread_to_wake = thread;
        }
    }

    if (thread_to_wake) {
        thread_to_wake->tick_to_wake_at = g_tick;
        thread_to_wake->blocking_object = NULL;
        thread_to_wake->blocking_object_acquired = true;
    }
    else if (semaphore->count < semaphore->count_max) {
        ++semaphore->count;
    }

    SP0RE_EXIT_CRITICAL(primask);

    if (thread_to_wake && (thread_to_wake->priority > thread_running->priority)) {
        sp0re_reschedule();
    }
}

void sp0re_mutex_create(sp0re_mutex* mutex)
{
    mutex->owner = NULL;
    mutex->owner_base_priority = SP0RE_THREAD_PRIORITY_LOWEST;
}

sp0re_error sp0re_mutex_lock(sp0re_mutex* mutex, sp0re_tick ticks)
{
    // TODO: Add double lock safety checks.

    uint32_t primask;
    SP0RE_ENTER_CRITICAL(primask);

    if (mutex->owner == NULL) {
        mutex->owner = thread_running;
        mutex->owner_base_priority = thread_running->priority;

        SP0RE_EXIT_CRITICAL(primask);

        return SP0RE_OK;
    }

    if (ticks == 0U) {
        SP0RE_EXIT_CRITICAL(primask);

        return SP0RE_ERROR_TIMEOUT;
    }

    if (thread_running->priority > mutex->owner->priority) {
        // Note: Priority inheritance protocol.
        mutex->owner->priority = thread_running->priority;
    }

    thread_running->tick_to_wake_at = ticks == SP0RE_TICK_MAX ? ticks : g_tick + ticks;
    thread_running->blocking_object = mutex;

    SP0RE_EXIT_CRITICAL(primask);

    sp0re_reschedule();

    return mutex->owner == thread_running ? SP0RE_OK : SP0RE_ERROR_TIMEOUT;
}

void sp0re_mutex_unlock(sp0re_mutex* mutex)
{
    // TODO: Add double unlock safety checks.

    uint32_t primask;
    SP0RE_ENTER_CRITICAL(primask);

    // Note: Thread's priority could have been inherited even if no waiter was found (due to their timeout on lock).
    thread_running->priority = mutex->owner_base_priority;

    sp0re_thread* highest_priority_waiter = NULL;

    for (uint8_t thread_index = 0U; thread_index < thread_count; ++thread_index) {
        sp0re_thread* thread = threads[thread_index];

        if (thread->blocking_object != mutex) {
            // Note: The thread is not sleeping on the mutex.
            continue;
        }

        if ((highest_priority_waiter == NULL) || (thread->priority > highest_priority_waiter->priority)) {
            highest_priority_waiter = thread;
        }
    }

    if (highest_priority_waiter == NULL) {
        mutex->owner = NULL;
        mutex->owner_base_priority = SP0RE_THREAD_PRIORITY_LOWEST;

        SP0RE_EXIT_CRITICAL(primask);

        // TODO: Reschedule on lowering priority?

        return;
    }

    mutex->owner = highest_priority_waiter;
    mutex->owner_base_priority = highest_priority_waiter->priority;

    highest_priority_waiter->tick_to_wake_at = g_tick;
    highest_priority_waiter->blocking_object = NULL;

    SP0RE_EXIT_CRITICAL(primask);

    sp0re_reschedule();
}
