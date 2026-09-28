#ifndef SERVO_H
#define SERVO_H

#include <stdint.h>

#define SERVO_PERIOD_US       20000U
#define SERVO_PULSE_MIN_US    500U
#define SERVO_PULSE_MAX_US    2500U
#define SERVO_PULSE_CENTER_US 1500U
#define MAX_WHEEL_STATE       1000
#define MIN_WHEEL_STATE       1000

/* Configure the servo PWM and start at the neutral pulse width. */
int servo_init(void);

/* Set the pulse width; values are clamped to the supported range. */
int servo_set_pulse_us(uint32_t pulse_us);

int set_wheel_angle(int32_t wheel_state);

#endif