/*
 * Lab 5 global time (handout Part 6): clock sync from the leader's REF
 * frames, drift correction between them, and global_gettime().
 *
 * Times are microseconds in int64_t. "local" is this node's free-running
 * monotonic clock; "global" is the leader's time base. The mapping is
 *
 *     global(local) = anchor_global + dl + dl * drift_ppb / 1e9,
 *     dl = local - anchor_local
 *
 * Follower: every REF pair (the master time of REF k-1, carried by REF k,
 * and the local receive time of REF k-1) is one sample. A sample re-anchors
 * the mapping at the leader's time (handout 6.1: follower_clock = L1 +
 * network_delay). Pairs at least GTIME_DRIFT_BASE_US apart give the drift
 * ratio (handout 6.2: (L2 - L1) / (F2 - F1)), low-pass filtered, which is
 * applied between samples. A ratio over one 20 ms period would carry the
 * stamp jitter (a few us) as hundreds of ppm, so the baseline is 1 s.
 *
 * Master: the mapping is frozen, so a follower that becomes leader continues
 * its own global time and the other followers see no step (checkoff 6).
 *
 * Plain C99, no Zephyr or Linux dependencies. Not thread-safe: the platform
 * serializes calls into one struct gtime.
 */
#ifndef GTIME_H
#define GTIME_H

#include <stdbool.h>
#include <stdint.h>

#include "raft_config.h"

/* REF carries master time modulo this (4000 s, below 2^32 us). The wrap is a
 * whole number of seconds, so two nodes that disagree by whole wraps still
 * put their PPS edges and blinker phases at the same instants. */
#define GTIME_WIRE_MOD_US     4000000000LL

/* Baseline for one drift measurement, and the filter weight of a new one */
#define GTIME_DRIFT_BASE_US   1000000
#define GTIME_DRIFT_FILTER    4          /* new = old + (raw - old) / 4 */
/* A raw drift beyond this is a bad pair, not a crystal (STM32/Pi crystals
 * are within +-50 ppm of each other) */
#define GTIME_DRIFT_MAX_PPB   500000

/* A sample whose error exceeds this is an outlier (a late interrupt) and is
 * ignored, unless GTIME_OUTLIER_RUN of them arrive in a row: then the master
 * time really moved (a new master with another time base) and we step. */
#define GTIME_OUTLIER_US      50
#define GTIME_OUTLIER_RUN     3

/* SYNC is valid while a sample arrived in the last 3 heartbeats */
#define GTIME_VALID_US        (RAFT_MISSED_HEARTBEATS * RAFT_HEARTBEAT_MS * 1000LL)

enum gtime_result {
	GTIME_FIRST = 0,   /* first sample: clock set */
	GTIME_OK,          /* re-anchored, error within GTIME_OUTLIER_US */
	GTIME_OUTLIER,     /* ignored */
	GTIME_STEPPED,     /* master time moved: clock stepped */
	GTIME_IGNORED,     /* this node is the master */
};

struct gtime {
	bool master;
	bool synced;               /* follower: at least one sample */
	int64_t anchor_local;
	int64_t anchor_global;
	int32_t drift_ppb;         /* global runs (1 + drift_ppb / 1e9) x local */
	bool drift_valid;
	int64_t base_local;        /* start of the current drift baseline */
	int64_t base_global;
	int64_t last_sample_local; /* for SYNC validity */

	/* Last accepted sample, for the console */
	int32_t last_error_us;     /* master - our estimate, before re-anchoring */
	int32_t last_drift_us;     /* drift correction included in that estimate */
	int32_t last_raw_ppb;      /* last unfiltered drift measurement */

	uint8_t outlier_run;
	uint32_t samples;
	uint32_t outliers;
	uint32_t steps;
};

/* Unsynced follower whose global time is its local time until the first
 * sample (so a node that leads from boot starts at its own clock). */
void gtime_init(struct gtime *g);

/* Become the time master: freeze the current mapping. */
void gtime_become_master(struct gtime *g);

/* Become a follower: keep the current mapping until samples arrive. */
void gtime_become_follower(struct gtime *g);

/* global_gettime() at local time `local`. */
int64_t gtime_global(const struct gtime *g, int64_t local);

/* Local time at which global time reaches `global` (for PPS and blinkers). */
int64_t gtime_local_of(const struct gtime *g, int64_t global);

/* Correction the drift ratio adds at `local` since the last anchor (us). */
int32_t gtime_drift_corr_us(const struct gtime *g, int64_t local);

/* Master: always. Follower: a sample in the last GTIME_VALID_US. */
bool gtime_valid(const struct gtime *g, int64_t local_now);

/* One REF pair: `master_wire` = the leader's send time of a REF (modulo
 * GTIME_WIRE_MOD_US), `local_rx` = our receive time of that same REF,
 * `delay_us` = network delay to add. */
enum gtime_result gtime_sample(struct gtime *g, uint32_t master_wire, int64_t local_rx,
			       int32_t delay_us);

/* Global time as sent in REF b3-b6 */
uint32_t gtime_wire(int64_t global);

#endif /* GTIME_H */
