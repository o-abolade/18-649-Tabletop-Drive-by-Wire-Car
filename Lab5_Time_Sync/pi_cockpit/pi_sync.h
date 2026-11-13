/*
 * Lab 5 time sync on the Pi (N1): the Linux side of gtime.c / refsync.c,
 * the counterpart of the STM32 gsync.c.
 *
 * Timestamps come from SocketCAN: SO_TIMESTAMPNS on every received frame,
 * and on the echo of each frame we sent (CAN_RAW_RECV_OWN_MSGS; the mcp251x
 * driver has IFF_ECHO, so the echo is delivered when the transmission
 * completes). Both are taken by the kernel in the driver's interrupt
 * thread, so they mark the end of the frame on the wire plus a similar
 * driver latency. The kernel stamps CLOCK_REALTIME, which NTP steers; this
 * code converts them to CLOCK_MONOTONIC_RAW (the raw crystal), so NTP
 * adjustments don't look like crystal drift to gtime.
 *
 * The Pi keeps its own global-time offset and does not set the system
 * clock (resetting it would fight NTP).
 *
 * Thread-safe: CAN handling runs in the main poll loop, PPS in its own
 * thread.
 */
#ifndef PI_SYNC_H
#define PI_SYNC_H

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include "gtime.h"
#include "raft_proto.h"

/* Network delay and relay delay added to samples (the STM32 values; the
 * Pi's driver latency is calibrated with the PPS traces) */
#ifndef PI_SYNC_NET_DELAY_US
#define PI_SYNC_NET_DELAY_US    0
#endif
#ifndef PI_SYNC_RELAY_DELAY_US
#define PI_SYNC_RELAY_DELAY_US  400
#endif

/* PPS: sleep to this long before the edge, then busy-wait (Linux wake-up
 * latency is tens to hundreds of us) */
#define PI_SYNC_PPS_WAKE_EARLY_US 2000
#define PI_SYNC_PPS_WIDTH_MS      100

enum pi_sync_pin { PI_PIN_PPS, PI_PIN_REF_RX, PI_PIN_LEADER, PI_PIN_SYNC };

/* GPIO setter supplied by the cockpit (must be callable from any thread) */
typedef void (*pi_sync_pin_fn)(enum pi_sync_pin pin, bool on);

/* `realtime_pps`: give the PPS thread SCHED_FIFO (needs root) */
void pi_sync_init(unsigned self, pi_sync_pin_fn set_pin, bool realtime_pps);

/* Local clock: CLOCK_MONOTONIC_RAW in us */
int64_t pi_local_us(void);

/* A kernel SO_TIMESTAMPNS stamp (CLOCK_REALTIME) as pi_local_us() time */
int64_t pi_stamp_to_local_us(const struct timespec *ts);

/* Every received frame copy (bus = 0 A, 1 B), with its local stamp */
void pi_sync_rx(unsigned bus, uint32_t id, const uint8_t d[RAFTP_DLC], int64_t local_us);

/* The echo of one of our frames: it finished on `bus` at local_us */
void pi_sync_tx_done(unsigned bus, uint32_t id, const uint8_t d[RAFTP_DLC], int64_t local_us);

/* Leader: per-bus copies of a REF built with raftp_encode_ref() */
void pi_sync_fill_ref(const uint8_t d[RAFTP_DLC], uint8_t out[RAFT_NUM_BUSES][RAFTP_DLC]);

void pi_sync_set_leader(unsigned leader);
void pi_sync_set_relayed_buses(unsigned mask);

/* Handout 6.2 */
int64_t global_gettime_us(void);

/* SYNC: master, or a sample in the last 3 heartbeats */
bool pi_sync_valid(void);

/* The 1 s DRIFT line (handout 6.2); call from the main loop */
void pi_sync_print_drift(void);

/* Sleep until global time `global`, ending with a busy-wait (the edge lands
 * within tens of us; Linux wake-up alone is late by up to ~1 ms) */
void pi_sync_wait_until(int64_t global);

#endif /* PI_SYNC_H */
