#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include "testpoints.h"

static const struct gpio_dt_spec testpoints[TESTPOINT_COUNT] = {
    [TP_PWM_SET] = GPIO_DT_SPEC_GET(DT_ALIAS(tp_pwm_set), gpios),
};

int testpoints_init(void)
{
    for (int i = 0; i < TESTPOINT_COUNT; i++) {
        if (!gpio_is_ready_dt(&testpoints[i])) {
            return -ENODEV;
        }
        int ret = gpio_pin_configure_dt(&testpoints[i], GPIO_OUTPUT_INACTIVE);
        if (ret < 0) {
            return ret;
        }
    }
    return 0;
}

void testpoint_set(enum testpoint_id id, bool on)
{
    if (id < TESTPOINT_COUNT) {
        gpio_pin_set_dt(&testpoints[id], on);
    }
}
