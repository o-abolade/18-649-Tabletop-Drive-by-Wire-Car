#ifndef PI_STM32_UART_H
#define PI_STM32_UART_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
	int16_t  steering;
	uint16_t throttle;
	uint16_t brake;
	uint8_t  buttons;
	uint8_t  seq;
} cmd_frame_t;

typedef struct {
	uint8_t  state;      /* 0=INIT,1=NORMAL,2=FAILSAFE,3=SELFTEST */
	uint8_t  seq;
	float    motor1_current; /* amps */
	float    motor2_current; /* amps */
	float    servo_current;  /* amps */
} status_frame_t;

extern struct k_sem cmd_received_sem;

void pi_stm32_uart_init(void);

/* Returns true if a valid command has ever been received.
 * Always fills *out with the latest known command (even if stale) —
 * caller checks staleness separately via pi_stm32_uart_ms_since_last_cmd(). */
bool pi_stm32_uart_get_latest_cmd(cmd_frame_t *out);

uint32_t pi_stm32_uart_ms_since_last_cmd(void);

/* Returns true once for each malformed, wrong-type, or out-of-range command
 * frame. The control state machine uses this to enter its safe error state. */
bool pi_stm32_uart_take_invalid_cmd(void);

void pi_stm32_uart_send_status(const status_frame_t *st);

#endif
