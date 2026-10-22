/*
 * Dual-bus CAN link for the STM32 zones (N2, N3).
 *
 * - Brings up CAN A and CAN B, registers the catalog IDs on both.
 * - One RX path for both buses: every frame is deduplicated by (ID, seq)
 *   before the zone sees it, so the second bus's copy never reaches a task.
 * - can_link_send() queues a frame on A then B back to back and bumps the
 *   ID's sequence counter once.
 * - Tracks the last time each zone's heartbeat was heard on each bus, for
 *   HB_MON (R4.5) and the degraded LEDs (R4.7).
 * - Pulses CAN_RX_A/B and CAN_TX_A/B test points.
 */
#ifndef CAN_LINK_H
#define CAN_LINK_H

#include <stdbool.h>
#include <stdint.h>

#include "can_proto.h"

/*
 * Called for every accepted (first-copy) frame. Runs in the MCP2515 driver's
 * cooperative RX thread (priority CONFIG_CAN_MCP2515_INT_THREAD_PRIO), so it
 * must be short and must not block: copy the data and wake a thread
 * (semaphore, k_msgq with K_NO_WAIT, atomic). Never log or call can_link_send
 * from here. `bus` is the bus this first copy arrived on.
 */
typedef void (*can_link_rx_cb_t)(uint32_t id, const uint8_t data[CANP_DLC],
				 enum canp_bus bus);

/*
 * Called in the bus's CAN_TX thread right after can_send() has handed a frame
 * to the MCP2515 (the TX-done callback means it doesn't wait for the bus).
 * Must be short and must not block. Used by RELAY for its RELAY_TX test point.
 */
typedef void (*can_link_sent_cb_t)(enum canp_bus bus, uint32_t id,
				   const uint8_t data[CANP_DLC]);

void can_link_set_sent_hook(can_link_sent_cb_t cb);

/* Start both buses. Returns the number of buses that came up (0..2); a bus
 * whose MCP2515 doesn't answer the SPI probe is left down and reported. */
int can_link_init(can_link_rx_cb_t rx_cb);

/* Overwrite the seq nibble of data[0] with this ID's next sequence number
 * (flags preserved), then queue on bus A and bus B. Never blocks: frames go
 * into a per-bus queue ordered by CAN ID, and a per-bus TX thread feeds the
 * MCP2515 (Zephyr's driver uses only one of its three TX buffers). Not for
 * CTRL: its CRC covers the seq, and only the Pi sends CTRL.
 * Returns a mask of the buses the frame was queued on (BIT(CANP_BUS_x)). */
int can_link_send(uint32_t id, uint8_t data[CANP_DLC]);

/* Queue on one bus exactly as given (no seq change). Used by RELAY on N2.
 * Returns 0, -ENETDOWN, or -ENOBUFS (queue full of higher-priority frames,
 * i.e. the bus has stopped ACKing). Never blocks. */
int can_link_send_on(enum canp_bus bus, uint32_t id, const uint8_t data[CANP_DLC]);

/* Uptime (ms) when zone's heartbeat was last heard on bus, any copy
 * (direct or relayed). -1 if never. */
int64_t can_link_last_hb_ms(enum canp_zone zone, enum canp_bus bus);

/* True if the bus's controller came up at init. */
bool can_link_bus_started(enum canp_bus bus);

struct can_link_stats {
	uint32_t rx_accepted;
	uint32_t rx_dropped;      /* duplicates and stale frames */
	uint32_t rx_bad;          /* wrong DLC, extended ID, unknown ID */
	uint32_t tx_queued[CANP_NUM_BUSES];
	uint32_t tx_busy[CANP_NUM_BUSES];   /* dropped: queue full or the TX buffer
					     * stayed busy 10 ms (bus not ACKing) */
	uint32_t tx_done_err[CANP_NUM_BUSES];
	uint32_t tx_recover[CANP_NUM_BUSES]; /* controller restarts for a stuck TX buffer */
	uint32_t int_recover[CANP_NUM_BUSES]; /* stuck INT line released */
};

/* Every HB_MON poll (10 ms): releases an MCP2515 INT line that stayed low,
 * which would otherwise leave that bus deaf until reset. */
void can_link_poll(void);

/* Copy of the running counters (since boot). */
void can_link_get_stats(struct can_link_stats *out);

/* CAN controller state of a bus as a short string, plus error counters. */
const char *can_link_bus_state(enum canp_bus bus, uint8_t *tec, uint8_t *rec);

#endif /* CAN_LINK_H */
