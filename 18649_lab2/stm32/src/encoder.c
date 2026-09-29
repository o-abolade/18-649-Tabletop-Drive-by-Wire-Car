#include <errno.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#include "encoder.h"

/*
 * Each encoder is decoded at x4: an interrupt on either A or B phase reads
 * both phases and advances the signed transition count.  The sign is a
 * wiring convention; reversing a wheel reverses its sign, which we will
 * normalize when the speed controller is added.
 */
struct quadrature_encoder {
	const char *name;
	struct gpio_dt_spec phase_a;
	struct gpio_dt_spec phase_b;
	struct gpio_callback phase_a_callback;
	struct gpio_callback phase_b_callback;
	atomic_t count;
	atomic_t phase_a_edges;
	atomic_t phase_b_edges;
	uint8_t previous_state;
};

static struct quadrature_encoder right_encoder = {
	.name = "right",
	.phase_a = GPIO_DT_SPEC_GET(DT_ALIAS(encoder_right_a), gpios),
	.phase_b = GPIO_DT_SPEC_GET(DT_ALIAS(encoder_right_b), gpios),
};

static struct quadrature_encoder left_encoder = {
	.name = "left",
	.phase_a = GPIO_DT_SPEC_GET(DT_ALIAS(encoder_left_a), gpios),
	.phase_b = GPIO_DT_SPEC_GET(DT_ALIAS(encoder_left_b), gpios),
};

/* Valid Gray-code transitions in the order 00 -> 01 -> 11 -> 10 -> 00. */
static const int8_t quadrature_delta[16] = {
	[0x1] = 1,  [0x4] = -1,
	[0x2] = -1, [0x7] = 1,
	[0x8] = 1,  [0xB] = -1,
	[0xD] = -1, [0xE] = 1,
};

static int read_state(const struct quadrature_encoder *encoder, uint8_t *state)
{
	int a = gpio_pin_get_dt(&encoder->phase_a);
	int b = gpio_pin_get_dt(&encoder->phase_b);

	if (a < 0) {
		return a;
	}
	if (b < 0) {
		return b;
	}

	*state = ((uint8_t)(a != 0) << 1) | (uint8_t)(b != 0);
	return 0;
}

static void count_transition(struct quadrature_encoder *encoder)
{
	uint8_t state;

	/* Interrupt context: keep this bounded; never print or calculate speed. */
	if (read_state(encoder, &state) == 0) {
		atomic_add(&encoder->count,
			   quadrature_delta[(encoder->previous_state << 2) | state]);
		encoder->previous_state = state;
	}
}

static void phase_a_callback(const struct device *port,
			     struct gpio_callback *callback, gpio_port_pins_t pins)
{
	ARG_UNUSED(port);
	ARG_UNUSED(pins);
	struct quadrature_encoder *encoder = CONTAINER_OF(
		callback, struct quadrature_encoder, phase_a_callback);

	atomic_inc(&encoder->phase_a_edges);
	count_transition(encoder);
}

static void phase_b_callback(const struct device *port,
			     struct gpio_callback *callback, gpio_port_pins_t pins)
{
	ARG_UNUSED(port);
	ARG_UNUSED(pins);
	struct quadrature_encoder *encoder = CONTAINER_OF(
		callback, struct quadrature_encoder, phase_b_callback);

	atomic_inc(&encoder->phase_b_edges);
	count_transition(encoder);
}

static int configure_encoder(struct quadrature_encoder *encoder)
{
	uint8_t state;
	int rc;

	if (!gpio_is_ready_dt(&encoder->phase_a) ||
	    !gpio_is_ready_dt(&encoder->phase_b)) {
		printk("%s encoder GPIO is not ready\n", encoder->name);
		return -ENODEV;
	}

	/* The internal pull-ups provide an idle level for open-drain encoders. */
	rc = gpio_pin_configure_dt(&encoder->phase_a, GPIO_INPUT | GPIO_PULL_UP);
	if (rc != 0) {
		return rc;
	}
	rc = gpio_pin_configure_dt(&encoder->phase_b, GPIO_INPUT | GPIO_PULL_UP);
	if (rc != 0) {
		return rc;
	}

	rc = read_state(encoder, &state);
	if (rc != 0) {
		return rc;
	}
	encoder->previous_state = state;
	atomic_set(&encoder->count, 0);
	atomic_set(&encoder->phase_a_edges, 0);
	atomic_set(&encoder->phase_b_edges, 0);

	gpio_init_callback(&encoder->phase_a_callback, phase_a_callback,
			   BIT(encoder->phase_a.pin));
	gpio_init_callback(&encoder->phase_b_callback, phase_b_callback,
			   BIT(encoder->phase_b.pin));

	rc = gpio_add_callback(encoder->phase_a.port, &encoder->phase_a_callback);
	if (rc != 0) {
		return rc;
	}
	rc = gpio_add_callback(encoder->phase_b.port, &encoder->phase_b_callback);
	if (rc != 0) {
		return rc;
	}

	rc = gpio_pin_interrupt_configure_dt(&encoder->phase_a, GPIO_INT_EDGE_BOTH);
	if (rc != 0) {
		return rc;
	}
	return gpio_pin_interrupt_configure_dt(&encoder->phase_b, GPIO_INT_EDGE_BOTH);
}

int encoder_init(void)
{
	int rc = configure_encoder(&right_encoder);

	if (rc != 0) {
		return rc;
	}
	rc = configure_encoder(&left_encoder);
	if (rc == 0) {
		printk("Encoders ready: right D3/D11, left D12/D14\n");
	}
	return rc;
}

void encoder_get_counts(int32_t *right_count, int32_t *left_count)
{
	if (right_count != NULL) {
		*right_count = (int32_t)atomic_get(&right_encoder.count);
	}
	if (left_count != NULL) {
		*left_count = (int32_t)atomic_get(&left_encoder.count);
	}
}

void encoder_zero_counts(void)
{
	atomic_set(&right_encoder.count, 0);
	atomic_set(&left_encoder.count, 0);
	atomic_set(&right_encoder.phase_a_edges, 0);
	atomic_set(&right_encoder.phase_b_edges, 0);
	atomic_set(&left_encoder.phase_a_edges, 0);
	atomic_set(&left_encoder.phase_b_edges, 0);
}

void encoder_get_edge_counts(int32_t *right_a, int32_t *right_b,
			     int32_t *left_a, int32_t *left_b)
{
	if (right_a != NULL) {
		*right_a = (int32_t)atomic_get(&right_encoder.phase_a_edges);
	}
	if (right_b != NULL) {
		*right_b = (int32_t)atomic_get(&right_encoder.phase_b_edges);
	}
	if (left_a != NULL) {
		*left_a = (int32_t)atomic_get(&left_encoder.phase_a_edges);
	}
	if (left_b != NULL) {
		*left_b = (int32_t)atomic_get(&left_encoder.phase_b_edges);
	}
}

int encoder_get_levels(uint8_t *right_a, uint8_t *right_b,
		       uint8_t *left_a, uint8_t *left_b)
{
	uint8_t right_state;
	uint8_t left_state;
	int rc = read_state(&right_encoder, &right_state);

	if (rc != 0) {
		return rc;
	}
	rc = read_state(&left_encoder, &left_state);
	if (rc != 0) {
		return rc;
	}

	*right_a = (right_state >> 1) & 1U;
	*right_b = right_state & 1U;
	*left_a = (left_state >> 1) & 1U;
	*left_b = left_state & 1U;
	return 0;
}
