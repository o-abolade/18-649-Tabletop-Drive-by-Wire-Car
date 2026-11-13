#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/printk.h>

#include "wcet.h"

static const char *const names[WCET_COUNT] = {
	[WCET_RX_DISPATCH] = "RX_DISPATCH",
	[WCET_CAN_TX] = "CAN_TX",
	[WCET_ACT] = "STEER/DRIVE",
	[WCET_BLINK] = "BLINK",
	[WCET_BTN] = "TEST_BTN",
	[WCET_PID] = "PID",
	[WCET_HB_MON] = "HB_MON",
	[WCET_HB_TX] = "HB_TX",
	[WCET_RELAY] = "RELAY",
	[WCET_FORCE_TX] = "FORCE_TX",
	[WCET_DRV_FRAME] = "DRV_FRAME",
	[WCET_STATUS] = "STATUS",
	[WCET_STATE_SPI] = "STATE_SPI",
};

static atomic_t max_cycles[WCET_COUNT];
static atomic_t jobs[WCET_COUNT];

uint64_t wcet_begin(void)
{
	k_thread_runtime_stats_t s;

	k_thread_runtime_stats_get(k_current_get(), &s);
	return s.execution_cycles;
}

void wcet_end(enum wcet_id id, uint64_t start)
{
	uint32_t c = (uint32_t)(wcet_begin() - start);
	atomic_val_t old;

	do {
		old = atomic_get(&max_cycles[id]);
		if ((uint32_t)old >= c) {
			break;
		}
	} while (!atomic_cas(&max_cycles[id], old, (atomic_val_t)c));
	atomic_inc(&jobs[id]);
}

/* Cycles -> "12.3" us, rounded up to 0.1 us */
static void print_us(uint32_t cycles)
{
	uint32_t ns = k_cyc_to_ns_ceil32(cycles);
	uint32_t tenths = (ns + 99) / 100;

	printk("%u.%u", tenths / 10, tenths % 10);
}

static void print_driver_thread(const struct k_thread *t, void *user)
{
	const char *name = k_thread_name_get((k_tid_t)t);

	ARG_UNUSED(user);
	if (name == NULL || strcmp(name, "mcp2515") != 0) {
		return;
	}
	k_thread_runtime_stats_t s;

	k_thread_runtime_stats_get((k_tid_t)t, &s);
	printk(" | MCP2515_THREAD peak ");
	print_us((uint32_t)s.peak_cycles);
}

void wcet_print(void)
{
	printk("WCET max us (jobs)");
	for (int i = 0; i < WCET_COUNT; i++) {
		uint32_t n = (uint32_t)atomic_get(&jobs[i]);

		if (n == 0) {
			continue;   /* task not present on this zone */
		}
		printk(" | %s ", names[i]);
		print_us((uint32_t)atomic_get(&max_cycles[i]));
		printk(" (%u)", n);
	}
	/* _unlocked: the locked variant keeps interrupts off through printk */
	k_thread_foreach_unlocked(print_driver_thread, NULL);
	printk("\n");
}
