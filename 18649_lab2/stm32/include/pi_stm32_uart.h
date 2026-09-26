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
	uint16_t motor1_current;
	uint16_t motor2_current;
	uint16_t servo_current;
} status_frame_t;

void pi_stm32_uart_init(void);

/* Returns true if a valid command has ever been received.
 * Always fills *out with the latest known command (even if stale) —
 * caller checks staleness separately via pi_stm32_uart_ms_since_last_cmd(). */
bool pi_stm32_uart_get_latest_cmd(cmd_frame_t *out);

uint32_t pi_stm32_uart_ms_since_last_cmd(void);

void pi_stm32_uart_send_status(const status_frame_t *st);

#endif