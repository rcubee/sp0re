#ifndef SP0RE_CONF_INTERNAL_H
#define SP0RE_CONF_INTERNAL_H

#include "sp0re_conf.h"

#ifndef SP0RE_CONF_CORE_CLK_HZ
    #error "SP0RE_CONF_CORE_CLK_HZ must be defined"
#endif

#ifndef SP0RE_CONF_TICK_RATE_HZ
    #error "SP0RE_CONF_TICK_RATE_HZ must be defined"
#endif

#ifndef SP0RE_CONF_MAX_THREAD_COUNT
    #error "SP0RE_CONF_MAX_THREAD_COUNT must be defined"
#endif

#ifdef SP0RE_CONF_IDLE_THREAD_FUNC
    #ifndef SP0RE_CONF_IDLE_THREAD_STACK_CAPACITY
        #error "Stack capacity for the idle thread must be provided (SP0RE_CONF_IDLE_THREAD_STACK_CAPACITY)."
    #endif
#else
    #define SP0RE_CONF_IDLE_THREAD_FUNC sp0re_default_idle_thread_func
    #define SP0RE_CONF_IDLE_THREAD_STACK_CAPACITY (128U)
    #define SP0RE_USING_DEFAULT_IDLE_THREAD_FUNC
#endif

// Note: This assertion does not take into account the amount of cycles needed for a context switch.
_Static_assert(SP0RE_CONF_CORE_CLK_HZ >= SP0RE_CONF_TICK_RATE_HZ, "Core clock has to be greater or equal to tick rate.");

#endif // SP0RE_CONF_INTERNAL_H
