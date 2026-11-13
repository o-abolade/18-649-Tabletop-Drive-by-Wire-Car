/*
 * Lab 5 Raft state machine: the network graph (handout Parts 3-5).
 *
 * Log events are one byte each. Applying committed events in index order
 * with netstate_apply() builds the graph; netstate_get() is getNetwork().
 * netstate_routes() distills the graph into one node's routing table.
 *
 * Plain C99, no Zephyr or Linux dependencies (shared by all three nodes).
 */
#ifndef NETSTATE_H
#define NETSTATE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "raft_config.h"

/* ---- Log events (1 byte) ----
 *   0x00              NONE (empty APPEND)
 *   0x01 / 0x02       COCKPIT_OK / COCKPIT_ERROR
 *   0x10 | X          NODE_X_UP     (X = 1..3)
 *   0x20 | X          NODE_X_DOWN
 *   0x40 | X<<1 | Y   CANLINK_X_Y_UP   (Y = 0 bus A, 1 bus B)
 *   0x80 | X<<1 | Y   CANLINK_X_Y_DOWN
 */
#define RAFT_EV_NONE          0x00
#define RAFT_EV_COCKPIT_OK    0x01
#define RAFT_EV_COCKPIT_ERROR 0x02

enum raft_ev_type {
	RAFT_EVT_INVALID = 0,
	RAFT_EVT_NONE,
	RAFT_EVT_COCKPIT,
	RAFT_EVT_NODE,
	RAFT_EVT_LINK,
};

struct raft_ev {
	enum raft_ev_type type;
	uint8_t node;   /* NODE, LINK */
	uint8_t bus;    /* LINK */
	bool up;        /* COCKPIT: ok; NODE/LINK: up */
};

static inline uint8_t raft_ev_cockpit(bool ok)
{
	return ok ? RAFT_EV_COCKPIT_OK : RAFT_EV_COCKPIT_ERROR;
}

static inline uint8_t raft_ev_node(unsigned node, bool up)
{
	return (uint8_t)((up ? 0x10 : 0x20) | (node & 0x0F));
}

static inline uint8_t raft_ev_link(unsigned node, unsigned bus, bool up)
{
	return (uint8_t)((up ? 0x40 : 0x80) | ((node & 0x07) << 1) | (bus & 1));
}

/* Decode an event byte. Returns false (type INVALID) for codes outside the
 * table above, including node IDs outside 1..RAFT_N. */
bool raft_ev_parse(uint8_t code, struct raft_ev *out);

/* Handout name of an event, e.g. "CANLINK_steering_A_DOWN". Always
 * NUL-terminates; returns buf. */
const char *raft_ev_name(uint8_t code, char *buf, size_t len);

const char *raft_node_name(unsigned node);

/* ---- The graph ---- */
struct netstate {
	bool node_up[RAFT_N + 1];                  /* index 1..RAFT_N */
	bool link_up[RAFT_N + 1][RAFT_NUM_BUSES];  /* link of node X to bus Y */
	bool cockpit_ok;                           /* false = failure mode */
};

/* Initial state at index 0, before any entry is applied: every node and link
 * up (the cluster's configured membership), cockpit in error. The system
 * powers up in the error state (Lab 1 R3), so the first COCKPIT_OK is a log
 * entry. */
void netstate_init(struct netstate *ns);

/* The four mutators (handout Part 4). */
void netstate_add_link(struct netstate *ns, unsigned node, unsigned bus);
void netstate_drop_link(struct netstate *ns, unsigned node, unsigned bus);
void netstate_add_node(struct netstate *ns, unsigned node);   /* no links */
void netstate_drop_node(struct netstate *ns, unsigned node);  /* and its links */

/* Apply one committed event. Returns true if the graph changed; invalid
 * codes are ignored and return false. */
bool netstate_apply(struct netstate *ns, uint8_t code);

/* True if applying code would change the graph. The leader appends a client
 * report only in that case, so three clients reporting one failure produce
 * one entry. */
bool netstate_would_change(const struct netstate *ns, uint8_t code);

/* getNetwork(): a copy of the whole graph. */
void netstate_get(const struct netstate *ns, struct netstate *out);

bool netstate_equal(const struct netstate *a, const struct netstate *b);

/* 16-bit form for the SNAPSHOT frame: bit (n-1) = node n up, bit 3 = cockpit
 * OK, bit 8 + 2(n-1) + y = link of node n to bus y up. */
uint16_t netstate_pack(const struct netstate *ns);
void netstate_unpack(uint16_t bits, struct netstate *ns);

/* A node is usable on a bus only if both the node and its link are up. */
static inline bool netstate_on_bus(const struct netstate *ns, unsigned node, unsigned bus)
{
	return node >= 1 && node <= RAFT_N && bus < RAFT_NUM_BUSES &&
	       ns->node_up[node] && ns->link_up[node][bus];
}

/* One line, e.g.
 * "cockpit:AB steering:A- drivetrain:-B | cockpit OK"
 * ("--" for a node that is down). Returns buf. */
const char *netstate_format(const struct netstate *ns, char *buf, size_t len);

/* ---- Routing (handout Part 5.1) ----
 * Node X's frames on bus b must be relayed onto the other bus o when some
 * live node Z is on o but not on b (so Z cannot hear X directly) and X is not
 * on o. The relay is the lowest-numbered relay-capable node on both buses;
 * every node holds the same committed graph, so all agree which node that
 * is, and exactly one copy is forwarded. */
struct route_table {
	/* relay[X][b]: this node forwards X's frames from bus b to the other bus */
	bool relay[RAFT_N + 1][RAFT_NUM_BUSES];
	/* reachable[X][Z]: Z can receive X's frames, directly or relayed */
	bool reachable[RAFT_N + 1][RAFT_N + 1];
	/* The node chosen to relay, 0 if no relay is needed */
	uint8_t relay_node;
	/* A relay is needed but no relay-capable node is on both buses (e.g.
	 * steering and drivetrain on opposite buses; only the Pi could bridge). */
	bool partitioned;
};

/* Build node self's table. relay_capable: bit n set if node n may relay
 * (normally RAFT_RELAY_CAPABLE). */
void netstate_routes(const struct netstate *ns, unsigned self, unsigned relay_capable,
		     struct route_table *rt);

/* ---- Client detection (design.md section 6) ----
 * heard[X][b]: node X's heartbeat arrived on bus b within the heartbeat
 * timeout (any copy, direct or relayed). heard[self][*] is ignored.
 *
 * A node vouches only for what it can tell apart:
 *  - itself: always up;
 *  - while it hears at least one other node (on either bus):
 *    - its own links: link b is up iff it hears any other node on bus b (a
 *      cut jumper cuts both directions, so nothing arrives on that bus);
 *    - other nodes up/down, and for the cockpit COCKPIT_OK/ERROR as well.
 *  A node that hears nobody cannot tell whether its links or the others
 *  failed (seen at boot, before the other board came up: its "my links are
 *  down" reports were committed the moment it won an election). It reports
 *  nothing; the others report it down, and its links come back with
 *  NODE_UP and its own link reports.
 * Writes the event codes that describe the current observation and returns
 * how many (at most NETSTATE_OBS_MAX). The client reports those that would
 * change the committed graph and withdraws their opposites. */
#define NETSTATE_OBS_MAX (RAFT_NUM_BUSES + 1 + 2 * (RAFT_N - 1))

unsigned netstate_observe(unsigned self, const bool heard[RAFT_N + 1][RAFT_NUM_BUSES],
			  uint8_t events[NETSTATE_OBS_MAX]);

/* The opposite of an event (UP <-> DOWN, OK <-> ERROR); NONE stays NONE. */
uint8_t raft_ev_opposite(uint8_t code);

/* Routing view for handout 5.2: the committed graph, with links replaced by
 * fresher local knowledge, so a relay starts before the failure commits.
 *  - self's links: from heard[] as in netstate_observe();
 *  - node X's links: from the health bits in X's latest heartbeat
 *    (health[X] bit b = X hears someone on bus b), if health[X] >= 0.
 * Node up/down stays as committed. */
void netstate_local_view(const struct netstate *committed, unsigned self,
			 const bool heard[RAFT_N + 1][RAFT_NUM_BUSES],
			 const int health[RAFT_N + 1], struct netstate *out);

#endif /* NETSTATE_H */
