#ifndef MOTOR_CONTROL_H
#define MOTOR_CONTROL_H

#include <stdbool.h>

#define MOTOR_TEST_DURATION_MS  3000U
#define MOTOR_TEST_DUTY_PERCENT 100U

/*
 * Low-level interface to the L298N. Initialization applies dynamic braking
 * (both PWM enables at 100% and all four direction inputs high); subsequent
 * commands change direction only after first disabling the PWM enable.
 */

int motor_control_init(void);
int motor_control_safe_stop(void);

enum motor_control_channel {
	MOTOR_CHANNEL_A, /* Left: L298N OUT1 / OUT2, controlled by ENA / IN1 / IN2 */
	MOTOR_CHANNEL_B, /* Right: L298N OUT3 / OUT4, controlled by ENB / IN3 / IN4 */
};

enum motor_control_state {
	MOTOR_CONTROL_COAST,
	MOTOR_CONTROL_FORWARD,
	MOTOR_CONTROL_REVERSE,
	MOTOR_CONTROL_BRAKE,
};

/* Set one H-bridge channel. Duty is a percent (0 through 100). */
int motor_control_set_state(enum motor_control_channel channel,
			    enum motor_control_state state, unsigned int duty_percent);

/* Apply the same forward duty to both physical wheels. */
int motor_control_drive_forward(unsigned int duty_percent);
int motor_control_dynamic_brake(void);

/*
 * Bench-only proof of the wired output path. This drives exactly one channel
 * at the configured test duty/duration, then returns to dynamic braking.
 */
int motor_control_pulse(enum motor_control_channel channel);
bool motor_control_manual_test_active(void);

#endif /* MOTOR_CONTROL_H */
