/*
 * Host tests: the Raft agent on three simulated nodes.
 *
 * The simulated network delivers each frame 1 ms after it is sent to every
 * live node that can hear the sender. reach[x][z] says whether node z hears
 * node x at all, directly or through a relay; the per-bus details are the
 * routing layer's job (test_netstate.c). Every test also checks Raft safety
 * on every commit: no two nodes commit different entries at one index, and
 * no term has two leaders.
 */
#include <string.h>

#include "check.h"
#include "raft.h"

enum { C = RAFT_NODE_COCKPIT, S = RAFT_NODE_STEERING, D = RAFT_NODE_DRIVETRAIN };

#define QUEUE_MAX 1024
#define MAX_INDEX 1024

struct frame {
	int64_t at;
	unsigned from;
	uint32_t id;
	uint8_t d[RAFTP_DLC];
};

struct node_ctx {
	struct sim *sim;
	unsigned n;
};

struct sim {
	struct raft node[RAFT_N + 1];
	struct node_ctx ctx[RAFT_N + 1];
	bool alive[RAFT_N + 1];
	bool reach[RAFT_N + 1][RAFT_N + 1];
	struct frame q[QUEUE_MAX];
	unsigned q_head, q_len;
	int64_t now;
	unsigned drop_pct;
	uint32_t rng;
	unsigned default_leader;
	uint32_t seed;

	/* Observations */
	unsigned sent[RAFTP_KIND_COUNT];
	uint8_t committed[MAX_INDEX + 1];          /* first committed event per index */
	uint8_t committed_term[MAX_INDEX + 1];
	bool committed_set[MAX_INDEX + 1];
	unsigned applies[RAFT_N + 1];
	unsigned replayed[RAFT_N + 1];
	uint8_t leader_of_term[256];
	int64_t became_leader_ms[RAFT_N + 1];
	unsigned snapshots[RAFT_N + 1];
	bool safety_ok;
};

static struct sim sim;   /* large: keep it off the stack */

static uint32_t sim_rand(struct sim *s)
{
	s->rng ^= s->rng << 13;
	s->rng ^= s->rng >> 17;
	s->rng ^= s->rng << 5;
	return s->rng;
}

static void io_send(void *vctx, uint32_t id, const uint8_t d[RAFTP_DLC])
{
	struct node_ctx *c = vctx;
	struct sim *s = c->sim;
	unsigned who = 0;
	enum raftp_kind kind = raftp_classify(id, &who);

	s->sent[kind]++;
	if (kind == RAFTP_REF) {
		/* Handed to the controller now; the next REF carries this time */
		raft_ref_sent(&s->node[c->n], (uint32_t)(s->now * 1000));
	}
	if (s->q_len == QUEUE_MAX) {
		return;
	}
	struct frame *f = &s->q[(s->q_head + s->q_len++) % QUEUE_MAX];

	f->at = s->now + 1;
	f->from = c->n;
	f->id = id;
	memcpy(f->d, d, RAFTP_DLC);
}

static void io_apply(void *vctx, uint16_t index, uint8_t term, uint8_t event, bool replayed)
{
	struct node_ctx *c = vctx;
	struct sim *s = c->sim;

	s->applies[c->n]++;
	if (replayed) {
		s->replayed[c->n]++;
	}
	if (index > MAX_INDEX) {
		return;
	}
	if (s->committed_set[index]) {
		if (s->committed[index] != event || s->committed_term[index] != term) {
			s->safety_ok = false;
			printf("  SAFETY: node %u committed 0x%02X/t%u at %u, others 0x%02X/t%u\n",
			       c->n, event, term, index, s->committed[index], s->committed_term[index]);
		}
	} else {
		s->committed_set[index] = true;
		s->committed[index] = event;
		s->committed_term[index] = term;
	}
}

static void io_snapshot(void *vctx, uint16_t index, const struct netstate *ns)
{
	struct node_ctx *c = vctx;

	(void)index;
	(void)ns;
	c->sim->snapshots[c->n]++;
}

static void io_role(void *vctx, enum raft_role role, unsigned leader, uint8_t term)
{
	struct node_ctx *c = vctx;
	struct sim *s = c->sim;

	(void)leader;
	if (role != RAFT_LEADER) {
		return;
	}
	s->became_leader_ms[c->n] = s->now;
	if (s->leader_of_term[term] && s->leader_of_term[term] != c->n) {
		s->safety_ok = false;
		printf("  SAFETY: two leaders in term %u (%u, %u)\n", term,
		       s->leader_of_term[term], c->n);
	}
	s->leader_of_term[term] = (uint8_t)c->n;
}

static void sim_boot(struct sim *s, unsigned n)
{
	struct raft_io io = {
		.ctx = &s->ctx[n], .send = io_send, .apply = io_apply,
		.snapshot = io_snapshot, .role = io_role,
	};

	s->ctx[n] = (struct node_ctx){ .sim = s, .n = n };
	s->alive[n] = true;
	raft_init_with_leader(&s->node[n], n, &io, s->now, s->default_leader, s->seed);
}

/* Fresh simulation; boots the nodes in `boot_mask` (bit n = node n). */
static void sim_start(struct sim *s, unsigned default_leader, uint32_t seed, unsigned boot_mask)
{
	memset(s, 0, sizeof(*s));
	s->rng = 0x12345678u;
	s->default_leader = default_leader;
	s->seed = seed;
	s->safety_ok = true;
	for (unsigned x = 1; x <= RAFT_N; x++) {
		for (unsigned z = 1; z <= RAFT_N; z++) {
			s->reach[x][z] = true;
		}
	}
	for (unsigned n = 1; n <= RAFT_N; n++) {
		if (boot_mask & (1u << n)) {
			sim_boot(s, n);
		}
	}
}

static void sim_run(struct sim *s, int64_t ms)
{
	int64_t end = s->now + ms;

	while (s->now < end) {
		/* Deliver frames due now (frames sent during delivery wait a tick) */
		unsigned due = s->q_len;

		while (due-- && s->q_len && s->q[s->q_head].at <= s->now) {
			struct frame f = s->q[s->q_head];

			s->q_head = (s->q_head + 1) % QUEUE_MAX;
			s->q_len--;
			for (unsigned z = 1; z <= RAFT_N; z++) {
				if (z == f.from || !s->alive[z] || !s->alive[f.from] ||
				    !s->reach[f.from][z]) {
					continue;
				}
				if (s->drop_pct && sim_rand(s) % 100 < s->drop_pct) {
					continue;
				}
				raft_on_frame(&s->node[z], f.id, f.d, s->now);
			}
		}
		for (unsigned n = 1; n <= RAFT_N; n++) {
			if (s->alive[n]) {
				raft_tick(&s->node[n], s->now);
			}
		}
		s->now++;
	}
}

/* Cut node n off from both others (both directions), or reconnect it. */
static void sim_isolate(struct sim *s, unsigned n, bool cut)
{
	for (unsigned m = 1; m <= RAFT_N; m++) {
		if (m != n) {
			s->reach[n][m] = !cut;
			s->reach[m][n] = !cut;
		}
	}
}

static unsigned leaders(const struct sim *s, unsigned *who)
{
	unsigned count = 0;

	for (unsigned n = 1; n <= RAFT_N; n++) {
		if (s->alive[n] && raft_is_leader(&s->node[n])) {
			count++;
			if (who) {
				*who = n;
			}
		}
	}
	return count;
}

static bool states_agree(const struct sim *s, unsigned mask)
{
	unsigned first = 0;

	for (unsigned n = 1; n <= RAFT_N; n++) {
		if (!(mask & (1u << n))) {
			continue;
		}
		if (!first) {
			first = n;
		} else if (!netstate_equal(&s->node[n].state, &s->node[first].state) ||
			   s->node[n].commit != s->node[first].commit) {
			return false;
		}
	}
	return true;
}

#define ALL ((1u << C) | (1u << S) | (1u << D))

/* Checkoff steps 1-2: predefined leader, no election, REF and empty APPEND
 * at their rates. */
static void test_default_leader_boot(void)
{
	sim_start(&sim, C, RAFT_SEED, ALL);
	sim_run(&sim, 2000);

	unsigned who = 0;

	CHECK_EQ(leaders(&sim, &who), 1);
	CHECK_EQ(who, C);
	for (unsigned n = 1; n <= RAFT_N; n++) {
		CHECK_EQ(sim.node[n].term, 1);
		CHECK_EQ(sim.node[n].leader, C);
	}
	CHECK_EQ(sim.sent[RAFTP_REQ_VOTE], 0);
	/* The Pi listens for one election timeout first (~99 ms), then sends
	 * REF every 20 ms and an empty APPEND every 100 ms */
	CHECK(sim.sent[RAFTP_REF] >= 92 && sim.sent[RAFTP_REF] <= 96);
	CHECK(sim.sent[RAFTP_APPEND] >= 18 && sim.sent[RAFTP_APPEND] <= 20);
	CHECK(sim.safety_ok);
}

/* The Pi boots long after the Nucleos: they wait for it instead of electing. */
static void test_late_cockpit(void)
{
	sim_start(&sim, C, RAFT_SEED, (1u << S) | (1u << D));
	sim_run(&sim, 5000);
	CHECK_EQ(sim.sent[RAFTP_REQ_VOTE], 0);
	CHECK_EQ(leaders(&sim, NULL), 0);

	sim_boot(&sim, C);
	sim_run(&sim, 1000);
	unsigned who = 0;

	CHECK_EQ(leaders(&sim, &who), 1);
	CHECK_EQ(who, C);
	CHECK_EQ(sim.node[S].term, 1);
	CHECK_EQ(sim.sent[RAFTP_REQ_VOTE], 0);
	CHECK(sim.safety_ok);
}

/* A link report from every node produces exactly one entry, committed at the
 * same index everywhere (checkoff step 4). */
static void test_client_reports(void)
{
	uint8_t ev = raft_ev_link(S, 0, false);

	sim_start(&sim, C, RAFT_SEED, ALL);
	sim_run(&sim, 500);
	for (unsigned n = 1; n <= RAFT_N; n++) {
		raft_client_report(&sim.node[n], ev);
	}
	sim_run(&sim, 500);
	for (unsigned n = 1; n <= RAFT_N; n++) {
		CHECK_EQ(sim.node[n].commit, 1);
		CHECK_EQ(sim.applies[n], 1);
		CHECK(!sim.node[n].state.link_up[S][0]);
		CHECK_EQ(sim.node[n].client_count, 0);   /* every client got its answer */
	}
	CHECK(sim.committed_set[1] && sim.committed[1] == ev);
	CHECK(states_agree(&sim, ALL));
	CHECK_EQ(sim.node[C].term, 1);
	CHECK(sim.safety_ok);
}

/* A withdrawn report never reaches the leader: an isolated node queues a
 * link-down report, its link comes back, and the report is dropped. */
static void test_client_withdraw(void)
{
	uint8_t down = raft_ev_link(D, 0, false), other = raft_ev_link(D, 1, false);

	sim_start(&sim, C, RAFT_SEED, ALL);
	sim_run(&sim, 500);
	sim_isolate(&sim, D, true);
	raft_client_report(&sim.node[D], down);
	raft_client_report(&sim.node[D], other);
	sim_run(&sim, 200);
	CHECK_EQ(sim.node[D].client_count, 2);   /* no leader reachable */
	CHECK(raft_client_withdraw(&sim.node[D], down));
	CHECK(raft_client_withdraw(&sim.node[D], other));
	CHECK(!raft_client_withdraw(&sim.node[D], other));
	CHECK_EQ(sim.node[D].client_count, 0);

	/* retain: keep only what is still observed */
	uint8_t still[] = { other };

	raft_client_report(&sim.node[D], down);
	raft_client_report(&sim.node[D], other);
	raft_client_retain(&sim.node[D], still, 1);
	CHECK_EQ(sim.node[D].client_count, 1);
	CHECK(raft_client_pending(&sim.node[D], other) && !raft_client_pending(&sim.node[D], down));
	raft_client_retain(&sim.node[D], still, 0);
	CHECK_EQ(sim.node[D].client_count, 0);
	sim_isolate(&sim, D, false);
	sim_run(&sim, 500);
	for (unsigned n = 1; n <= RAFT_N; n++) {
		CHECK_EQ(sim.node[n].commit, 0);
		CHECK(sim.node[n].state.link_up[D][0]);
	}
	CHECK(sim.safety_ok);
}

/* Seen on hardware: the Pi rejoined an STM32 leader, caught up, then
 * rebooted again (logs are in RAM). The leader still held the Pi's old
 * match index, so it never backed off far enough to catch the empty log up,
 * and because the stuck Pi was always served first, the other follower got
 * no new entries either: nothing committed any more. */
static void test_follower_reboot_after_catch_up(void)
{
	sim_start(&sim, C, RAFT_SEED, ALL);
	sim_run(&sim, 500);
	raft_client_report(&sim.node[C], RAFT_EV_COCKPIT_OK);
	sim_run(&sim, 500);
	sim.alive[C] = false;
	sim_run(&sim, 600);

	unsigned leader = 0;

	CHECK_EQ(leaders(&sim, &leader), 1);
	raft_client_report(&sim.node[leader], raft_ev_node(C, false));
	sim_run(&sim, 300);

	/* The Pi comes back and catches up */
	sim_boot(&sim, C);
	sim_run(&sim, 1000);
	CHECK_EQ(sim.node[C].commit, sim.node[leader].commit);
	raft_client_report(&sim.node[leader], raft_ev_node(C, true));
	sim_run(&sim, 300);
	CHECK_EQ(sim.node[C].commit, sim.node[leader].commit);

	/* ... and reboots again with an empty log */
	sim_boot(&sim, C);
	sim_run(&sim, 1000);
	CHECK_EQ(sim.node[C].commit, sim.node[leader].commit);

	/* New reports still commit everywhere */
	uint16_t before = sim.node[leader].commit;

	raft_client_report(&sim.node[D], raft_ev_link(D, 0, false));
	sim_run(&sim, 500);
	CHECK_EQ(sim.node[leader].commit, before + 1);
	CHECK(states_agree(&sim, ALL));
	CHECK(leaders(&sim, NULL) == 1 && raft_is_leader(&sim.node[leader]));
	CHECK(sim.safety_ok);
}

/* Seen on hardware: corrupted frames carried terms 0, 248 and 252 while the
 * cluster was at term 2; the nodes adopted them, and the term then hit 255.
 * Votes and acknowledgements far ahead are dropped; a leader's REF is still
 * followed (that is how a rebooted node learns the term). The term never
 * wraps to 0. */
static void test_term_guard(void)
{
	uint8_t d[RAFTP_DLC];

	sim_start(&sim, C, RAFT_SEED, ALL);
	sim_run(&sim, 500);
	CHECK_EQ(sim.node[S].term, 1);

	struct raftp_req_vote rv = { .term = 248, .last_index = 0, .last_term = 0 };

	raftp_encode_req_vote(d, 0, &rv);
	raft_on_frame(&sim.node[S], raftp_id(RAFTP_REQ_VOTE, D), d, sim.now);
	rv.term = 0;
	raftp_encode_req_vote(d, 1, &rv);
	raft_on_frame(&sim.node[S], raftp_id(RAFTP_REQ_VOTE, D), d, sim.now);

	struct raftp_vote v = { .term = 252, .candidate = S, .granted = false };

	raftp_encode_vote(d, 0, &v);
	raft_on_frame(&sim.node[S], raftp_id(RAFTP_VOTE, D), d, sim.now);

	struct raftp_append_ack a = { .term = 200, .success = false, .match_index = 0 };

	raftp_encode_append_ack(d, 0, &a);
	raft_on_frame(&sim.node[C], raftp_id(RAFTP_APPEND_ACK, D), d, sim.now);
	CHECK_EQ(sim.node[S].term, 1);
	CHECK_EQ(sim.node[C].term, 1);
	CHECK(raft_is_leader(&sim.node[C]));

	/* A small jump (a real election elsewhere) is still followed */
	rv.term = 3;
	rv.last_index = 99;
	rv.last_term = 3;
	raftp_encode_req_vote(d, 2, &rv);
	raft_on_frame(&sim.node[S], raftp_id(RAFTP_REQ_VOTE, D), d, sim.now);
	CHECK_EQ(sim.node[S].term, 3);

	/* At 255 the term stays: no wrap to 0 */
	sim_start(&sim, RAFT_LEADER_NONE, RAFT_SEED, ALL);
	sim_run(&sim, 1000);
	for (unsigned n = 1; n <= RAFT_N; n++) {
		sim.node[n].term = 255;
	}
	sim.q_len = 0;
	unsigned leader = 0;

	CHECK_EQ(leaders(&sim, &leader), 1);
	sim.alive[leader] = false;
	sim_run(&sim, 1000);
	for (unsigned n = 1; n <= RAFT_N; n++) {
		if (sim.alive[n]) {
			CHECK_EQ(sim.node[n].term, 255);
		}
	}
}

/* A client that addresses a follower is redirected to the leader. */
static void test_redirect(void)
{
	sim_start(&sim, C, RAFT_SEED, ALL);
	sim_run(&sim, 500);
	sim.node[D].client_target = S;   /* stale idea of the leader */
	raft_client_report(&sim.node[D], raft_ev_link(D, 1, false));
	sim_run(&sim, 300);
	CHECK(sim.sent[RAFTP_EVENT_RESP] >= 2);   /* the redirect and the OK */
	CHECK_EQ(sim.node[D].client_count, 0);
	CHECK_EQ(sim.node[D].commit, 1);
	CHECK(!sim.node[S].state.link_up[D][1]);
	CHECK(sim.safety_ok);
}

/* Checkoff step 6: the Pi powers off; one STM32 leads within 1 s, in term 2,
 * after exactly one election. */
static void test_leader_failure(void)
{
	sim_start(&sim, C, RAFT_SEED, ALL);
	sim_run(&sim, 1000);
	int64_t t_fail = sim.now;

	sim.alive[C] = false;
	sim_run(&sim, 1000);

	unsigned who = 0;

	CHECK_EQ(leaders(&sim, &who), 1);
	CHECK(who == S || who == D);
	CHECK(sim.became_leader_ms[who] - t_fail < 1000);
	CHECK_EQ(sim.node[S].term, 2);
	CHECK_EQ(sim.node[D].term, 2);
	CHECK_EQ(sim.node[S].elections + sim.node[D].elections, 1);
	CHECK_EQ(sim.node[S].leader, who);
	CHECK_EQ(sim.node[D].leader, who);
	CHECK(sim.safety_ok);
}

/* Checkoff step 7: entries committed while the Pi was down replay to it once,
 * in order, marked replayed; the STM32 stays leader. */
static void test_rejoin_replay(void)
{
	sim_start(&sim, C, RAFT_SEED, ALL);
	sim_run(&sim, 500);
	raft_client_report(&sim.node[C], RAFT_EV_COCKPIT_OK);   /* the proxy started */
	sim_run(&sim, 500);
	CHECK_EQ(sim.node[S].commit, 1);
	sim.alive[C] = false;
	sim_run(&sim, 500);

	unsigned leader = 0;

	CHECK_EQ(leaders(&sim, &leader), 1);
	/* The STM32s' clients report the cockpit loss */
	raft_client_report(&sim.node[S], raft_ev_node(C, false));
	raft_client_report(&sim.node[D], raft_ev_node(C, false));
	raft_client_report(&sim.node[S], RAFT_EV_COCKPIT_ERROR);
	sim_run(&sim, 300);
	CHECK_EQ(sim.node[S].commit, 3);   /* one NODE_DOWN despite two reports */
	CHECK_EQ(sim.node[D].commit, 3);

	/* The Pi powers back on with an empty log */
	sim_boot(&sim, C);
	sim_run(&sim, 1000);
	CHECK_EQ(leaders(&sim, NULL), 1);
	CHECK(raft_is_leader(&sim.node[leader]));    /* leadership stays */
	CHECK_EQ(sim.node[C].role, RAFT_FOLLOWER);
	CHECK_EQ(sim.node[C].leader, leader);
	CHECK_EQ(sim.node[C].term, 2);
	CHECK_EQ(sim.node[C].commit, 3);
	CHECK_EQ(sim.applies[C], 4);    /* 1 before the outage + 3 replayed */
	CHECK_EQ(sim.replayed[C], 3);   /* every entry of its new log, in order */
	CHECK(states_agree(&sim, ALL));

	/* Live entries after the replay are not marked replayed */
	raft_client_report(&sim.node[C], raft_ev_node(C, true));
	sim_run(&sim, 300);
	CHECK_EQ(sim.applies[C], 5);
	CHECK_EQ(sim.replayed[C], 3);
	CHECK(sim.safety_ok);
}

/* Checkoff step 10: an isolated follower cannot raise the term (pre-vote),
 * so reconnecting it causes no election. */
static void test_isolated_follower(void)
{
	sim_start(&sim, C, RAFT_SEED, ALL);
	sim_run(&sim, 1000);
	sim_isolate(&sim, D, true);
	sim_run(&sim, 3000);
	CHECK(raft_is_leader(&sim.node[C]));
	CHECK_EQ(sim.node[C].term, 1);
	CHECK_EQ(sim.node[S].term, 1);
	CHECK_EQ(sim.node[D].term, 1);    /* pre-votes failed; term unchanged */
	CHECK(sim.sent[RAFTP_REQ_VOTE] > 0);

	/* Entries committed meanwhile reach it after reconnection */
	raft_client_report(&sim.node[S], raft_ev_link(D, 0, false));
	sim_run(&sim, 200);
	sim_isolate(&sim, D, false);
	sim_run(&sim, 1000);
	for (unsigned n = 1; n <= RAFT_N; n++) {
		CHECK_EQ(sim.node[n].term, 1);
	}
	CHECK(raft_is_leader(&sim.node[C]));
	CHECK(states_agree(&sim, ALL));
	CHECK(sim.safety_ok);
}

/* Checkoff step 10, leader case: the term changes exactly once, and the
 * isolated leader steps down (check-quorum). */
static void test_isolated_leader(void)
{
	sim_start(&sim, C, RAFT_SEED, ALL);
	sim_run(&sim, 1000);
	int64_t t_cut = sim.now;

	sim_isolate(&sim, C, true);
	sim_run(&sim, 2000);

	unsigned who = 0;

	CHECK(!raft_is_leader(&sim.node[C]));   /* stepped down, same term */
	CHECK_EQ(sim.node[C].term, 1);
	CHECK(leaders(&sim, &who) == 1 && (who == S || who == D));
	CHECK(sim.became_leader_ms[who] - t_cut < 1000);
	CHECK_EQ(sim.node[S].term, 2);

	sim_isolate(&sim, C, false);
	sim_run(&sim, 1000);
	for (unsigned n = 1; n <= RAFT_N; n++) {
		CHECK_EQ(sim.node[n].term, 2);
	}
	CHECK(raft_is_leader(&sim.node[who]));
	CHECK_EQ(sim.node[C].leader, who);
	CHECK(sim.safety_ok);
}

/* Checkoff step 8: LEADER=random elects exactly one leader; losing it elects
 * another within 1 s. */
static void test_random_mode(void)
{
	sim_start(&sim, RAFT_LEADER_NONE, RAFT_SEED, ALL);
	sim_run(&sim, 1000);

	unsigned first = 0;

	CHECK_EQ(leaders(&sim, &first), 1);
	CHECK(sim.sent[RAFTP_REQ_VOTE] > 0);
	for (unsigned n = 1; n <= RAFT_N; n++) {
		CHECK_EQ(sim.node[n].leader, first);
	}
	int64_t t_fail = sim.now;

	sim.alive[first] = false;
	sim_run(&sim, 1000);

	unsigned second = 0;

	CHECK_EQ(leaders(&sim, &second), 1);
	CHECK(second != first);
	CHECK(sim.became_leader_ms[second] - t_fail < 1000);
	CHECK(sim.safety_ok);

	/* Different seeds give different winners: find one where the Pi wins */
	bool pi_won = false;

	for (uint32_t seed = 1; seed < 64 && !pi_won; seed++) {
		sim_start(&sim, RAFT_LEADER_NONE, seed, ALL);
		sim_run(&sim, 1000);
		pi_won = leaders(&sim, &first) == 1 && first == C;
		CHECK(sim.safety_ok);
	}
	CHECK(pi_won);
}

/* A Nucleo reboots after the leader compacted the entries it needs: it is
 * caught up with one SNAPSHOT frame and the entries after it. */
static void test_snapshot_catch_up(void)
{
	sim_start(&sim, C, RAFT_SEED, ALL);
	sim_run(&sim, 500);
	sim.alive[D] = false;
	sim_run(&sim, 400);
	for (unsigned i = 0; i < RAFT_LOG_WINDOW + 50; i++) {
		raft_client_report(&sim.node[C], raft_ev_link(S, 0, i % 2 == 1));
		sim_run(&sim, 3);
	}
	sim_run(&sim, 300);
	CHECK_EQ(sim.node[C].commit, RAFT_LOG_WINDOW + 50);
	CHECK(sim.node[C].log.snap_index > 0);   /* compacted */

	sim_boot(&sim, D);
	sim_run(&sim, 1000);
	CHECK_EQ(sim.snapshots[D], 1);
	CHECK_EQ(sim.node[D].commit, sim.node[C].commit);
	CHECK(states_agree(&sim, ALL));
	CHECK(sim.safety_ok);
}

/* 15 % frame loss with reports from every node, six random runs: logs never
 * diverge, and everything converges once the loss stops. */
static void test_lossy_network(void)
{
	static const uint8_t events[] = {
		0x80 | (1 << 1) | 1, 0x40 | (1 << 1) | 1,   /* cockpit B down / up */
		0x80 | (2 << 1) | 0, 0x40 | (2 << 1) | 0,   /* steering A down / up */
		0x80 | (3 << 1) | 1, 0x40 | (3 << 1) | 1,   /* drivetrain B down / up */
	};

	for (uint32_t run = 1; run <= 6; run++) {
		sim_start(&sim, C, RAFT_SEED, ALL);
		sim.rng = 0x9E3779B9u * run;
		sim_run(&sim, 500);
		sim.drop_pct = 15;
		for (unsigned i = 0; i < 120; i++) {
			unsigned n = 1 + sim_rand(&sim) % RAFT_N;

			raft_client_report(&sim.node[n], events[sim_rand(&sim) % sizeof(events)]);
			sim_run(&sim, 37);
		}
		sim.drop_pct = 0;
		sim_run(&sim, 2000);
		CHECK_EQ(leaders(&sim, NULL), 1);
		CHECK(sim.node[C].commit > 10);
		CHECK(states_agree(&sim, ALL));
		CHECK(sim.safety_ok);
	}
}

void test_raft_all(void)
{
	RUN(test_default_leader_boot);
	RUN(test_late_cockpit);
	RUN(test_client_reports);
	RUN(test_redirect);
	RUN(test_leader_failure);
	RUN(test_rejoin_replay);
	RUN(test_isolated_follower);
	RUN(test_isolated_leader);
	RUN(test_random_mode);
	RUN(test_snapshot_catch_up);
	RUN(test_lossy_network);
	RUN(test_client_withdraw);
	RUN(test_follower_reboot_after_catch_up);
	RUN(test_term_guard);
}
