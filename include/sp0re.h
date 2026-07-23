#ifndef SP0RE_H
#define SP0RE_H

#include <stdint.h>
#include <stdbool.h>
#include "sp0re_conf_internal.h"

#define SP0RE_TICK_MAX UINT64_MAX

#define SP0RE_THREAD_PRIORITY_LOWEST 0U

#define SP0RE_DISABLE_IRQ() asm volatile("CPSID i")
#define SP0RE_ENABLE_IRQ() asm volatile("CPSIE i")

#define SP0RE_ENTER_CRITICAL(primask) \
    asm volatile( \
        "MRS %0, primask\n\t" \
        "CPSID i\n\t" \
        : /* outputs */ "=r" (primask) \
        : /* inputs */ \
        : /* clobbers */ "memory" \
    )

#define SP0RE_EXIT_CRITICAL(primask) \
    asm volatile( \
        "MSR primask, %0\n\t" \
        : /* outputs */ \
        : /* inputs */ "r" (primask) \
        : /* clobbers */ "memory" \
    )

typedef uint64_t sp0re_tick;

typedef void (*sp0re_thread_func_ptr)(void*);

typedef uint8_t sp0re_thread_priority;

typedef enum
{
    SP0RE_OK = 0U,
    SP0RE_ERROR_TIMEOUT
} sp0re_error;

typedef struct
{
    void* sp;

    sp0re_thread_priority priority;

    sp0re_tick tick_to_wake_at;

    void* blocking_object;
    bool blocking_object_acquired;
} sp0re_thread;

// Note: A thread can only wait on 1 semaphore/mutex
typedef struct
{
    uint8_t count;
    uint8_t count_max;
} sp0re_semaphore;

typedef struct
{
    volatile sp0re_thread* owner;
    sp0re_thread_priority owner_base_priority;
} sp0re_mutex;

void sp0re_thread_create(
    sp0re_thread* thread,
    sp0re_thread_priority priority,
    sp0re_thread_func_ptr func_ptr,
    void* func_arg,
    void* stack_buf,
    uint32_t stack_buf_capacity
);

void sp0re_start();

void sp0re_reschedule();

sp0re_tick sp0re_get_tick();

void sp0re_sleep(sp0re_tick ticks);

void sp0re_sleep_until(sp0re_tick tick);

void sp0re_wake(sp0re_thread* thread);

void sp0re_semaphore_create(sp0re_semaphore* semaphore, uint8_t max_count);

sp0re_error sp0re_semaphore_wait(sp0re_semaphore* semaphore, sp0re_tick ticks);

void sp0re_semaphore_signal(sp0re_semaphore* semaphore);

void sp0re_mutex_create(sp0re_mutex* mutex);

sp0re_error sp0re_mutex_lock(sp0re_mutex* mutex, sp0re_tick ticks);

void sp0re_mutex_unlock(sp0re_mutex* mutex);

#endif // SP0RE_H
