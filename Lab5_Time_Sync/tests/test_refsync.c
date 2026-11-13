/*
 * Host tests: REF time exchange (refsync.c), with the situations found on
 * the bench: a late relayed copy, one bus stopping, a relayed-only bus.
 */
#include "check.h"
#include "refsync.h"

#define L 1                 /* leader (cockpit) */
#define F 2                 /* follower */
#define F_OFFSET 5000000    /* follower clock = leader clock + 5 s */
#define BUS_B_LAG 5         /* bus B copy ends 5 us after bus A's */

static struct refsync lead, fol;
static uint8_t cycle;

static int64_t abs64(int64_t v)
{
	return v < 0 ? -v : v;
}

/* One REF at leader time t: leader sends, follower receives on the buses in
 * `buses` (bit b), each copy `extra[b]` us late (a relay). */
static void ref_at(int64_t t, unsigned buses, const int64_t extra[2])
{
	uint8_t d[RAFTP_DLC], copy[RAFT_NUM_BUSES][RAFTP_DLC];
	struct raftp_ref m = { .term = 1, .cycle = cycle++ };

	raftp_encode_ref(d, 0, &m);
	refsync_fill_ref(&lead, d, copy);
	for (unsigned b = 0; b < RAFT_NUM_BUSES; b++) {
		int64_t end = t + (b ? BUS_B_LAG : 0);
		bool sampled;
		enum gtime_result res;

		refsync_tx_done(&lead, b, raftp_id(RAFTP_REF, L), copy[b], end, true);
		if ((buses >> b) & 1) {
			refsync_rx(&fol, b, raftp_id(RAFTP_REF, L), copy[b],
				   end + extra[b] + F_OFFSET, true, &sampled, &res);
		}
	}
}

static int64_t err_at(int64_t t)
{
	return gtime_global(&fol.g, t + F_OFFSET) - gtime_global(&lead.g, t);
}

static void test_refsync_bench_cases(void)
{
	const int64_t none[2] = { 0, 0 };
	int64_t t = 0;

	refsync_init(&lead, L, 0, 0);
	refsync_init(&fol, F, 0, 400);
	refsync_set_leader(&lead, L);
	refsync_set_leader(&fol, L);

	for (int i = 0; i < 100; i++, t += 20000) {
		ref_at(t, 0x3, none);
	}
	CHECK(fol.g.samples > 90);
	CHECK_EQ(fol.sync_bus, 0);          /* stays on bus A */
	CHECK(abs64(err_at(t)) <= 1);
	CHECK_EQ(fol.g.steps, 0);

	/* A relayed copy of the same REF on bus A, 300 us later: ignored */
	uint8_t d[RAFTP_DLC];
	bool sampled;
	enum gtime_result res;
	struct raftp_ref m = { .term = 1, .cycle = (uint8_t)(cycle - 1),
			       .flags = RAFTP_REF_TIME_VALID };

	raftp_encode_ref(d, 0, &m);
	CHECK(!refsync_rx(&fol, 0, raftp_id(RAFTP_REF, L), d, t - 20000 + 300 + F_OFFSET, true,
			  &sampled, &res));
	CHECK(!sampled);

	/* Bus A goes quiet: after 40 ms the follower syncs from bus B */
	for (int i = 0; i < 20; i++, t += 20000) {
		ref_at(t, 0x2, none);
	}
	CHECK_EQ(fol.sync_bus, 1);
	CHECK(abs64(err_at(t)) <= 1);

	/* Leader off bus B: REF there arrives relayed, 400 us late. With the
	 * relay delay applied the error stays small. */
	const int64_t relayed[2] = { 0, 400 };

	fol.relayed_buses = 0x2;
	for (int i = 0; i < 20; i++, t += 20000) {
		ref_at(t, 0x2, relayed);
	}
	CHECK(abs64(err_at(t)) <= 1);
	CHECK_EQ(fol.g.steps, 0);
	CHECK_EQ(fol.g.outliers, 0);

	/* No leader: REFs are ignored */
	uint32_t samples = fol.g.samples;

	refsync_set_leader(&fol, 0);
	for (int i = 0; i < 5; i++, t += 20000) {
		ref_at(t, 0x3, none);
	}
	CHECK_EQ(fol.g.samples, samples);

	/* A REF without a valid time (first REF of a new leader) is no sample */
	CHECK(lead.ref_tx_valid[0] > 0);
}

void test_refsync_all(void)
{
	RUN(test_refsync_bench_cases);
}
