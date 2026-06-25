#ifndef M0SS_H
#define M0SS_H

#include <stdint.h>

typedef void* m0ss_thread_func_ptr;

typedef struct
{
    void* sp;
} m0ss_thread;

void m0ss_thread_create(m0ss_thread* thread, m0ss_thread_func_ptr func_ptr, void* stack_buf, uint32_t stack_buf_capacity);

void m0ss_start();

void m0ss_schedule();

#endif // M0SS_H
