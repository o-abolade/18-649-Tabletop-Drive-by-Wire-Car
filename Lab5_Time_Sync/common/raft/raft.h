/*
 * Lab 5 Raft agent and Raft client (handout Parts 1-4).
 *
 * One struct raft per node. The agent is event-driven and never touches
 * hardware: the platform calls raft_tick() every few ms and
 * raft_on_frame() for every accepted Raft frame, and the agent calls back
 * through struct raft_io to send frames and to report commits and role
 * changes. The same file runs on both STM32 zones and on the Pi.
 *
 * Beyond basic Raft:
 *  - Predefined leader (default_leader): every node boots in term 1 with that
 *    leader, so no election is held. A follower starts its election timer
 *    only after hearing that leader once (the Pi boots seconds after the
 *    Nucleos). The default leader itself first listens for one election
 *    timeout, so a rebooted Pi rejoins as a follower if someone else leads.
 *  - Pre-vote: a node raises its term only if a majority would vote for it.
 *    An isolated node never gets that majority, so the term changes only
 *    when the leader really is gone (checkoff step 10).
 *  - Check-quorum: a leader that has heard no acknowledgement from a
 *    majority for RAFT_QUORUM_MS steps down (same term), so an isolated
 *    leader turns its LEADER LED off.
 *  - Log compaction: entries beyond RAFT_LOG_WINDOW are folded into a
 *    snapshot of the network graph, which fits in one SNAPSHOT frame.
 *  - Client: each node's client reports network events to the leader with
 *    EVENT frames, retries until it gets an answer, and follows redirects.
 *    The leader appends a report only if it changes the network state.
 *
 * Terms are 8 bits (255 elections). Commit of entries from earlier terms
 * waits for the first entry of the current term, as in standard Raft.
 *
 * Plain C99, no Zephyr or Linux dependencies. Not thread-safe: the caller
 * serializes calls into one struct raft.
 */
#ifndef RAFT_H
#define RAFT_H

#include <stdbool.h>
#include <stdint.h>

#include "netstate.h"
#include "raft_config.h"
#include "raft_log.h"
#include "raft_proto.h"

/* Leader acknowledgement window for check-quorum */
#define RAFT_QUORUM_MS          300
/* Minimum spacing between catch-up APPEND frames, and the resend time for an
 * unacknowledged one */
#define RAFT_APPEND_GAP_MS      2
#define RAFT_APPEND_RETRY_MS    20
/* Client: resend an unanswered EVENT after this long */
#define RAFT_CLIENT_RETRY_MS    50
#define RAFT_CLIENT_QUEUE       8

enum raft_role {
	RAFT_FOLLOWER = 0,
	RAFT_PRECANDIDATE,   /* asking for pre-votes; term unchanged */
	RAFT_CANDIDATE,
	RAFT_LEADER,
};

struct raft_io {
	void *ctx;
	/* Queue a frame on both buses. Required. */
	void (*send)(void *ctx, uint32_t id, const uint8_t d[RAFTP_DLC]);
	/* A committed entry was applied to the state machine (in index order).
	 * `replayed`: it arrived already committed (catch-up). Optional. */
	void (*apply)(void *ctx, uint16_t index, uint8_t term, uint8_t event, bool replayed);
	/* The state was replaced by the leader's snapshot. Optional. */
	void (*snapshot)(void *ctx, uint16_t index, const struct netstate *ns);
	/* Role, leader or term changed. leader = 0 if unknown. Optional. */
	void (*role)(void *ctx, enum raft_role role, unsigned leader, uint8_t term);
	/* Debug line (RAFT_DEBUG): elections and appends. Optional. */
	void (*debug)(void *ctx, const char *line);
};

struct raft {
	unsigned self;
	struct raft_io io;

	/* Persistent Raft state (in RAM on the Nucleos) */
	uint8_t term;
	uint8_t voted_for;          /* 0 = none */
	struct raft_log log;

	/* Volatile state */
	enum raft_role role;
	unsigned leader;            /* 0 = unknown */
	uint16_t commit;
	uint16_t applied;
	struct netstate state;      /* graph after applying 1..applied */

	/* Election timing */
	int64_t last_heard_ms;      /* last valid message from the leader */
	int64_t election_deadline;
	bool timer_armed;           /* false until the default leader is heard */
	int64_t boot_listen_until;  /* default leader's listen window */
	uint32_t rng;
	uint8_t votes;              /* bit n = node n granted (pre)vote */

	/* Leader state */
	uint16_t next_index[RAFT_N + 1];
	uint16_t match_index[RAFT_N + 1];
	int64_t last_ack_ms[RAFT_N + 1];
	int64_t next_ref_ms;
	int64_t next_empty_ms;
	int64_t append_sent_ms;
	uint16_t append_sent_index;
	uint16_t commit_sent;       /* commit index last broadcast */
	uint8_t cycle;              /* REF cycle counter */
	uint32_t ref_prev_send_us;
	bool ref_time_valid;
	/* Per client: the last report handled, and its result. A client has one
	 * report outstanding at a time; OK is answered once its entry commits. */
	bool client_seen[RAFT_N + 1];
	uint8_t client_last_seq[RAFT_N + 1];
	uint8_t client_last_result[RAFT_N + 1];
	uint16_t client_last_index[RAFT_N + 1];

	/* Client state */
	uint8_t client_queue[RAFT_CLIENT_QUEUE];
	unsigned client_count;
	uint8_t client_seq;
	unsigned client_target;     /* node the head report is addressed to */
	int64_t client_retry_ms;

	/* Statistics */
	uint32_t elections;         /* elections this node started (real votes) */
	uint32_t frames_sent;
};

/* Initialize at boot with the config's default_leader and seed. `now_ms` is
 * the platform's monotonic clock. */
void raft_init(struct raft *r, unsigned self, const struct raft_io *io, int64_t now_ms);

/* Same, with the startup mode given at run time (LEADER=cockpit / random):
 * default_leader is a node ID or RAFT_LEADER_NONE. All three nodes must use
 * the same value. */
void raft_init_with_leader(struct raft *r, unsigned self, const struct raft_io *io,
			   int64_t now_ms, unsigned default_leader, uint32_t seed);

/* Call every 1-5 ms: timers, REF, replication, client retries. */
void raft_tick(struct raft *r, int64_t now_ms);

/* Call for every accepted (deduplicated) Raft frame. Non-Raft IDs are
 * ignored. A node never receives its own frames. */
void raft_on_frame(struct raft *r, uint32_t id, const uint8_t d[RAFTP_DLC], int64_t now_ms);

/* Client: report a network event (an event code from netstate.h). Queued
 * and delivered to the leader; duplicates already queued are dropped. */
void raft_client_report(struct raft *r, uint8_t event);

/* Client: drop a queued report that is no longer true (the observation
 * flipped back before the report was answered, e.g. on an isolated node).
 * Returns true if it was queued. A report already taken by the leader may
 * still commit; the opposite report then corrects it. */
bool raft_client_withdraw(struct raft *r, uint8_t event);

/* Client: is this report still queued (not yet answered)? */
bool raft_client_pending(const struct raft *r, uint8_t event);

/* The platform reports when the REF it just sent was handed to the CAN
 * controller, in its microsecond clock; the next REF carries it. */
void raft_ref_sent(struct raft *r, uint32_t send_us);

/* getNetwork(): the committed network graph. */
static inline void raft_get_network(const struct raft *r, struct netstate *out)
{
	netstate_get(&r->state, out);
}

static inline bool raft_is_leader(const struct raft *r)
{
	return r->role == RAFT_LEADER;
}

const char *raft_role_name(enum raft_role role);

#endif /* RAFT_H */
