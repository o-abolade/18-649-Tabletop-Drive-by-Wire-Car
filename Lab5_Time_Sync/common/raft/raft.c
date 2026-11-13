/* Lab 5 Raft agent and client. See raft.h for the behaviour summary. */
#include "raft.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* Client result recorded on the leader while the client's entry commits */
#define RESULT_PENDING 0xFF
/* Followers that stopped acknowledging are probed this often */
#define PROBE_MS 200

/* ---------------- helpers ---------------- */

static void dbg(struct raft *r, const char *fmt, ...)
{
#if RAFT_DEBUG
	char line[112];
	va_list ap;

	if (!r->io.debug) {
		return;
	}
	va_start(ap, fmt);
	vsnprintf(line, sizeof(line), fmt, ap);
	va_end(ap);
	r->io.debug(r->io.ctx, line);
#else
	(void)r;
	(void)fmt;
#endif
}

const char *raft_role_name(enum raft_role role)
{
	switch (role) {
	case RAFT_FOLLOWER: return "follower";
	case RAFT_PRECANDIDATE: return "pre-candidate";
	case RAFT_CANDIDATE: return "candidate";
	case RAFT_LEADER: return "leader";
	default: return "?";
	}
}

static uint32_t rnd(struct raft *r)
{
	uint32_t x = r->rng;

	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	r->rng = x;
	return x;
}

static void reset_election_timer(struct raft *r, int64_t now)
{
	r->election_deadline = now + RAFT_ELECTION_BASE_MS +
			       (int64_t)(rnd(r) % (RAFT_ELECTION_JITTER_MS + 1));
}

static unsigned popcount(uint8_t v)
{
	unsigned n = 0;

	for (; v; v &= (uint8_t)(v - 1)) {
		n++;
	}
	return n;
}

static void send(struct raft *r, enum raftp_kind kind, const uint8_t d[RAFTP_DLC])
{
	r->io.send(r->io.ctx, raftp_id(kind, r->self), d);
	r->frames_sent++;
}

static void notify_role(struct raft *r)
{
	if (r->io.role) {
		r->io.role(r->io.ctx, r->role, r->leader, r->term);
	}
}

/* Learned who leads: the client addresses its reports there. */
static void set_leader(struct raft *r, unsigned leader)
{
	r->leader = leader;
	if (leader) {
		r->client_target = leader;
	}
}

/*
 * Terms are 8 bits on the wire. They are compared as plain numbers: every
 * node in LEADER=cockpit mode boots in term 1, so wrap-around (serial)
 * comparison would make a rebooted node's term 1 look newer than a cluster
 * term of 129-255. Instead the term never wraps: 255 elections take a cold
 * restart to reset. Before, the next term after 255 was 0, which every node
 * refused, so no leader could ever be elected again (seen on hardware, after
 * corrupted frames had pushed the term up; see RAFT_TERM_JUMP_MAX).
 */
static bool term_newer(uint8_t a, uint8_t b)
{
	return a > b;
}

static uint8_t term_next(uint8_t t)
{
	return t < UINT8_MAX ? (uint8_t)(t + 1) : t;
}

static bool up_to_date(const struct raft *r, uint8_t last_term, uint16_t last_index)
{
	uint8_t my_term = raft_log_last_term(&r->log);

	return term_newer(last_term, my_term) || (last_term == my_term && last_index >= r->log.last);
}

/* The graph including uncommitted entries: what the leader will commit. */
static void projected_state(const struct raft *r, struct netstate *out)
{
	*out = r->state;
	for (uint16_t i = (uint16_t)(r->applied + 1); i <= r->log.last && i != 0; i++) {
		const struct raft_entry *e = raft_log_get(&r->log, i);

		if (e) {
			netstate_apply(out, e->event);
		}
	}
}

/* Append, compacting applied entries into the snapshot if the ring is full. */
static bool append_entry(struct raft *r, uint8_t term, uint8_t event, uint8_t flags)
{
	if (raft_log_count(&r->log) >= RAFT_LOG_WINDOW && r->applied > r->log.snap_index) {
		raft_log_compact(&r->log, r->applied, &r->state);
	}
	return raft_log_append(&r->log, term, event, flags);
}

static void send_ack(struct raft *r, bool success, uint16_t match)
{
	uint8_t d[RAFTP_DLC];
	struct raftp_append_ack a = { .term = r->term, .success = success, .match_index = match };

	raftp_encode_append_ack(d, 0, &a);
	send(r, RAFTP_APPEND_ACK, d);
}

static void send_event_resp(struct raft *r, unsigned client, uint8_t seq, uint8_t result,
			    uint16_t index)
{
	uint8_t d[RAFTP_DLC];
	struct raftp_event_resp m = {
		.client = (uint8_t)client, .client_seq = seq, .result = result,
		.leader = (uint8_t)r->leader, .index = index,
	};

	raftp_encode_event_resp(d, 0, &m);
	send(r, RAFTP_EVENT_RESP, d);
}

/* ---------------- applying committed entries ---------------- */

static void answer_committed_clients(struct raft *r)
{
	for (unsigned c = 1; c <= RAFT_N; c++) {
		if (r->client_seen[c] && r->client_last_result[c] == RESULT_PENDING &&
		    r->client_last_index[c] <= r->commit) {
			r->client_last_result[c] = RAFTP_RESULT_OK;
			send_event_resp(r, c, r->client_last_seq[c], RAFTP_RESULT_OK,
					r->client_last_index[c]);
		}
	}
}

static void apply_committed(struct raft *r)
{
	char name[40];

	while (r->applied < r->commit) {
		const struct raft_entry *e = raft_log_get(&r->log, (uint16_t)(r->applied + 1));

		if (!e) {
			break;
		}
		r->applied++;
		netstate_apply(&r->state, e->event);
		dbg(r, "commit %u %s%s", r->applied, raft_ev_name(e->event, name, sizeof(name)),
		    (e->flags & RAFT_ENTRY_REPLAYED) ? " (replayed)" : "");
		if (r->io.apply) {
			r->io.apply(r->io.ctx, r->applied, e->term, e->event,
				    (e->flags & RAFT_ENTRY_REPLAYED) != 0);
		}
	}
	if (r->role == RAFT_LEADER) {
		answer_committed_clients(r);
	}
}

/* ---------------- role changes ---------------- */

static void become_follower(struct raft *r, uint8_t term, unsigned leader, int64_t now)
{
	bool changed = term != r->term || r->role != RAFT_FOLLOWER || leader != r->leader;

	if (term_newer(term, r->term)) {
		r->term = term;
		r->voted_for = 0;
	}
	r->role = RAFT_FOLLOWER;
	r->votes = 0;
	set_leader(r, leader);
	reset_election_timer(r, now);
	if (changed) {
		dbg(r, "follower term %u leader %s", r->term, leader ? raft_node_name(leader) : "none");
		notify_role(r);
	}
}

static void become_leader(struct raft *r, int64_t now)
{
	r->role = RAFT_LEADER;
	set_leader(r, r->self);
	r->votes = 0;
	for (unsigned f = 1; f <= RAFT_N; f++) {
		r->next_index[f] = (uint16_t)(r->log.last + 1);
		r->match_index[f] = 0;
		r->last_ack_ms[f] = now;
		r->client_seen[f] = false;
	}
	r->next_ref_ms = now;
	r->next_empty_ms = now;
	r->commit_sent = r->commit;
	r->append_sent_index = 0;
	r->append_sent_ms = now - RAFT_APPEND_RETRY_MS;
	r->ref_time_valid = false;   /* no REF of ours has been sent yet */
	dbg(r, "leader term %u (last %u commit %u)", r->term, r->log.last, r->commit);
	notify_role(r);
}

static void send_req_vote(struct raft *r, uint8_t term, bool pre)
{
	uint8_t d[RAFTP_DLC];
	struct raftp_req_vote m = {
		.term = term, .last_index = r->log.last,
		.last_term = raft_log_last_term(&r->log), .pre = pre,
	};

	raftp_encode_req_vote(d, 0, &m);
	send(r, RAFTP_REQ_VOTE, d);
}

static void start_prevote(struct raft *r, int64_t now)
{
	r->role = RAFT_PRECANDIDATE;
	set_leader(r, 0);
	r->votes = (uint8_t)(1u << r->self);
	reset_election_timer(r, now);
	dbg(r, "pre-vote for term %u", term_next(r->term));
	notify_role(r);
	send_req_vote(r, term_next(r->term), true);
}

static void start_election(struct raft *r, int64_t now)
{
	r->term = term_next(r->term);
	r->voted_for = (uint8_t)r->self;
	r->role = RAFT_CANDIDATE;
	set_leader(r, 0);
	r->votes = (uint8_t)(1u << r->self);
	r->elections++;
	reset_election_timer(r, now);
	dbg(r, "election term %u", r->term);
	notify_role(r);
	send_req_vote(r, r->term, false);
	if (popcount(r->votes) >= RAFT_MAJORITY) {
		become_leader(r, now);   /* only for a one-node cluster */
	}
}

/* ---------------- leader ---------------- */

static void leader_advance_commit(struct raft *r)
{
	for (uint16_t n = r->log.last; n > r->commit; n--) {
		uint8_t t;

		if (!raft_log_term_at(&r->log, n, &t) || term_newer(r->term, t)) {
			break;   /* terms never decrease along the log */
		}
		unsigned count = 1;

		for (unsigned f = 1; f <= RAFT_N; f++) {
			if (f != r->self && r->match_index[f] >= n) {
				count++;
			}
		}
		if (count >= RAFT_MAJORITY) {
			r->commit = n;
			apply_committed(r);
			break;
		}
	}
}

static void send_ref(struct raft *r)
{
	uint8_t d[RAFTP_DLC];
	struct raftp_ref m = {
		.term = r->term,
		.cycle = r->cycle++,
		.prev_send_us = r->ref_prev_send_us,
		.flags = (uint8_t)((r->ref_time_valid ? RAFTP_REF_TIME_VALID : 0) |
				   (r->state.cockpit_ok ? 0 : RAFTP_REF_FAILURE)),
	};

	raftp_encode_ref(d, 0, &m);
	send(r, RAFTP_REF, d);
}

static void send_append(struct raft *r, uint16_t index, bool empty)
{
	uint8_t d[RAFTP_DLC];
	struct raftp_append m = { .term = r->term, .index = index,
				  .commit_delta = raftp_commit_delta(index, r->commit) };

	if (empty) {
		raft_log_term_at(&r->log, index, &m.entry_term);
	} else {
		const struct raft_entry *e = raft_log_get(&r->log, index);

		if (!e) {
			return;
		}
		m.entry_term = e->term;
		m.event = e->event;
		raft_log_term_at(&r->log, (uint16_t)(index - 1), &m.prev_term);
	}
	raftp_encode_append(d, 0, &m);
	send(r, RAFTP_APPEND, d);
}

static void send_snapshot(struct raft *r)
{
	uint8_t d[RAFTP_DLC];
	struct raftp_snapshot m = {
		.term = r->term, .index = r->log.snap_index, .index_term = r->log.snap_term,
		.state = netstate_pack(&r->log.snap_state),
	};

	raftp_encode_snapshot(d, 0, &m);
	send(r, RAFTP_SNAPSHOT, d);
	dbg(r, "snapshot %u to followers", m.index);
}

/* Send an entry a follower still needs: active followers first, taking
 * turns, so one follower that is far behind (or stuck) cannot starve the
 * others. A follower that stopped acknowledging is only probed every
 * PROBE_MS. */
static void leader_replicate(struct raft *r, int64_t now)
{
	unsigned pick = 0;
	bool pick_active = false;

	for (unsigned k = 1; k <= RAFT_N; k++) {
		/* Start after the follower served last */
		unsigned f = (r->replicate_last + k - 1) % RAFT_N + 1;

		if (f == r->self || r->next_index[f] > r->log.last) {
			continue;
		}
		bool active = now - r->last_ack_ms[f] <= RAFT_QUORUM_MS;

		if (!pick || (active && !pick_active)) {
			pick = f;
			pick_active = active;
		}
	}
	if (!pick || now - r->append_sent_ms < RAFT_APPEND_GAP_MS) {
		return;
	}
	uint16_t idx = r->next_index[pick];
	int64_t wait = pick_active ? RAFT_APPEND_RETRY_MS : PROBE_MS;

	if (idx == r->append_sent_index && now - r->append_sent_ms < wait) {
		return;   /* sent, waiting for the acknowledgement */
	}
	if (idx <= r->log.snap_index) {
		send_snapshot(r);
	} else {
		send_append(r, idx, false);
	}
	r->append_sent_index = idx;
	r->append_sent_ms = now;
	r->replicate_last = pick;
}

static void leader_tick(struct raft *r, int64_t now)
{
	/* Check-quorum: step down (same term) if a majority stopped answering */
	unsigned active = 1;

	for (unsigned f = 1; f <= RAFT_N; f++) {
		if (f != r->self && now - r->last_ack_ms[f] <= RAFT_QUORUM_MS) {
			active++;
		}
	}
	if (active < RAFT_MAJORITY) {
		dbg(r, "lost quorum, stepping down");
		become_follower(r, r->term, 0, now);
		return;
	}

	if (now >= r->next_ref_ms) {
		send_ref(r);
		r->next_ref_ms += RAFT_HEARTBEAT_MS;
		if (r->next_ref_ms <= now) {
			r->next_ref_ms = now + RAFT_HEARTBEAT_MS;
		}
	}
	leader_replicate(r, now);
	if (now >= r->next_empty_ms || r->commit != r->commit_sent) {
		send_append(r, r->log.last, true);
		r->commit_sent = r->commit;
		r->next_empty_ms = now + RAFT_EMPTY_APPEND_MS;
	}
}

/* A client report on the leader (from EVENT, or the leader's own client).
 * Returns the result; *index gets the new entry's index if appended. */
static uint8_t leader_take_report(struct raft *r, uint8_t event, uint16_t *index)
{
	struct netstate proj;
	char name[40];

	projected_state(r, &proj);
	if (!netstate_would_change(&proj, event)) {
		return RAFTP_RESULT_NO_CHANGE;
	}
	if (!append_entry(r, r->term, event, 0)) {
		return RAFTP_RESULT_NO_LEADER;   /* log full: the client retries */
	}
	*index = r->log.last;
	dbg(r, "append %u %s", r->log.last, raft_ev_name(event, name, sizeof(name)));
	return RAFTP_RESULT_OK;
}

/* ---------------- frame handlers ---------------- */

/* Common check for REF, APPEND and SNAPSHOT. Returns true if the sender is
 * the current leader and the message should be processed. */
static bool accept_leader_msg(struct raft *r, unsigned sender, uint8_t term, int64_t now)
{
	if (term_newer(r->term, term)) {
		send_ack(r, false, r->log.last);   /* tells a stale leader our term */
		return false;
	}
	if (r->role == RAFT_LEADER && term == r->term) {
		return false;   /* cannot happen: one leader per term */
	}
	if (term_newer(term, r->term) || r->role != RAFT_FOLLOWER || r->leader != sender) {
		become_follower(r, term, sender, now);
	}
	r->last_heard_ms = now;
	r->timer_armed = true;
	r->boot_listen_until = 0;
	reset_election_timer(r, now);
	return true;
}

static void follower_commit(struct raft *r, const struct raftp_append *a, uint16_t verified)
{
	uint16_t commit;

	if (!raftp_commit_index(a, &commit)) {
		return;
	}
	if (commit > verified) {
		commit = verified;
	}
	if (commit > r->commit) {
		r->commit = commit;
		apply_committed(r);
	}
}

static void on_append(struct raft *r, unsigned sender, const struct raftp_append *a, int64_t now)
{
	uint8_t t;
	uint16_t commit = 0;
	char name[40];

	if (!accept_leader_msg(r, sender, a->term, now)) {
		return;
	}
	if (a->event == RAFT_EV_NONE) {
		if (raft_log_term_at(&r->log, a->index, &t) && t == a->entry_term) {
			send_ack(r, true, a->index);
			follower_commit(r, a, a->index);
		} else {
			send_ack(r, false, r->log.last);
		}
		return;
	}
	if (a->index <= r->log.snap_index) {
		send_ack(r, true, a->index);   /* already folded into our snapshot */
		return;
	}
	uint16_t prev = (uint16_t)(a->index - 1);

	if (!raft_log_term_at(&r->log, prev, &t)) {
		send_ack(r, false, r->log.last);   /* missing entries before this one */
		return;
	}
	if (t != a->prev_term) {
		/* Our entry at prev conflicts with the leader's: drop it (never a
		 * committed one) and let the leader back off. */
		if (prev > r->commit) {
			raft_log_truncate(&r->log, prev);
		}
		send_ack(r, false, (uint16_t)(prev - 1));
		return;
	}
	if (raft_log_term_at(&r->log, a->index, &t)) {
		if (t == a->entry_term) {
			send_ack(r, true, a->index);   /* duplicate */
			follower_commit(r, a, a->index);
			return;
		}
		if (a->index <= r->commit) {
			send_ack(r, false, r->log.last);   /* cannot happen in correct Raft */
			return;
		}
		raft_log_truncate(&r->log, a->index);
	}
	bool replayed = raftp_commit_index(a, &commit) && commit >= a->index;

	if (!append_entry(r, a->entry_term, a->event, replayed ? RAFT_ENTRY_REPLAYED : 0)) {
		send_ack(r, false, r->log.last);
		return;
	}
	dbg(r, "append %u %s%s", a->index, raft_ev_name(a->event, name, sizeof(name)),
	    replayed ? " (replayed)" : "");
	send_ack(r, true, a->index);
	follower_commit(r, a, a->index);
}

static void on_snapshot(struct raft *r, unsigned sender, const struct raftp_snapshot *s, int64_t now)
{
	if (!accept_leader_msg(r, sender, s->term, now)) {
		return;
	}
	if (s->index > r->commit) {
		struct netstate ns;
		char line[96];

		netstate_unpack(s->state, &ns);
		raft_log_install(&r->log, s->index, s->index_term, &ns);
		r->commit = r->applied = s->index;
		r->state = ns;
		dbg(r, "installed snapshot %u: %s", s->index, netstate_format(&ns, line, sizeof(line)));
		if (r->io.snapshot) {
			r->io.snapshot(r->io.ctx, s->index, &ns);
		}
	}
	send_ack(r, true, s->index);
}

static void on_append_ack(struct raft *r, unsigned f, const struct raftp_append_ack *a, int64_t now)
{
	if (term_newer(a->term, r->term)) {
		become_follower(r, a->term, 0, now);
		return;
	}
	if (r->role != RAFT_LEADER || a->term != r->term) {
		return;
	}
	r->last_ack_ms[f] = now;
	if (a->success) {
		uint16_t m = a->match_index > r->log.last ? r->log.last : a->match_index;

		if (m > r->match_index[f]) {
			r->match_index[f] = m;
		}
		r->next_index[f] = (uint16_t)(r->match_index[f] + 1);
		leader_advance_commit(r);
	} else {
		/* A follower whose log is now shorter than what it had matched lost
		 * its log: it rebooted (the logs are in RAM). Standard Raft never
		 * lowers match_index, which left a rebooted Pi stuck forever
		 * (seen on hardware). Start over from what it has now. */
		if (a->match_index < r->match_index[f]) {
			r->match_index[f] = a->match_index;
		}
		uint16_t n = r->next_index[f] > 1 ? (uint16_t)(r->next_index[f] - 1) : 1;
		uint16_t hint = (uint16_t)(a->match_index + 1);

		if (hint < n) {
			n = hint;
		}
		if (n <= r->match_index[f]) {
			n = (uint16_t)(r->match_index[f] + 1);
		}
		r->next_index[f] = n < 1 ? 1 : n;
	}
}

static void on_req_vote(struct raft *r, unsigned cand, const struct raftp_req_vote *m, int64_t now)
{
	uint8_t d[RAFTP_DLC];
	struct raftp_vote v = { .candidate = (uint8_t)cand, .pre = m->pre };

	if (m->pre) {
		/* Grant only if we have not heard a live leader recently */
		bool leader_alive = r->role == RAFT_LEADER ||
				    (r->leader != 0 && r->timer_armed &&
				     now - r->last_heard_ms < RAFT_ELECTION_BASE_MS);

		v.granted = !leader_alive && term_newer(m->term, r->term) &&
			    up_to_date(r, m->last_term, m->last_index);
		v.term = v.granted ? m->term : r->term;
	} else {
		if (term_newer(m->term, r->term)) {
			become_follower(r, m->term, 0, now);
		}
		v.granted = m->term == r->term &&
			    (r->voted_for == 0 || r->voted_for == cand) &&
			    up_to_date(r, m->last_term, m->last_index);
		if (v.granted) {
			r->voted_for = (uint8_t)cand;
			reset_election_timer(r, now);
		}
		v.term = r->term;
	}
	dbg(r, "%svote for %s term %u: %s", m->pre ? "pre-" : "", raft_node_name(cand), m->term,
	    v.granted ? "granted" : "denied");
	raftp_encode_vote(d, 0, &v);
	send(r, RAFTP_VOTE, d);
}

static void on_vote(struct raft *r, unsigned voter, const struct raftp_vote *v, int64_t now)
{
	if (v->candidate != r->self) {
		return;
	}
	if (v->pre) {
		if (r->role != RAFT_PRECANDIDATE) {
			return;
		}
		if (v->granted && v->term == term_next(r->term)) {
			r->votes |= (uint8_t)(1u << voter);
			if (popcount(r->votes) >= RAFT_MAJORITY) {
				start_election(r, now);
			}
		} else if (!v->granted && term_newer(v->term, r->term)) {
			become_follower(r, v->term, 0, now);
		}
		return;
	}
	if (term_newer(v->term, r->term)) {
		become_follower(r, v->term, 0, now);
		return;
	}
	if (r->role != RAFT_CANDIDATE || v->term != r->term || !v->granted) {
		return;
	}
	r->votes |= (uint8_t)(1u << voter);
	if (popcount(r->votes) >= RAFT_MAJORITY) {
		become_leader(r, now);
	}
}

static void on_event(struct raft *r, unsigned client, const struct raftp_event *e)
{
	uint16_t index = 0;

	if (e->leader != r->self) {
		return;   /* addressed to another node */
	}
	if (r->role != RAFT_LEADER) {
		send_event_resp(r, client, e->client_seq,
				r->leader ? RAFTP_RESULT_REDIRECT : RAFTP_RESULT_NO_LEADER, 0);
		return;
	}
	if (r->client_seen[client] && r->client_last_seq[client] == e->client_seq) {
		/* A resend: answer again, or keep waiting for the commit */
		if (r->client_last_result[client] != RESULT_PENDING) {
			send_event_resp(r, client, e->client_seq, r->client_last_result[client],
					r->client_last_index[client]);
		}
		return;
	}
	uint8_t result = leader_take_report(r, e->event, &index);

	if (result == RAFTP_RESULT_NO_LEADER) {
		return;   /* log full: no answer, the client retries */
	}
	r->client_seen[client] = true;
	r->client_last_seq[client] = e->client_seq;
	r->client_last_index[client] = index;
	if (result == RAFTP_RESULT_OK) {
		r->client_last_result[client] = RESULT_PENDING;   /* answered on commit */
	} else {
		r->client_last_result[client] = result;
		send_event_resp(r, client, e->client_seq, result, 0);
	}
}

static void client_pop(struct raft *r)
{
	if (r->client_count == 0) {
		return;
	}
	memmove(&r->client_queue[0], &r->client_queue[1], r->client_count - 1);
	r->client_count--;
	r->client_seq++;
	r->client_retry_ms = 0;
}

static void on_event_resp(struct raft *r, const struct raftp_event_resp *m, int64_t now)
{
	if (m->client != r->self || r->client_count == 0 || m->client_seq != r->client_seq) {
		return;
	}
	switch (m->result) {
	case RAFTP_RESULT_OK:
	case RAFTP_RESULT_NO_CHANGE:
		client_pop(r);
		break;
	case RAFTP_RESULT_REDIRECT:
		if (m->leader && m->leader != r->self) {
			dbg(r, "client: redirected to %s", raft_node_name(m->leader));
			r->client_target = m->leader;
		}
		r->client_retry_ms = 0;   /* resend now */
		break;
	default:   /* NO_LEADER: wait for one */
		r->client_target = 0;
		r->client_retry_ms = now + RAFT_CLIENT_RETRY_MS;
		break;
	}
}

/* ---------------- client ---------------- */

void raft_client_report(struct raft *r, uint8_t event)
{
	for (unsigned i = 0; i < r->client_count; i++) {
		if (r->client_queue[i] == event) {
			return;
		}
	}
	if (r->client_count < RAFT_CLIENT_QUEUE) {
		r->client_queue[r->client_count++] = event;
	}
}

void raft_client_retain(struct raft *r, const uint8_t *events, unsigned n)
{
	unsigned i = 0;

	while (i < r->client_count) {
		uint8_t q = r->client_queue[i];
		bool keep = false;

		for (unsigned k = 0; k < n && !keep; k++) {
			keep = events[k] == q;
		}
		if (keep) {
			i++;
		} else {
			raft_client_withdraw(r, q);   /* shifts the queue down */
		}
	}
}

bool raft_client_pending(const struct raft *r, uint8_t event)
{
	for (unsigned i = 0; i < r->client_count; i++) {
		if (r->client_queue[i] == event) {
			return true;
		}
	}
	return false;
}

bool raft_client_withdraw(struct raft *r, uint8_t event)
{
	for (unsigned i = 0; i < r->client_count; i++) {
		if (r->client_queue[i] != event) {
			continue;
		}
		memmove(&r->client_queue[i], &r->client_queue[i + 1], r->client_count - i - 1);
		r->client_count--;
		if (i == 0) {
			/* The head was in flight: a late answer to it must not pop
			 * the next report */
			r->client_seq++;
			r->client_retry_ms = 0;
		}
		return true;
	}
	return false;
}

static void client_tick(struct raft *r, int64_t now)
{
	if (r->client_count == 0) {
		return;
	}
	if (r->role == RAFT_LEADER) {
		uint16_t index;

		if (leader_take_report(r, r->client_queue[0], &index) != RAFTP_RESULT_NO_LEADER) {
			client_pop(r);
		}
		return;
	}
	unsigned target = r->client_target ? r->client_target : r->leader;

	if (target == 0 || target == r->self || now < r->client_retry_ms) {
		return;
	}
	uint8_t d[RAFTP_DLC];
	struct raftp_event m = { .leader = (uint8_t)target, .event = r->client_queue[0],
				 .client_seq = r->client_seq };

	raftp_encode_event(d, 0, &m);
	send(r, RAFTP_EVENT, d);
	r->client_retry_ms = now + RAFT_CLIENT_RETRY_MS;
}

/* ---------------- public API ---------------- */

void raft_init_with_leader(struct raft *r, unsigned self, const struct raft_io *io,
			   int64_t now_ms, unsigned default_leader, uint32_t seed)
{
	memset(r, 0, sizeof(*r));
	r->self = self;
	r->io = *io;
	raft_log_init(&r->log);
	netstate_init(&r->state);
	r->rng = (seed * 2654435761u) ^ (self * 0x9E3779B9u);
	if (r->rng == 0) {
		r->rng = 1;
	}
	r->role = RAFT_FOLLOWER;
	r->last_heard_ms = now_ms;
	if (default_leader != RAFT_LEADER_NONE) {
		r->term = 1;
		r->timer_armed = false;
		if (self == default_leader) {
			/* Listen first: lead in term 1 only if nobody else is leading */
			r->boot_listen_until = now_ms + RAFT_ELECTION_BASE_MS + RAFT_ELECTION_JITTER_MS;
			set_leader(r, 0);
		} else {
			set_leader(r, default_leader);
		}
	} else {
		r->term = 0;
		r->timer_armed = true;
		set_leader(r, 0);
	}
	reset_election_timer(r, now_ms);
	dbg(r, "boot term %u leader %s", r->term, r->leader ? raft_node_name(r->leader) : "none");
	notify_role(r);
}

void raft_init(struct raft *r, unsigned self, const struct raft_io *io, int64_t now_ms)
{
	raft_init_with_leader(r, self, io, now_ms, RAFT_DEFAULT_LEADER, RAFT_SEED);
}

void raft_tick(struct raft *r, int64_t now)
{
	if (r->boot_listen_until && now >= r->boot_listen_until) {
		r->boot_listen_until = 0;
		if (r->role == RAFT_FOLLOWER && r->leader == 0) {
			become_leader(r, now);   /* predefined leader, term 1, no election */
		}
	}
	if (r->role == RAFT_LEADER) {
		leader_tick(r, now);
	} else if (r->timer_armed && r->boot_listen_until == 0 && now >= r->election_deadline) {
		start_prevote(r, now);
	}
	client_tick(r, now);
}

void raft_ref_sent(struct raft *r, uint32_t send_us)
{
	r->ref_prev_send_us = send_us;
	r->ref_time_valid = true;
}

void raft_on_frame(struct raft *r, uint32_t id, const uint8_t d[RAFTP_DLC], int64_t now)
{
	unsigned sender = 0;
	enum raftp_kind kind = raftp_classify(id, &sender);

	if (kind == RAFTP_NONE || sender == r->self) {
		return;
	}
	/* A term far ahead of ours: seen on hardware while a bus was being
	 * disturbed, a vote request with term 0 and then terms 248 and 252 when
	 * the cluster was at 2 (frames with a Raft ID and another frame's data,
	 * suspected in the Pi's driver). Adopting such a term moves the whole
	 * cluster there. Elections advance the term one at a time, so votes and
	 * acknowledgements far ahead are dropped. Leader messages are kept: a
	 * rebooted node learns the current term from them. The raw frame is
	 * logged to find the source. */
	if ((d[0] >> 4) != raftp_crc4(d)) {
		dbg(r, "dropped, bad CRC in id 0x%03x: %02x %02x %02x %02x %02x %02x %02x %02x",
		    (unsigned)id, d[0], d[1], d[2], d[3], d[4], d[5], d[6], d[7]);
		return;
	}
	if (kind == RAFTP_REQ_VOTE || kind == RAFTP_VOTE || kind == RAFTP_APPEND_ACK ||
	    kind == RAFTP_REF || kind == RAFTP_APPEND || kind == RAFTP_SNAPSHOT) {
		bool leader_msg = kind == RAFTP_REF || kind == RAFTP_APPEND ||
				  kind == RAFTP_SNAPSHOT;

		if (d[1] > r->term + RAFT_TERM_JUMP_MAX || d[1] == 0) {
			dbg(r, "%s term %u (ours %u) in id 0x%03x from %s: "
			    "%02x %02x %02x %02x %02x %02x %02x %02x",
			    leader_msg && d[1] != 0 ? "far" : "dropped, bad", d[1], r->term,
			    (unsigned)id, raft_node_name(sender), d[0], d[1], d[2], d[3], d[4],
			    d[5], d[6], d[7]);
			if (!leader_msg || d[1] == 0) {
				return;
			}
		}
	}
	switch (kind) {
	case RAFTP_REF: {
		struct raftp_ref m;

		if (raftp_decode_ref(d, &m)) {
			accept_leader_msg(r, sender, m.term, now);
		}
		break;
	}
	case RAFTP_REQ_VOTE: {
		struct raftp_req_vote m;

		if (raftp_decode_req_vote(d, &m)) {
			on_req_vote(r, sender, &m, now);
		}
		break;
	}
	case RAFTP_VOTE: {
		struct raftp_vote m;

		if (raftp_decode_vote(d, &m)) {
			on_vote(r, sender, &m, now);
		}
		break;
	}
	case RAFTP_APPEND: {
		struct raftp_append m;

		if (raftp_decode_append(d, &m)) {
			on_append(r, sender, &m, now);
		}
		break;
	}
	case RAFTP_APPEND_ACK: {
		struct raftp_append_ack m;

		if (raftp_decode_append_ack(d, &m)) {
			on_append_ack(r, sender, &m, now);
		}
		break;
	}
	case RAFTP_SNAPSHOT: {
		struct raftp_snapshot m;

		if (raftp_decode_snapshot(d, &m)) {
			on_snapshot(r, sender, &m, now);
		}
		break;
	}
	case RAFTP_EVENT: {
		struct raftp_event m;

		if (raftp_decode_event(d, &m)) {
			on_event(r, sender, &m);
		}
		break;
	}
	case RAFTP_EVENT_RESP: {
		struct raftp_event_resp m;

		if (raftp_decode_event_resp(d, &m)) {
			on_event_resp(r, &m, now);
		}
		break;
	}
	default:
		break;
	}
}
