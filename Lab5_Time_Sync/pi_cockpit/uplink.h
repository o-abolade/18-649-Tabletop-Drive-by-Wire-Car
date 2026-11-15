/*
 * Lab 5 extra credit: the Pi mirrors the vehicle's Raft log into Redis on a
 * cloud VM, for the browser GUI (Lab5_Time_Sync/cloud/).
 *
 *   vehicle:log        stream: one entry per committed log entry
 *   vehicle:heartbeat  hash, TTL 5 s, every 1 s: wall clock, global time,
 *                      Raft role/term/leader/commit, held entries, sync
 *                      figures, network state (JSON)
 *   vehicle:last       the same hash without a TTL (shown while stale)
 *   vehicle:index      index -> term of every entry already published
 *   vehicle:uplink     last acknowledged index and stream id
 *
 * Asynchronous and best-effort (handout): the cockpit only copies an entry
 * into a queue (microseconds, under a mutex). A separate thread, SCHED_OTHER
 * at nice 10 on CPUs 0-1 (the cockpit loop is SCHED_FIFO on CPU 3, PPS on
 * CPU 2), does all network I/O, so publishing never delays REF, APPEND or
 * CTRL. If the uplink is down the queue keeps the last UPLINK_QUEUE entries;
 * on reconnect it resumes after the last acknowledged entry. vehicle:index
 * also makes a restarted Pi skip entries already published, so only the
 * entries the leader held while the Pi was away are added, marked replayed.
 *
 * Configured from the environment (~/lab5-cockpit.env): UPLINK_HOST,
 * UPLINK_PORT (default 6379), UPLINK_PASSWORD. No UPLINK_HOST: disabled.
 * In our deployment the host is 127.0.0.1:6380, the Pi's end of an SSH
 * tunnel to the VM, so Redis itself is never reachable from the internet.
 */
#ifndef UPLINK_H
#define UPLINK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define UPLINK_QUEUE 1000

struct uplink_status {
	int64_t global_us;
	const char *role;
	unsigned term;
	unsigned leader;
	unsigned commit;
	bool sync;
	int32_t drift_ppb;
	int32_t err_us;
	const char *cockpit_state;   /* NORMAL / ERROR / LOCAL_FAIL */
	char state_json[320];        /* getNetwork() */
};

/* Start the publisher thread if UPLINK_HOST is set. */
void uplink_init(void);

/* A committed entry (call from Raft's apply callback). Never blocks on I/O. */
void uplink_entry(unsigned index, unsigned term, uint8_t event, bool replayed,
		  int64_t global_us);

/* The latest status, published as the 1 s heartbeat. Never blocks on I/O. */
void uplink_status(const struct uplink_status *s);

/* One line for the console: connected, queued, published, dropped. */
void uplink_describe(char *buf, size_t n);

#endif /* UPLINK_H */
