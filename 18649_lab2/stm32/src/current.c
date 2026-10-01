#include <zephyr/kernel.h>
#include <zephyr/drivers/adc.h>
#include "current.h"

/* ---- ACS712 5A variant (ACS712ELCTR-05B-T), confirmed from datasheet ---- */
#define ADC_VREF_MV       3300
#define ADC_MAX_COUNT     4096      /* 12-bit resolution */
#define ACS712_VCC_MV     5000
#define ACS712_ZERO_MV    (ACS712_VCC_MV / 2)  /* nominal VIOUT(Q), 2500 mV */
#define ACS712_MV_PER_A   185.0f    /* 5A variant: 180-190 mV/A, typ 185 */

static const struct adc_dt_spec current_channels[CURRENT_COUNT] = {
    [CURRENT_MOTOR_A] = ADC_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 0),
    [CURRENT_MOTOR_B] = ADC_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 1),
    [CURRENT_SERVO]   = ADC_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 2),
};

static int16_t sample_buf;

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
    if (id >= CURRENT_COUNT) {
        return -EINVAL;
    }

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
    return (pin_mv - ACS712_ZERO_MV) / ACS712_MV_PER_A;
}

int current_read_amps(enum current_id id, float *out_amps)
{
    int raw = current_read_raw(id);
    if (raw < 0) {
        return raw;
    }
    *out_amps = current_raw_to_amps(raw);
    return 0;
}