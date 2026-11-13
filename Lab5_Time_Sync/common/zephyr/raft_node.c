/* Lab 5 Raft on the STM32 zones: see raft_node.h. */
#include <string.h>

#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/spinlock.h>
#include <zephyr/sys/printk.h>

#include "can_link.h"
#include "gsync.h"
#include "netstate.h"
#include "raft.h"
#include "raft_node.h"
#include "zone_config.h"

#ifndef CONFIG_LAB5_RAFT_SEED
#define CONFIG_LAB5_RAFT_SEED RAFT_SEED
#endif

static const struct gpio_dt_spec boot_btn = GPIO_DT_SPEC_GET(DT_ALIAS(test_btn), gpios);

struct rframe {
	uint32_t id;
	uint8_t data[CANP_DLC];
};

K_MSGQ_DEFINE(raft_q, sizeof(struct rframe), 32, 4);

/* Owned by the RAFT thread */
static struct raft r;
static unsigned self;
static unsigned start_leader;   /* default_leader in use */
static int64_t boot_ms;
static int64_t last_report_ms[256];

/* Published by the RAFT thread for the RX path, RELAY and the console */
static struct k_spinlock lock;
static struct netstate pub_state;
static struct route_table rt_committed, rt_view;
static enum raft_role pub_role;
static unsigned pub_leader;
static uint8_t pub_term;
static uint16_t pub_commit;
static uint32_t pub_elections, dropped_frames;

/* Latest heartbeat health per node (RX path) */
static int64_t health_ms[RAFT_N + 1] = { -1, -1, -1, -1 };
static uint8_t health_bits[RAFT_N + 1];

/* ---- raft_io callbacks (RAFT thread) ---- */

static void io_send(void *ctx, uint32_t id, const uint8_t d[RAFTP_DLC])
{
	uint8_t copy[RAFTP_DLC];
	unsigned sender;

	ARG_UNUSED(ctx);
	memcpy(copy, d, RAFTP_DLC);
	if (raftp_classify(id, &sender) == RAFTP_REF) {
		gsync_send_ref(copy);   /* per-bus send times */
	} else {
		can_link_send(id, copy);
	}
}

static void io_apply(void *ctx, uint16_t index, uint8_t term, uint8_t event, bool replayed)
{
	char name[32];

	ARG_UNUSED(ctx);
	printk("COMMIT N%u #%u term %u %s%s\n", self, index, term,
	       raft_ev_name(event, name, sizeof(name)), replayed ? " (replayed)" : "");
}

static void io_snapshot(void *ctx, uint16_t index, const struct netstate *ns)
{
	char buf[64];

	ARG_UNUSED(ctx);
	printk("SNAPSHOT N%u installed at #%u: %s\n", self, index,
	       netstate_format(ns, buf, sizeof(buf)));
}

static void io_role(void *ctx, enum raft_role role, unsigned leader, uint8_t term)
{
	static enum raft_role last_role = (enum raft_role)-1;
	static unsigned last_leader;
	static uint8_t last_term;

	ARG_UNUSED(ctx);
	gsync_set_leader(leader);   /* time master = Raft leader; LEADER LED */
	if (role == last_role && leader == last_leader && term == last_term) {
		return;   /* a repeated pre-vote round */
	}
	last_role = role;
	last_leader = leader;
	last_term = term;
	printk("ROLE N%u: %s, term %u, leader %s\n", self, raft_role_name(role), term,
	       leader ? raft_node_name(leader) : "none");
}

static void io_debug(void *ctx, const char *line)
{
	ARG_UNUSED(ctx);
	printk("RAFT N%u: %s\n", self, line);
}

static const struct raft_io io = {
	.send = io_send,
	.apply = io_apply,
	.snapshot = io_snapshot,
	.role = io_role,
	.debug = io_debug,
};

/* ---- RX path (MCP2515 threads) ---- */

void raft_node_on_frame(uint32_t id, const uint8_t data[CANP_DLC], enum canp_bus bus)
{
	unsigned sender;

	ARG_UNUSED(bus);
	int zone = canp_hb_zone(id);

	if (zone >= 1 && zone <= RAFT_N) {
		k_spinlock_key_t key = k_spin_lock(&lock);

		health_ms[zone] = k_uptime_get();
		health_bits[zone] = canp_hb_health(data);
		k_spin_unlock(&lock, key);
		return;
	}
	/* A relayed copy of our own frame can come back: never hand it to
	 * Raft (raft.h: a node never receives its own frames) */
	if (raftp_classify(id, &sender) == RAFTP_NONE || sender == self) {
		return;
	}
	struct rframe f = { .id = id };

	memcpy(f.data, data, CANP_DLC);
	if (k_msgq_put(&raft_q, &f, K_NO_WAIT) != 0) {
		k_spinlock_key_t key = k_spin_lock(&lock);

		dropped_frames++;
		k_spin_unlock(&lock, key);
	}
}

bool raft_node_should_relay(uint32_t id, unsigned sender, enum canp_bus from_bus)
{
	unsigned s;
	bool ref = raftp_classify(id, &s) == RAFTP_REF;

	if (sender < 1 || sender > RAFT_N || sender == self) {
		return false;
	}
	k_spinlock_key_t key = k_spin_lock(&lock);
	/* REF is the one frame whose data differs per bus. Relay it only when
	 * the leader's latest heartbeat says it is off the other bus, so the
	 * relayed copy never meets the leader's own copy there (same ID,
	 * different data = bit errors, Lab 3). */
	bool relay = rt_view.relay[sender][from_bus] ||
		     (!ref && rt_committed.relay[sender][from_bus]);

	k_spin_unlock(&lock, key);
	return relay;
}

bool raft_node_failure_mode(void)
{
	k_spinlock_key_t key = k_spin_lock(&lock);
	bool fm = !pub_state.cockpit_ok;

	k_spin_unlock(&lock, key);
	return fm;
}

/* ---- RAFT thread ---- */

/* Client: report what we observe and the committed graph doesn't show */
static void observe(int64_t now, bool heard[RAFT_N + 1][RAFT_NUM_BUSES])
{
	uint8_t ev[NETSTATE_OBS_MAX];
	unsigned n = netstate_observe(self, heard, ev);

	for (unsigned i = 0; i < n; i++) {
		/* An opposite report still queued is no longer true */
		raft_client_withdraw(&r, raft_ev_opposite(ev[i]));
		if (!netstate_would_change(&r.state, ev[i]) || raft_client_pending(&r, ev[i]) ||
		    now - boot_ms < RAFT_NODE_BOOT_QUIET_MS ||
		    now - last_report_ms[ev[i]] < RAFT_NODE_REPORT_MS) {
			continue;
		}
		char name[32];

		printk("CLIENT N%u: report %s\n", self, raft_ev_name(ev[i], name, sizeof(name)));
		raft_client_report(&r, ev[i]);
		last_report_ms[ev[i]] = now;
	}
}

static void publish(int64_t now, const bool heard[RAFT_N + 1][RAFT_NUM_BUSES])
{
	int health[RAFT_N + 1];
	struct netstate committed, view;
	struct route_table rc, rv;

	raft_get_network(&r, &committed);

	k_spinlock_key_t key = k_spin_lock(&lock);

	for (unsigned x = 0; x <= RAFT_N; x++) {
		health[x] = (x >= 1 && health_ms[x] >= 0 &&
			     now - health_ms[x] <= ZONE_HB_TIMEOUT_MS) ? health_bits[x] : -1;
	}
	k_spin_unlock(&lock, key);

	netstate_local_view(&committed, self, heard, health, &view);
	netstate_routes(&committed, self, RAFT_RELAY_CAPABLE, &rc);
	netstate_routes(&view, self, RAFT_RELAY_CAPABLE, &rv);

	/* A bus the leader is off (by its heartbeats) only carries relayed REFs */
	unsigned relayed = 0;

	if (r.leader != 0 && r.leader != self) {
		for (unsigned b = 0; b < RAFT_NUM_BUSES; b++) {
			if (!view.link_up[r.leader][b]) {
				relayed |= 1u << b;
			}
		}
	}
	gsync_set_relayed_buses(relayed);

	key = k_spin_lock(&lock);
	pub_state = committed;
	rt_committed = rc;
	rt_view = rv;
	pub_role = r.role;
	pub_leader = r.leader;
	pub_term = r.term;
	pub_commit = r.commit;
	pub_elections = r.elections;
	k_spin_unlock(&lock, key);
}

static void raft_thread(void *a, void *b, void *c)
{
	ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
	int64_t next_obs = k_uptime_get();

	while (1) {
		struct rframe f;

		/* Frames as they come; a tick at least every 2 ms */
		while (k_msgq_get(&raft_q, &f, K_MSEC(RAFT_NODE_TICK_MS)) == 0) {
			raft_on_frame(&r, f.id, f.data, k_uptime_get());
		}
		int64_t now = k_uptime_get();

		raft_tick(&r, now);
		if (now < next_obs) {
			continue;
		}
		next_obs += RAFT_NODE_OBSERVE_MS;
		if (now > next_obs) {
			next_obs = now + RAFT_NODE_OBSERVE_MS;
		}

		bool heard[RAFT_N + 1][RAFT_NUM_BUSES] = { 0 };

		for (unsigned x = 1; x <= RAFT_N; x++) {
			for (unsigned bus = 0; bus < RAFT_NUM_BUSES; bus++) {
				int64_t t = can_link_last_hb_ms(x, bus);

				heard[x][bus] = x != self && t >= 0 && now - t <= ZONE_HB_TIMEOUT_MS;
			}
		}
		observe(now, heard);
		publish(now, heard);
	}
}

K_THREAD_DEFINE(raft_tid, 2048, raft_thread, NULL, NULL, NULL, ZONE_PRIO_RAFT, 0,
		SYS_FOREVER_MS);

void raft_node_init(enum canp_zone zone)
{
	self = zone;
	boot_ms = k_uptime_get();
	for (unsigned i = 0; i < ARRAY_SIZE(last_report_ms); i++) {
		last_report_ms[i] = -RAFT_NODE_REPORT_MS;
	}

	/* LEADER=cockpit or LEADER=random; B1 held at reset flips it */
	bool random_mode = IS_ENABLED(CONFIG_LAB5_LEADER_RANDOM);

	if (gpio_pin_get_dt(&boot_btn) > 0) {
		random_mode = !random_mode;
	}
	start_leader = random_mode ? RAFT_LEADER_NONE : RAFT_DEFAULT_LEADER;
	printk("RAFT N%u: LEADER=%s%s (seed 0x%x)\n", self, random_mode ? "random" : "cockpit",
	       gpio_pin_get_dt(&boot_btn) > 0 ? " (B1 held at boot)" : "",
	       (unsigned)CONFIG_LAB5_RAFT_SEED);

	gsync_init(self);
	raft_init_with_leader(&r, self, &io, k_uptime_get(), start_leader,
			      CONFIG_LAB5_RAFT_SEED);
	k_thread_start(raft_tid);
}

void raft_node_print_status(void)
{
	char net[64];

	k_spinlock_key_t key = k_spin_lock(&lock);
	struct netstate ns = pub_state;
	struct route_table rv = rt_view, rc = rt_committed;
	enum raft_role role = pub_role;
	unsigned leader = pub_leader, term = pub_term, commit = pub_commit;
	uint32_t elections = pub_elections, dropped = dropped_frames;

	k_spin_unlock(&lock, key);

	printk("   raft: %s term %u leader %s commit %u elections %u dropped %u | %s\n",
	       raft_role_name(role), term, leader ? raft_node_name(leader) : "none", commit,
	       elections, dropped, netstate_format(&ns, net, sizeof(net)));
	printk("   route: relay node committed %s%s, by heartbeats %s%s\n",
	       rc.relay_node ? raft_node_name(rc.relay_node) : "none",
	       rc.partitioned ? " (PARTITIONED)" : "",
	       rv.relay_node ? raft_node_name(rv.relay_node) : "none",
	       rv.partitioned ? " (PARTITIONED)" : "");
}
