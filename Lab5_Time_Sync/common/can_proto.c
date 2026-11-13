#include <string.h>

#include "can_proto.h"

/* Verbatim port of Zephyr's crc8_ccitt() (subsys/crc/crc8_sw.c). */
static const uint8_t crc8_ccitt_small_table[16] = {
	0x00, 0x07, 0x0e, 0x09, 0x1c, 0x1b, 0x12, 0x15,
	0x38, 0x3f, 0x36, 0x31, 0x24, 0x23, 0x2a, 0x2d,
};

uint8_t canp_crc8(uint8_t val, const uint8_t *buf, size_t cnt)
{
	for (size_t i = 0; i < cnt; i++) {
		val ^= buf[i];
		val = (uint8_t)(val << 4) ^ crc8_ccitt_small_table[val >> 4];
		val = (uint8_t)(val << 4) ^ crc8_ccitt_small_table[val >> 4];
	}
	return val;
}

static void start_frame(uint8_t d[CANP_DLC], uint8_t seq)
{
	memset(d, 0, CANP_DLC);
	d[0] = seq & CANP_SEQ_MASK;
}

/* CRC over b0..b5 with the flags nibble masked out of b0 */
static uint8_t ctrl_crc(const uint8_t d[CANP_DLC])
{
	uint8_t tmp[6];

	memcpy(tmp, d, sizeof(tmp));
	tmp[0] &= CANP_SEQ_MASK;
	return canp_crc8(CANP_CRC8_INIT, tmp, sizeof(tmp));
}

void canp_encode_ctrl(uint8_t d[CANP_DLC], uint8_t seq, const struct canp_ctrl *c)
{
	uint16_t steer = (uint16_t)c->steer;

	start_frame(d, seq);
	d[1] = steer & 0xFF;
	d[2] = (steer >> 8) & 0xFF;
	d[3] = c->throttle;
	d[4] = c->brake;
	d[5] = c->buttons;
	d[6] = ctrl_crc(d);
}

enum canp_ctrl_result canp_decode_ctrl(const uint8_t d[CANP_DLC], struct canp_ctrl *out)
{
	if (ctrl_crc(d) != d[6]) {
		return CANP_CTRL_BAD_CRC;
	}

	struct canp_ctrl c = {
		.steer = (int16_t)(uint16_t)(d[1] | (d[2] << 8)),
		.throttle = d[3],
		.brake = d[4],
		.buttons = d[5],
	};

	if (c.steer < -CANP_STEER_MAX || c.steer > CANP_STEER_MAX ||
	    c.throttle > CANP_PEDAL_MAX || c.brake > CANP_PEDAL_MAX ||
	    (c.buttons & ~CANP_BTN_VALID_MASK) != 0) {
		return CANP_CTRL_OUT_OF_RANGE;
	}

	*out = c;
	return CANP_CTRL_OK;
}

void canp_encode_hb(uint8_t d[CANP_DLC], uint8_t seq, enum canp_zone_state state,
		    uint8_t health)
{
	start_frame(d, seq);
	d[1] = (uint8_t)state;
	d[2] = health & (CANP_HEALTH_A | CANP_HEALTH_B);
}

void canp_encode_blink(uint8_t d[CANP_DLC], uint8_t seq, enum canp_blink_state s,
		       bool lamps_on)
{
	start_frame(d, seq);
	d[1] = (uint8_t)s;
	d[2] = lamps_on ? 1 : 0;
}

void canp_encode_fault(uint8_t d[CANP_DLC], uint8_t seq, enum canp_zone zone,
		       enum canp_fault_reason reason)
{
	start_frame(d, seq);
	d[1] = (uint8_t)zone;
	d[2] = (uint8_t)reason;
}

void canp_encode_force(uint8_t d[CANP_DLC], uint8_t seq, int8_t force)
{
	start_frame(d, seq);
	d[1] = (uint8_t)force;
}

bool canp_dedup_accept(struct canp_dedup *d, uint8_t seq, int64_t now_ms)
{
	seq &= CANP_SEQ_MASK;

	uint8_t ahead = (uint8_t)((seq - d->last_seq) & CANP_SEQ_MASK);
	bool resync = !d->valid || (now_ms - d->last_ms) > CANP_DEDUP_RESYNC_MS;

	if (!resync && (ahead == 0 || ahead > 7)) {
		return false;   /* duplicate copy, or an older (stale) frame */
	}

	d->valid = true;
	d->last_seq = seq;
	d->last_ms = now_ms;
	return true;
}
