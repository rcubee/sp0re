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

typedef struct sp0re_mutex sp0re_mutex;

typedef struct
{
    void* sp;

    sp0re_thread_priority base_priority;
    sp0re_thread_priority priority; // TODO: Make volatile as it can change elsewhere?

    sp0re_tick tick_to_wake_at;

    void* blocking_object; // A pointer to an object (semaphore/mutex) on which the thread is blocked. The thread can be blocked only one object at a time.
    bool semaphore_acquired; // A flag used to check if semaphore was signaled or timeout occured.

    sp0re_mutex* owned_mutexes; // A singly linked list of owned mutexes.
} sp0re_thread;

typedef struct
{
    uint8_t count;
    uint8_t count_max;
} sp0re_semaphore;

// Note: A single thread can lock multiple mutexes simultaneously.
struct sp0re_mutex
{
    sp0re_mutex* next; // Pointer to the next mutex in the list.
    volatile sp0re_thread* owner; // Pointer to the owner thread.
    sp0re_thread_priority inherited_priority; // Highest priority inherited through this mutex.
};

void sp0re_thread_create(
    sp0re_thread* thread,
    sp0re_thread_priority priority,
    sp0re_thread_func_ptr func_ptr,
    void* func_arg,
    void* stack_buf,
    uint32_t stack_buf_capacity
);

void sp0re_thread_destroy(sp0re_thread* thread);

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
