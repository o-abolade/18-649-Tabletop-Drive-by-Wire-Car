/*
 * Lab 5 step 3 bench (design.md section 9): global time and PPS on the two
 * Nucleos, before the Raft glue. Wire both to the CAN A and B rails as in
 * Lab 3 and scope PPS (A2) on both boards.
 *
 * A stand-in for the election picks the time master; Raft replaces it in
 * step 4:
 *  - A node with no REF from anyone for TAKEOVER_MS becomes master: N2 after
 *    60 ms, N3 after 100 ms, so N2 leads after a joint boot.
 *  - A master that hears a REF from a lower-numbered node yields to it.
 *  - B1 (user button) toggles "silent": the node sends no REF and ignores
 *    REFs, as if its CAN links were cut, while its PPS keeps running on its
 *    own clock. Silencing the master makes the other node take over with
 *    its own global time (the leader-handover case); un-silencing shows how
 *    far the silent node drifted (err@REF on its first sample).
 */
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/printk.h>

#include "can_link.h"
#include "gsync.h"
#include "raft_proto.h"

#define SELF          CONFIG_SYNC_BENCH_NODE
#define TAKEOVER_MS   (60 + 40 * (SELF - 2))
#define YIELD_MS      (2 * RAFT_HEARTBEAT_MS)
#define LOOP_MS       10

static const struct gpio_dt_spec btn = GPIO_DT_SPEC_GET(DT_ALIAS(test_btn), gpios);

/* Written by the CAN RX callback (driver thread), read by main */
static atomic_t last_ref_ms;       /* uptime of the last REF from another node */
static atomic_t last_ref_sender;
static atomic_t silent;

static bool master;
static uint8_t cycle;

/* First copy of every accepted frame (can_link dedups the second bus) */
static void rx_cb(uint32_t id, const uint8_t data[CANP_DLC], enum canp_bus bus)
{
	unsigned sender;

	ARG_UNUSED(data); ARG_UNUSED(bus);
	if (raftp_classify(id, &sender) == RAFTP_REF && sender != SELF &&
	    !atomic_get(&silent)) {
		atomic_set(&last_ref_sender, (atomic_val_t)sender);
		atomic_set(&last_ref_ms, (atomic_val_t)k_uptime_get());
	}
}

/* REF every heartbeat while master, on an absolute 20 ms schedule */
static void ref_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);
	int64_t next = k_uptime_get();

	while (1) {
		next += RAFT_HEARTBEAT_MS;
		k_sleep(K_TIMEOUT_ABS_MS(next));
		if (!master || atomic_get(&silent)) {
			continue;
		}

		uint8_t d[RAFTP_DLC];
		struct raftp_ref m = { .term = 1, .cycle = cycle++ };

		raftp_encode_ref(d, 0, &m);
		gsync_send_ref(d);
	}
}

/* Just below the CAN TX threads (ZONE_PRIO_CAN_TX = 0) */
K_THREAD_DEFINE(ref_tid, 1024, ref_thread, NULL, NULL, NULL, 1, 0, SYS_FOREVER_MS);

static unsigned cur_leader = 0xFF;

static void set_leader(unsigned leader, const char *why)
{
	if (leader == cur_leader) {
		return;
	}
	cur_leader = leader;
	master = (leader == SELF);
	gsync_set_leader(leader);
	if (leader == 0) {
		printk("ROLE N%u: no master (%s)\n", SELF, why);
	} else {
		printk("ROLE N%u: master is N%u (%s)\n", SELF, leader, why);
	}
}

int main(void)
{
	printk("\nLab 5 sync bench, node N%u (takeover after %d ms of silence)\n", SELF,
	       TAKEOVER_MS);
	int up = can_link_init(rx_cb);

	printk("CAN: %d of 2 buses up\n", up);
	gpio_pin_configure_dt(&btn, GPIO_INPUT);
	gsync_init(SELF);
	atomic_set(&last_ref_ms, (atomic_val_t)k_uptime_get());   /* boot = heard */
	k_thread_start(ref_tid);

	bool btn_prev = false;
	int64_t next = k_uptime_get();

	while (1) {
		next += LOOP_MS;
		k_sleep(K_TIMEOUT_ABS_MS(next));

		int64_t now = k_uptime_get();
		bool pressed = gpio_pin_get_dt(&btn) > 0;

		if (pressed && !btn_prev) {
			bool s = !atomic_get(&silent);

			atomic_set(&silent, s);
			printk("B1: N%u %s\n", SELF, s ? "SILENT (no REF out, REFs ignored)"
						       : "back on the bus");
			if (s) {
				set_leader(0, "silent");
			} else {
				atomic_set(&last_ref_ms, (atomic_val_t)now);
			}
		}
		btn_prev = pressed;

		if (!atomic_get(&silent)) {
			int64_t age = now - (int64_t)atomic_get(&last_ref_ms);
			unsigned from = (unsigned)atomic_get(&last_ref_sender);

			if (master) {
				if (age <= YIELD_MS && from != 0 && from < SELF) {
					set_leader(from, "lower node is master");
				}
			} else if (age <= TAKEOVER_MS && from != 0) {
				set_leader(from, "heard its REF");
			} else if (age > TAKEOVER_MS) {
				set_leader(SELF, "no REF, taking over");
			}
		}
		gsync_poll();
	}
	return 0;
}
