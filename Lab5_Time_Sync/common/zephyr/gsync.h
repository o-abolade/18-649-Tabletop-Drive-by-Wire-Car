/*
 * Lab 5 time sync on the STM32 zones (handout Part 6, design.md section 7):
 * the Zephyr side of gtime.c.
 *
 * - Leader: each REF carries, per bus, the time at which the previous REF
 *   finished on that bus (the MCP2515 TX-complete interrupt edge, from
 *   can_link), in global time.
 * - Follower: the receive edge of REF k-1 on a bus, paired with the send
 *   time that REF k carries for that same bus, is one gtime sample. Both
 *   stamps mark the end of the same frame on the same wire, so the network
 *   delay is a small constant (GSYNC_NET_DELAY_US, calibrated on the scope).
 *   Each sample also sets the Zephyr realtime clock (sys_clock_settime).
 * - global_gettime() returns the drift-corrected global time.
 * - PPS: a rising edge at every whole second of global time, and a console
 *   line per edge; a 1 s task prints the drift correction.
 * - LEDs/test points: LEADER, SYNC, REF_RX (toggles on each REF).
 *
 * The time master is whoever the caller names with gsync_set_leader() (the
 * Raft leader in the full system).
 */
#ifndef GSYNC_H
#define GSYNC_H

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include "raft_proto.h"

/* Network delay added to every sample: receiver's end-of-frame edge minus
 * the sender's (both are interrupt edges at the end of the same frame) */
#ifndef GSYNC_NET_DELAY_US
#define GSYNC_NET_DELAY_US     0
#endif

/* PPS: wake this long before the edge, then busy-wait to it */
#define GSYNC_PPS_WAKE_EARLY_US 300
#define GSYNC_PPS_WIDTH_MS      100
#define GSYNC_DRIFT_PRINT_MS    1000

/* Thread priorities. PPS is cooperative and above the MCP2515 threads
 * (K_PRIO_COOP(2)) so nothing preempts the final busy-wait, which lasts at
 * most GSYNC_PPS_WAKE_EARLY_US once a second. The console threads are low. */
#define GSYNC_PRIO_PPS          K_PRIO_COOP(1)
#define GSYNC_PRIO_PRINT        K_PRIO_PREEMPT(12)

/* Configure the pins, install the can_link stamp hooks, start PPS and the
 * console threads. Call after can_link_init(). `self` = this node. */
int gsync_init(unsigned self);

/* The current time master: `self` makes this node master (frozen global
 * time), another node makes us follow its REFs, 0 = none (ignore REFs). */
void gsync_set_leader(unsigned leader);

/* Send a REF built with raftp_encode_ref() (term, cycle, flags): fills each
 * bus's b3-b6 send time and RAFTP_REF_TIME_VALID, and queues it on both
 * buses. Returns the can_link bus mask. */
int gsync_send_ref(uint8_t d[RAFTP_DLC]);

/* Handout 6.2: global time, drift corrected. */
int64_t global_gettime_us(void);
int global_gettime(struct timespec *ts);

/* Local monotonic clock (us) used by gtime */
int64_t gsync_local_us(void);

/* SYNC: master, or a sample in the last 3 heartbeats */
bool gsync_valid(void);

/* Every ~10 ms: SYNC LED. */
void gsync_poll(void);

struct gsync_stats {
	uint32_t ref_rx;           /* REF copies received (both buses) */
	uint32_t stamp_rx_bad;     /* receive edge not trusted */
	uint32_t stamp_tx_bad;     /* our REF's TX-complete edge not trusted */
	uint32_t samples;
	uint32_t outliers;
	uint32_t steps;
	uint32_t ref_tx_valid[2];  /* REFs sent with a valid time, per bus */
};

void gsync_get_stats(struct gsync_stats *out);

#endif /* GSYNC_H */
