/*
 * Host tests: global time (gtime.c).
 *
 * A simulated master and followers whose clocks run at different rates
 * (crystal drift, in ppb) exchange REF pairs every 20 ms with a few us of
 * timestamp jitter, the way the MCP2515 interrupt stamps behave.
 */
#include "check.h"
#include "gtime.h"

#define REF_US      (RAFT_HEARTBEAT_MS * 1000LL)
#define WIRE_US     2      /* end of frame at the sender vs. at the receiver */

struct clk {
	int64_t offset;   /* reading at true time 0 */
	int64_t ppb;      /* rate error */
};

static int64_t clk_read(const struct clk *c, int64_t t)
{
	return c->offset + t + t * c->ppb / 1000000000LL;
}

static uint32_t rng = 12345;

/* Receive-stamp jitter, 0..4 us */
static int64_t jitter(void)
{
	rng = rng * 1103515245u + 12345u;
	return (rng >> 16) % 5;
}

static int64_t abs64(int64_t v)
{
	return v < 0 ? -v : v;
}

/* |a - b| modulo the REF wrap: nodes agree on time modulo 4000 s */
static int64_t wrap_err(int64_t a, int64_t b)
{
	int64_t d = (a - b) % GTIME_WIRE_MOD_US;

	if (d >= GTIME_WIRE_MOD_US / 2) {
		d -= GTIME_WIRE_MOD_US;
	} else if (d < -GTIME_WIRE_MOD_US / 2) {
		d += GTIME_WIRE_MOD_US;
	}
	return abs64(d);
}

/* Follower `fc` with state `g` follows master time `mtime(t)` from true time
 * t0 to t1: REF k is sent at t, stamped by the master at m(t) and by the
 * follower at its receive time; the pair is used when REF k+1 arrives.
 * Returns the worst |error| of global time seen at mid-period instants
 * after `settle_us`. */
typedef int64_t (*master_fn)(int64_t t);

static int64_t follow(struct gtime *g, const struct clk *fc, master_fn mtime, int64_t t0,
		      int64_t t1, int64_t settle_us)
{
	int64_t worst = 0;
	bool have_prev = false;
	uint32_t prev_master = 0;
	int64_t prev_rx = 0;

	for (int64_t t = t0; t < t1; t += REF_US) {
		int64_t rx = clk_read(fc, t + WIRE_US) + jitter();

		if (have_prev) {
			gtime_sample(g, prev_master, prev_rx, WIRE_US);
		}
		prev_master = gtime_wire(mtime(t));
		prev_rx = rx;
		have_prev = true;

		/* Check halfway to the next REF, the worst point between syncs */
		int64_t tm = t + REF_US / 2;

		if (t - t0 >= settle_us) {
			int64_t e = wrap_err(gtime_global(g, clk_read(fc, tm)), mtime(tm));

			if (e > worst) {
				worst = e;
			}
		}
	}
	return worst;
}

static const struct clk master_clk = { .offset = 1234567, .ppb = 40000 };   /* +40 ppm */

static int64_t master_time(int64_t t)
{
	return clk_read(&master_clk, t);
}

static void test_wire(void)
{
	CHECK_EQ(gtime_wire(0), 0);
	CHECK_EQ(gtime_wire(GTIME_WIRE_MOD_US + 5), 5);
	CHECK_EQ(gtime_wire(-1), GTIME_WIRE_MOD_US - 1);
	/* The wrap is a whole number of seconds, so PPS edges agree across it */
	CHECK_EQ(GTIME_WIRE_MOD_US % 1000000, 0);
	CHECK(GTIME_WIRE_MOD_US <= 0xFFFFFFFFLL);
}

static void test_lock_and_drift(void)
{
	struct gtime g;
	struct clk fc = { .offset = 999, .ppb = -30000 };   /* -30 ppm */

	gtime_init(&g);
	CHECK(!gtime_valid(&g, 0));
	/* Before the first sample, global time is local time */
	CHECK_EQ(gtime_global(&g, 777), 777);

	int64_t worst = follow(&g, &fc, master_time, 0, 5000000, 2000000);

	CHECK(worst <= 10);
	if (worst > 10) {
		printf("  worst error %lld us\n", (long long)worst);
	}
	CHECK(g.drift_valid);
	/* (1 + 40 ppm) / (1 - 30 ppm) = 1 + 70.002 ppm */
	CHECK(abs64(g.drift_ppb - 70002) <= 3000);
	CHECK_EQ(g.steps, 0);
	CHECK_EQ(g.outliers, 0);
	CHECK(gtime_valid(&g, g.last_sample_local + GTIME_VALID_US));
	CHECK(!gtime_valid(&g, g.last_sample_local + GTIME_VALID_US + 1));

	/* Holdover: one second with no REF (an election). The drift ratio keeps
	 * the error small; without it the error would be 70 us. */
	int64_t t_end = 5000000 + 1000000;
	int64_t local_end = clk_read(&fc, t_end);
	int64_t e = abs64(gtime_global(&g, local_end) - master_time(t_end));

	CHECK(e <= 10);
	CHECK(abs64(gtime_drift_corr_us(&g, local_end) - 70) <= 5);
}

static void test_inverse(void)
{
	struct gtime g;
	struct clk fc = { .offset = 50, .ppb = 25000 };

	gtime_init(&g);
	follow(&g, &fc, master_time, 0, 3000000, 0);
	for (int64_t gl = 1000000; gl < 20000000; gl += 777777) {
		int64_t l = gtime_local_of(&g, gl);

		CHECK(abs64(gtime_global(&g, l) - gl) <= 1);
	}
}

static void test_outlier_and_step(void)
{
	struct gtime g;
	struct clk fc = { .offset = 0, .ppb = 0 };

	gtime_init(&g);
	follow(&g, &fc, master_time, 0, 2000000, 0);

	int64_t t = 2000000, rx = clk_read(&fc, t);
	uint32_t m = gtime_wire(master_time(t));

	/* One late interrupt (stamp 300 us late) is ignored */
	CHECK_EQ(gtime_sample(&g, m, rx + 300, 0), GTIME_OUTLIER);
	CHECK_EQ(gtime_sample(&g, m, rx, 0), GTIME_OK);
	CHECK(abs64(gtime_global(&g, rx) - master_time(t)) <= 5);

	/* A master 2 s away (another time base): stepped on the third sample */
	for (int i = 0; i < GTIME_OUTLIER_RUN; i++) {
		t += REF_US;
		rx = clk_read(&fc, t);
		m = gtime_wire(master_time(t) + 2000000);
		CHECK_EQ(gtime_sample(&g, m, rx, 0),
			 i < GTIME_OUTLIER_RUN - 1 ? GTIME_OUTLIER : GTIME_STEPPED);
	}
	CHECK_EQ(g.steps, 1);
	CHECK(abs64(gtime_global(&g, rx) - (master_time(t) + 2000000)) <= 1);

	/* The master ignores samples */
	gtime_become_master(&g);
	CHECK_EQ(gtime_sample(&g, 0, rx, 0), GTIME_IGNORED);
	CHECK(gtime_valid(&g, rx + 100000000));
}

/* Leader handover (checkoff 6): followers A and B follow M; M stops; after an
 * election gap A becomes master. B must see no step. */
static struct gtime *handover_a;
static const struct clk a_clk = { .offset = 70000, .ppb = -30000 };

static int64_t a_time(int64_t t)
{
	return gtime_global(handover_a, clk_read(&a_clk, t));
}

static void test_handover(void)
{
	struct gtime a, b;
	struct clk b_clk = { .offset = 5, .ppb = 10000 };

	gtime_init(&a);
	gtime_init(&b);
	follow(&a, &a_clk, master_time, 0, 4000000, 0);
	follow(&b, &b_clk, master_time, 0, 4000000, 0);

	/* M fails at 4 s; A wins the election 150 ms later */
	int64_t t_up = 4000000 + 150000;
	int64_t la = clk_read(&a_clk, t_up);
	int64_t before = gtime_global(&a, la);

	gtime_become_master(&a);
	CHECK_EQ(gtime_global(&a, la), before);   /* no step at the new master */

	/* Both free-ran through the gap: they still agree within a few us */
	int64_t gap = gtime_global(&b, clk_read(&b_clk, t_up)) - before;

	CHECK(abs64(gap) <= 10);

	/* B now follows A: no step, error stays small */
	handover_a = &a;
	uint32_t steps = b.steps, outliers = b.outliers;
	int64_t worst = follow(&b, &b_clk, a_time, t_up, t_up + 2000000, 0);

	CHECK(worst <= 10);
	CHECK_EQ(b.steps, steps);
	CHECK_EQ(b.outliers, outliers);

	/* A former master that follows again is compared with the new master,
	 * not re-locked: its first sample reports how far it drifted */
	struct gtime m;

	gtime_init(&m);
	gtime_become_master(&m);
	gtime_become_follower(&m);
	CHECK_EQ(gtime_sample(&m, 1000040, 1000000, 0), GTIME_OK);
	CHECK_EQ(m.last_error_us, 40);
}

/* Master time crosses the REF wrap (4000 s) mid-run: no step, no outlier */
static const struct clk wrap_clk = { .offset = GTIME_WIRE_MOD_US - 1000000, .ppb = 0 };

static int64_t wrap_time(int64_t t)
{
	return clk_read(&wrap_clk, t);
}

static void test_wrap(void)
{
	struct gtime g;
	struct clk fc = { .offset = 123, .ppb = 15000 };

	gtime_init(&g);
	int64_t worst = follow(&g, &fc, wrap_time, 0, 3000000, 100000);

	CHECK(worst <= 10);
	CHECK_EQ(g.steps, 0);
	CHECK_EQ(g.outliers, 0);
}

void test_gtime_all(void)
{
	RUN(test_wire);
	RUN(test_lock_and_drift);
	RUN(test_inverse);
	RUN(test_outlier_and_step);
	RUN(test_handover);
	RUN(test_wrap);
}
