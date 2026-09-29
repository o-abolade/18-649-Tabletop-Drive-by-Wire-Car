#ifndef MOTOR_CONTROL_H
#define MOTOR_CONTROL_H

#include <stdbool.h>

/*
 * Safe low-level interface to the L298N.
 *
 * The initial implementation deliberately has no drive command.  It only
 * configures the six wired control pins and forces a safe coast state:
 * PWM enables at 0% and all four direction inputs low.
 */

int motor_control_init(void);
int motor_control_safe_stop(void);

enum motor_control_channel {
	MOTOR_CHANNEL_A, /* L298N OUT1 / OUT2, controlled by ENA / IN1 / IN2 */
	MOTOR_CHANNEL_B, /* L298N OUT3 / OUT4, controlled by ENB / IN3 / IN4 */
};

/*
 * Bench-only proof of the wired output path. This drives exactly one channel
 * at 50% PWM for 300 ms, then always returns to motor_control_safe_stop().
 */
int motor_control_pulse(enum motor_control_channel channel);
bool motor_control_manual_test_active(void);

#endif /* MOTOR_CONTROL_H */
