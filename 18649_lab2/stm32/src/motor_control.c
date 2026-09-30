#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/atomic.h>

#include "motor_control.h"

/*
 * Device-tree aliases make this file read like the wiring diagram rather
 * than scattering port/pin numbers through the control code.
 */
static const struct pwm_dt_spec motor_left_pwm =
	PWM_DT_SPEC_GET(DT_ALIAS(motor_left_pwm));
static const struct pwm_dt_spec motor_right_pwm =
	PWM_DT_SPEC_GET(DT_ALIAS(motor_right_pwm));

static const struct gpio_dt_spec motor_left_in1 =
	GPIO_DT_SPEC_GET(DT_ALIAS(motor_left_in1), gpios);
static const struct gpio_dt_spec motor_left_in2 =
	GPIO_DT_SPEC_GET(DT_ALIAS(motor_left_in2), gpios);
static const struct gpio_dt_spec motor_right_in3 =
	GPIO_DT_SPEC_GET(DT_ALIAS(motor_right_in3), gpios);
static const struct gpio_dt_spec motor_right_in4 =
	GPIO_DT_SPEC_GET(DT_ALIAS(motor_right_in4), gpios);

static const struct gpio_dt_spec *const direction_pins[] = {
	&motor_left_in1,
	&motor_left_in2,
	&motor_right_in3,
	&motor_right_in4,
};

static atomic_t manual_test_active = ATOMIC_INIT(0);

static int first_error(int previous, int candidate)
{
	return previous != 0 ? previous : candidate;
}

static int set_directions(const struct gpio_dt_spec *first,
			  const struct gpio_dt_spec *second, int first_value,
			  int second_value)
{
	int rc = gpio_pin_set_dt(first, first_value);

	return first_error(rc, gpio_pin_set_dt(second, second_value));
}

static int set_pwm_percent(const struct pwm_dt_spec *pwm, unsigned int duty_percent)
{
	if (duty_percent > 100U) {
		return -EINVAL;
	}

	return pwm_set_dt(pwm, pwm->period,
			  (pwm->period * duty_percent) / 100U);
}

int motor_control_safe_stop(void)
{
	int rc = 0;

	/* Disable the two L298N channels before changing either direction pair. */
	rc = first_error(rc, pwm_set_dt(&motor_left_pwm, motor_left_pwm.period, 0));
	rc = first_error(rc, pwm_set_dt(&motor_right_pwm, motor_right_pwm.period, 0));

	/* Both inputs low with enable low is a non-driving/coast state. */
	for (size_t i = 0; i < ARRAY_SIZE(direction_pins); ++i) {
		rc = first_error(rc, gpio_pin_set_dt(direction_pins[i], 0));
	}

	return rc;
}

int motor_control_set_state(enum motor_control_channel channel,
			    enum motor_control_state state, unsigned int duty_percent)
{
	const struct pwm_dt_spec *pwm;
	const struct gpio_dt_spec *first;
	const struct gpio_dt_spec *second;
	int forward_first;
	int forward_second;
	int rc;

	if (channel == MOTOR_CHANNEL_A) {
		pwm = &motor_left_pwm;
		first = &motor_left_in1;
		second = &motor_left_in2;
		/* Left motor: red -> OUT1, black -> OUT2; verified forward polarity. */
		forward_first = 1;
		forward_second = 0;
	} else if (channel == MOTOR_CHANNEL_B) {
		pwm = &motor_right_pwm;
		first = &motor_right_in3;
		second = &motor_right_in4;
		/* Right motor: verified forward polarity on OUT3/OUT4. */
		forward_first = 0;
		forward_second = 1;
	} else {
		return -EINVAL;
	}

	if (state != MOTOR_CONTROL_BRAKE && duty_percent > 100U) {
		return -EINVAL;
	}

	/* Never change H-bridge direction while its PWM enable is asserted. */
	rc = set_pwm_percent(pwm, 0U);
	if (rc != 0) {
		return rc;
	}

	switch (state) {
	case MOTOR_CONTROL_COAST:
		return set_directions(first, second, 0, 0);
	case MOTOR_CONTROL_FORWARD:
		rc = set_directions(first, second, forward_first, forward_second);
		break;
	case MOTOR_CONTROL_REVERSE:
		rc = set_directions(first, second, !forward_first, !forward_second);
		break;
	case MOTOR_CONTROL_BRAKE:
		rc = set_directions(first, second, 0, 0);
		duty_percent = 100U;
		break;
	default:
		return -EINVAL;
	}

	if (rc != 0) {
		return rc;
	}
	return set_pwm_percent(pwm, duty_percent);
}

int motor_control_drive_forward(unsigned int duty_percent)
{
	int rc = motor_control_set_state(MOTOR_CHANNEL_A, MOTOR_CONTROL_FORWARD,
					 duty_percent);

	return first_error(rc, motor_control_set_state(MOTOR_CHANNEL_B,
						      MOTOR_CONTROL_FORWARD,
						      duty_percent));
}

int motor_control_dynamic_brake(void)
{
	int rc = motor_control_set_state(MOTOR_CHANNEL_A, MOTOR_CONTROL_BRAKE, 0U);

	return first_error(rc, motor_control_set_state(MOTOR_CHANNEL_B,
						      MOTOR_CONTROL_BRAKE, 0U));
}

int motor_control_init(void)
{
	int rc;

	if (!pwm_is_ready_dt(&motor_left_pwm) || !pwm_is_ready_dt(&motor_right_pwm)) {
		printk("Motor PWM device is not ready\n");
		return -ENODEV;
	}

	for (size_t i = 0; i < ARRAY_SIZE(direction_pins); ++i) {
		if (!gpio_is_ready_dt(direction_pins[i])) {
			printk("Motor direction GPIO is not ready\n");
			return -ENODEV;
		}

		rc = gpio_pin_configure_dt(direction_pins[i], GPIO_OUTPUT_INACTIVE);
		if (rc != 0) {
			return rc;
		}
	}

	rc = motor_control_safe_stop();
	if (rc == 0) {
		printk("Motor outputs initialized safe: PWM=0, IN1..IN4=0\n");
	}

	return rc;
}

bool motor_control_manual_test_active(void)
{
	return atomic_get(&manual_test_active) != 0;
}

int motor_control_pulse(enum motor_control_channel channel)
{
	int rc;

	if (channel != MOTOR_CHANNEL_A && channel != MOTOR_CHANNEL_B) {
		return -EINVAL;
	}

	/* A second shell request cannot overlap an active timed pulse. */
	if (!atomic_cas(&manual_test_active, 0, 1)) {
		return -EBUSY;
	}

	/* Begin from a known non-driving state before selecting a direction. */
	rc = motor_control_safe_stop();
	if (rc != 0) {
		goto done;
	}

	rc = motor_control_set_state(channel, MOTOR_CONTROL_FORWARD,
				     MOTOR_TEST_DUTY_PERCENT);
	if (rc == 0) {
		k_sleep(K_MSEC(MOTOR_TEST_DURATION_MS));
	}

	/* This runs after success or an error: no test leaves PWM enabled. */
	rc = first_error(rc, motor_control_safe_stop());

done:
	atomic_set(&manual_test_active, 0);
	return rc;
}
