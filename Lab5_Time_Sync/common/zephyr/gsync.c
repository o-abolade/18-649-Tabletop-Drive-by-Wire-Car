/* Lab 5 time sync on the STM32 zones: see gsync.h. */
#include <string.h>

#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/spinlock.h>
#include <zephyr/sys/clock.h>
#include <zephyr/sys/printk.h>

#include "can_link.h"
#include "gsync.h"
#include "gtime.h"

#define US_PER_S 1000000LL

static const struct gpio_dt_spec pps_pin = GPIO_DT_SPEC_GET(DT_ALIAS(pps_out), gpios);
static const struct gpio_dt_spec ref_rx_pin = GPIO_DT_SPEC_GET(DT_ALIAS(tp_ref_rx), gpios);
static const struct gpio_dt_spec leader_led = GPIO_DT_SPEC_GET(DT_ALIAS(led_leader), gpios);
static const struct gpio_dt_spec sync_led = GPIO_DT_SPEC_GET(DT_ALIAS(led_sync), gpios);

/* Everything below is guarded by `lock`: the stamp hooks run in the two
 * MCP2515 threads, the rest in app threads. */
static struct k_spinlock lock;
static struct gtime g;
static unsigned self_node;
static unsigned leader_node;
static struct gsync_stats stats;

/* Follower: the last REF copy received on each bus */
struct rx_slot {
	bool have;
	bool ok;
	uint8_t sender;
	uint8_t cycle;
	int64_t local_us;
};
static struct rx_slot rx_slot[CANP_NUM_BUSES];
static uint8_t toggle_sender, toggle_cycle;   /* REF_RX: once per REF, not per copy */

/* Samples come from one bus at a time. The two MCP2515 INT edges of one REF
 * land microseconds apart and their ISRs run one after the other, so the two
 * buses' pairs differ by a few us; mixing them made the anchor saw-tooth
 * (err@REF about -10 us every REF on the bench) and the drift baseline
 * noisy. Switch bus only when the current one gave no sample for this long. */
#define SYNC_BUS_SWITCH_US (2 * RAFT_HEARTBEAT_MS * 1000LL)
static int sync_bus = -1;
static int64_t sync_bus_last_us;
static int32_t err_max_us;      /* worst |err@REF| since the last DRIFT line */

/* Leader: when our last REF finished on each bus */
struct tx_slot {
	bool have;
	bool ok;
	uint8_t cycle;
	int64_t local_us;
};
static struct tx_slot tx_slot[CANP_NUM_BUSES];

int64_t gsync_local_us(void)
{
	return (int64_t)k_cyc_to_us_floor64(k_cycle_get_64());
}

static int64_t floor_div(int64_t a, int64_t b)
{
	int64_t q = a / b;

	return (a % b != 0 && (a < 0) != (b < 0)) ? q - 1 : q;
}

static void to_timespec(int64_t global_us, struct timespec *ts)
{
	if (global_us < 0) {
		global_us = gtime_wire(global_us);
	}
	ts->tv_sec = (time_t)(global_us / US_PER_S);
	ts->tv_nsec = (long)(global_us % US_PER_S) * 1000L;
}

static void put_u32(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
	p[2] = (uint8_t)(v >> 16);
	p[3] = (uint8_t)(v >> 24);
}

/* ---- Stamp hooks (MCP2515 driver threads) ---- */

static void on_rx_stamp(enum canp_bus bus, uint32_t id, const uint8_t data[CANP_DLC],
			uint64_t edge_cyc, bool edge_ok)
{
	unsigned sender;
	struct raftp_ref m;

	if (raftp_classify(id, &sender) != RAFTP_REF || !raftp_decode_ref(data, &m)) {
		return;
	}

	int64_t local = (int64_t)k_cyc_to_us_floor64(edge_cyc);
	bool toggle = false, set_clock = false;
	int64_t now_global = 0;
	k_spinlock_key_t key = k_spin_lock(&lock);
	struct rx_slot *p = &rx_slot[bus];

	stats.ref_rx++;
	if (!edge_ok) {
		stats.stamp_rx_bad++;
	}
	if (sender != toggle_sender || m.cycle != toggle_cycle) {
		toggle_sender = (uint8_t)sender;
		toggle_cycle = m.cycle;
		toggle = true;
	}

	/* REF k carries the send time of REF k-1 on this bus */
	if (sender == leader_node && sender != self_node && !g.master &&
	    (m.flags & RAFTP_REF_TIME_VALID) && p->have && p->ok && p->sender == sender &&
	    (uint8_t)(p->cycle + 1) == m.cycle &&
	    (sync_bus < 0 || sync_bus == (int)bus ||
	     local - sync_bus_last_us > SYNC_BUS_SWITCH_US)) {
		enum gtime_result r = gtime_sample(&g, m.prev_send_us, p->local_us,
						   GSYNC_NET_DELAY_US);

		sync_bus = bus;
		sync_bus_last_us = local;
		if (r == GTIME_OK) {
			int32_t e = g.last_error_us < 0 ? -g.last_error_us : g.last_error_us;

			if (e > err_max_us) {
				err_max_us = e;
			}
		}
		stats.samples = g.samples;
		stats.outliers = g.outliers;
		stats.steps = g.steps;
		if (r == GTIME_FIRST || r == GTIME_OK || r == GTIME_STEPPED) {
			set_clock = true;
			now_global = gtime_global(&g, gsync_local_us());
		}
	}
	*p = (struct rx_slot){ .have = true, .ok = edge_ok, .sender = (uint8_t)sender,
			       .cycle = m.cycle, .local_us = local };
	k_spin_unlock(&lock, key);

	if (toggle) {
		gpio_pin_toggle_dt(&ref_rx_pin);
	}
	if (set_clock) {
		/* Handout 6.1: the Zephyr system time follows the leader */
		struct timespec ts;

		to_timespec(now_global, &ts);
		(void)sys_clock_settime(SYS_CLOCK_REALTIME, &ts);
	}
}

static void on_tx_stamp(enum canp_bus bus, uint32_t id, const uint8_t data[CANP_DLC],
			uint64_t edge_cyc, bool edge_ok)
{
	unsigned sender;

	if (raftp_classify(id, &sender) != RAFTP_REF || sender != self_node) {
		return;
	}

	k_spinlock_key_t key = k_spin_lock(&lock);

	if (!edge_ok) {
		stats.stamp_tx_bad++;
	}
	tx_slot[bus] = (struct tx_slot){ .have = true, .ok = edge_ok, .cycle = data[2],
					 .local_us = (int64_t)k_cyc_to_us_floor64(edge_cyc) };
	k_spin_unlock(&lock, key);
}

/* ---- Leader ---- */

int gsync_send_ref(uint8_t d[RAFTP_DLC])
{
	uint8_t copy[CANP_NUM_BUSES][CANP_DLC];
	uint8_t cycle = d[2];
	k_spinlock_key_t key = k_spin_lock(&lock);

	for (int bus = 0; bus < CANP_NUM_BUSES; bus++) {
		const struct tx_slot *t = &tx_slot[bus];

		memcpy(copy[bus], d, CANP_DLC);
		if (g.master && t->have && t->ok && (uint8_t)(t->cycle + 1) == cycle) {
			put_u32(&copy[bus][3], gtime_wire(gtime_global(&g, t->local_us)));
			copy[bus][7] |= RAFTP_REF_TIME_VALID;
			stats.ref_tx_valid[bus]++;
		} else {
			put_u32(&copy[bus][3], 0);
			copy[bus][7] &= (uint8_t)~RAFTP_REF_TIME_VALID;
		}
	}
	k_spin_unlock(&lock, key);

	return can_link_send_each(raftp_id(RAFTP_REF, self_node), copy);
}

void gsync_set_leader(unsigned leader)
{
	k_spinlock_key_t key = k_spin_lock(&lock);

	if (leader == self_node && !g.master) {
		gtime_become_master(&g);
	} else if (leader != self_node && g.master) {
		gtime_become_follower(&g);
	}
	if (leader != leader_node) {
		sync_bus = -1;
	}
	leader_node = leader;
	k_spin_unlock(&lock, key);

	gpio_pin_set_dt(&leader_led, leader == self_node);
}

/* ---- Global time ---- */

int64_t global_gettime_us(void)
{
	int64_t local = gsync_local_us();
	k_spinlock_key_t key = k_spin_lock(&lock);
	int64_t gl = gtime_global(&g, local);

	k_spin_unlock(&lock, key);
	return gl;
}

int global_gettime(struct timespec *ts)
{
	to_timespec(global_gettime_us(), ts);
	return 0;
}

bool gsync_valid(void)
{
	int64_t local = gsync_local_us();
	k_spinlock_key_t key = k_spin_lock(&lock);
	bool v = gtime_valid(&g, local);

	k_spin_unlock(&lock, key);
	return v;
}

void gsync_poll(void)
{
	gpio_pin_set_dt(&sync_led, gsync_valid());
}

void gsync_get_stats(struct gsync_stats *out)
{
	k_spinlock_key_t key = k_spin_lock(&lock);

	*out = stats;
	k_spin_unlock(&lock, key);
}

/* ---- PPS (handout 6.4) ---- */

struct pps_rec {
	int64_t global_us;     /* the whole second the edge marks */
	int64_t local_us;      /* local clock at the edge */
	int32_t drift_ppb;
	int32_t corr_us;       /* drift correction since the last REF */
	int32_t last_err_us;   /* error removed at the last REF */
	int32_t late_us;       /* edge later than its target (busy-wait overrun) */
	uint8_t leader;
	bool master;
	bool valid;
};

K_MSGQ_DEFINE(pps_q, sizeof(struct pps_rec), 4, 8);

/* Sleep at most this long at a time while waiting for an edge */
#define PPS_SLICE_US     100000
/* An edge we are later than this for is skipped, not fired late */
#define PPS_LATE_MAX_US  1000

static void pps_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);
	int64_t next = -1;   /* global time of the next edge */

	while (1) {
		int64_t now = gsync_local_us();
		k_spinlock_key_t key = k_spin_lock(&lock);
		int64_t g_now = gtime_global(&g, now);

		/* Pick the next whole second, and pick again whenever global time
		 * stepped (first sync, new master) past it or back from it. A
		 * stale edge maps to a local time far in the past. */
		if (next < 0 || g_now - next > PPS_LATE_MAX_US || next - g_now > US_PER_S) {
			next = (floor_div(g_now, US_PER_S) + 1) * US_PER_S;
		}
		int64_t target = gtime_local_of(&g, next);

		k_spin_unlock(&lock, key);

		/* Sleep to just before the edge, in slices so a REF that moves
		 * global time is noticed */
		if (target - now > GSYNC_PPS_WAKE_EARLY_US) {
			k_sleep(K_USEC(MIN(target - now - GSYNC_PPS_WAKE_EARLY_US, PPS_SLICE_US)));
			continue;
		}

		/* Busy-wait the last <= GSYNC_PPS_WAKE_EARLY_US (or not at all if
		 * already up to PPS_LATE_MAX_US late) */
		uint64_t target_cyc = k_us_to_cyc_ceil64((uint64_t)MAX(target, 0));

		while (k_cycle_get_64() < target_cyc) {
		}
		gpio_pin_set_dt(&pps_pin, 1);
		uint64_t edge_cyc = k_cycle_get_64();

		struct pps_rec rec;

		key = k_spin_lock(&lock);
		rec = (struct pps_rec){
			.global_us = next,
			.local_us = target,
			.drift_ppb = g.drift_ppb,
			.corr_us = gtime_drift_corr_us(&g, target),
			.last_err_us = g.last_error_us,
			.late_us = (int32_t)k_cyc_to_us_floor64(edge_cyc - target_cyc),
			.leader = (uint8_t)leader_node,
			.master = g.master,
			.valid = gtime_valid(&g, target),
		};
		k_spin_unlock(&lock, key);
		(void)k_msgq_put(&pps_q, &rec, K_NO_WAIT);
		next += US_PER_S;

		k_sleep(K_MSEC(GSYNC_PPS_WIDTH_MS));
		gpio_pin_set_dt(&pps_pin, 0);
	}
}

K_THREAD_DEFINE(gsync_pps_tid, 1024, pps_thread, NULL, NULL, NULL, GSYNC_PRIO_PPS, 0,
		SYS_FOREVER_MS);

/* ---- Console ---- */

/* "+12.345" from a ppb value, as ppm */
static void ppm_str(char *buf, size_t n, int32_t ppb)
{
	uint32_t a = (uint32_t)(ppb < 0 ? -(int64_t)ppb : ppb);

	snprintk(buf, n, "%c%u.%03u", ppb < 0 ? '-' : '+', a / 1000, a % 1000);
}

static void time_str(char *buf, size_t n, int64_t us)
{
	if (us < 0) {
		us = gtime_wire(us);
	}
	snprintk(buf, n, "%u.%06u", (uint32_t)(us / US_PER_S), (uint32_t)(us % US_PER_S));
}

static void print_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);
	int64_t next_drift = k_uptime_get() + GSYNC_DRIFT_PRINT_MS / 2;

	while (1) {
		struct pps_rec rec;
		int64_t wait = next_drift - k_uptime_get();
		char gs[24], ls[24], ppm[16];

		if (k_msgq_get(&pps_q, &rec, K_MSEC(wait > 0 ? wait : 0)) == 0) {
			/* Handout 6.4: global and local time at the edge, the drift
			 * ratio, and the correction applied since the last REF */
			time_str(gs, sizeof(gs), rec.global_us);
			time_str(ls, sizeof(ls), rec.local_us);
			ppm_str(ppm, sizeof(ppm), rec.drift_ppb);
			printk("PPS N%u g=%s local=%s drift=%s ppm corr=%+d us err@REF=%+d us "
			       "late=%d us %s%s\n",
			       self_node, gs, ls, ppm, rec.corr_us, rec.last_err_us, rec.late_us,
			       rec.master ? "MASTER" : "follower", rec.valid ? "" : " NO-SYNC");
			continue;
		}
		if (k_uptime_get() < next_drift) {
			continue;
		}
		next_drift += GSYNC_DRIFT_PRINT_MS;

		/* Handout 6.2: the drift error currently being corrected */
		int64_t local = gsync_local_us();
		k_spinlock_key_t key = k_spin_lock(&lock);
		struct gtime snap = g;
		struct gsync_stats st = stats;
		int32_t err_max = err_max_us;
		int bus = sync_bus;

		err_max_us = 0;
		unsigned leader = leader_node;

		k_spin_unlock(&lock, key);

		char raw[16];

		time_str(gs, sizeof(gs), gtime_global(&snap, local));
		ppm_str(ppm, sizeof(ppm), snap.drift_ppb);
		ppm_str(raw, sizeof(raw), snap.last_raw_ppb);
		printk("DRIFT N%u leader=N%u g=%s drift=%s ppm (last raw %s) corr now=%+d us "
		       "err@REF=%+d us (drift part %+d) samples=%u outliers=%u steps=%u "
		       "bad stamps rx=%u tx=%u | max|err| 1s=%d us bus=%c%s\n",
		       self_node, leader, gs, ppm, raw, gtime_drift_corr_us(&snap, local),
		       snap.last_error_us, snap.last_drift_us, snap.samples, snap.outliers,
		       snap.steps, st.stamp_rx_bad, st.stamp_tx_bad, err_max,
		       bus < 0 ? '-' : 'A' + bus,
		       gtime_valid(&snap, local) ? "" : " NO-SYNC");
	}
}

K_THREAD_DEFINE(gsync_print_tid, 1536, print_thread, NULL, NULL, NULL, GSYNC_PRIO_PRINT, 0,
		SYS_FOREVER_MS);

int gsync_init(unsigned self)
{
	self_node = self;
	gtime_init(&g);
	gpio_pin_configure_dt(&pps_pin, GPIO_OUTPUT_INACTIVE);
	gpio_pin_configure_dt(&ref_rx_pin, GPIO_OUTPUT_INACTIVE);
	gpio_pin_configure_dt(&leader_led, GPIO_OUTPUT_INACTIVE);
	gpio_pin_configure_dt(&sync_led, GPIO_OUTPUT_INACTIVE);
	can_link_set_stamp_hooks(on_rx_stamp, on_tx_stamp);
	k_thread_start(gsync_pps_tid);
	k_thread_start(gsync_print_tid);
	return 0;
}
