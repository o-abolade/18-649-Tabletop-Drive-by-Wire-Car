/*
 * Lab 3 CAN message catalog and payload layouts.
 *
 * Plain C99, no Zephyr or Linux dependencies: compiled into both STM32 zone
 * apps AND the Pi cockpit, so every node agrees on the wire format. Change a
 * layout here and both sides change together.
 *
 * Every frame: standard 11-bit ID, DLC 8, sent on both buses.
 * Byte 0: low nibble = 4-bit sequence counter (one counter per ID per sender),
 *         high nibble = flags (CANP_FLAG_*).
 * Unused bytes are 0.
 */
#ifndef CAN_PROTO_H
#define CAN_PROTO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CANP_DLC 8

/* ---- Catalog IDs (lower ID wins arbitration) ---- */
#define CANP_ID_CTRL   0x010
#define CANP_ID_FAULT  0x020
#define CANP_ID_HB_N1  0x100
#define CANP_ID_HB_N2  0x101
#define CANP_ID_HB_N3  0x102
#define CANP_ID_BLINK  0x200
#define CANP_ID_FORCE  0x300

#define CANP_ID_HB(zone) (CANP_ID_HB_N1 + ((zone) - 1))

enum canp_zone {
	CANP_ZONE_N1 = 1,   /* cockpit (Pi) */
	CANP_ZONE_N2 = 2,   /* steering (front) */
	CANP_ZONE_N3 = 3,   /* drivetrain (rear) */
};
#define CANP_NUM_ZONES 3

enum canp_bus { CANP_BUS_A = 0, CANP_BUS_B = 1 };
#define CANP_NUM_BUSES 2

/* ---- Byte 0: sequence + flags ---- */
#define CANP_SEQ_MASK      0x0F
#define CANP_FLAG_RELAYED  0x10   /* reserved; relays forward frames unchanged (relay.c) */

static inline uint8_t canp_seq(const uint8_t *d)   { return d[0] & CANP_SEQ_MASK; }
static inline uint8_t canp_flags(const uint8_t *d) { return d[0] & (uint8_t)~CANP_SEQ_MASK; }

/* ---- Timing (handout Part 2 / R4.5 / R4.7) ---- */
#define CANP_CTRL_PERIOD_MS     20
#define CANP_HB_PERIOD_MS       20
#define CANP_FORCE_PERIOD_MS    50
#define CANP_BLINK_TOGGLE_MS    500
#define CANP_HB_MISSED_LIMIT    3     /* 3 missed = 60 ms: bus degraded / zone failed */

/* ---- CTRL 0x010, N1 -> N2, N3 ----
 * b1-2 steer int16 LE  -1000..+1000 (left negative)
 * b3   throttle u8     0..100 (%)
 * b4   brake u8        0..100 (%)
 * b5   buttons u8      bit0 left turn, bit1 right turn, bit2 self-test
 * b6   CRC-8 over b0 (seq only, flags masked) .. b5
 * b7   0
 * The CRC masks the flags nibble so N2's relay can set CANP_FLAG_RELAYED
 * without re-computing it. */
#define CANP_STEER_MAX       1000
#define CANP_PEDAL_MAX       100
#define CANP_BTN_LEFT        0x01
#define CANP_BTN_RIGHT       0x02
#define CANP_BTN_SELF_TEST   0x04
#define CANP_BTN_VALID_MASK  0x07

struct canp_ctrl {
	int16_t steer;
	uint8_t throttle;
	uint8_t brake;
	uint8_t buttons;
};

enum canp_ctrl_result {
	CANP_CTRL_OK = 0,
	CANP_CTRL_BAD_CRC,        /* corrupted: drop, keep last good command */
	CANP_CTRL_OUT_OF_RANGE,   /* valid CRC, bad values: error state + FAULT */
};

/* ---- Zone state, carried in every heartbeat ---- */
enum canp_zone_state {
	CANP_ZS_ERROR = 0,        /* system error state (power-up, lost zone, FAULT) */
	CANP_ZS_NORMAL = 1,
	CANP_ZS_LOCAL_FAIL = 2,   /* this zone's own fault (test button, bad CTRL) */
};

/* ---- HB 0x100-0x102 ----
 * b1 zone state (enum canp_zone_state)
 * b2 bus health as seen by the sender: bit0 bus A ok, bit1 bus B ok */
#define CANP_HEALTH_A 0x01
#define CANP_HEALTH_B 0x02

/* ---- BLINK 0x200, N1 -> N2, N3 ----
 * b1 blinker state (enum canp_blink_state)
 * b2 phase: 1 = lamps on, 0 = lamps off */
enum canp_blink_state {
	CANP_BLINK_OFF = 0,
	CANP_BLINK_LEFT = 1,
	CANP_BLINK_RIGHT = 2,
	CANP_BLINK_HAZARD = 3,
};

/* ---- FAULT 0x020, any -> all ----
 * b1 failing zone (enum canp_zone)
 * b2 reason (enum canp_fault_reason) */
enum canp_fault_reason {
	CANP_FAULT_TEST_BUTTON = 1,
	CANP_FAULT_CTRL_RANGE = 2,
	CANP_FAULT_HB_LOST = 3,
};

/* ---- FORCE 0x300, N2 -> N1 ----
 * b1 force int8 (dummy counter in Lab 3) */

/* ---- CRC-8 ----
 * Same algorithm as Zephyr's crc8_ccitt() (poly 0x07, no reflection), seeded
 * with CANP_CRC8_INIT. A copy lives in can_proto.c so the Pi runs identical
 * code; never substitute a different "CRC-8" on one side. */
#define CANP_CRC8_INIT 0xFF
uint8_t canp_crc8(uint8_t seed, const uint8_t *buf, size_t len);

/* ---- Encoders: fill all 8 bytes (seq goes in b0 low nibble) ---- */
void canp_encode_ctrl(uint8_t d[CANP_DLC], uint8_t seq, const struct canp_ctrl *c);
void canp_encode_hb(uint8_t d[CANP_DLC], uint8_t seq, enum canp_zone_state state,
		    uint8_t health);
void canp_encode_blink(uint8_t d[CANP_DLC], uint8_t seq, enum canp_blink_state s,
		       bool lamps_on);
void canp_encode_fault(uint8_t d[CANP_DLC], uint8_t seq, enum canp_zone zone,
		       enum canp_fault_reason reason);
void canp_encode_force(uint8_t d[CANP_DLC], uint8_t seq, int8_t force);

/* ---- Decoders ---- */
enum canp_ctrl_result canp_decode_ctrl(const uint8_t d[CANP_DLC], struct canp_ctrl *out);

static inline enum canp_zone_state canp_hb_state(const uint8_t *d) { return d[1]; }
static inline uint8_t canp_hb_health(const uint8_t *d) { return d[2]; }
static inline enum canp_blink_state canp_blink_state(const uint8_t *d) { return d[1] & 0x3; }
static inline bool canp_blink_lamps_on(const uint8_t *d) { return d[2] != 0; }
static inline uint8_t canp_fault_zone(const uint8_t *d) { return d[1]; }
static inline uint8_t canp_fault_reason(const uint8_t *d) { return d[2]; }
static inline int8_t canp_force(const uint8_t *d) { return (int8_t)d[1]; }

/* Which zone sent a frame with this ID; 0 if it isn't a heartbeat. */
static inline int canp_hb_zone(uint32_t id)
{
	return (id >= CANP_ID_HB_N1 && id <= CANP_ID_HB_N3) ? (int)(id - CANP_ID_HB_N1 + 1) : 0;
}

/* ---- Duplicate / stale rejection, one instance per catalog ID ----
 * Every frame arrives twice (bus A and bus B, plus a relayed copy). Accept a
 * frame if its seq is ahead of the last accepted one by 1..7 (mod 16), or if
 * nothing was accepted for CANP_DEDUP_RESYNC_MS (sender rebooted, or first
 * frame). Rejects the second copy (same seq) and late frames that sat in a
 * TX buffer while a bus was unplugged (older seq).
 * Not thread-safe: the caller serializes access. */
#define CANP_DEDUP_RESYNC_MS 100

struct canp_dedup {
	bool valid;
	uint8_t last_seq;
	int64_t last_ms;
};

bool canp_dedup_accept(struct canp_dedup *d, uint8_t seq, int64_t now_ms);

#endif /* CAN_PROTO_H */
