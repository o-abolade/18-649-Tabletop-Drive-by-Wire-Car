/* Lab 5 REF time exchange: see refsync.h. */
#include <string.h>

#include "refsync.h"

void refsync_init(struct refsync *s, unsigned self, int32_t net_delay_us,
		  int32_t relay_delay_us)
{
	memset(s, 0, sizeof(*s));
	gtime_init(&s->g);
	s->self = self;
	s->net_delay_us = net_delay_us;
	s->relay_delay_us = relay_delay_us;
	s->sync_bus = -1;
}

void refsync_set_leader(struct refsync *s, unsigned leader)
{
	if (leader == s->self && !s->g.master) {
		gtime_become_master(&s->g);
	} else if (leader != s->self && s->g.master) {
		gtime_become_follower(&s->g);
	}
	if (leader != s->leader) {
		s->sync_bus = -1;
	}
	s->leader = leader;
}

static void put_u32(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
	p[2] = (uint8_t)(v >> 16);
	p[3] = (uint8_t)(v >> 24);
}

bool refsync_rx(struct refsync *s, unsigned bus, uint32_t id, const uint8_t d[RAFTP_DLC],
		int64_t local_us, bool ok, bool *sampled, enum gtime_result *res)
{
	unsigned sender;
	struct raftp_ref m;

	*sampled = false;
	if (bus >= RAFT_NUM_BUSES || raftp_classify(id, &sender) != RAFTP_REF ||
	    sender == s->self || !raftp_decode_ref(d, &m)) {
		return false;
	}

	struct refsync_rx *p = &s->rx[bus];

	s->ref_rx++;
	/* First copy on this bus only */
	if (p->have && p->sender == sender && p->cycle == m.cycle) {
		return false;
	}
	if (!ok) {
		s->stamp_rx_bad++;
	}
	bool new_ref = sender != s->last_sender || m.cycle != s->last_cycle;

	s->last_sender = (uint8_t)sender;
	s->last_cycle = m.cycle;

	/* REF k carries the send time of REF k-1 on this bus */
	if (sender == s->leader && !s->g.master && (m.flags & RAFTP_REF_TIME_VALID) &&
	    p->have && p->ok && p->sender == sender && (uint8_t)(p->cycle + 1) == m.cycle &&
	    (s->sync_bus < 0 || s->sync_bus == (int)bus ||
	     local_us - s->sync_bus_last_us > REFSYNC_BUS_SWITCH_US)) {
		/* The delay belongs to the stamp being corrected: REF k-1's */
		int32_t delay = s->net_delay_us + (p->relayed ? s->relay_delay_us : 0);

		*res = gtime_sample(&s->g, m.prev_send_us, p->local_us, delay);
		*sampled = true;
		s->sync_bus = (int)bus;
		s->sync_bus_last_us = local_us;
		if (*res == GTIME_OK) {
			int32_t e = s->g.last_error_us < 0 ? -s->g.last_error_us : s->g.last_error_us;

			if (e > s->err_max_us) {
				s->err_max_us = e;
			}
		}
	}
	*p = (struct refsync_rx){ .have = true, .ok = ok,
				  .relayed = (s->relayed_buses >> bus) & 1,
				  .sender = (uint8_t)sender, .cycle = m.cycle,
				  .local_us = local_us };
	return new_ref;
}

void refsync_tx_done(struct refsync *s, unsigned bus, uint32_t id, const uint8_t d[RAFTP_DLC],
		     int64_t local_us, bool ok)
{
	unsigned sender;

	if (bus >= RAFT_NUM_BUSES || raftp_classify(id, &sender) != RAFTP_REF ||
	    sender != s->self) {
		return;
	}
	if (!ok) {
		s->stamp_tx_bad++;
	}
	s->tx[bus] = (struct refsync_tx){ .have = true, .ok = ok, .cycle = d[2],
					  .local_us = local_us };
}

void refsync_fill_ref(struct refsync *s, const uint8_t d[RAFTP_DLC],
		      uint8_t out[RAFT_NUM_BUSES][RAFTP_DLC])
{
	uint8_t cycle = d[2];

	for (unsigned bus = 0; bus < RAFT_NUM_BUSES; bus++) {
		const struct refsync_tx *t = &s->tx[bus];

		memcpy(out[bus], d, RAFTP_DLC);
		if (s->g.master && t->have && t->ok && (uint8_t)(t->cycle + 1) == cycle) {
			put_u32(&out[bus][3], gtime_wire(gtime_global(&s->g, t->local_us)));
			out[bus][7] |= RAFTP_REF_TIME_VALID;
			s->ref_tx_valid[bus]++;
		} else {
			put_u32(&out[bus][3], 0);
			out[bus][7] &= (uint8_t)~RAFTP_REF_TIME_VALID;
		}
	}
}
