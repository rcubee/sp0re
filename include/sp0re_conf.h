#ifndef SP0RE_CONF_H
#define SP0RE_CONF_H

#define SP0RE_CONF_CORE_CLK_HZ 48000000U
#define SP0RE_CONF_TICK_RATE_HZ 1000U

#define SP0RE_CONF_MAX_THREAD_COUNT 4U

// Note: This assertion does not take into account the amount of cycles needed for a context switch.
_Static_assert(SP0RE_CONF_CORE_CLK_HZ >= SP0RE_CONF_TICK_RATE_HZ, "Core clock has to be greater or equal to tick rate.");

#endif // SP0RE_CONF_H
