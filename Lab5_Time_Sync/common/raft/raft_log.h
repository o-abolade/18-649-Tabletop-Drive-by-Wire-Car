/*
 * Lab 5 replicated log: a ring of the most recent entries plus a snapshot
 * (log compaction, Progress_doc_lab5.md question 8).
 *
 * Indexes are logical and start at 1; index 0 is "before the first entry"
 * (term 0). Entries snap_index+1 .. last are held in the ring; everything up
 * to snap_index is folded into snap_state, the network graph as of that
 * index. The ring holds RAFT_LOG_WINDOW entries; indexes run up to
 * RAFT_LOG_SIZE (log_size).
 *
 * Plain C99, no Zephyr or Linux dependencies.
 */
#ifndef RAFT_LOG_H
#define RAFT_LOG_H

#include <stdbool.h>
#include <stdint.h>

#include "netstate.h"
#include "raft_config.h"

/* Entry flag: the entry was already committed by the leader when this node
 * received it (catch-up after an outage); printed as "replayed". */
#define RAFT_ENTRY_REPLAYED 0x01

struct raft_entry {
	uint8_t term;
	uint8_t event;
	uint8_t flags;
};

struct raft_log {
	struct raft_entry ring[RAFT_LOG_WINDOW];
	uint16_t snap_index;            /* last index folded into the snapshot */
	uint8_t snap_term;              /* term of entry snap_index (0 if none) */
	struct netstate snap_state;     /* graph after applying 1..snap_index */
	uint16_t last;                  /* last index in the log */
};

void raft_log_init(struct raft_log *l);

static inline uint16_t raft_log_last(const struct raft_log *l)
{
	return l->last;
}

/* Number of entries held in the ring */
static inline uint16_t raft_log_count(const struct raft_log *l)
{
	return (uint16_t)(l->last - l->snap_index);
}

/* Term of entry i. True for i in snap_index..last (snap_index gives
 * snap_term, so index 0 of an empty log gives term 0); false otherwise. */
bool raft_log_term_at(const struct raft_log *l, uint16_t i, uint8_t *term);

uint8_t raft_log_last_term(const struct raft_log *l);

/* Entry i, for snap_index < i <= last; NULL otherwise. */
const struct raft_entry *raft_log_get(const struct raft_log *l, uint16_t i);

/* Append at last + 1. False if the ring is full or the log reached
 * RAFT_LOG_SIZE; the caller compacts first (raft_log_compact). */
bool raft_log_append(struct raft_log *l, uint8_t term, uint8_t event, uint8_t flags);

/* Delete entries from..last (a follower whose log conflicts with the
 * leader's). Ignored unless snap_index < from <= last. */
void raft_log_truncate(struct raft_log *l, uint16_t from);

/* Fold entries up to `upto` into the snapshot. `state` must be the graph as
 * of `upto` (the applied state). Ignored unless snap_index < upto <= last. */
void raft_log_compact(struct raft_log *l, uint16_t upto, const struct netstate *state);

/* Replace the whole log with a snapshot received from the leader. */
void raft_log_install(struct raft_log *l, uint16_t index, uint8_t term,
		      const struct netstate *state);

#endif /* RAFT_LOG_H */
