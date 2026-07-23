# sp0re - ARM Cortex-M0/M0+ RTOS
sp0re is a lightweight RTOS designed for the most resource-constrained ARM Cortex-M0/M0+ devices. It's intended to be used in soft real-time applications where memory footprint is the primary constraint.

## Features
- Preemptive priority scheduler.
- No dynamic memory allocation.
- Straightforward API: Sleep | Wake | Counting Semaphores | Mutexes (with priority inheritance protocol)
- Configuration through a single header file.

## Blinky example
```c
#include "sp0re.h"

/* TCB of the blinky thread. */
static sp0re_thread blinky_thread;

_Alignas(8) static uint8_t blinky_stack[256];

static void blinky_func(void) {
    while(1) {
        toggle_led();

        sp0re_sleep(500);
    }
}

int main(void) {
    /* Create the thread. */
    sp0re_thread_create(&blinky_thread, SP0RE_THREAD_PRIORITY_LOWEST,
                        blinky_func, blinky_stack, sizeof(blinky_stack));

    /* Start the scheduler (never returns). */
    sp0re_start();
}
