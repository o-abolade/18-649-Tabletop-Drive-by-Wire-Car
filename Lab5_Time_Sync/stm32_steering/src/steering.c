#include <zephyr/kernel.h>
#include <zephyr/drivers/pwm.h>

#include "steering.h"

static const struct pwm_dt_spec servo_pwm =
	PWM_DT_SPEC_GET(DT_ALIAS(servo_pwm));

int servo_init(void)
{
	if (!pwm_is_ready_dt(&servo_pwm)) {
		return -ENODEV;
	}

	return servo_set_pulse_us(SERVO_PULSE_CENTER_US);
}

int servo_set_pulse_us(uint32_t pulse_us)
{
	if (pulse_us < SERVO_PULSE_MIN_US) {
		pulse_us = SERVO_PULSE_MIN_US;
	} else if (pulse_us > SERVO_PULSE_MAX_US) {
		pulse_us = SERVO_PULSE_MAX_US;
	}
	return pwm_set_dt(&servo_pwm, servo_pwm.period, PWM_USEC(pulse_us));
}

int servo_disable(void)
{
    return pwm_set_dt(&servo_pwm, servo_pwm.period, PWM_USEC(0));
}

// according to currently defined mapping -1000 to 1000
int set_wheel_angle(int16_t wheel_state)
{
    if (wheel_state < MIN_WHEEL_STATE) wheel_state = MIN_WHEEL_STATE;
    if (wheel_state > MAX_WHEEL_STATE) wheel_state = MAX_WHEEL_STATE;

    int32_t pulse;
    if (wheel_state >= 0) {
        pulse = SERVO_PULSE_CENTER_US +
                (wheel_state * (int32_t)(SERVO_PULSE_RIGHT_US - SERVO_PULSE_CENTER_US)) / 1000;
    } else {
        pulse = SERVO_PULSE_CENTER_US +
                (wheel_state * (int32_t)(SERVO_PULSE_CENTER_US - SERVO_PULSE_LEFT_US)) / 1000;
    }
    return servo_set_pulse_us((uint32_t)pulse);
}