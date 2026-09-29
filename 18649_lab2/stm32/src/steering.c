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

	return pwm_set_dt(&servo_pwm, servo_pwm.period, PWM_USEC(pulse_us));
}

int servo_disable(void)
{
    return pwm_set_dt(&servo_pwm, servo_pwm.period, PWM_USEC(0));
}

// according to currently defined mapping -1000 to 1000
int set_wheel_angle(int16_t wheel_state) 
{
    if (wheel_state < MIN_WHEEL_STATE) {
        wheel_state = MIN_WHEEL_STATE;
    } else if (wheel_state > MAX_WHEEL_STATE) {
        wheel_state = MAX_WHEEL_STATE;
    }
    return servo_set_pulse_us((uint32_t)(1500 + wheel_state));
}