#include "sp0re.h"
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

#define SYSTICK_CVR 0xE000E018U

static volatile sp0re_tick g_tick = 0U;

static sp0re_thread* threads[SP0RE_CONF_MAX_THREAD_COUNT];
static uint8_t thread_count;

static sp0re_thread* volatile thread_running;
static sp0re_thread* volatile thread_to_run;

static sp0re_thread idle_thread;
_Alignas(8) static uint8_t idle_thread_stack[SP0RE_CONF_IDLE_THREAD_STACK_CAPACITY];

#ifdef SP0RE_USING_DEFAULT_IDLE_THREAD_FUNC
// Note: Definition of default idle thread function.
__attribute__((naked))
static void sp0re_default_idle_thread_func(void*)
{
    asm volatile(
        "thread_idle_func_loop:\n\t"
        "WFI\n\t"
        "B thread_idle_func_loop\n\t"
    );
}
#else
// Note: Forward declaration of the user-provided idle thread function.
void SP0RE_CONF_IDLE_THREAD_FUNC(void*);
#endif // SP0RE_USING_DEFAULT_IDLE_THREAD_FUNC

static void sp0re_thread_init(
    sp0re_thread* thread,
    sp0re_thread_priority priority,
    sp0re_thread_func_ptr func_ptr,
    void* func_arg,
    void* stack_buf,
    uint32_t stack_buf_capacity
)
{
    SP0RE_ASSERT(thread != NULL);
    SP0RE_ASSERT(func_ptr != NULL);
    SP0RE_ASSERT(stack_buf != NULL);
    SP0RE_ASSERT(stack_buf_capacity > 0U); // TODO: Calculate the minimal stack buffer capacity which makes sense.

    /* Note:
     * In ARM Cortex-M0/M0+:
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
    *(--sp) = (uint32_t)func_arg; // R0 (AAPCS states that the first function argument should be put into R0)

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

    thread->base_priority = priority;
    thread->priority = priority;

    thread->tick_to_wake_at = 0U;

    thread->blocking_object = NULL;

    thread->owned_mutexes = NULL;
}

static void sp0re_init()
{
    sp0re_thread_init(
        &idle_thread,
        SP0RE_THREAD_PRIORITY_LOWEST,
        SP0RE_CONF_IDLE_THREAD_FUNC,
        NULL,
        idle_thread_stack,
        SP0RE_CONF_IDLE_THREAD_STACK_CAPACITY
    );

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
        thread_to_run = &idle_thread;
    }
}

/* Note: This method should be called inside critical section.
 * Wakes the thread and cancels blocking on an object.
 */
static inline void sp0re_thread_wake(sp0re_thread* thread)
{
    // Note: Access is not atomic.
    thread->tick_to_wake_at = g_tick;

    // Note: Stop blocking on any objects.
    thread->blocking_object = NULL;
}

static inline void sp0re_thread_block_on_object(sp0re_thread* thread, void* object, sp0re_tick ticks)
{
    thread->tick_to_wake_at = ticks == SP0RE_TICK_MAX ? ticks : g_tick + ticks;
    thread->blocking_object = object;
}

// Note: Get the thread with the highest priority blocking on an object.
static sp0re_thread* sp0re_get_highest_priority_waiter(void* blocking_object)
{
    sp0re_thread* highest_priority_waiter = NULL;

    for (uint8_t thread_index = 0U; thread_index < thread_count; ++thread_index) {
        sp0re_thread* thread = threads[thread_index];

        if (thread->blocking_object != blocking_object) {
            continue;
        }

        if ((highest_priority_waiter == NULL) ||
            (thread->priority > highest_priority_waiter->priority)) {
            highest_priority_waiter = thread;
        }
    }

    return highest_priority_waiter;
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

void sp0re_thread_create(
    sp0re_thread* thread,
    sp0re_thread_priority priority,
    sp0re_thread_func_ptr func_ptr,
    void* func_arg,
    void* stack_buf,
    uint32_t stack_buf_capacity
)
{
    SP0RE_ASSERT(thread_count < SP0RE_CONF_MAX_THREAD_COUNT);

    sp0re_thread_init(
        thread,
        priority,
        func_ptr,
        func_arg,
        stack_buf,
        stack_buf_capacity
    );

    threads[thread_count++] = thread;
}

void sp0re_thread_destroy(sp0re_thread* thread)
{
    if (thread == NULL) {
        thread = thread_running;
    }

    SP0RE_ASSERT(thread != &idle_thread);
    SP0RE_ASSERT(thread->blocking_object == NULL); // TODO: Release resources automatically?

    uint32_t primask;
    SP0RE_ENTER_CRITICAL(primask);

    // Note: Remove the thread from the global threads array.
    for (uint8_t thread_index = 0U; thread_index < thread_count; ++thread_index) {
        if (threads[thread_index] == thread) {
            threads[thread_index] = threads[--thread_count];

            break;
        }

        // Note: Assert if the thread was not found in the global threads array.
        SP0RE_ASSERT(thread_index != (thread_count - 1));
    }

    // Note: If the thread being destroyed is the thread running.
    if (thread == thread_running) {
        thread_running = NULL;

        sp0re_reschedule();
    }

    SP0RE_EXIT_CRITICAL(primask);
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

void sp0re_wake(sp0re_thread* thread)
{
    SP0RE_ASSERT(thread != NULL);

    uint32_t primask;
    SP0RE_ENTER_CRITICAL(primask);

    sp0re_thread_wake(thread);

    SP0RE_EXIT_CRITICAL(primask);

    // Note: If the waken thread has higher priority, reschedule
    if (thread->priority > thread_running->priority) {
        sp0re_reschedule();
    }
}

void sp0re_semaphore_create(sp0re_semaphore* semaphore, uint8_t max_count)
{
    SP0RE_ASSERT(semaphore != NULL);

    semaphore->count = 0U;
    semaphore->count_max = max_count;
}

sp0re_error sp0re_semaphore_wait(sp0re_semaphore* semaphore, sp0re_tick ticks)
{
    SP0RE_ASSERT(semaphore != NULL);

    uint32_t primask;
    SP0RE_ENTER_CRITICAL(primask);

    if (semaphore->count) {
        --semaphore->count;

        SP0RE_EXIT_CRITICAL(primask);

        return SP0RE_OK;
    }

    if (ticks != 0U) {
        sp0re_thread_block_on_object(thread_running, semaphore, ticks);
    }

    thread_running->semaphore_acquired = false;

    SP0RE_EXIT_CRITICAL(primask);

    if (ticks != 0U) {
        // Note: Wait for the signal or timeout.
        sp0re_reschedule();
    }

    return thread_running->semaphore_acquired ? SP0RE_OK : SP0RE_ERROR_TIMEOUT;
}

void sp0re_semaphore_signal(sp0re_semaphore* semaphore)
{
    uint32_t primask;
    SP0RE_ENTER_CRITICAL(primask);

    sp0re_thread* thread_to_wake = sp0re_get_highest_priority_waiter(semaphore);

    if (thread_to_wake) {
        sp0re_thread_wake(thread_to_wake);

        thread_to_wake->semaphore_acquired = true;
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
    SP0RE_ASSERT(mutex != NULL);

    mutex->next = NULL;
    mutex->owner = NULL;
    mutex->inherited_priority = SP0RE_THREAD_PRIORITY_LOWEST;
}

static void sp0re_mutex_lock_internal(sp0re_mutex* mutex, sp0re_thread* thread)
{
    mutex->owner = thread;
    mutex->inherited_priority = thread->priority;

    // Note: Insert the mutex at the start of the list of owned mutexes.
    mutex->next = thread->owned_mutexes;
    thread->owned_mutexes = mutex;
}

sp0re_error sp0re_mutex_lock(sp0re_mutex* mutex, sp0re_tick ticks)
{
    SP0RE_ASSERT(mutex != NULL);

    uint32_t primask;
    SP0RE_ENTER_CRITICAL(primask);

    if (mutex->owner == NULL) /* Mutex is unlocked. */ {
        sp0re_mutex_lock_internal(mutex, thread_running);

        SP0RE_EXIT_CRITICAL(primask);

        return SP0RE_OK;
    }

    /* Mutex is locked. */

    if (ticks == 0U) {
        SP0RE_EXIT_CRITICAL(primask);

        return SP0RE_ERROR_TIMEOUT;
    }

    // Note: Block on the mutex.
    sp0re_thread_block_on_object(thread_running, mutex, ticks);

    // Note: Priority inheritance protocol.
    if (thread_running->priority > mutex->inherited_priority) {
        // Note: Register the priority of the highest priority waiter.
        mutex->inherited_priority = thread_running->priority;

        // Note: Make the mutex owner inherit the priority.
        if (mutex->inherited_priority > mutex->owner->priority) {
            mutex->owner->priority = mutex->inherited_priority;
        }
    }

    SP0RE_EXIT_CRITICAL(primask);

    sp0re_reschedule();

    // Note: mutex->owner should be qualified as volatile, because it can change outside of this function.
    return mutex->owner == thread_running ? SP0RE_OK : SP0RE_ERROR_TIMEOUT;
}

static void sp0re_recalculate_inherited_priority(sp0re_thread* thread)
{
    // Note: The thread's priority could have been inherited even if no waiter was found (due to timeout on blocked object).
    sp0re_thread_priority priority = thread_running->base_priority;

    /* Note: Iterate through the list of owned mutexes.
     * The thread may have locked other mutexes. If PIP occured
     * on any of them, it should be taken into account.
     */
    sp0re_mutex* mutex = thread_running->owned_mutexes;
    while (mutex != NULL) {
        if (mutex->inherited_priority > priority) {
            priority = mutex->inherited_priority;
        }

        mutex = mutex->next;
    }

    thread->priority = priority;
}

void sp0re_mutex_unlock(sp0re_mutex* mutex)
{
    SP0RE_ASSERT(mutex);
    SP0RE_ASSERT(mutex->owner == thread_running);
    SP0RE_ASSERT(thread_running->owned_mutexes == mutex); // Mutexes should be unlocked in reverse order they were locked.

    uint32_t primask;
    SP0RE_ENTER_CRITICAL(primask);

    // Note: Remove the mutex from the top of the list of owned mutexes.
    thread_running->owned_mutexes = thread_running->owned_mutexes->next;

    sp0re_recalculate_inherited_priority(thread_running);

    sp0re_thread* highest_priority_waiter = sp0re_get_highest_priority_waiter(mutex);

    if (highest_priority_waiter == NULL) {
        /* If no waiter was found, simply unlock the mutex. */

        // Note: Unlock the mutex.
        mutex->owner = NULL;
        mutex->inherited_priority = SP0RE_THREAD_PRIORITY_LOWEST;

        SP0RE_EXIT_CRITICAL(primask);

        // TODO: Reschedule on lowering priority of the current thread releasing mutex?

        return;
    }

    sp0re_mutex_lock_internal(mutex, highest_priority_waiter);

    sp0re_thread_wake(highest_priority_waiter);

    SP0RE_EXIT_CRITICAL(primask);

    if (highest_priority_waiter->priority > thread_running->priority) {
        sp0re_reschedule();
    }
}
