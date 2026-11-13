/*
 * Lab 5 REF time exchange (design.md section 7), shared by the STM32 glue
 * (gsync.c) and the Pi cockpit.
 *
 * Leader: each REF carries, per bus, the global time at which the previous
 * REF finished on that bus (refsync_tx_done() records it, refsync_fill_ref()
 * writes it into that bus's copy).
 * Follower: the receive stamp of REF k-1 on a bus, paired with the send
 * time REF k carries for that same bus, is one gtime sample. Rules learned
 * on the bench:
 *  - only the first copy of a REF on each bus counts (a relayed copy of the
 *    same REF comes later);
 *  - samples come from one bus at a time: the two buses' stamps of one REF
 *    differ by a few us, and mixing them made the clock saw-tooth;
 *  - a bus on which the leader's REF only arrives relayed gets the relay
 *    delay added.
 * Timestamps are the caller's local microsecond clock; `ok` = the stamp can
 * be trusted (taken at the end of the frame).
 *
 * Plain C99, not thread-safe: the caller serializes calls.
 */
#ifndef REFSYNC_H
#define REFSYNC_H

#include <stdbool.h>
#include <stdint.h>

#include "gtime.h"
#include "raft_proto.h"

/* Switch sync bus only after this long without a sample on the current one */
#define REFSYNC_BUS_SWITCH_US (2 * RAFT_HEARTBEAT_MS * 1000LL)

struct refsync_rx {
	bool have;
	bool ok;
	bool relayed;     /* this copy came through a relay (delay applies to it) */
	uint8_t sender;
	uint8_t cycle;
	int64_t local_us;
};

struct refsync_tx {
	bool have;
	bool ok;
	uint8_t cycle;
	int64_t local_us;
};

struct refsync {
	struct gtime g;
	unsigned self;
	unsigned leader;            /* 0 = none: REFs are ignored */
	int32_t net_delay_us;
	int32_t relay_delay_us;
	unsigned relayed_buses;     /* bit b: REF on bus b arrives relayed */

	struct refsync_rx rx[RAFT_NUM_BUSES];
	struct refsync_tx tx[RAFT_NUM_BUSES];
	int sync_bus;               /* -1 = none yet */
	int64_t sync_bus_last_us;
	uint8_t last_sender, last_cycle;   /* first copy of a REF on any bus */

	/* Statistics */
	int32_t err_max_us;         /* worst |err| since the caller reset it */
	uint32_t ref_rx;            /* REF copies received */
	uint32_t stamp_rx_bad;
	uint32_t stamp_tx_bad;
	uint32_t ref_tx_valid[RAFT_NUM_BUSES];
};

void refsync_init(struct refsync *s, unsigned self, int32_t net_delay_us,
		  int32_t relay_delay_us);

/* The Raft leader is the time master: self = master, 0 = none. */
void refsync_set_leader(struct refsync *s, unsigned leader);

/* A received copy of any frame on `bus`, stamped at `local_us`. Returns true
 * if it is the first copy of a new REF (pulse REF_RX). *sampled is set to
 * true if it produced a gtime sample, with the result in *res. */
bool refsync_rx(struct refsync *s, unsigned bus, uint32_t id, const uint8_t d[RAFTP_DLC],
		int64_t local_us, bool ok, bool *sampled, enum gtime_result *res);

/* One of our frames finished on `bus` at `local_us`. */
void refsync_tx_done(struct refsync *s, unsigned bus, uint32_t id, const uint8_t d[RAFTP_DLC],
		     int64_t local_us, bool ok);

/* Leader: the per-bus copies of a REF built with raftp_encode_ref(). */
void refsync_fill_ref(struct refsync *s, const uint8_t d[RAFTP_DLC],
		      uint8_t out[RAFT_NUM_BUSES][RAFTP_DLC]);

#endif /* REFSYNC_H */
