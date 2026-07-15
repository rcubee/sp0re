#ifndef SP0RE_H
#define SP0RE_H

#include <stdint.h>

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

typedef void* sp0re_thread_func_ptr;

typedef uint8_t sp0re_thread_priority;

typedef struct
{
    void* sp;

    sp0re_thread_priority priority;

    sp0re_tick tick_to_wake_at;
} sp0re_thread;

void sp0re_thread_create(sp0re_thread* thread, sp0re_thread_priority priority, sp0re_thread_func_ptr func_ptr, void* stack_buf, uint32_t stack_buf_capacity);

void sp0re_start();

void sp0re_reschedule();

sp0re_tick sp0re_get_tick();

void sp0re_sleep(sp0re_tick ticks);

void sp0re_sleep_until(sp0re_tick tick);

void sp0re_wake(sp0re_thread* thread);

#endif // SP0RE_H
