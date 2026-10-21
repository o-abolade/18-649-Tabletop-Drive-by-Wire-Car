/*
 * N2 steering zone (front).
 *   STEER     on CTRL -> servo PWM (SRV). Keeps tracking in the system error
 *             state (R3); holds its position while N2 itself is LOCAL_FAIL
 *             (R4.7 "steering held"). Isolated, no CTRL arrives, so it holds.
 *   FORCE_TX  every 50 ms, dummy counter (handout: force feedback is a dummy)
 *   RELAY     see relay.c
 *   BLINK_F, HB_TX, HB_MON, TEST_BTN, error state: common/zephyr/zone_core.c
 */
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "can_link.h"
#include "relay.h"
#include "steering.h"
#include "zone_config.h"
#include "wcet.h"
#include "zone_core.h"

/* Safe state = hold the current angle (R4.7), so nothing to do here */
static void steer_safe(void)
{
}

static void steer_thread(void *a, void *b, void *c)
{
	ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
	struct canp_ctrl cmd;

	while (1) {
		if (zone_wait_ctrl(&cmd, K_MSEC(ZONE_CTRL_TIMEOUT_MS)) != 0) {
			continue;   /* no CTRL: the servo holds where it is */
		}
		uint64_t w0 = wcet_begin();

		/* R3: track the wheel even in ERROR, unless N2 is the failed zone */
		if (zone_state() != CANP_ZS_LOCAL_FAIL) {
			(void)set_wheel_angle(cmd.steer);
		}
		wcet_end(WCET_ACT, w0);
	}
}

static void force_tx_thread(void *a, void *b, void *c)
{
	ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
	int64_t next = k_uptime_get();
	int8_t counter = 0;

	while (1) {
		next += CANP_FORCE_PERIOD_MS;
		k_sleep(K_TIMEOUT_ABS_MS(next));
		uint64_t w0 = wcet_begin();

		uint8_t d[CANP_DLC];

		canp_encode_force(d, 0, counter++);
		can_link_send(CANP_ID_FORCE, d);
		wcet_end(WCET_FORCE_TX, w0);
	}
}

K_THREAD_DEFINE(steer_tid, 1024, steer_thread, NULL, NULL, NULL,
		ZONE_PRIO_ACT, 0, SYS_FOREVER_MS);
K_THREAD_DEFINE(force_tid, 1024, force_tx_thread, NULL, NULL, NULL,
		ZONE_PRIO_FORCE_TX, 0, SYS_FOREVER_MS);

static const struct zone_config n2_cfg = {
	.zone = CANP_ZONE_N2,
	.enter_safe = steer_safe,
	.rx_hook = relay_rx_hook,
};

int main(void)
{
	printk("\n=== Lab 3 N2 steering zone ===\n");

	if (servo_init() != 0) {
		printk("N2: servo PWM not ready\n");
	}
	zone_core_init(&n2_cfg);
	k_thread_start(steer_tid);
	k_thread_start(force_tid);

	/* Status printing is background work: below every real-time task */
	k_thread_priority_set(k_current_get(), ZONE_PRIO_STATUS);

	for (uint32_t tick = 1;; tick++) {
		k_msleep(ZONE_STATUS_PERIOD_MS);
		uint64_t w0 = wcet_begin();

		zone_print_status();
		if (tick % 5 == 0) {
			wcet_print();
		}

		uint32_t relayed, overflow;

		relay_get_stats(&relayed, &overflow);
		printk("   relay: %u forwarded, %u dropped (queue full)\n", relayed, overflow);
		wcet_end(WCET_STATUS, w0);
	}
	return 0;
}
