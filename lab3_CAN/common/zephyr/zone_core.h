/*
 * Behaviour shared by both STM32 zones:
 *   HB_TX, HB_MON (+ DEG_A/DEG_B/ERR LEDs), the error state machine,
 *   TEST_BTN (single press = local fail, double press = restore), FAULT
 *   TX/RX, the independent watchdog, and the zone's two blinker lamps
 *   driven by BLINK (or a local hazard while not NORMAL).
 * The zone app adds only its actuator (STEER / DRIVE, PID, RELAY, FORCE).
 *
 * Error state machine (Lab 1 R3, handout Part 5):
 *   power-up in ERROR;
 *   ERROR -> NORMAL when both other zones' heartbeats are seen on >= 1 bus
 *            and a valid CTRL arrived recently;
 *   -> ERROR on: a zone missing on both buses, a FAULT from another zone,
 *               out-of-range CTRL (also sends FAULT), or CTRL stale;
 *   -> LOCAL_FAIL on a test-button press (stops this zone's heartbeat and
 *      sends FAULT); a second press within 500 ms restores.
 */
#ifndef ZONE_CORE_H
#define ZONE_CORE_H

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/kernel.h>

#include "can_proto.h"

struct zone_config {
	enum canp_zone zone;

	/* Called right after the zone leaves NORMAL (brake / hold steering).
	 * Runs in HB_MON or TEST_BTN thread context; keep it short. */
	void (*enter_safe)(void);

	/* Optional: every accepted frame, in the MCP2515 RX thread (cooperative,
	 * must not block). N2 uses it to feed RELAY. */
	void (*rx_hook)(uint32_t id, const uint8_t data[CANP_DLC], enum canp_bus bus);
};

/* Configures LEDs, button and watchdog, starts both CAN buses and the shared
 * threads. Returns the number of CAN buses that came up. */
int zone_core_init(const struct zone_config *cfg);

enum canp_zone_state zone_state(void);
const char *zone_state_name(enum canp_zone_state s);

/* Block until the next valid CTRL (latest wins). Returns 0 with *out filled,
 * or -EAGAIN on timeout. */
int zone_wait_ctrl(struct canp_ctrl *out, k_timeout_t timeout);

/* One-line console summary (rate-limit the caller). */
void zone_print_status(void);

#endif /* ZONE_CORE_H */
