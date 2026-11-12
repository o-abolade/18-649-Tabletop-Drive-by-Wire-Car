/*
 * Lab 5 CAN frames for Raft and time sync (design.md section 3).
 *
 * Every frame: standard 11-bit ID, DLC 8, sent on both buses. Byte 0 keeps
 * the Lab 3 rule: low nibble = 4-bit sequence counter, high nibble = flags
 * (0). Each kind has a separate ID per sender (base + node), so two nodes
 * that briefly both act as leader never send the same ID with different
 * data. Terms are 8 bits and log indexes 16 bits, little-endian.
 *
 * Plain C99, no Zephyr or Linux dependencies (shared by all three nodes).
 */
#ifndef RAFT_PROTO_H
#define RAFT_PROTO_H

#include <stdbool.h>
#include <stdint.h>

#include "raft_config.h"

#define RAFTP_DLC 8

/* ---- IDs: base + sender node (1..3) ----
 * Below CTRL 0x010 and FAULT 0x020, as Lab 1 R4.4 requires for the safety
 * messages, and above the Lab 3 heartbeats (0x100). */
#define RAFTP_ID_REF_BASE        0x024   /* 0x025..0x027 */
#define RAFTP_ID_REQ_VOTE_BASE   0x030   /* 0x031..0x033 */
#define RAFTP_ID_VOTE_BASE       0x034   /* 0x035..0x037 */
#define RAFTP_ID_APPEND_BASE     0x040   /* 0x041..0x043 */
#define RAFTP_ID_APPEND_ACK_BASE 0x044   /* 0x045..0x047 */
#define RAFTP_ID_EVENT_BASE      0x060   /* 0x061..0x063 */
#define RAFTP_ID_EVENT_RESP_BASE 0x064   /* 0x065..0x067 */

enum raftp_kind {
	RAFTP_NONE = 0,
	RAFTP_REF,
	RAFTP_REQ_VOTE,
	RAFTP_VOTE,
	RAFTP_APPEND,
	RAFTP_APPEND_ACK,
	RAFTP_EVENT,
	RAFTP_EVENT_RESP,
};

uint32_t raftp_id(enum raftp_kind kind, unsigned sender);

/* Kind and sender of a CAN ID; RAFTP_NONE if it is not a Raft frame. */
enum raftp_kind raftp_classify(uint32_t id, unsigned *sender);

/* ---- REF: leader -> all, every heartbeat_ms ----
 * b1 term · b2 cycle counter · b3-b6 master time (us, u32 LE): the leader's
 * clock when the PREVIOUS REF was handed to the CAN controller · b7 flags */
#define RAFTP_REF_TIME_VALID  0x01   /* b3-b6 hold a real send time */
#define RAFTP_REF_FAILURE     0x02   /* COCKPIT_ERROR committed: failure mode */

struct raftp_ref {
	uint8_t term;
	uint8_t cycle;
	uint32_t prev_send_us;
	uint8_t flags;
};

/* ---- REQ_VOTE: candidate -> all ----
 * b1 term · b2-b3 last log index · b4 last log term */
struct raftp_req_vote {
	uint8_t term;
	uint16_t last_index;
	uint8_t last_term;
};

/* ---- VOTE: voter -> candidate ----
 * b1 term · b2 candidate · b3 granted (0/1) */
struct raftp_vote {
	uint8_t term;
	uint8_t candidate;
	bool granted;
};

/* ---- APPEND: leader -> all ----
 * b1 term · b2-b3 index · b4 entry term · b5 prev term · b6 event ·
 * b7 commit delta
 *  - With an entry (event != 0): the entry at `index`, whose predecessor
 *    (index - 1) has term `prev_term`.
 *  - Empty (event == 0): `index` is the leader's last index and
 *    `entry_term` its term; nothing is appended, prev_term is 0.
 *  - commit = index - commit_delta. 255 means "no commit information" (the
 *    commit index lags by 255 or more): the follower keeps its own. */
#define RAFTP_COMMIT_UNKNOWN 255

struct raftp_append {
	uint8_t term;
	uint16_t index;
	uint8_t entry_term;
	uint8_t prev_term;
	uint8_t event;
	uint8_t commit_delta;
};

/* commit_delta for a leader commit index, saturating at RAFTP_COMMIT_UNKNOWN */
uint8_t raftp_commit_delta(uint16_t index, uint16_t commit);

/* Commit index carried by an APPEND; false if it carries none. */
bool raftp_commit_index(const struct raftp_append *a, uint16_t *commit);

/* ---- APPEND_ACK: follower -> leader ----
 * b1 term · b2 success (0/1) · b3-b4 match index (on success: the index the
 * follower's log now matches up to; on failure: its last index, a hint for
 * the leader's back-off) */
struct raftp_append_ack {
	uint8_t term;
	bool success;
	uint16_t match_index;
};

/* ---- EVENT: client -> leader ----
 * b1 addressed leader · b2 event · b3 client sequence number */
struct raftp_event {
	uint8_t leader;
	uint8_t event;
	uint8_t client_seq;
};

/* ---- EVENT_RESP: leader (or follower) -> client ----
 * b1 client · b2 client sequence number · b3 result · b4 leader ·
 * b5-b6 index of the committed entry (OK only) */
enum raftp_result {
	RAFTP_RESULT_OK = 0,        /* committed at `index` */
	RAFTP_RESULT_REDIRECT = 1,  /* not the leader; resend to `leader` */
	RAFTP_RESULT_NO_CHANGE = 2, /* already the current state; nothing appended */
	RAFTP_RESULT_NO_LEADER = 3, /* election in progress; retry later */
};

struct raftp_event_resp {
	uint8_t client;
	uint8_t client_seq;
	uint8_t result;
	uint8_t leader;
	uint16_t index;
};

/* Encoders fill all 8 bytes; `seq` goes in the low nibble of byte 0 (the
 * Zephyr can_link_send() overwrites it anyway). */
void raftp_encode_ref(uint8_t d[RAFTP_DLC], uint8_t seq, const struct raftp_ref *m);
void raftp_encode_req_vote(uint8_t d[RAFTP_DLC], uint8_t seq, const struct raftp_req_vote *m);
void raftp_encode_vote(uint8_t d[RAFTP_DLC], uint8_t seq, const struct raftp_vote *m);
void raftp_encode_append(uint8_t d[RAFTP_DLC], uint8_t seq, const struct raftp_append *m);
void raftp_encode_append_ack(uint8_t d[RAFTP_DLC], uint8_t seq,
			     const struct raftp_append_ack *m);
void raftp_encode_event(uint8_t d[RAFTP_DLC], uint8_t seq, const struct raftp_event *m);
void raftp_encode_event_resp(uint8_t d[RAFTP_DLC], uint8_t seq,
			     const struct raftp_event_resp *m);

/* Decoders return false for a malformed payload (non-zero flags nibble, an
 * out-of-range node, or a boolean byte other than 0/1). */
bool raftp_decode_ref(const uint8_t d[RAFTP_DLC], struct raftp_ref *m);
bool raftp_decode_req_vote(const uint8_t d[RAFTP_DLC], struct raftp_req_vote *m);
bool raftp_decode_vote(const uint8_t d[RAFTP_DLC], struct raftp_vote *m);
bool raftp_decode_append(const uint8_t d[RAFTP_DLC], struct raftp_append *m);
bool raftp_decode_append_ack(const uint8_t d[RAFTP_DLC], struct raftp_append_ack *m);
bool raftp_decode_event(const uint8_t d[RAFTP_DLC], struct raftp_event *m);
bool raftp_decode_event_resp(const uint8_t d[RAFTP_DLC], struct raftp_event_resp *m);

#endif /* RAFT_PROTO_H */
