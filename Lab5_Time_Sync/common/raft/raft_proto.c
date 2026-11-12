/* Lab 5 Raft and time-sync frames. See raft_proto.h for the layouts. */
#include "raft_proto.h"

#include <string.h>

static const uint32_t kind_base[] = {
	[RAFTP_REF] = RAFTP_ID_REF_BASE,
	[RAFTP_REQ_VOTE] = RAFTP_ID_REQ_VOTE_BASE,
	[RAFTP_VOTE] = RAFTP_ID_VOTE_BASE,
	[RAFTP_APPEND] = RAFTP_ID_APPEND_BASE,
	[RAFTP_APPEND_ACK] = RAFTP_ID_APPEND_ACK_BASE,
	[RAFTP_EVENT] = RAFTP_ID_EVENT_BASE,
	[RAFTP_EVENT_RESP] = RAFTP_ID_EVENT_RESP_BASE,
	[RAFTP_SNAPSHOT] = RAFTP_ID_SNAPSHOT_BASE,
};

#define NUM_KINDS RAFTP_KIND_COUNT

static bool valid_node(unsigned n)
{
	return n >= 1 && n <= RAFT_N;
}

uint32_t raftp_id(enum raftp_kind kind, unsigned sender)
{
	if (kind == RAFTP_NONE || (unsigned)kind >= NUM_KINDS || !valid_node(sender)) {
		return 0;
	}
	return kind_base[kind] + sender;
}

enum raftp_kind raftp_classify(uint32_t id, unsigned *sender)
{
	for (unsigned k = RAFTP_REF; k < NUM_KINDS; k++) {
		if (id > kind_base[k] && id <= kind_base[k] + RAFT_N) {
			if (sender) {
				*sender = id - kind_base[k];
			}
			return (enum raftp_kind)k;
		}
	}
	return RAFTP_NONE;
}

static void put16(uint8_t *p, uint16_t v)
{
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
}

static uint16_t get16(const uint8_t *p)
{
	return (uint16_t)(p[0] | (p[1] << 8));
}

static void put32(uint8_t *p, uint32_t v)
{
	put16(p, (uint16_t)v);
	put16(p + 2, (uint16_t)(v >> 16));
}

static uint32_t get32(const uint8_t *p)
{
	return get16(p) | ((uint32_t)get16(p + 2) << 16);
}

/* Byte 0: seq in the low nibble, flags nibble 0 */
static void start(uint8_t d[RAFTP_DLC], uint8_t seq)
{
	memset(d, 0, RAFTP_DLC);
	d[0] = seq & 0x0F;
}

static bool flags_ok(const uint8_t d[RAFTP_DLC])
{
	return (d[0] & 0xF0) == 0;
}

static bool is_bool(uint8_t b)
{
	return b <= 1;
}

uint8_t raftp_commit_delta(uint16_t index, uint16_t commit)
{
	if (commit >= index) {
		/* Catch-up: the entry is already committed. A follower caps its
		 * commit at the entry's index anyway, so "through index" is exact. */
		return 0;
	}
	if (index - commit >= RAFTP_COMMIT_UNKNOWN) {
		return RAFTP_COMMIT_UNKNOWN;
	}
	return (uint8_t)(index - commit);
}

bool raftp_commit_index(const struct raftp_append *a, uint16_t *commit)
{
	if (a->commit_delta == RAFTP_COMMIT_UNKNOWN || a->commit_delta > a->index) {
		return false;
	}
	*commit = (uint16_t)(a->index - a->commit_delta);
	return true;
}

void raftp_encode_ref(uint8_t d[RAFTP_DLC], uint8_t seq, const struct raftp_ref *m)
{
	start(d, seq);
	d[1] = m->term;
	d[2] = m->cycle;
	put32(&d[3], m->prev_send_us);
	d[7] = m->flags;
}

bool raftp_decode_ref(const uint8_t d[RAFTP_DLC], struct raftp_ref *m)
{
	if (!flags_ok(d)) {
		return false;
	}
	m->term = d[1];
	m->cycle = d[2];
	m->prev_send_us = get32(&d[3]);
	m->flags = d[7];
	return true;
}

void raftp_encode_req_vote(uint8_t d[RAFTP_DLC], uint8_t seq, const struct raftp_req_vote *m)
{
	start(d, seq);
	d[1] = m->term;
	put16(&d[2], m->last_index);
	d[4] = m->last_term;
	d[5] = m->pre ? 1 : 0;
}

bool raftp_decode_req_vote(const uint8_t d[RAFTP_DLC], struct raftp_req_vote *m)
{
	if (!flags_ok(d) || !is_bool(d[5])) {
		return false;
	}
	m->term = d[1];
	m->last_index = get16(&d[2]);
	m->last_term = d[4];
	m->pre = d[5] != 0;
	return true;
}

void raftp_encode_vote(uint8_t d[RAFTP_DLC], uint8_t seq, const struct raftp_vote *m)
{
	start(d, seq);
	d[1] = m->term;
	d[2] = m->candidate;
	d[3] = m->granted ? 1 : 0;
	d[4] = m->pre ? 1 : 0;
}

bool raftp_decode_vote(const uint8_t d[RAFTP_DLC], struct raftp_vote *m)
{
	if (!flags_ok(d) || !valid_node(d[2]) || !is_bool(d[3]) || !is_bool(d[4])) {
		return false;
	}
	m->term = d[1];
	m->candidate = d[2];
	m->granted = d[3] != 0;
	m->pre = d[4] != 0;
	return true;
}

void raftp_encode_append(uint8_t d[RAFTP_DLC], uint8_t seq, const struct raftp_append *m)
{
	start(d, seq);
	d[1] = m->term;
	put16(&d[2], m->index);
	d[4] = m->entry_term;
	d[5] = m->prev_term;
	d[6] = m->event;
	d[7] = m->commit_delta;
}

bool raftp_decode_append(const uint8_t d[RAFTP_DLC], struct raftp_append *m)
{
	if (!flags_ok(d)) {
		return false;
	}
	m->term = d[1];
	m->index = get16(&d[2]);
	m->entry_term = d[4];
	m->prev_term = d[5];
	m->event = d[6];
	m->commit_delta = d[7];
	/* An entry needs a real index; an empty APPEND may carry index 0 (empty log) */
	return m->event == 0 || m->index >= 1;
}

void raftp_encode_append_ack(uint8_t d[RAFTP_DLC], uint8_t seq,
			     const struct raftp_append_ack *m)
{
	start(d, seq);
	d[1] = m->term;
	d[2] = m->success ? 1 : 0;
	put16(&d[3], m->match_index);
}

bool raftp_decode_append_ack(const uint8_t d[RAFTP_DLC], struct raftp_append_ack *m)
{
	if (!flags_ok(d) || !is_bool(d[2])) {
		return false;
	}
	m->term = d[1];
	m->success = d[2] != 0;
	m->match_index = get16(&d[3]);
	return true;
}

void raftp_encode_event(uint8_t d[RAFTP_DLC], uint8_t seq, const struct raftp_event *m)
{
	start(d, seq);
	d[1] = m->leader;
	d[2] = m->event;
	d[3] = m->client_seq;
}

bool raftp_decode_event(const uint8_t d[RAFTP_DLC], struct raftp_event *m)
{
	if (!flags_ok(d) || !valid_node(d[1])) {
		return false;
	}
	m->leader = d[1];
	m->event = d[2];
	m->client_seq = d[3];
	return true;
}

void raftp_encode_event_resp(uint8_t d[RAFTP_DLC], uint8_t seq,
			     const struct raftp_event_resp *m)
{
	start(d, seq);
	d[1] = m->client;
	d[2] = m->client_seq;
	d[3] = m->result;
	d[4] = m->leader;
	put16(&d[5], m->index);
}

bool raftp_decode_event_resp(const uint8_t d[RAFTP_DLC], struct raftp_event_resp *m)
{
	if (!flags_ok(d) || !valid_node(d[1]) || d[3] > RAFTP_RESULT_NO_LEADER ||
	    (d[4] != 0 && !valid_node(d[4]))) {
		return false;
	}
	m->client = d[1];
	m->client_seq = d[2];
	m->result = d[3];
	m->leader = d[4];
	m->index = get16(&d[5]);
	return true;
}

void raftp_encode_snapshot(uint8_t d[RAFTP_DLC], uint8_t seq, const struct raftp_snapshot *m)
{
	start(d, seq);
	d[1] = m->term;
	put16(&d[2], m->index);
	d[4] = m->index_term;
	put16(&d[5], m->state);
}

bool raftp_decode_snapshot(const uint8_t d[RAFTP_DLC], struct raftp_snapshot *m)
{
	if (!flags_ok(d)) {
		return false;
	}
	m->term = d[1];
	m->index = get16(&d[2]);
	m->index_term = d[4];
	m->state = get16(&d[5]);
	return true;
}
