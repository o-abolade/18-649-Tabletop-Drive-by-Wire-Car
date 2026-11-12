/* Lab 5 replicated log: ring + snapshot. See raft_log.h. */
#include "raft_log.h"

#include <string.h>

static unsigned slot(uint16_t i)
{
	return i % RAFT_LOG_WINDOW;
}

void raft_log_init(struct raft_log *l)
{
	memset(l, 0, sizeof(*l));
	netstate_init(&l->snap_state);
}

bool raft_log_term_at(const struct raft_log *l, uint16_t i, uint8_t *term)
{
	if (i == l->snap_index) {
		*term = l->snap_term;
		return true;
	}
	if (i > l->snap_index && i <= l->last) {
		*term = l->ring[slot(i)].term;
		return true;
	}
	return false;
}

uint8_t raft_log_last_term(const struct raft_log *l)
{
	uint8_t t = 0;

	raft_log_term_at(l, l->last, &t);
	return t;
}

const struct raft_entry *raft_log_get(const struct raft_log *l, uint16_t i)
{
	if (i <= l->snap_index || i > l->last) {
		return NULL;
	}
	return &l->ring[slot(i)];
}

bool raft_log_append(struct raft_log *l, uint8_t term, uint8_t event, uint8_t flags)
{
	if (raft_log_count(l) >= RAFT_LOG_WINDOW || l->last >= RAFT_LOG_SIZE) {
		return false;
	}
	l->last++;
	l->ring[slot(l->last)] = (struct raft_entry){ .term = term, .event = event, .flags = flags };
	return true;
}

void raft_log_truncate(struct raft_log *l, uint16_t from)
{
	if (from > l->snap_index && from <= l->last) {
		l->last = (uint16_t)(from - 1);
	}
}

void raft_log_compact(struct raft_log *l, uint16_t upto, const struct netstate *state)
{
	uint8_t t;

	if (upto <= l->snap_index || upto > l->last || !raft_log_term_at(l, upto, &t)) {
		return;
	}
	l->snap_term = t;
	l->snap_index = upto;
	l->snap_state = *state;
}

void raft_log_install(struct raft_log *l, uint16_t index, uint8_t term,
		      const struct netstate *state)
{
	l->snap_index = index;
	l->snap_term = term;
	l->snap_state = *state;
	l->last = index;
}
