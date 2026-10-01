#include <zephyr/kernel.h>
#include <zephyr/drivers/adc.h>
#include "current.h"

/* The sensor output is divided by 2 before reaching the STM32 ADC pin. */
#define ADC_VREF_MV       3300
#define ADC_MAX_COUNT     4095      /* maximum code for a 12-bit ADC */
#define DIVIDER_RATIO     2.0f
#define CURRENT_ZERO_MV   2500.0f
#define CURRENT_MV_PER_A  185.0f    /* 2.5 V to 5 V spans 0 A to 5 A */

static const struct adc_dt_spec current_channels[CURRENT_COUNT] = {
    [CURRENT_MOTOR_A] = ADC_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 0),
    [CURRENT_MOTOR_B] = ADC_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 1),
    [CURRENT_SERVO]   = ADC_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 2),
};

int current_init(void)
{
    for (int i = 0; i < CURRENT_COUNT; i++) {
        if (!adc_is_ready_dt(&current_channels[i])) {
            return -ENODEV;
        }
        int ret = adc_channel_setup_dt(&current_channels[i]);
        if (ret < 0) {
            return ret;
        }
    }
    return 0;
}

int current_read_raw(enum current_id id)
{
    if (id < 0 || id >= CURRENT_COUNT) {
        return -EINVAL;
    }

    int16_t sample_buf;
    struct adc_sequence sequence = {
        .buffer = &sample_buf,
        .buffer_size = sizeof(sample_buf),
    };

    int ret = adc_sequence_init_dt(&current_channels[id], &sequence);
    if (ret < 0) {
        return ret;
    }

    ret = adc_read_dt(&current_channels[id], &sequence);
    if (ret < 0) {
        return ret;
    }

    return sample_buf;
}

float current_raw_to_amps(int32_t raw_count)
{
    float pin_mv = (raw_count * (float)ADC_VREF_MV) / ADC_MAX_COUNT;
    float sensor_mv = pin_mv * DIVIDER_RATIO;
    return (sensor_mv - CURRENT_ZERO_MV) / CURRENT_MV_PER_A;
}

int current_read_amps(enum current_id id, float *out_amps)
{
    if (out_amps == NULL) {
        return -EINVAL;
    }

    int raw = current_read_raw(id);
    if (raw < 0) {
        return raw;
    }
    *out_amps = current_raw_to_amps(raw);
    return 0;
}