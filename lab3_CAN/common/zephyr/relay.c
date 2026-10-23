/*
 * RELAY (N2 and N3): forwards traffic between the buses so a double fault
 * like R4.8 scenario (c) keeps every zone reachable. Checkoff tests four
 * cases and the relay must run on an STM32:
 *   N1 off B + N3 off A, N1 off A + N3 off B  -> N2 relays
 *   N1 off A + N2 off B, N1 off B + N2 off A  -> N3 relays
 * (N2 and N3 split across the buses would need the Pi to relay: not tested.)
 *
 * Design: always relay, no fault detection needed, so the first relayed
 * frame isn't delayed by the 60 ms degraded-bus detection. Cost: every
 * frame from another zone appears up to three times on each bus (receivers
 * drop the extra copies by seq).
 *
 * What gets relayed: every accepted (first-copy) frame that this zone didn't
 * send itself, onto the OTHER bus from the one it arrived on, UNCHANGED.
 *
 * Why unchanged: a relayed copy can reach the bus at the same moment as the
 * sender's own copy on that bus (seen with N3 forwarding N2's HB_N2 onto B
 * while N2's B copy was still queued). Same ID with different data (e.g. a
 * "relayed" flag bit) is a bit error for both transmitters, and they retry
 * into each other: TEC climbed to ~76 and TX buffers stuck. Identical frames
 * sent together are legal CAN and simply go out once.
 *
 * No loops without a flag: dedup hands this hook only the FIRST copy of each
 * (ID, seq), and a zone never relays its own frames, so each zone forwards a
 * given frame at most once and forwarding stops after every zone has it.
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

/* Depth covers one 20 ms cycle of the other zones' traffic with room to spare */
K_MSGQ_DEFINE(relay_q, sizeof(struct relay_item), 16, 4);

/* RELAY_TX test point: only on zones whose overlay has one (N2) */
#if DT_NODE_EXISTS(DT_ALIAS(tp_relay_tx))
#define HAVE_RELAY_TP 1
static const struct gpio_dt_spec tp_relay_tx = GPIO_DT_SPEC_GET(DT_ALIAS(tp_relay_tx), gpios);
#else
#define HAVE_RELAY_TP 0
#endif

static enum canp_zone self;
static atomic_t relayed, overflow;

/* Which zone sent a frame; 0 if unknown */
static int sender_of(uint32_t id, const uint8_t *data)
{
	switch (id) {
	case CANP_ID_CTRL:
	case CANP_ID_BLINK:
	case CANP_ID_HB_N1:
		return CANP_ZONE_N1;
	case CANP_ID_HB_N2:
	case CANP_ID_FORCE:
		return CANP_ZONE_N2;
	case CANP_ID_HB_N3:
		return CANP_ZONE_N3;
	case CANP_ID_FAULT:
		return canp_fault_zone(data);
	default:
		return 0;
	}
}

static bool should_relay(uint32_t id, const uint8_t *data)
{
	int sender = sender_of(id, data);

	return sender != 0 && sender != (int)self;
}

/* RX dispatch context: copy and hand off, never block */
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

/* CAN_TX thread context, right after can_send() returned. Measurement #6
 * (RELAY C) ends here: CAN_RX_A rising -> RELAY_TX rising. */
static void relay_sent(enum canp_bus bus, uint32_t id, const uint8_t data[CANP_DLC])
{
	ARG_UNUSED(bus);
	/* A frame this zone sends but didn't originate is a relayed one */
	if (!HAVE_RELAY_TP || !should_relay(id, data)) {
		return;
	}
#if HAVE_RELAY_TP
	/* Held >= RELAY_TP_MIN_US so the scope can see it at 100 us/div */
	gpio_pin_set_dt(&tp_relay_tx, 1);
	k_busy_wait(RELAY_TP_MIN_US);
	gpio_pin_set_dt(&tp_relay_tx, 0);
#endif
}

static void relay_thread(void *a, void *b, void *c)
{
	ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
	struct relay_item item;

	while (1) {
		k_msgq_get(&relay_q, &item, K_FOREVER);
		uint64_t w0 = wcet_begin();

		enum canp_bus to = (item.from_bus == CANP_BUS_A) ? CANP_BUS_B : CANP_BUS_A;

		if (can_link_send_on(to, item.id, item.data) == 0) {
			atomic_inc(&relayed);
		}
		wcet_end(WCET_RELAY, w0);
	}
}

K_THREAD_DEFINE(relay_tid, 1024, relay_thread, NULL, NULL, NULL, ZONE_PRIO_RELAY, 0, 0);

void relay_init(enum canp_zone zone)
{
	self = zone;
#if HAVE_RELAY_TP
	gpio_pin_configure_dt(&tp_relay_tx, GPIO_OUTPUT_INACTIVE);
#endif
	can_link_set_sent_hook(relay_sent);
}

void relay_get_stats(uint32_t *n_relayed, uint32_t *n_overflow)
{
	*n_relayed = atomic_get(&relayed);
	*n_overflow = atomic_get(&overflow);
}
