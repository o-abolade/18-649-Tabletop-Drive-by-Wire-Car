/* Host tests: Lab 5 frame IDs and payload layouts. */
#include <string.h>

#include "check.h"
#include "netstate.h"
#include "raft_proto.h"

static void test_ids(void)
{
	unsigned sender = 0;

	CHECK_EQ(raftp_id(RAFTP_REF, 1), 0x025);
	CHECK_EQ(raftp_id(RAFTP_REF, 3), 0x027);
	CHECK_EQ(raftp_id(RAFTP_REQ_VOTE, 2), 0x032);
	CHECK_EQ(raftp_id(RAFTP_VOTE, 2), 0x036);
	CHECK_EQ(raftp_id(RAFTP_APPEND, 1), 0x041);
	CHECK_EQ(raftp_id(RAFTP_APPEND_ACK, 3), 0x047);
	CHECK_EQ(raftp_id(RAFTP_EVENT, 2), 0x062);
	CHECK_EQ(raftp_id(RAFTP_EVENT_RESP, 1), 0x065);
	CHECK_EQ(raftp_id(RAFTP_REF, 0), 0);
	CHECK_EQ(raftp_id(RAFTP_REF, 4), 0);
	CHECK_EQ(raftp_id(RAFTP_NONE, 1), 0);

	/* Every kind and sender round-trips through classify */
	CHECK_EQ(raftp_id(RAFTP_SNAPSHOT, 2), 0x052);
	for (unsigned k = RAFTP_REF; k < RAFTP_KIND_COUNT; k++) {
		for (unsigned n = 1; n <= RAFT_N; n++) {
			uint32_t id = raftp_id((enum raftp_kind)k, n);

			CHECK_EQ(raftp_classify(id, &sender), k);
			CHECK_EQ(sender, n);
		}
	}
	/* Bases themselves and Lab 3 IDs are not Raft frames */
	CHECK_EQ(raftp_classify(0x024, &sender), RAFTP_NONE);
	CHECK_EQ(raftp_classify(0x028, &sender), RAFTP_NONE);
	CHECK_EQ(raftp_classify(0x010, &sender), RAFTP_NONE);   /* CTRL */
	CHECK_EQ(raftp_classify(0x020, &sender), RAFTP_NONE);   /* FAULT */
	CHECK_EQ(raftp_classify(0x100, &sender), RAFTP_NONE);   /* HB_N1 */
	CHECK_EQ(raftp_classify(0x200, &sender), RAFTP_NONE);   /* BLINK */

	/* Priority rule (Lab 1 R4.4): CTRL and FAULT win arbitration over REF */
	CHECK(0x010 < raftp_id(RAFTP_REF, 1) && 0x020 < raftp_id(RAFTP_REF, 1));
	/* REF wins over the other Raft frames and the Lab 3 heartbeats */
	CHECK(raftp_id(RAFTP_REF, 3) < raftp_id(RAFTP_REQ_VOTE, 1));
	CHECK(raftp_id(RAFTP_EVENT_RESP, 3) < 0x100);
}

static void test_ref(void)
{
	uint8_t d[8];
	struct raftp_ref in = { .term = 7, .cycle = 200, .prev_send_us = 0xA1B2C3D4u,
				.flags = RAFTP_REF_TIME_VALID | RAFTP_REF_FAILURE };
	struct raftp_ref out;

	raftp_encode_ref(d, 0x1B, &in);   /* only the low nibble of seq is kept */
	CHECK_EQ(d[0], 0x0B);
	CHECK_EQ(d[3], 0xD4);   /* little-endian master time */
	CHECK_EQ(d[6], 0xA1);
	CHECK(raftp_decode_ref(d, &out));
	CHECK_EQ(out.term, 7);
	CHECK_EQ(out.cycle, 200);
	CHECK_EQ(out.prev_send_us, 0xA1B2C3D4u);
	CHECK_EQ(out.flags, in.flags);

	d[0] |= 0x10;   /* a set flags nibble is malformed */
	CHECK(!raftp_decode_ref(d, &out));
}

static void test_votes(void)
{
	uint8_t d[8];
	struct raftp_req_vote rv = { .term = 3, .last_index = 30000, .last_term = 2, .pre = true }, rv2;
	struct raftp_vote v = { .term = 3, .candidate = 2, .granted = true, .pre = true }, v2;

	raftp_encode_req_vote(d, 1, &rv);
	CHECK(raftp_decode_req_vote(d, &rv2));
	CHECK_EQ(rv2.term, 3);
	CHECK_EQ(rv2.last_index, 30000);
	CHECK_EQ(rv2.last_term, 2);
	CHECK(rv2.pre);
	d[5] = 2;   /* pre must be 0/1 */
	CHECK(!raftp_decode_req_vote(d, &rv2));

	raftp_encode_vote(d, 2, &v);
	CHECK(raftp_decode_vote(d, &v2));
	CHECK_EQ(v2.candidate, 2);
	CHECK(v2.granted);
	CHECK(v2.pre);
	d[3] = 2;   /* granted must be 0/1 */
	CHECK(!raftp_decode_vote(d, &v2));
	d[3] = 0;
	d[2] = 9;   /* candidate out of range */
	CHECK(!raftp_decode_vote(d, &v2));
}

static void test_append(void)
{
	uint8_t d[8];
	uint16_t commit = 0;
	struct raftp_append in = {
		.term = 4, .index = 1234, .entry_term = 3, .prev_term = 3,
		.event = 0x8C, .commit_delta = raftp_commit_delta(1234, 1230),
	}, out;

	CHECK_EQ(in.commit_delta, 4);
	raftp_encode_append(d, 5, &in);
	CHECK(raftp_decode_append(d, &out));
	CHECK_EQ(out.term, 4);
	CHECK_EQ(out.index, 1234);
	CHECK_EQ(out.entry_term, 3);
	CHECK_EQ(out.prev_term, 3);
	CHECK_EQ(out.event, 0x8C);
	CHECK(raftp_commit_index(&out, &commit));
	CHECK_EQ(commit, 1230);

	/* Commit lagging by 255 or more carries no commit information */
	CHECK_EQ(raftp_commit_delta(1000, 700), RAFTP_COMMIT_UNKNOWN);
	CHECK_EQ(raftp_commit_delta(10, 11), 0);   /* catch-up: committed through index */
	CHECK_EQ(raftp_commit_delta(300, 46), 254);
	out.commit_delta = RAFTP_COMMIT_UNKNOWN;
	CHECK(!raftp_commit_index(&out, &commit));

	/* Empty APPEND on an empty log: index 0, commit 0 */
	struct raftp_append empty = { .term = 1, .index = 0, .event = 0,
				      .commit_delta = raftp_commit_delta(0, 0) };

	raftp_encode_append(d, 0, &empty);
	CHECK(raftp_decode_append(d, &out));
	CHECK(raftp_commit_index(&out, &commit));
	CHECK_EQ(commit, 0);

	/* An entry at index 0 is malformed */
	in.index = 0;
	raftp_encode_append(d, 0, &in);
	CHECK(!raftp_decode_append(d, &out));
}

static void test_ack_and_events(void)
{
	uint8_t d[8];
	struct raftp_append_ack a = { .term = 9, .success = false, .match_index = 513 }, a2;
	struct raftp_event e = { .leader = 1, .event = 0x47, .client_seq = 33 }, e2;
	struct raftp_event_resp r = { .client = 3, .client_seq = 33,
				      .result = RAFTP_RESULT_REDIRECT, .leader = 2,
				      .index = 0 }, r2;

	raftp_encode_append_ack(d, 0, &a);
	CHECK(raftp_decode_append_ack(d, &a2));
	CHECK(!a2.success);
	CHECK_EQ(a2.match_index, 513);

	raftp_encode_event(d, 0, &e);
	CHECK(raftp_decode_event(d, &e2));
	CHECK_EQ(e2.leader, 1);
	CHECK_EQ(e2.event, 0x47);
	CHECK_EQ(e2.client_seq, 33);
	d[1] = 0;   /* addressed leader must be a node */
	CHECK(!raftp_decode_event(d, &e2));

	raftp_encode_event_resp(d, 0, &r);
	CHECK(raftp_decode_event_resp(d, &r2));
	CHECK_EQ(r2.result, RAFTP_RESULT_REDIRECT);
	CHECK_EQ(r2.leader, 2);
	r.leader = 0;   /* "no known leader" is allowed */
	r.result = RAFTP_RESULT_NO_LEADER;
	raftp_encode_event_resp(d, 0, &r);
	CHECK(raftp_decode_event_resp(d, &r2));
	d[3] = 7;   /* unknown result code */
	CHECK(!raftp_decode_event_resp(d, &r2));
}

static void test_snapshot_frame(void)
{
	uint8_t d[8];
	struct netstate ns, back;
	struct raftp_snapshot in, out;

	/* Pack/unpack round-trips every node, link and cockpit bit */
	netstate_init(&ns);
	ns.cockpit_ok = true;
	netstate_apply(&ns, raft_ev_link(1, 1, false));
	netstate_apply(&ns, raft_ev_node(3, false));
	netstate_unpack(netstate_pack(&ns), &back);
	CHECK(netstate_equal(&ns, &back));
	netstate_init(&ns);
	netstate_unpack(netstate_pack(&ns), &back);
	CHECK(netstate_equal(&ns, &back));

	in = (struct raftp_snapshot){ .term = 5, .index = 4000, .index_term = 4,
				      .state = netstate_pack(&ns) };
	raftp_encode_snapshot(d, 3, &in);
	CHECK(raftp_decode_snapshot(d, &out));
	CHECK_EQ(out.term, 5);
	CHECK_EQ(out.index, 4000);
	CHECK_EQ(out.index_term, 4);
	CHECK_EQ(out.state, in.state);
}

void test_raft_proto_all(void)
{
	RUN(test_snapshot_frame);
	RUN(test_ids);
	RUN(test_ref);
	RUN(test_votes);
	RUN(test_append);
	RUN(test_ack_and_events);
}
