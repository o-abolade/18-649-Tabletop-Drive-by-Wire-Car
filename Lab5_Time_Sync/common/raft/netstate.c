/* Lab 5 Raft state machine: network graph, events, routing. See netstate.h. */
#include "netstate.h"

#include <stdio.h>
#include <string.h>

static bool valid_node(unsigned node)
{
	return node >= 1 && node <= RAFT_N;
}

bool raft_ev_parse(uint8_t code, struct raft_ev *out)
{
	struct raft_ev ev = { .type = RAFT_EVT_INVALID };
	unsigned node;

	if (code == RAFT_EV_NONE) {
		ev.type = RAFT_EVT_NONE;
	} else if (code == RAFT_EV_COCKPIT_OK || code == RAFT_EV_COCKPIT_ERROR) {
		ev.type = RAFT_EVT_COCKPIT;
		ev.up = (code == RAFT_EV_COCKPIT_OK);
	} else if ((code & 0xF0) == 0x10 || (code & 0xF0) == 0x20) {
		node = code & 0x0F;
		if (valid_node(node)) {
			ev.type = RAFT_EVT_NODE;
			ev.node = (uint8_t)node;
			ev.up = (code & 0xF0) == 0x10;
		}
	} else if ((code & 0xF0) == 0x40 || (code & 0xF0) == 0x80) {
		node = (code >> 1) & 0x07;
		if (valid_node(node)) {
			ev.type = RAFT_EVT_LINK;
			ev.node = (uint8_t)node;
			ev.bus = code & 1;
			ev.up = (code & 0xF0) == 0x40;
		}
	}
	*out = ev;
	return ev.type != RAFT_EVT_INVALID;
}

const char *raft_node_name(unsigned node)
{
	switch (node) {
	case RAFT_NODE_COCKPIT: return "cockpit";
	case RAFT_NODE_STEERING: return "steering";
	case RAFT_NODE_DRIVETRAIN: return "drivetrain";
	default: return "?";
	}
}

const char *raft_ev_name(uint8_t code, char *buf, size_t len)
{
	struct raft_ev ev;

	if (len == 0) {
		return buf;
	}
	raft_ev_parse(code, &ev);
	switch (ev.type) {
	case RAFT_EVT_NONE:
		snprintf(buf, len, "NONE");
		break;
	case RAFT_EVT_COCKPIT:
		snprintf(buf, len, ev.up ? "COCKPIT_OK" : "COCKPIT_ERROR");
		break;
	case RAFT_EVT_NODE:
		snprintf(buf, len, "NODE_%s_%s", raft_node_name(ev.node), ev.up ? "UP" : "DOWN");
		break;
	case RAFT_EVT_LINK:
		snprintf(buf, len, "CANLINK_%s_%c_%s", raft_node_name(ev.node), 'A' + ev.bus,
			 ev.up ? "UP" : "DOWN");
		break;
	default:
		snprintf(buf, len, "INVALID_0x%02X", code);
		break;
	}
	return buf;
}

void netstate_init(struct netstate *ns)
{
	memset(ns, 0, sizeof(*ns));
	for (unsigned n = 1; n <= RAFT_N; n++) {
		ns->node_up[n] = true;
		for (unsigned b = 0; b < RAFT_NUM_BUSES; b++) {
			ns->link_up[n][b] = true;
		}
	}
	ns->cockpit_ok = false;
}

void netstate_add_link(struct netstate *ns, unsigned node, unsigned bus)
{
	if (valid_node(node) && bus < RAFT_NUM_BUSES) {
		ns->link_up[node][bus] = true;
	}
}

void netstate_drop_link(struct netstate *ns, unsigned node, unsigned bus)
{
	if (valid_node(node) && bus < RAFT_NUM_BUSES) {
		ns->link_up[node][bus] = false;
	}
}

void netstate_add_node(struct netstate *ns, unsigned node)
{
	if (valid_node(node)) {
		/* Alive again with no links until addLink is applied for it */
		if (!ns->node_up[node]) {
			for (unsigned b = 0; b < RAFT_NUM_BUSES; b++) {
				ns->link_up[node][b] = false;
			}
		}
		ns->node_up[node] = true;
	}
}

void netstate_drop_node(struct netstate *ns, unsigned node)
{
	if (valid_node(node)) {
		ns->node_up[node] = false;
		for (unsigned b = 0; b < RAFT_NUM_BUSES; b++) {
			ns->link_up[node][b] = false;
		}
	}
}

bool netstate_apply(struct netstate *ns, uint8_t code)
{
	struct raft_ev ev;
	struct netstate before = *ns;

	if (!raft_ev_parse(code, &ev)) {
		return false;
	}
	switch (ev.type) {
	case RAFT_EVT_COCKPIT:
		ns->cockpit_ok = ev.up;
		break;
	case RAFT_EVT_NODE:
		if (ev.up) {
			netstate_add_node(ns, ev.node);
		} else {
			netstate_drop_node(ns, ev.node);
		}
		break;
	case RAFT_EVT_LINK:
		if (ev.up) {
			netstate_add_link(ns, ev.node, ev.bus);
		} else {
			netstate_drop_link(ns, ev.node, ev.bus);
		}
		break;
	default:
		break;
	}
	return !netstate_equal(&before, ns);
}

bool netstate_would_change(const struct netstate *ns, uint8_t code)
{
	struct netstate copy = *ns;

	return netstate_apply(&copy, code);
}

void netstate_get(const struct netstate *ns, struct netstate *out)
{
	*out = *ns;
}

bool netstate_equal(const struct netstate *a, const struct netstate *b)
{
	if (a->cockpit_ok != b->cockpit_ok) {
		return false;
	}
	for (unsigned n = 1; n <= RAFT_N; n++) {
		if (a->node_up[n] != b->node_up[n]) {
			return false;
		}
		for (unsigned bus = 0; bus < RAFT_NUM_BUSES; bus++) {
			if (a->link_up[n][bus] != b->link_up[n][bus]) {
				return false;
			}
		}
	}
	return true;
}

uint16_t netstate_pack(const struct netstate *ns)
{
	uint16_t bits = ns->cockpit_ok ? (1u << 3) : 0;

	for (unsigned n = 1; n <= RAFT_N; n++) {
		if (ns->node_up[n]) {
			bits |= (uint16_t)(1u << (n - 1));
		}
		for (unsigned b = 0; b < RAFT_NUM_BUSES; b++) {
			if (ns->link_up[n][b]) {
				bits |= (uint16_t)(1u << (8 + 2 * (n - 1) + b));
			}
		}
	}
	return bits;
}

void netstate_unpack(uint16_t bits, struct netstate *ns)
{
	memset(ns, 0, sizeof(*ns));
	ns->cockpit_ok = (bits & (1u << 3)) != 0;
	for (unsigned n = 1; n <= RAFT_N; n++) {
		ns->node_up[n] = (bits & (1u << (n - 1))) != 0;
		for (unsigned b = 0; b < RAFT_NUM_BUSES; b++) {
			ns->link_up[n][b] = (bits & (1u << (8 + 2 * (n - 1) + b))) != 0;
		}
	}
}

const char *netstate_format(const struct netstate *ns, char *buf, size_t len)
{
	size_t used = 0;

	if (len == 0) {
		return buf;
	}
	buf[0] = '\0';
	for (unsigned n = 1; n <= RAFT_N && used < len; n++) {
		int w = snprintf(buf + used, len - used, "%s%s:%c%c", n > 1 ? " " : "",
				 raft_node_name(n),
				 netstate_on_bus(ns, n, 0) ? 'A' : '-',
				 netstate_on_bus(ns, n, 1) ? 'B' : '-');

		if (w < 0) {
			return buf;
		}
		used += (size_t)w;
	}
	if (used < len) {
		snprintf(buf + used, len - used, " | cockpit %s", ns->cockpit_ok ? "OK" : "ERROR");
	}
	return buf;
}

/* Lowest-numbered relay-capable node that is up on every bus, or 0. */
static unsigned pick_relay(const struct netstate *ns, unsigned relay_capable)
{
	for (unsigned r = 1; r <= RAFT_N; r++) {
		if ((relay_capable & (1u << r)) && netstate_on_bus(ns, r, 0) &&
		    netstate_on_bus(ns, r, 1)) {
			return r;
		}
	}
	return 0;
}

void netstate_routes(const struct netstate *ns, unsigned self, unsigned relay_capable,
		     struct route_table *rt)
{
	unsigned relay = pick_relay(ns, relay_capable);

	memset(rt, 0, sizeof(*rt));
	for (unsigned x = 1; x <= RAFT_N; x++) {
		for (unsigned z = 1; z <= RAFT_N; z++) {
			if (x == z || !ns->node_up[x] || !ns->node_up[z]) {
				continue;
			}
			bool direct = false;

			for (unsigned b = 0; b < RAFT_NUM_BUSES; b++) {
				direct |= netstate_on_bus(ns, x, b) && netstate_on_bus(ns, z, b);
			}
			if (direct) {
				rt->reachable[x][z] = true;
				continue;
			}
			/* Not direct: either one of them has no link at all (isolated,
			 * no relay helps) or they are on opposite buses. */
			for (unsigned b = 0; b < RAFT_NUM_BUSES; b++) {
				unsigned o = 1 - b;

				if (!netstate_on_bus(ns, x, b) || !netstate_on_bus(ns, z, o)) {
					continue;
				}
				if (relay == 0) {
					rt->partitioned = true;
					continue;
				}
				rt->reachable[x][z] = true;
				rt->relay_node = (uint8_t)relay;
				if (self == relay) {
					rt->relay[x][b] = true;
				}
			}
		}
	}
}

/* ---------------- Client detection ---------------- */

static bool hears_bus(unsigned self, const bool heard[RAFT_N + 1][RAFT_NUM_BUSES], unsigned b)
{
	for (unsigned x = 1; x <= RAFT_N; x++) {
		if (x != self && heard[x][b]) {
			return true;
		}
	}
	return false;
}

unsigned netstate_observe(unsigned self, const bool heard[RAFT_N + 1][RAFT_NUM_BUSES],
			  uint8_t events[NETSTATE_OBS_MAX])
{
	unsigned n = 0;
	bool hears_anyone = false;

	for (unsigned b = 0; b < RAFT_NUM_BUSES; b++) {
		hears_anyone |= hears_bus(self, heard, b);
	}
	events[n++] = raft_ev_node(self, true);
	if (!hears_anyone) {
		return n;
	}
	for (unsigned b = 0; b < RAFT_NUM_BUSES; b++) {
		events[n++] = raft_ev_link(self, b, hears_bus(self, heard, b));
	}
	for (unsigned x = 1; x <= RAFT_N; x++) {
		if (x == self) {
			continue;
		}
		bool alive = false;

		for (unsigned b = 0; b < RAFT_NUM_BUSES; b++) {
			alive |= heard[x][b];
		}
		events[n++] = raft_ev_node(x, alive);
		if (x == RAFT_NODE_COCKPIT) {
			events[n++] = raft_ev_cockpit(alive);
		}
	}
	return n;
}

uint8_t raft_ev_opposite(uint8_t code)
{
	struct raft_ev ev;

	if (!raft_ev_parse(code, &ev)) {
		return code;
	}
	switch (ev.type) {
	case RAFT_EVT_COCKPIT: return raft_ev_cockpit(!ev.up);
	case RAFT_EVT_NODE:    return raft_ev_node(ev.node, !ev.up);
	case RAFT_EVT_LINK:    return raft_ev_link(ev.node, ev.bus, !ev.up);
	default:               return code;
	}
}

void netstate_local_view(const struct netstate *committed, unsigned self,
			 const bool heard[RAFT_N + 1][RAFT_NUM_BUSES],
			 const int health[RAFT_N + 1], struct netstate *out)
{
	*out = *committed;
	for (unsigned x = 1; x <= RAFT_N; x++) {
		for (unsigned b = 0; b < RAFT_NUM_BUSES; b++) {
			if (x == self) {
				out->link_up[x][b] = hears_bus(self, heard, b);
			} else if (health[x] >= 0) {
				out->link_up[x][b] = (health[x] >> b) & 1;
			}
		}
	}
}
