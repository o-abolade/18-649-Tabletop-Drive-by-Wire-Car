/* Lab 5 global time: see gtime.h. */
#include "gtime.h"

#define PPB 1000000000LL

static int64_t abs64(int64_t v)
{
	return v < 0 ? -v : v;
}

static int32_t clamp32(int64_t v)
{
	if (v > INT32_MAX) {
		return INT32_MAX;
	}
	if (v < INT32_MIN) {
		return INT32_MIN;
	}
	return (int32_t)v;
}

/* Re-anchor at a sample and restart the drift baseline there */
static void reset_at(struct gtime *g, int64_t local, int64_t global)
{
	g->anchor_local = local;
	g->anchor_global = global;
	g->base_local = local;
	g->base_global = global;
	g->last_sample_local = local;
}

void gtime_init(struct gtime *g)
{
	*g = (struct gtime){ 0 };   /* global = local until the first sample */
}

void gtime_become_master(struct gtime *g)
{
	g->master = true;
	g->synced = true;   /* our time is the global time: a later master is
			     * compared with it, not adopted blindly */
	g->outlier_run = 0;
}

void gtime_become_follower(struct gtime *g)
{
	g->master = false;
	g->outlier_run = 0;
}

int32_t gtime_drift_corr_us(const struct gtime *g, int64_t local)
{
	return clamp32((local - g->anchor_local) * g->drift_ppb / PPB);
}

int64_t gtime_global(const struct gtime *g, int64_t local)
{
	int64_t dl = local - g->anchor_local;

	return g->anchor_global + dl + dl * g->drift_ppb / PPB;
}

int64_t gtime_local_of(const struct gtime *g, int64_t global)
{
	/* dl = dg / (1 + p), written as dg - dg * p / (1 + p) */
	int64_t dg = global - g->anchor_global;

	return g->anchor_local + dg - dg * g->drift_ppb / (PPB + g->drift_ppb);
}

bool gtime_valid(const struct gtime *g, int64_t local_now)
{
	if (g->master) {
		return true;
	}
	return g->synced && local_now - g->last_sample_local <= GTIME_VALID_US;
}

uint32_t gtime_wire(int64_t global)
{
	int64_t m = global % GTIME_WIRE_MOD_US;

	return (uint32_t)(m < 0 ? m + GTIME_WIRE_MOD_US : m);
}

enum gtime_result gtime_sample(struct gtime *g, uint32_t master_wire, int64_t local_rx,
			       int32_t delay_us)
{
	if (g->master) {
		return GTIME_IGNORED;
	}

	/* Unwrap the master time to the copy nearest our own estimate */
	int64_t est = gtime_global(g, local_rx);
	int64_t d = (int64_t)master_wire - (int64_t)gtime_wire(est);

	if (d >= GTIME_WIRE_MOD_US / 2) {
		d -= GTIME_WIRE_MOD_US;
	} else if (d < -GTIME_WIRE_MOD_US / 2) {
		d += GTIME_WIRE_MOD_US;
	}

	int64_t master = est + d + delay_us;
	int64_t err = master - est;

	if (!g->synced) {
		g->samples++;
		g->synced = true;
		g->outlier_run = 0;
		g->last_error_us = 0;
		g->last_drift_us = 0;
		reset_at(g, local_rx, master);
		return GTIME_FIRST;
	}

	if (abs64(err) > GTIME_OUTLIER_US) {
		if (++g->outlier_run < GTIME_OUTLIER_RUN) {
			g->outliers++;
			return GTIME_OUTLIER;
		}
		/* Several in a row: the master's time really is elsewhere */
		g->outlier_run = 0;
		g->samples++;
		g->steps++;
		g->last_error_us = clamp32(err);
		g->last_drift_us = 0;
		reset_at(g, local_rx, master);
		return GTIME_STEPPED;
	}

	g->outlier_run = 0;
	g->samples++;
	g->last_error_us = (int32_t)err;
	g->last_drift_us = gtime_drift_corr_us(g, local_rx);
	g->anchor_local = local_rx;
	g->anchor_global = master;
	g->last_sample_local = local_rx;

	/* Drift: (L2 - L1) / (F2 - F1) over at least GTIME_DRIFT_BASE_US */
	int64_t dl = local_rx - g->base_local;

	if (dl >= GTIME_DRIFT_BASE_US) {
		int64_t dg = master - g->base_global;
		int64_t raw = (dg - dl) * PPB / dl;

		if (abs64(raw) <= GTIME_DRIFT_MAX_PPB) {
			g->last_raw_ppb = (int32_t)raw;
			if (g->drift_valid) {
				g->drift_ppb += (int32_t)((raw - g->drift_ppb) / GTIME_DRIFT_FILTER);
			} else {
				g->drift_ppb = (int32_t)raw;
				g->drift_valid = true;
			}
		}
		g->base_local = local_rx;
		g->base_global = master;
	}
	return GTIME_OK;
}
