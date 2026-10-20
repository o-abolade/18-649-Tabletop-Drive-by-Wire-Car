/*
 * N2 steering zone (front).
 *   STEER     on CTRL -> servo PWM (SRV); centred while not NORMAL
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
#include "zone_core.h"

static void steer_safe(void)
{
	(void)set_wheel_angle(0);
}

static void steer_thread(void *a, void *b, void *c)
{
	ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
	struct canp_ctrl cmd;

	while (1) {
		if (zone_wait_ctrl(&cmd, K_MSEC(ZONE_CTRL_TIMEOUT_MS)) != 0) {
			continue;   /* HB_MON moves us to ERROR and centres via steer_safe */
		}
		if (zone_state() == CANP_ZS_NORMAL) {
			(void)set_wheel_angle(cmd.steer);
		}
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

		uint8_t d[CANP_DLC];

		canp_encode_force(d, 0, counter++);
		can_link_send(CANP_ID_FORCE, d);
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

	while (1) {
		k_msleep(ZONE_STATUS_PERIOD_MS);
		zone_print_status();

		uint32_t relayed, overflow;

		relay_get_stats(&relayed, &overflow);
		printk("   relay: %u forwarded, %u dropped (queue full)\n", relayed, overflow);
	}
	return 0;
}
