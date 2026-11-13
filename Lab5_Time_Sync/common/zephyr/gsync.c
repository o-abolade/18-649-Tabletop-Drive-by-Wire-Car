/* Lab 5 time sync on the STM32 zones: see gsync.h. The REF pairing rules
 * live in the portable refsync.c, shared with the Pi. */
#include <string.h>

#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/spinlock.h>
#include <zephyr/sys/clock.h>
#include <zephyr/sys/printk.h>

#include "can_link.h"
#include "gsync.h"
#include "gtime.h"
#include "refsync.h"

#define US_PER_S 1000000LL

static const struct gpio_dt_spec pps_pin = GPIO_DT_SPEC_GET(DT_ALIAS(pps_out), gpios);
static const struct gpio_dt_spec ref_rx_pin = GPIO_DT_SPEC_GET(DT_ALIAS(tp_ref_rx), gpios);
static const struct gpio_dt_spec leader_led = GPIO_DT_SPEC_GET(DT_ALIAS(led_leader), gpios);
static const struct gpio_dt_spec sync_led = GPIO_DT_SPEC_GET(DT_ALIAS(led_sync), gpios);

/* Guarded by `lock`: the stamp hooks run in the two MCP2515 threads, the
 * rest in app threads. */
static struct k_spinlock lock;
static struct refsync rs;

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

/* ---- Stamp hooks (MCP2515 driver threads) ---- */

static void on_rx_stamp(enum canp_bus bus, uint32_t id, const uint8_t data[CANP_DLC],
			uint64_t edge_cyc, bool edge_ok)
{
	bool sampled;
	enum gtime_result r = GTIME_IGNORED;
	int64_t now_global = 0;
	k_spinlock_key_t key = k_spin_lock(&lock);
	bool new_ref = refsync_rx(&rs, bus, id, data, (int64_t)k_cyc_to_us_floor64(edge_cyc),
				  edge_ok, &sampled, &r);
	bool set_clock = sampled && (r == GTIME_FIRST || r == GTIME_OK || r == GTIME_STEPPED);

	if (set_clock) {
		now_global = gtime_global(&rs.g, gsync_local_us());
	}
	k_spin_unlock(&lock, key);

	if (new_ref) {
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
	k_spinlock_key_t key = k_spin_lock(&lock);

	refsync_tx_done(&rs, bus, id, data, (int64_t)k_cyc_to_us_floor64(edge_cyc), edge_ok);
	k_spin_unlock(&lock, key);
}

/* ---- Leader ---- */

int gsync_send_ref(uint8_t d[RAFTP_DLC])
{
	uint8_t copy[CANP_NUM_BUSES][CANP_DLC];
	k_spinlock_key_t key = k_spin_lock(&lock);

	refsync_fill_ref(&rs, d, copy);
	k_spin_unlock(&lock, key);
	return can_link_send_each(raftp_id(RAFTP_REF, rs.self), copy);
}

void gsync_set_leader(unsigned leader)
{
	k_spinlock_key_t key = k_spin_lock(&lock);

	refsync_set_leader(&rs, leader);
	k_spin_unlock(&lock, key);
	gpio_pin_set_dt(&leader_led, leader == rs.self);
}

void gsync_set_relayed_buses(unsigned mask)
{
	k_spinlock_key_t key = k_spin_lock(&lock);

	rs.relayed_buses = mask;
	k_spin_unlock(&lock, key);
}

/* ---- Global time ---- */

int64_t global_gettime_us(void)
{
	int64_t local = gsync_local_us();
	k_spinlock_key_t key = k_spin_lock(&lock);
	int64_t gl = gtime_global(&rs.g, local);

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
	bool v = gtime_valid(&rs.g, local);

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

	*out = (struct gsync_stats){
		.ref_rx = rs.ref_rx, .stamp_rx_bad = rs.stamp_rx_bad,
		.stamp_tx_bad = rs.stamp_tx_bad, .samples = rs.g.samples,
		.outliers = rs.g.outliers, .steps = rs.g.steps,
		.ref_tx_valid = { rs.ref_tx_valid[0], rs.ref_tx_valid[1] },
	};
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
		int64_t g_now = gtime_global(&rs.g, now);

		/* Pick the next whole second, and pick again whenever global time
		 * stepped (first sync, new master) past it or back from it. A
		 * stale edge maps to a local time far in the past. */
		if (next < 0 || g_now - next > PPS_LATE_MAX_US || next - g_now > US_PER_S) {
			next = (floor_div(g_now, US_PER_S) + 1) * US_PER_S;
		}
		int64_t target = gtime_local_of(&rs.g, next);

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
			.drift_ppb = rs.g.drift_ppb,
			.corr_us = gtime_drift_corr_us(&rs.g, target),
			.last_err_us = rs.g.last_error_us,
			.late_us = (int32_t)k_cyc_to_us_floor64(edge_cyc - target_cyc),
			.leader = (uint8_t)rs.leader,
			.master = rs.g.master,
			.valid = gtime_valid(&rs.g, target),
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
			       rs.self, gs, ls, ppm, rec.corr_us, rec.last_err_us, rec.late_us,
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
		struct gtime snap = rs.g;
		uint32_t rx_bad = rs.stamp_rx_bad, tx_bad = rs.stamp_tx_bad;
		int32_t err_max = rs.err_max_us;
		int bus = rs.sync_bus;

		rs.err_max_us = 0;
		unsigned leader = rs.leader;

		k_spin_unlock(&lock, key);

		char raw[16];

		time_str(gs, sizeof(gs), gtime_global(&snap, local));
		ppm_str(ppm, sizeof(ppm), snap.drift_ppb);
		ppm_str(raw, sizeof(raw), snap.last_raw_ppb);
		printk("DRIFT N%u leader=N%u g=%s drift=%s ppm (last raw %s) corr now=%+d us "
		       "err@REF=%+d us (drift part %+d) samples=%u outliers=%u steps=%u "
		       "bad stamps rx=%u tx=%u | max|err| 1s=%d us bus=%c%s\n",
		       rs.self, leader, gs, ppm, raw, gtime_drift_corr_us(&snap, local),
		       snap.last_error_us, snap.last_drift_us, snap.samples, snap.outliers,
		       snap.steps, rx_bad, tx_bad, err_max,
		       bus < 0 ? '-' : 'A' + bus,
		       gtime_valid(&snap, local) ? "" : " NO-SYNC");
	}
}

K_THREAD_DEFINE(gsync_print_tid, 1536, print_thread, NULL, NULL, NULL, GSYNC_PRIO_PRINT, 0,
		SYS_FOREVER_MS);

int gsync_init(unsigned self)
{
	refsync_init(&rs, self, GSYNC_NET_DELAY_US, GSYNC_RELAY_DELAY_US);
	gpio_pin_configure_dt(&pps_pin, GPIO_OUTPUT_INACTIVE);
	gpio_pin_configure_dt(&ref_rx_pin, GPIO_OUTPUT_INACTIVE);
	gpio_pin_configure_dt(&leader_led, GPIO_OUTPUT_INACTIVE);
	gpio_pin_configure_dt(&sync_led, GPIO_OUTPUT_INACTIVE);
	can_link_set_stamp_hooks(on_rx_stamp, on_tx_stamp);
	k_thread_start(gsync_pps_tid);
	k_thread_start(gsync_print_tid);
	return 0;
}
