#ifndef M0SS_CONF_H
#define M0SS_CONF_H

#define M0SS_CONF_CORE_CLK_HZ 48000000U
#define M0SS_CONF_TICK_RATE_HZ 1000U

#define M0SS_CONF_MAX_THREAD_COUNT 4U

// Note: This assertion does not take into account the amount of cycles needed for a context switch.
_Static_assert(M0SS_CONF_CORE_CLK_HZ >= M0SS_CONF_TICK_RATE_HZ, "Core clock has to be greater or equal to tick rate.");

#endif // M0SS_CONF_H
