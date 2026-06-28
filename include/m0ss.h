#ifndef M0SS_H
#define M0SS_H

#include <stdint.h>

#define M0SS_THREAD_PRIORITY_LOWEST 0U

typedef void* m0ss_thread_func_ptr;

typedef uint8_t m0ss_thread_priority;

typedef struct
{
    void* sp;

    m0ss_thread_priority priority;
    uint32_t ticks_to_block;
} m0ss_thread;

void m0ss_thread_create(m0ss_thread* thread, m0ss_thread_priority priority, m0ss_thread_func_ptr func_ptr, void* stack_buf, uint32_t stack_buf_capacity);

void m0ss_start();

void m0ss_schedule();

void m0ss_delay(uint32_t ticks);

#endif // M0SS_H
