/*
 * Lab 5 Raft on the STM32 zones (design.md sections 4-6): the Zephyr side of
 * raft.c and netstate.c.
 *
 * - RAFT thread: owns the one struct raft. Raft frames from the RX path are
 *   queued to it; it calls raft_tick() every 2 ms. REF goes out through
 *   gsync (per-bus send times), everything else through can_link.
 * - Raft client: every 10 ms it compares what this node hears (can_link's
 *   per-bus heartbeat times) with the committed graph and reports the
 *   differences to the leader (netstate_observe()).
 * - Routing: the relay decision for each received frame comes from the
 *   committed graph plus the links the latest heartbeats report
 *   (netstate_local_view(), handout 5.2).
 * - Console: elections and appends (RAFT_DEBUG), every commit, and a status
 *   line with getNetwork().
 *
 * Startup mode: CONFIG_LAB5_LEADER_RANDOM picks LEADER=random, otherwise
 * LEADER=cockpit; holding B1 while the board resets picks the other one,
 * and holding it 3 s more picks the seed that makes the Pi win. All three
 * nodes must run the same mode and seed.
 */
#ifndef RAFT_NODE_H
#define RAFT_NODE_H

#include <stdbool.h>
#include <stdint.h>

#include "can_proto.h"

/* Client: re-send a report that still differs from the committed graph
 * after this long, and send none in the first RAFT_NODE_BOOT_QUIET_MS after
 * boot (nothing heard yet would look like every link down). */
#define RAFT_NODE_REPORT_MS      200
#define RAFT_NODE_BOOT_QUIET_MS  200
#define RAFT_NODE_TICK_MS        2
#define RAFT_NODE_OBSERVE_MS     10
/* B1 held this long at reset (LEADER=random): use RAFT_SEED_PI_WINS */
#define RAFT_NODE_SEED_HOLD_MS   3000

/* Call after zone_core_init() (CAN is up). Starts gsync and the RAFT
 * thread. */
void raft_node_init(enum canp_zone self);

/* Every accepted frame, from the MCP2515 RX thread (zone_core). Never
 * blocks. */
void raft_node_on_frame(uint32_t id, const uint8_t data[CANP_DLC], enum canp_bus bus);

/* RELAY: should this node forward `sender`'s frame from `from_bus` to the
 * other bus? */
bool raft_node_should_relay(uint32_t id, unsigned sender, enum canp_bus from_bus);

/* Failure mode: COCKPIT_ERROR is committed (hazards at 4 Hz, step 5). */
bool raft_node_failure_mode(void);

/* Console lines for the zone's status print */
void raft_node_print_status(void);

#endif /* RAFT_NODE_H */
