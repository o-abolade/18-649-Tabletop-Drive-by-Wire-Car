#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include "blinker.h"

static const struct gpio_dt_spec blinkers[BLINKER_COUNT] = {
    [BLINKER_FL] = GPIO_DT_SPEC_GET(DT_ALIAS(blinker_fl), gpios),
    [BLINKER_FR] = GPIO_DT_SPEC_GET(DT_ALIAS(blinker_fr), gpios),
    [BLINKER_RL] = GPIO_DT_SPEC_GET(DT_ALIAS(blinker_rl), gpios),
    [BLINKER_RR] = GPIO_DT_SPEC_GET(DT_ALIAS(blinker_rr), gpios),
};

int blinker_init(enum blinker_id id) {
    if (id >= BLINKER_COUNT) {
        return -EINVAL;
    }
    if (!gpio_is_ready_dt(&blinkers[id])) {
            return -ENODEV;
    }
    return gpio_pin_configure_dt(&blinkers[id], GPIO_OUTPUT_INACTIVE);
}

int blinker_init_all(void)
{
    for (int i = 0; i < BLINKER_COUNT; i++) {
        int ret;
        if ((ret = blinker_init(i)) < 0) {
            return ret;
        }
    }
    return 0;
}

void blinker_set(enum blinker_id id, bool on)
{
    if (id < BLINKER_COUNT) {
        gpio_pin_set_dt(&blinkers[id], on);
    }
}