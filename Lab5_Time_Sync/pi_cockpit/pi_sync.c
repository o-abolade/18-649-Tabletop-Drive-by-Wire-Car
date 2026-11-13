/* Lab 5 time sync on the Pi: see pi_sync.h. */
#define _GNU_SOURCE
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <string.h>

#include "pi_sync.h"
#include "refsync.h"

#define US_PER_S 1000000LL
#define PPS_SLICE_US    100000
#define PPS_LATE_MAX_US 1000

static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static struct refsync rs;          /* guarded by mu */
static pi_sync_pin_fn set_pin;

static int64_t ts_us(const struct timespec *ts)
{
	return (int64_t)ts->tv_sec * US_PER_S + ts->tv_nsec / 1000;
}

int64_t pi_local_us(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
	return ts_us(&ts);
}

int64_t pi_stamp_to_local_us(const struct timespec *ts)
{
	struct timespec real, raw;

	/* The offset between the two clocks moves only by NTP's slew (ppm), so
	 * converting within milliseconds of the stamp is exact to well under a
	 * microsecond */
	clock_gettime(CLOCK_REALTIME, &real);
	clock_gettime(CLOCK_MONOTONIC_RAW, &raw);
	return ts_us(ts) - (ts_us(&real) - ts_us(&raw));
}

static int64_t floor_div(int64_t a, int64_t b)
{
	int64_t q = a / b;

	return (a % b != 0 && (a < 0) != (b < 0)) ? q - 1 : q;
}

void pi_sync_rx(unsigned bus, uint32_t id, const uint8_t d[RAFTP_DLC], int64_t local_us)
{
	bool sampled;
	enum gtime_result res;

	pthread_mutex_lock(&mu);
	bool new_ref = refsync_rx(&rs, bus, id, d, local_us, true, &sampled, &res);
	static bool toggle;

	if (new_ref) {
		toggle = !toggle;
	}
	bool level = toggle;

	pthread_mutex_unlock(&mu);
	if (new_ref) {
		set_pin(PI_PIN_REF_RX, level);
	}
}

void pi_sync_tx_done(unsigned bus, uint32_t id, const uint8_t d[RAFTP_DLC], int64_t local_us)
{
	pthread_mutex_lock(&mu);
	refsync_tx_done(&rs, bus, id, d, local_us, true);
	pthread_mutex_unlock(&mu);
}

void pi_sync_fill_ref(const uint8_t d[RAFTP_DLC], uint8_t out[RAFT_NUM_BUSES][RAFTP_DLC])
{
	pthread_mutex_lock(&mu);
	refsync_fill_ref(&rs, d, out);
	pthread_mutex_unlock(&mu);
}

void pi_sync_set_leader(unsigned leader)
{
	pthread_mutex_lock(&mu);
	refsync_set_leader(&rs, leader);
	unsigned self = rs.self;

	pthread_mutex_unlock(&mu);
	set_pin(PI_PIN_LEADER, leader == self);
}

void pi_sync_set_relayed_buses(unsigned mask)
{
	pthread_mutex_lock(&mu);
	rs.relayed_buses = mask;
	pthread_mutex_unlock(&mu);
}

int64_t global_gettime_us(void)
{
	int64_t local = pi_local_us();

	pthread_mutex_lock(&mu);
	int64_t g = gtime_global(&rs.g, local);

	pthread_mutex_unlock(&mu);
	return g;
}

bool pi_sync_valid(void)
{
	int64_t local = pi_local_us();

	pthread_mutex_lock(&mu);
	bool v = gtime_valid(&rs.g, local);

	pthread_mutex_unlock(&mu);
	return v;
}

/* ---- Console ---- */

static void ppm_str(char *buf, size_t n, int32_t ppb)
{
	long long a = ppb < 0 ? -(long long)ppb : ppb;

	snprintf(buf, n, "%c%lld.%03lld", ppb < 0 ? '-' : '+', a / 1000, a % 1000);
}

static void time_str(char *buf, size_t n, int64_t us)
{
	if (us < 0) {
		us = gtime_wire(us);
	}
	snprintf(buf, n, "%lld.%06lld", (long long)(us / US_PER_S), (long long)(us % US_PER_S));
}

void pi_sync_print_drift(void)
{
	int64_t local = pi_local_us();
	char gs[32], ppm[24], raw[24];

	pthread_mutex_lock(&mu);
	struct gtime g = rs.g;
	int32_t err_max = rs.err_max_us;
	int bus = rs.sync_bus;
	unsigned leader = rs.leader, self = rs.self;
	uint32_t rx = rs.ref_rx;

	rs.err_max_us = 0;
	pthread_mutex_unlock(&mu);

	time_str(gs, sizeof(gs), gtime_global(&g, local));
	ppm_str(ppm, sizeof(ppm), g.drift_ppb);
	ppm_str(raw, sizeof(raw), g.last_raw_ppb);
	printf("DRIFT N%u leader=N%u g=%s drift=%s ppm (last raw %s) corr now=%+d us "
	       "err@REF=%+d us samples=%u outliers=%u steps=%u ref copies=%u | "
	       "max|err| 1s=%d us bus=%c%s\n",
	       self, leader, gs, ppm, raw, gtime_drift_corr_us(&g, local), g.last_error_us,
	       g.samples, g.outliers, g.steps, rx, err_max, bus < 0 ? '-' : 'A' + bus,
	       gtime_valid(&g, local) ? "" : " NO-SYNC");
}

static void sleep_us(int64_t us);

void pi_sync_wait_until(int64_t global)
{
	int64_t target;

	for (;;) {
		int64_t now = pi_local_us();

		pthread_mutex_lock(&mu);
		target = gtime_local_of(&rs.g, global);
		pthread_mutex_unlock(&mu);
		if (target - now <= PI_SYNC_PPS_WAKE_EARLY_US) {
			break;
		}
		int64_t t = target - now - PI_SYNC_PPS_WAKE_EARLY_US;

		sleep_us(t < PPS_SLICE_US ? t : PPS_SLICE_US);
	}
	while (pi_local_us() < target) {
	}
}

/* ---- PPS (handout 6.4) ---- */

static void sleep_us(int64_t us)
{
	struct timespec ts = { .tv_sec = us / US_PER_S, .tv_nsec = (us % US_PER_S) * 1000 };

	clock_nanosleep(CLOCK_MONOTONIC, 0, &ts, NULL);
}

static void *pps_thread(void *arg)
{
	(void)arg;
	int64_t next = -1;

	for (;;) {
		int64_t now = pi_local_us();

		pthread_mutex_lock(&mu);
		int64_t g_now = gtime_global(&rs.g, now);

		if (next < 0 || g_now - next > PPS_LATE_MAX_US || next - g_now > US_PER_S) {
			next = (floor_div(g_now, US_PER_S) + 1) * US_PER_S;
		}
		int64_t target = gtime_local_of(&rs.g, next);

		pthread_mutex_unlock(&mu);

		if (target - now > PI_SYNC_PPS_WAKE_EARLY_US) {
			int64_t t = target - now - PI_SYNC_PPS_WAKE_EARLY_US;

			sleep_us(t < PPS_SLICE_US ? t : PPS_SLICE_US);
			continue;
		}
		while (pi_local_us() < target) {
		}
		set_pin(PI_PIN_PPS, true);
		int64_t edge = pi_local_us();

		pthread_mutex_lock(&mu);
		struct gtime g = rs.g;
		unsigned self = rs.self;

		pthread_mutex_unlock(&mu);

		char gs[32], ls[32], ppm[24];

		time_str(gs, sizeof(gs), next);
		time_str(ls, sizeof(ls), target);
		ppm_str(ppm, sizeof(ppm), g.drift_ppb);
		printf("PPS N%u g=%s local=%s drift=%s ppm corr=%+d us err@REF=%+d us late=%lld us "
		       "%s%s\n",
		       self, gs, ls, ppm, gtime_drift_corr_us(&g, target), g.last_error_us,
		       (long long)(edge - target), g.master ? "MASTER" : "follower",
		       gtime_valid(&g, target) ? "" : " NO-SYNC");
		next += US_PER_S;
		sleep_us(PI_SYNC_PPS_WIDTH_MS * 1000);
		set_pin(PI_PIN_PPS, false);
	}
	return NULL;
}

void pi_sync_init(unsigned self, pi_sync_pin_fn pin_fn, bool realtime_pps)
{
	pthread_t tid;
	pthread_attr_t attr;

	refsync_init(&rs, self, PI_SYNC_NET_DELAY_US, PI_SYNC_RELAY_DELAY_US);
	set_pin = pin_fn;
	pthread_attr_init(&attr);
	if (realtime_pps) {
		struct sched_param sp = { .sched_priority = 85 };
		cpu_set_t set;

		/* Above the cockpit loop (80), on the core the loop doesn't use */
		pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
		pthread_attr_setschedpolicy(&attr, SCHED_FIFO);
		pthread_attr_setschedparam(&attr, &sp);
		CPU_ZERO(&set);
		CPU_SET(2, &set);
		pthread_attr_setaffinity_np(&attr, sizeof(set), &set);
	}
	if (pthread_create(&tid, &attr, pps_thread, NULL) != 0) {
		perror("pi_sync: PPS thread (SCHED_FIFO needs root)");
		if (realtime_pps) {
			pthread_attr_destroy(&attr);
			pthread_attr_init(&attr);
			pthread_create(&tid, &attr, pps_thread, NULL);
		}
	}
	pthread_attr_destroy(&attr);
}
