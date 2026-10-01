#ifndef CURRENT_H
#define CURRENT_H

#include <stdint.h>

enum current_id {
    CURRENT_MOTOR_A,   /* left motor: A2 / PA4 / IN4 -> index 0 */
    CURRENT_MOTOR_B,   /* right motor: A3 / PB0 / IN8 -> index 1 */
    CURRENT_SERVO,     /* steering: A1 / PA1 / IN1 -> index 2 */
    CURRENT_COUNT
};
/* Configures the ADC channel(s). Returns 0 on success, or a negative
 * errno (-ENODEV if the ADC device isn't ready, or whatever
 * adc_channel_setup_dt returns). */
int current_init(void);

/* Reads one channel and returns the raw ADC count (0..4095 for 12-bit).
 * Returns a negative errno on failure. */
int current_read_raw(enum current_id id);

/* Converts a raw ADC count on the STM32 pin back to the current in amps
 * that the ACS712 is reporting, undoing the voltage divider. */
float current_raw_to_amps(int32_t raw_count);

/* Convenience: reads and converts in one call. Returns 0 on success and
 * writes the result to *out_amps; returns a negative errno on failure. */
int current_read_amps(enum current_id id, float *out_amps);

#endif /* CURRENT_H */