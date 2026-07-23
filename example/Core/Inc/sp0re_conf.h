#ifndef SP0RE_CONF_H
#define SP0RE_CONF_H

/* MCU Core Clock in Hz. */
#define SP0RE_CONF_CORE_CLK_HZ 48000000U

/* The desired tick rate in Hz. */
#define SP0RE_CONF_TICK_RATE_HZ 1000U

/* The maximum number of threads that can be started. */
#define SP0RE_CONF_MAX_THREAD_COUNT 4U

/* Assertions. */
extern void assert_handler(const char*);
#define TO_STRING_HELPER(x) #x
#define TO_STRING(x) TO_STRING_HELPER(x)
#define SP0RE_CONF_ASSERT(expression) if (!(expression)) { assert_handler(__FILE__ ":" TO_STRING(__LINE__)); }

/* User-provided idle thread function. */
#define SP0RE_CONF_IDLE_THREAD_FUNC idle_thread_func
#define SP0RE_CONF_IDLE_THREAD_STACK_CAPACITY 256U

#endif // SP0RE_CONF_H
