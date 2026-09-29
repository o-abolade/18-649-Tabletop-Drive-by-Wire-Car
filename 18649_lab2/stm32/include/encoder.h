#ifndef ENCODER_H
#define ENCODER_H

#include <stdint.h>

/* Configure the four wired quadrature inputs and their edge interrupts. */
int encoder_init(void);

/* Signed transition counts since boot or the last encoder_zero_counts(). */
void encoder_get_counts(int32_t *right_count, int32_t *left_count);
void encoder_zero_counts(void);

/* Raw GPIO interrupt counts, used only to diagnose encoder wiring. */
void encoder_get_edge_counts(int32_t *right_a, int32_t *right_b,
			     int32_t *left_a, int32_t *left_b);

/* Read the live logic levels for wiring diagnosis; does not affect counts. */
int encoder_get_levels(uint8_t *right_a, uint8_t *right_b,
		       uint8_t *left_a, uint8_t *left_b);

#endif /* ENCODER_H */
