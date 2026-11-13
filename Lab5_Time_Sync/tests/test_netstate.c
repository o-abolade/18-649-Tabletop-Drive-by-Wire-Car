/* Host tests: log events, the network-graph state machine, routing. */
#include <string.h>

#include "check.h"
#include "netstate.h"

enum { C = RAFT_NODE_COCKPIT, S = RAFT_NODE_STEERING, D = RAFT_NODE_DRIVETRAIN };
enum { A = 0, B = 1 };

static void test_event_codes(void)
{
	struct raft_ev ev;
	char name[40];

	/* Every valid code round-trips through parse */
	for (unsigned n = 1; n <= RAFT_N; n++) {
		CHECK(raft_ev_parse(raft_ev_node(n, true), &ev));
		CHECK_EQ(ev.type, RAFT_EVT_NODE);
		CHECK_EQ(ev.node, n);
		CHECK(ev.up);
		CHECK(raft_ev_parse(raft_ev_node(n, false), &ev));
		CHECK(!ev.up);
		for (unsigned b = 0; b < RAFT_NUM_BUSES; b++) {
			CHECK(raft_ev_parse(raft_ev_link(n, b, false), &ev));
			CHECK_EQ(ev.type, RAFT_EVT_LINK);
			CHECK_EQ(ev.node, n);
			CHECK_EQ(ev.bus, b);
			CHECK(!ev.up);
		}
	}
	CHECK(raft_ev_parse(RAFT_EV_COCKPIT_ERROR, &ev));
	CHECK_EQ(ev.type, RAFT_EVT_COCKPIT);
	CHECK(!ev.up);
	CHECK(raft_ev_parse(RAFT_EV_NONE, &ev));
	CHECK_EQ(ev.type, RAFT_EVT_NONE);

	/* Invalid: node 0 and node 4+, unused high nibbles */
	CHECK(!raft_ev_parse(0x10, &ev));
	CHECK(!raft_ev_parse(0x14, &ev));
	CHECK(!raft_ev_parse(0x40 | (4 << 1), &ev));
	CHECK(!raft_ev_parse(0x03, &ev));
	CHECK(!raft_ev_parse(0xF0, &ev));

	/* All 20 valid codes are distinct */
	uint8_t codes[20];
	unsigned k = 0;

	codes[k++] = RAFT_EV_COCKPIT_OK;
	codes[k++] = RAFT_EV_COCKPIT_ERROR;
	for (unsigned n = 1; n <= RAFT_N; n++) {
		codes[k++] = raft_ev_node(n, true);
		codes[k++] = raft_ev_node(n, false);
		for (unsigned b = 0; b < RAFT_NUM_BUSES; b++) {
			codes[k++] = raft_ev_link(n, b, true);
			codes[k++] = raft_ev_link(n, b, false);
		}
	}
	CHECK_EQ(k, 20);
	for (unsigned i = 0; i < k; i++) {
		for (unsigned j = i + 1; j < k; j++) {
			CHECK(codes[i] != codes[j]);
		}
	}

	CHECK(strcmp(raft_ev_name(raft_ev_link(S, A, false), name, sizeof(name)),
		     "CANLINK_steering_A_DOWN") == 0);
	CHECK(strcmp(raft_ev_name(raft_ev_node(C, true), name, sizeof(name)),
		     "NODE_cockpit_UP") == 0);
	CHECK(strcmp(raft_ev_name(RAFT_EV_COCKPIT_OK, name, sizeof(name)), "COCKPIT_OK") == 0);
}

static void test_initial_state(void)
{
	struct netstate ns;
	char line[96];

	netstate_init(&ns);
	for (unsigned n = 1; n <= RAFT_N; n++) {
		CHECK(ns.node_up[n]);
		CHECK(netstate_on_bus(&ns, n, A));
		CHECK(netstate_on_bus(&ns, n, B));
	}
	CHECK(!ns.cockpit_ok);
	CHECK(strcmp(netstate_format(&ns, line, sizeof(line)),
		     "cockpit:AB steering:AB drivetrain:AB | cockpit ERROR") == 0);
}

/* The handout Part 4 table, row by row */
static void test_mutators(void)
{
	struct netstate ns;

	netstate_init(&ns);

	/* dropLink: link down, node and bus stay */
	CHECK(netstate_apply(&ns, raft_ev_link(S, A, false)));
	CHECK(ns.node_up[S]);
	CHECK(!ns.link_up[S][A]);
	CHECK(ns.link_up[S][B]);
	CHECK(!netstate_apply(&ns, raft_ev_link(S, A, false)));   /* idempotent */

	/* addLink: back up */
	CHECK(netstate_apply(&ns, raft_ev_link(S, A, true)));
	CHECK(ns.link_up[S][A]);

	/* dropNode: removes the node and every link attached to it */
	CHECK(netstate_apply(&ns, raft_ev_node(D, false)));
	CHECK(!ns.node_up[D]);
	CHECK(!ns.link_up[D][A]);
	CHECK(!ns.link_up[D][B]);
	CHECK(!netstate_on_bus(&ns, D, A));

	/* addNode: alive again, with no links until addLink */
	CHECK(netstate_apply(&ns, raft_ev_node(D, true)));
	CHECK(ns.node_up[D]);
	CHECK(!ns.link_up[D][A]);
	CHECK(!ns.link_up[D][B]);
	CHECK(netstate_apply(&ns, raft_ev_link(D, A, true)));
	CHECK(netstate_apply(&ns, raft_ev_link(D, B, true)));
	CHECK(netstate_on_bus(&ns, D, A) && netstate_on_bus(&ns, D, B));

	/* addNode on a live node changes nothing (its links stay) */
	CHECK(!netstate_apply(&ns, raft_ev_node(D, true)));
	CHECK(ns.link_up[D][A]);

	/* Cockpit failure mode */
	CHECK(netstate_apply(&ns, RAFT_EV_COCKPIT_OK));
	CHECK(ns.cockpit_ok);
	CHECK(netstate_apply(&ns, RAFT_EV_COCKPIT_ERROR));
	CHECK(!ns.cockpit_ok);

	/* NONE and invalid codes are ignored */
	struct netstate before = ns;

	CHECK(!netstate_apply(&ns, RAFT_EV_NONE));
	CHECK(!netstate_apply(&ns, 0xFF));
	CHECK(netstate_equal(&before, &ns));
}

static void test_would_change_and_get(void)
{
	struct netstate ns, copy;

	netstate_init(&ns);
	CHECK(netstate_would_change(&ns, raft_ev_link(C, B, false)));
	CHECK(!netstate_would_change(&ns, raft_ev_link(C, B, true)));
	/* would_change never mutates */
	CHECK(ns.link_up[C][B]);

	netstate_apply(&ns, raft_ev_link(C, B, false));
	netstate_get(&ns, &copy);
	CHECK(netstate_equal(&ns, &copy));
	CHECK(!copy.link_up[C][B]);
}

/* Replaying the same log on two nodes gives the same graph */
static void test_replay_agrees(void)
{
	const uint8_t log[] = {
		RAFT_EV_COCKPIT_OK,
		raft_ev_link(C, B, false),
		raft_ev_link(D, A, false),
		raft_ev_node(S, false),
		raft_ev_node(S, true),
		raft_ev_link(S, A, true),
		raft_ev_link(D, A, true),
	};
	struct netstate n1, n2;

	netstate_init(&n1);
	netstate_init(&n2);
	for (unsigned i = 0; i < sizeof(log); i++) {
		netstate_apply(&n1, log[i]);
	}
	for (unsigned i = 0; i < sizeof(log); i++) {
		netstate_apply(&n2, log[i]);
	}
	CHECK(netstate_equal(&n1, &n2));
	CHECK(n1.cockpit_ok);
	CHECK(!netstate_on_bus(&n1, C, B));
	CHECK(netstate_on_bus(&n1, S, A));
	CHECK(!netstate_on_bus(&n1, S, B));   /* re-added node: only link A came back */
	CHECK(netstate_on_bus(&n1, D, A));
}

/* Build a graph from a list of links that are down */
static void graph(struct netstate *ns, const uint8_t *down, unsigned n)
{
	netstate_init(ns);
	ns->cockpit_ok = true;
	for (unsigned i = 0; i < n; i++) {
		netstate_apply(ns, down[i]);
	}
}

static bool all_reachable(const struct route_table *rt, const struct netstate *ns)
{
	for (unsigned x = 1; x <= RAFT_N; x++) {
		for (unsigned z = 1; z <= RAFT_N; z++) {
			if (x != z && ns->node_up[x] && ns->node_up[z] && !rt->reachable[x][z]) {
				return false;
			}
		}
	}
	return true;
}

static void test_routes_no_relay_needed(void)
{
	struct netstate ns;
	struct route_table rt;

	/* All up */
	graph(&ns, NULL, 0);
	netstate_routes(&ns, S, RAFT_RELAY_CAPABLE, &rt);
	CHECK_EQ(rt.relay_node, 0);
	CHECK(!rt.partitioned);
	CHECK(all_reachable(&rt, &ns));

	/* One node off one bus (rows 1-6): the other bus still connects everyone */
	for (unsigned n = 1; n <= RAFT_N; n++) {
		for (unsigned b = 0; b < RAFT_NUM_BUSES; b++) {
			uint8_t down[] = { raft_ev_link(n, b, false) };

			graph(&ns, down, 1);
			for (unsigned self = 1; self <= RAFT_N; self++) {
				netstate_routes(&ns, self, RAFT_RELAY_CAPABLE, &rt);
				CHECK_EQ(rt.relay_node, 0);
				CHECK(all_reachable(&rt, &ns));
			}
		}
	}

	/* Whole bus A lost (row 16) */
	uint8_t bus_a[] = { raft_ev_link(C, A, false), raft_ev_link(S, A, false),
			    raft_ev_link(D, A, false) };

	graph(&ns, bus_a, 3);
	netstate_routes(&ns, D, RAFT_RELAY_CAPABLE, &rt);
	CHECK_EQ(rt.relay_node, 0);
	CHECK(all_reachable(&rt, &ns));
}

/* Rows 10-13: N1 and one STM32 on opposite buses; the other STM32 relays */
static void test_routes_relay_cases(void)
{
	struct {
		uint8_t down[2];
		unsigned relay;
		unsigned x, x_bus;   /* node whose frames the relay forwards, and from which bus */
	} cases[] = {
		{ { raft_ev_link(C, B, false), raft_ev_link(D, A, false) }, S, C, A },  /* (c) */
		{ { raft_ev_link(C, A, false), raft_ev_link(D, B, false) }, S, C, B },
		{ { raft_ev_link(C, A, false), raft_ev_link(S, B, false) }, D, C, B },
		{ { raft_ev_link(C, B, false), raft_ev_link(S, A, false) }, D, C, A },
	};

	for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		struct netstate ns;
		struct route_table rt;

		graph(&ns, cases[i].down, 2);
		for (unsigned self = 1; self <= RAFT_N; self++) {
			netstate_routes(&ns, self, RAFT_RELAY_CAPABLE, &rt);
			CHECK_EQ(rt.relay_node, cases[i].relay);   /* everyone agrees */
			CHECK(!rt.partitioned);
			CHECK(all_reachable(&rt, &ns));
			/* Only the relay forwards, and only from the bus the frames are on */
			bool fwd = rt.relay[cases[i].x][cases[i].x_bus];

			CHECK_EQ(fwd, self == cases[i].relay);
			CHECK(!rt.relay[cases[i].x][1 - cases[i].x_bus]);
		}
		/* The relay also forwards the cut-off STM32's frames the other way */
		netstate_routes(&ns, cases[i].relay, RAFT_RELAY_CAPABLE, &rt);
		unsigned other = (cases[i].relay == S) ? D : S;

		CHECK(rt.relay[other][1 - cases[i].x_bus]);
		/* ...but never its own frames */
		CHECK(!rt.relay[cases[i].relay][A] && !rt.relay[cases[i].relay][B]);
	}
}

/* Rows 14-15: N2 and N3 on opposite buses. Only the Pi could bridge, and the
 * Pi never relays, so the graph reports a partition. */
static void test_routes_partition(void)
{
	uint8_t down[] = { raft_ev_link(S, A, false), raft_ev_link(D, B, false) };
	struct netstate ns;
	struct route_table rt;

	graph(&ns, down, 2);
	netstate_routes(&ns, S, RAFT_RELAY_CAPABLE, &rt);
	CHECK(rt.partitioned);
	CHECK(!rt.reachable[S][D]);
	CHECK(!rt.reachable[D][S]);
	CHECK(rt.reachable[S][C] && rt.reachable[C][D]);   /* the Pi still hears both */

	/* If the Pi were allowed to relay, it would bridge them */
	netstate_routes(&ns, C, RAFT_RELAY_CAPABLE | (1u << C), &rt);
	CHECK(!rt.partitioned);
	CHECK_EQ(rt.relay_node, C);
	CHECK(rt.relay[S][B] && rt.relay[D][A]);
}

/* Row 8: a node off both buses. No relay helps; nobody can reach it, and it
 * is not reported as a partition (it gets NODE_DOWN instead). */
static void test_routes_isolated(void)
{
	uint8_t down[] = { raft_ev_link(S, A, false), raft_ev_link(S, B, false) };
	struct netstate ns;
	struct route_table rt;

	graph(&ns, down, 2);
	netstate_routes(&ns, D, RAFT_RELAY_CAPABLE, &rt);
	CHECK(!rt.partitioned);
	CHECK(!rt.reachable[C][S]);
	CHECK(!rt.reachable[S][D]);
	CHECK(rt.reachable[C][D]);

	/* Once NODE_DOWN is committed the node drops out of the table */
	netstate_apply(&ns, raft_ev_node(S, false));
	netstate_routes(&ns, D, RAFT_RELAY_CAPABLE, &rt);
	CHECK(!rt.reachable[S][D] && !rt.reachable[D][S]);
}

static bool has(const uint8_t *ev, unsigned n, uint8_t code)
{
	for (unsigned i = 0; i < n; i++) {
		if (ev[i] == code) {
			return true;
		}
	}
	return false;
}

static void test_observe(void)
{
	bool heard[RAFT_N + 1][RAFT_NUM_BUSES];
	uint8_t ev[NETSTATE_OBS_MAX];
	unsigned n;

	/* Everyone heard on both buses */
	memset(heard, 1, sizeof(heard));
	n = netstate_observe(S, heard, ev);
	CHECK_EQ(n, 6);
	CHECK(has(ev, n, raft_ev_link(S, A, true)) && has(ev, n, raft_ev_link(S, B, true)));
	CHECK(has(ev, n, raft_ev_node(C, true)) && has(ev, n, raft_ev_cockpit(true)));
	CHECK(has(ev, n, raft_ev_node(D, true)) && has(ev, n, raft_ev_node(S, true)));
	CHECK_EQ(netstate_observe(C, heard, ev), 5);   /* the cockpit: no COCKPIT_* */

	/* Drivetrain's A jumper pulled: it hears nobody on A, so its own A link
	 * is down; the others hear it only on B, but vouch for its node only */
	memset(heard, 1, sizeof(heard));
	heard[C][A] = heard[S][A] = false;
	n = netstate_observe(D, heard, ev);
	CHECK(has(ev, n, raft_ev_link(D, A, false)) && has(ev, n, raft_ev_link(D, B, true)));
	CHECK(has(ev, n, raft_ev_node(C, true)) && has(ev, n, raft_ev_node(S, true)));
	memset(heard, 1, sizeof(heard));
	heard[D][A] = false;
	n = netstate_observe(S, heard, ev);
	CHECK(has(ev, n, raft_ev_node(D, true)));
	CHECK(!has(ev, n, raft_ev_link(D, A, false)));   /* not steering's to say */

	/* Cockpit lost on both buses */
	memset(heard, 1, sizeof(heard));
	heard[C][A] = heard[C][B] = false;
	n = netstate_observe(S, heard, ev);
	CHECK(has(ev, n, raft_ev_node(C, false)) && has(ev, n, raft_ev_cockpit(false)));

	/* Hears nobody (isolated, or alone at boot): only itself */
	memset(heard, 0, sizeof(heard));
	n = netstate_observe(D, heard, ev);
	CHECK_EQ(n, 1);
	CHECK(has(ev, n, raft_ev_node(D, true)));
	CHECK(!has(ev, n, raft_ev_link(D, A, false)) && !has(ev, n, raft_ev_node(C, false)));

	CHECK_EQ(raft_ev_opposite(raft_ev_link(S, B, true)), raft_ev_link(S, B, false));
	CHECK_EQ(raft_ev_opposite(raft_ev_node(C, false)), raft_ev_node(C, true));
	CHECK_EQ(raft_ev_opposite(RAFT_EV_COCKPIT_OK), RAFT_EV_COCKPIT_ERROR);
	CHECK_EQ(raft_ev_opposite(RAFT_EV_NONE), RAFT_EV_NONE);
}

/* Checkoff step 5 before anything commits: cockpit off B, drivetrain off A.
 * The heartbeat health bits alone make steering the relay. */
static void test_local_view_relay(void)
{
	struct netstate committed, view;
	struct route_table rt;
	bool heard[RAFT_N + 1][RAFT_NUM_BUSES];
	int health[RAFT_N + 1] = { -1, 0x1, 0x3, 0x2 };   /* C: A only, D: B only */

	netstate_init(&committed);
	netstate_routes(&committed, S, RAFT_RELAY_CAPABLE, &rt);
	CHECK_EQ(rt.relay_node, 0);

	memset(heard, 0, sizeof(heard));
	heard[C][A] = heard[D][B] = true;
	netstate_local_view(&committed, S, heard, health, &view);
	CHECK(view.link_up[S][A] && view.link_up[S][B]);
	CHECK(!view.link_up[C][B] && !view.link_up[D][A]);
	netstate_routes(&view, S, RAFT_RELAY_CAPABLE, &rt);
	CHECK_EQ(rt.relay_node, S);
	CHECK(rt.relay[C][A] && rt.relay[D][B]);
	CHECK(rt.reachable[C][D] && rt.reachable[D][C]);

	/* Unknown health keeps the committed links */
	int none[RAFT_N + 1] = { -1, -1, -1, -1 };

	netstate_local_view(&committed, S, heard, none, &view);
	CHECK(view.link_up[C][B] && view.link_up[D][A]);
}

void test_netstate_all(void)
{
	RUN(test_observe);
	RUN(test_local_view_relay);
	RUN(test_event_codes);
	RUN(test_initial_state);
	RUN(test_mutators);
	RUN(test_would_change_and_get);
	RUN(test_replay_agrees);
	RUN(test_routes_no_relay_needed);
	RUN(test_routes_relay_cases);
	RUN(test_routes_partition);
	RUN(test_routes_isolated);
}
