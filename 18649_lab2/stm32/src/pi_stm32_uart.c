#include "pi_stm32_uart.h"
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/crc.h>
#include <string.h>

#define SYNC0       0xAA
#define SYNC1       0x55
#define TYPE_CMD    0x01
#define TYPE_STATUS 0x02
#define FRAME_LEN   13          // total bytes including sync+crc
#define PAYLOAD_LEN (FRAME_LEN - 2 - 2)  // minus 2 sync, minus 2 crc
#define BYTE_TIMEOUT_MS 20

// Validation ranges
#define STEERING_MIN -1000
#define STEERING_MAX 1000
#define THROTTLE_MAX 1000
#define BRAKE_MAX 1000

static const struct device *uart_dev = DEVICE_DT_GET(DT_ALIAS(rpilink));

// RX parser state machine
enum rx_state { SEEK_SYNC0, SEEK_SYNC1, COLLECT };

static enum rx_state rx_state = SEEK_SYNC0;
static uint8_t  rx_buf[FRAME_LEN];
static uint8_t  rx_idx;

// Shared "latest command" slot
static struct k_spinlock cmd_lock;
static cmd_frame_t latest_cmd;
static bool        have_cmd;
static int64_t     last_cmd_uptime_ms;

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
			if (rx_idx >= FRAME_LEN) {
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
	uint8_t buf[FRAME_LEN];
	buf[0] = SYNC0;
	buf[1] = SYNC1;
	buf[2] = TYPE_STATUS;
	buf[3] = 0; // or mirror last cmd->seq if you thread it through
	buf[4] = st->state;
	buf[5] = st->motor1_current & 0xFF;
	buf[6] = (st->motor1_current >> 8) & 0xFF;
	buf[7] = st->motor2_current & 0xFF;
	buf[8] = (st->motor2_current >> 8) & 0xFF;
	buf[9] = st->servo_current & 0xFF;
	buf[10] = (st->servo_current >> 8) & 0xFF;

	uint16_t crc = crc16_ccitt(0xFFFF, &buf[2], 9);
	buf[11] = crc & 0xFF;
	buf[12] = (crc >> 8) & 0xFF;

	for (int i = 0; i < FRAME_LEN; i++) {
		uart_poll_out(uart_dev, buf[i]);
	}
}