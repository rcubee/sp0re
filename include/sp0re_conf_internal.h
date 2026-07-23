#ifndef SP0RE_CONF_INTERNAL_H
#define SP0RE_CONF_INTERNAL_H

#include "sp0re_conf.h"

#ifndef SP0RE_CONF_CORE_CLK_HZ
    #error "SP0RE_CONF_CORE_CLK_HZ must be defined."
#endif // SP0RE_CONF_CORE_CLK_HZ

#ifndef SP0RE_CONF_TICK_RATE_HZ
    #error "SP0RE_CONF_TICK_RATE_HZ must be defined."
#endif // SP0RE_CONF_TICK_RATE_HZ

#ifndef SP0RE_CONF_MAX_THREAD_COUNT
    #error "SP0RE_CONF_MAX_THREAD_COUNT must be defined."
#endif // SP0RE_CONF_MAX_THREAD_COUNT

#ifdef SP0RE_CONF_IDLE_THREAD_FUNC
    #ifndef SP0RE_CONF_IDLE_THREAD_STACK_CAPACITY
        #error "Stack capacity for the idle thread must be provided (SP0RE_CONF_IDLE_THREAD_STACK_CAPACITY)."
    #endif
#else
    #define SP0RE_CONF_IDLE_THREAD_FUNC sp0re_default_idle_thread_func
    #define SP0RE_CONF_IDLE_THREAD_STACK_CAPACITY (128U)
    #define SP0RE_USING_DEFAULT_IDLE_THREAD_FUNC
#endif // SP0RE_CONF_IDLE_THREAD_FUNC

#ifdef SP0RE_CONF_ASSERT
    #define SP0RE_ASSERT(expression) SP0RE_CONF_ASSERT(expression)
#else // SP0RE_CONF_ASSERT
    #define SP0RE_ASSERT(expression) (void)0
#endif // SP0RE_CONF_ASSERT

#define SYSTICK_RVR_RELOAD_VALUE ((SP0RE_CONF_CORE_CLK_HZ / SP0RE_CONF_TICK_RATE_HZ) - 1U)
_Static_assert((SYSTICK_RVR_RELOAD_VALUE >= 0x00000001U && SYSTICK_RVR_RELOAD_VALUE <= 0x00FFFFFFU), "SysTick RVR RELOAD value is out of range.");

// Note: This assertion does not take into account the amount of cycles needed for a context switch.
_Static_assert(SP0RE_CONF_CORE_CLK_HZ >= SP0RE_CONF_TICK_RATE_HZ, "Core clock has to be greater or equal to tick rate.");

#endif // SP0RE_CONF_INTERNAL_H
