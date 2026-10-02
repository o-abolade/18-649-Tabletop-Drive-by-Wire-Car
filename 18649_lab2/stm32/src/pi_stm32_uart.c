#include "pi_stm32_uart.h"
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/crc.h>
#include <float.h>
#include <string.h>
#include "testpoints.h"

#define SYNC0       0xAA
#define SYNC1       0x55
#define TYPE_CMD    0x01
#define TYPE_STATUS 0x02
#define CMD_FRAME_LEN    13
#define STATUS_FRAME_LEN 19
#define BYTE_TIMEOUT_MS 20

_Static_assert(sizeof(float) == sizeof(uint32_t), "status protocol requires 32-bit floats");
_Static_assert(FLT_RADIX == 2 && FLT_MANT_DIG == 24 && FLT_MAX_EXP == 128,
	       "status protocol requires IEEE-754 binary32 floats");

// Validation ranges
#define STEERING_MIN -1000
#define STEERING_MAX 1000
#define THROTTLE_MAX 1000
#define BRAKE_MAX 1000

static const struct device *uart_dev = DEVICE_DT_GET(DT_ALIAS(rpilink));

// RX parser state machine
enum rx_state { SEEK_SYNC0, SEEK_SYNC1, COLLECT };

static enum rx_state rx_state = SEEK_SYNC0;
static uint8_t  rx_buf[CMD_FRAME_LEN];
static uint8_t  rx_idx;

// Shared "latest command" slot
static struct k_spinlock cmd_lock;
static cmd_frame_t latest_cmd;
static bool        have_cmd;
static int64_t     last_cmd_uptime_ms;

K_SEM_DEFINE(cmd_received_sem, 0, 1);

static void cmd_rx_pulse_timer_expired(struct k_timer *t)
{
	testpoint_set(TP_CMD_RX, false);
}
K_TIMER_DEFINE(cmd_rx_pulse_timer, cmd_rx_pulse_timer_expired, NULL);

// byte-timeout timer: resyncs parser if a frame stalls mid-collect
static void byte_timeout_expired(struct k_timer *t)
{
	rx_state = SEEK_SYNC0;
	rx_idx = 0;
}
K_TIMER_DEFINE(byte_timer, byte_timeout_expired, NULL);

static void handle_complete_frame(void)
{
	// rx_buf[0-1] = sync, [2]=type, [3]=seq, [4-10]=payload, [11-12]=crc
	uint16_t rx_crc = rx_buf[11] | (rx_buf[12] << 8);
	uint16_t calc_crc = crc16_ccitt(0xFFFF, &rx_buf[2], 9); // TYPE..BUTTONS = 9 bytes

	// Check for malformed frame
	if (rx_crc != calc_crc) {
		return;
	}

	// Check if it's a command frame
	if (rx_buf[2] != TYPE_CMD) {
		return;
	}

	cmd_frame_t parsed;
	parsed.seq      = rx_buf[3];
	parsed.steering = (int16_t)(rx_buf[4] | (rx_buf[5] << 8));
	parsed.throttle = (uint16_t)(rx_buf[6] | (rx_buf[7] << 8));
	parsed.brake    = (uint16_t)(rx_buf[8] | (rx_buf[9] << 8));
	parsed.buttons  = rx_buf[10];

	// Range validation (Part 2 requirement)
	if (parsed.steering < STEERING_MIN || parsed.steering > STEERING_MAX ||
	    parsed.throttle < 0 || parsed.throttle > THROTTLE_MAX ||
	    parsed.brake < 0 || parsed.brake > BRAKE_MAX) {
		return;
	}

	k_spinlock_key_t key = k_spin_lock(&cmd_lock);
	latest_cmd = parsed;       // overwrite-always — no queueing of stale commands
	have_cmd = true;
	last_cmd_uptime_ms = k_uptime_get();
	k_spin_unlock(&cmd_lock, key);

	testpoint_set(TP_CMD_RX, true);
	k_busy_wait(100);
	testpoint_set(TP_CMD_RX, false);

	k_sem_give(&cmd_received_sem);
}

static void uart_isr(const struct device *dev, void *user_data)
{
	ARG_UNUSED(user_data);

	uart_irq_update(dev);

	if (!uart_irq_rx_ready(dev)) {
		return;
	}

	uint8_t current_byte;
	while (uart_fifo_read(dev, &current_byte, 1) == 1) {
		switch (rx_state) {
		case SEEK_SYNC0:
			if (current_byte == SYNC0) {
				rx_buf[0] = current_byte;
				rx_state = SEEK_SYNC1;
			}
			break;

		case SEEK_SYNC1:
			if (current_byte == SYNC1) {
				rx_buf[1] = current_byte;
				rx_idx = 2;
				rx_state = COLLECT;
				k_timer_start(&byte_timer, K_MSEC(BYTE_TIMEOUT_MS), K_NO_WAIT);
			} else if (current_byte == SYNC0) {
				continue; // Edge case: If received 0xAA again, we stay in SEEK_SYNC1 and treat it as the first sync byte of a new frame.
			} else {
				rx_state = SEEK_SYNC0;
			}
			break;

		case COLLECT:
			rx_buf[rx_idx++] = current_byte;
			k_timer_start(&byte_timer, K_MSEC(BYTE_TIMEOUT_MS), K_NO_WAIT); /* reset watchdog */
			if (rx_idx >= CMD_FRAME_LEN) {
				k_timer_stop(&byte_timer);
				handle_complete_frame();
				rx_state = SEEK_SYNC0;
				rx_idx = 0;
			}
			break;
		}
	}
}

void pi_stm32_uart_init(void)
{
	uart_irq_callback_user_data_set(uart_dev, uart_isr, NULL);
	uart_irq_rx_enable(uart_dev);
}

bool pi_stm32_uart_get_latest_cmd(cmd_frame_t *out)
{
	k_spinlock_key_t key = k_spin_lock(&cmd_lock);
	bool valid = have_cmd;
	if (valid) {
		*out = latest_cmd;
	}
	k_spin_unlock(&cmd_lock, key);
	return valid;
}

uint32_t pi_stm32_uart_ms_since_last_cmd(void)
{
	k_spinlock_key_t key = k_spin_lock(&cmd_lock);
	int64_t last = last_cmd_uptime_ms;
	k_spin_unlock(&cmd_lock, key);
	return (uint32_t)(k_uptime_get() - last);
}

void pi_stm32_uart_send_status(const status_frame_t *st)
{
	uint8_t buf[STATUS_FRAME_LEN];
	buf[0] = SYNC0;
	buf[1] = SYNC1;
	buf[2] = TYPE_STATUS;
	buf[3] = st->seq;
	buf[4] = st->state;

	const float currents[] = {
		st->motor1_current,
		st->motor2_current,
		st->servo_current,
	};
	for (size_t i = 0; i < ARRAY_SIZE(currents); i++) {
		uint32_t bits;
		memcpy(&bits, &currents[i], sizeof(bits));
		size_t offset = 5 + i * sizeof(bits);
		buf[offset] = bits & 0xFF;
		buf[offset + 1] = (bits >> 8) & 0xFF;
		buf[offset + 2] = (bits >> 16) & 0xFF;
		buf[offset + 3] = (bits >> 24) & 0xFF;
	}

	uint16_t crc = crc16_ccitt(0xFFFF, &buf[2], 15);
	buf[17] = crc & 0xFF;
	buf[18] = (crc >> 8) & 0xFF;

	for (int i = 0; i < STATUS_FRAME_LEN; i++) {
		uart_poll_out(uart_dev, buf[i]);
	}
}