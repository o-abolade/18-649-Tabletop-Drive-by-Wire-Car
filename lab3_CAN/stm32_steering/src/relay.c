/*
 * RELAY (N2 only): N2 is the middle node, on both buses, so it forwards
 * traffic between them for R4.8 scenario (c) (N1 only on A, N3 only on B).
 *
 * Design: always relay, no fault detection needed, so the first relayed
 * frame isn't delayed by the 60 ms degraded-bus detection. Cost: N1's and
 * N3's frames appear twice on each bus (receivers drop the copy by seq).
 *
 * What gets relayed: every accepted (first-copy) frame that N2 didn't send
 * itself, onto the OTHER bus from the one it arrived on, with
 * CANP_FLAG_RELAYED set. CTRL's CRC masks the flags nibble, so it stays
 * valid. No loops: only N2 relays, and it never re-relays a flagged frame.
 */
#include <string.h>

#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>

#include "can_link.h"
#include "relay.h"
#include "wcet.h"
#include "zone_config.h"

struct relay_item {
	uint32_t id;
	uint8_t data[CANP_DLC];
	uint8_t from_bus;
};

#define RELAY_TP_MIN_US 10

/* Depth covers one 20 ms cycle of N1 + N3 traffic with room to spare */
K_MSGQ_DEFINE(relay_q, sizeof(struct relay_item), 16, 4);

static const struct gpio_dt_spec tp_relay_tx = GPIO_DT_SPEC_GET(DT_ALIAS(tp_relay_tx), gpios);
static atomic_t relayed, overflow;

static bool should_relay(uint32_t id, const uint8_t *data)
{
	if (canp_flags(data) & CANP_FLAG_RELAYED) {
		return false;
	}
	switch (id) {
	case CANP_ID_CTRL:
	case CANP_ID_BLINK:
	case CANP_ID_HB_N1:
	case CANP_ID_HB_N3:
		return true;
	case CANP_ID_FAULT:
		return canp_fault_zone(data) != CANP_ZONE_N2;
	default:
		return false;   /* FORCE and HB_N2 are N2's own */
	}
}

/* MCP2515 RX thread context: copy and hand off, never block */
void relay_rx_hook(uint32_t id, const uint8_t data[CANP_DLC], enum canp_bus bus)
{
	if (!should_relay(id, data)) {
		return;
	}
	struct relay_item item = { .id = id, .from_bus = bus };

	memcpy(item.data, data, CANP_DLC);
	if (k_msgq_put(&relay_q, &item, K_NO_WAIT) != 0) {
		atomic_inc(&overflow);
	}
}

static void relay_thread(void *a, void *b, void *c)
{
	ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
	struct relay_item item;

	gpio_pin_configure_dt(&tp_relay_tx, GPIO_OUTPUT_INACTIVE);

	while (1) {
		k_msgq_get(&relay_q, &item, K_FOREVER);
		uint64_t w0 = wcet_begin();

		enum canp_bus to = (item.from_bus == CANP_BUS_A) ? CANP_BUS_B : CANP_BUS_A;

		item.data[0] |= CANP_FLAG_RELAYED;

		/* RELAY_TX test point: rising edge = forwarded frame queued. Held
		 * >= RELAY_TP_MIN_US: queuing alone takes ~1 us, too narrow for
		 * the scope at the 100 us/div needed to see UDP_RX too. */
		uint32_t start = k_cycle_get_32();

		gpio_pin_set_dt(&tp_relay_tx, 1);
		if (can_link_send_on(to, item.id, item.data) == 0) {
			atomic_inc(&relayed);
		}
		uint32_t elapsed_us = k_cyc_to_us_floor32(k_cycle_get_32() - start);

		if (elapsed_us < RELAY_TP_MIN_US) {
			k_busy_wait(RELAY_TP_MIN_US - elapsed_us);
		}
		gpio_pin_set_dt(&tp_relay_tx, 0);
		wcet_end(WCET_RELAY, w0);   /* includes the >=10 us test-point hold */
	}
}

K_THREAD_DEFINE(relay_tid, 1024, relay_thread, NULL, NULL, NULL, ZONE_PRIO_RELAY, 0, 0);

void relay_get_stats(uint32_t *n_relayed, uint32_t *n_overflow)
{
	*n_relayed = atomic_get(&relayed);
	*n_overflow = atomic_get(&overflow);
}
