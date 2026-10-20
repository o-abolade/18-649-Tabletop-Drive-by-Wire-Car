#include <errno.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/can.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/kernel.h>
#include <zephyr/spinlock.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#include "can_link.h"

/* Test-point pulses are held at least this long so a scope can trigger on
 * them (Lab 2: back-to-back set/clear was too narrow to see). */
#define TP_MIN_PULSE_US 10

#define MCP2515_OP_READ      0x03
#define MCP2515_REG_CANSTAT  0x0E
#define MCP2515_OPMOD_CONFIG 0x4
#define PROBE_SPI_OP (SPI_WORD_SET(8) | SPI_TRANSFER_MSB | SPI_OP_MODE_CONTROLLER)

/* One dedup/seq slot per catalog message. FAULT can come from any zone, each
 * with its own counter, so it gets one slot per sending zone. */
enum {
	SLOT_CTRL,
	SLOT_HB_N1, SLOT_HB_N2, SLOT_HB_N3,
	SLOT_BLINK,
	SLOT_FORCE,
	SLOT_FAULT_N1, SLOT_FAULT_N2, SLOT_FAULT_N3,
	NUM_SLOTS,
};

static const uint32_t catalog_ids[] = {
	CANP_ID_CTRL, CANP_ID_FAULT, CANP_ID_HB_N1, CANP_ID_HB_N2, CANP_ID_HB_N3,
	CANP_ID_BLINK, CANP_ID_FORCE,
};

struct bus {
	const char *name;
	const struct device *dev;
	struct spi_dt_spec spi;
	struct gpio_dt_spec tp_rx;
	struct gpio_dt_spec tp_tx;
	bool started;
};

static struct bus buses[CANP_NUM_BUSES] = {
	[CANP_BUS_A] = {
		.name = "CAN A",
		.dev = DEVICE_DT_GET(DT_ALIAS(can_a)),
		.spi = SPI_DT_SPEC_GET(DT_ALIAS(can_a), PROBE_SPI_OP),
		.tp_rx = GPIO_DT_SPEC_GET(DT_ALIAS(tp_can_rx_a), gpios),
		.tp_tx = GPIO_DT_SPEC_GET(DT_ALIAS(tp_can_tx_a), gpios),
	},
	[CANP_BUS_B] = {
		.name = "CAN B",
		.dev = DEVICE_DT_GET(DT_ALIAS(can_b)),
		.spi = SPI_DT_SPEC_GET(DT_ALIAS(can_b), PROBE_SPI_OP),
		.tp_rx = GPIO_DT_SPEC_GET(DT_ALIAS(tp_can_rx_b), gpios),
		.tp_tx = GPIO_DT_SPEC_GET(DT_ALIAS(tp_can_tx_b), gpios),
	},
};

static can_link_rx_cb_t user_rx_cb;

/* Guards dedup, last_hb_ms, tx_seq and stats. Touched from both MCP2515 RX
 * threads and from app threads; never held across an SPI transaction. */
static struct k_spinlock lock;
static struct canp_dedup dedup[NUM_SLOTS];
static uint8_t tx_seq[NUM_SLOTS];
static int64_t last_hb_ms[CANP_NUM_ZONES + 1][CANP_NUM_BUSES];
static struct can_link_stats stats;

static int slot_of(uint32_t id, const uint8_t *data)
{
	switch (id) {
	case CANP_ID_CTRL:  return SLOT_CTRL;
	case CANP_ID_HB_N1: return SLOT_HB_N1;
	case CANP_ID_HB_N2: return SLOT_HB_N2;
	case CANP_ID_HB_N3: return SLOT_HB_N3;
	case CANP_ID_BLINK: return SLOT_BLINK;
	case CANP_ID_FORCE: return SLOT_FORCE;
	case CANP_ID_FAULT: {
		uint8_t zone = canp_fault_zone(data);

		return (zone >= 1 && zone <= CANP_NUM_ZONES) ? SLOT_FAULT_N1 + zone - 1 : -1;
	}
	default:            return -1;
	}
}

static void tp_end(const struct gpio_dt_spec *tp, uint32_t start_cyc)
{
	uint32_t elapsed_us = k_cyc_to_us_floor32(k_cycle_get_32() - start_cyc);

	if (elapsed_us < TP_MIN_PULSE_US) {
		k_busy_wait(TP_MIN_PULSE_US - elapsed_us);
	}
	gpio_pin_set_dt(tp, 0);
}

/* Runs in the MCP2515 driver's cooperative thread for this bus */
static void rx_handler(const struct device *dev, struct can_frame *frame, void *user_data)
{
	enum canp_bus bus = (enum canp_bus)(uintptr_t)user_data;
	const struct gpio_dt_spec *tp = &buses[bus].tp_rx;
	uint32_t start = k_cycle_get_32();
	bool accept = false;

	ARG_UNUSED(dev);
	gpio_pin_set_dt(tp, 1);

	int slot = slot_of(frame->id, frame->data);
	int64_t now = k_uptime_get();
	k_spinlock_key_t key = k_spin_lock(&lock);

	if (frame->dlc != CANP_DLC || (frame->flags & CAN_FRAME_IDE) || slot < 0) {
		stats.rx_bad++;
	} else {
		int zone = canp_hb_zone(frame->id);

		/* Per-bus liveness counts every copy, including duplicates */
		if (zone) {
			last_hb_ms[zone][bus] = now;
		}
		accept = canp_dedup_accept(&dedup[slot], canp_seq(frame->data), now);
		if (accept) {
			stats.rx_accepted++;
		} else {
			stats.rx_dropped++;
		}
	}
	k_spin_unlock(&lock, key);

	if (accept && user_rx_cb) {
		user_rx_cb(frame->id, frame->data, bus);
	}
	tp_end(tp, start);
}

static void tx_done(const struct device *dev, int error, void *user_data)
{
	enum canp_bus bus = (enum canp_bus)(uintptr_t)user_data;

	ARG_UNUSED(dev);
	if (error) {
		k_spinlock_key_t key = k_spin_lock(&lock);

		stats.tx_done_err[bus]++;
		k_spin_unlock(&lock, key);
	}
}

int can_link_send_on(enum canp_bus bus, uint32_t id, const uint8_t data[CANP_DLC])
{
	struct bus *b = &buses[bus];
	struct can_frame f = { .id = id, .dlc = CANP_DLC };

	if (!b->started) {
		return -ENETDOWN;
	}
	memcpy(f.data, data, CANP_DLC);

	uint32_t start = k_cycle_get_32();

	gpio_pin_set_dt(&b->tp_tx, 1);
	/* Never block: a bus that isn't ACKing must not stall the other bus */
	int ret = can_send(b->dev, &f, K_NO_WAIT, tx_done, (void *)(uintptr_t)bus);

	tp_end(&b->tp_tx, start);

	k_spinlock_key_t key = k_spin_lock(&lock);

	if (ret == 0) {
		stats.tx_queued[bus]++;
	} else {
		stats.tx_busy[bus]++;
	}
	k_spin_unlock(&lock, key);
	return ret;
}

int can_link_send(uint32_t id, uint8_t data[CANP_DLC])
{
	int slot = slot_of(id, data);
	int mask = 0;

	if (slot < 0 || id == CANP_ID_CTRL) {
		return -EINVAL;
	}

	k_spinlock_key_t key = k_spin_lock(&lock);

	data[0] = canp_flags(data) | (tx_seq[slot] & CANP_SEQ_MASK);
	tx_seq[slot]++;
	k_spin_unlock(&lock, key);

	for (int bus = 0; bus < CANP_NUM_BUSES; bus++) {
		if (can_link_send_on(bus, id, data) == 0) {
			mask |= BIT(bus);
		}
	}
	return mask;
}

int64_t can_link_last_hb_ms(enum canp_zone zone, enum canp_bus bus)
{
	k_spinlock_key_t key = k_spin_lock(&lock);
	int64_t t = last_hb_ms[zone][bus];

	k_spin_unlock(&lock, key);
	return t;
}

bool can_link_bus_started(enum canp_bus bus)
{
	return buses[bus].started;
}

void can_link_get_stats(struct can_link_stats *out)
{
	k_spinlock_key_t key = k_spin_lock(&lock);

	*out = stats;
	k_spin_unlock(&lock, key);
}

const char *can_link_bus_state(enum canp_bus bus, uint8_t *tec, uint8_t *rec)
{
	enum can_state state;
	struct can_bus_err_cnt cnt = { 0 };

	*tec = 0;
	*rec = 0;
	if (!buses[bus].started) {
		return "DOWN";
	}
	if (can_get_state(buses[bus].dev, &state, &cnt)) {
		return "?";
	}
	*tec = cnt.tx_err_cnt;
	*rec = cnt.rx_err_cnt;
	switch (state) {
	case CAN_STATE_ERROR_ACTIVE:  return "ACTIVE";
	case CAN_STATE_ERROR_WARNING: return "WARNING";
	case CAN_STATE_ERROR_PASSIVE: return "PASSIVE";
	case CAN_STATE_BUS_OFF:       return "BUS-OFF";
	default:                      return "STOPPED";
	}
}

/* The MCP2515 driver's init only writes to the chip, so "device ready" does
 * not prove the module is there. Read CANSTAT directly: a live chip is still
 * in configuration mode (0x80) at this point. */
static bool spi_probe(const struct bus *b)
{
	uint8_t tx[3] = { MCP2515_OP_READ, MCP2515_REG_CANSTAT, 0x00 };
	uint8_t rx[3] = { 0 };
	const struct spi_buf tx_buf = { .buf = tx, .len = sizeof(tx) };
	const struct spi_buf rx_buf = { .buf = rx, .len = sizeof(rx) };
	const struct spi_buf_set tx_set = { .buffers = &tx_buf, .count = 1 };
	const struct spi_buf_set rx_set = { .buffers = &rx_buf, .count = 1 };

	if (spi_transceive_dt(&b->spi, &tx_set, &rx_set)) {
		return false;
	}
	if ((rx[2] >> 5) != MCP2515_OPMOD_CONFIG) {
		printk("%s: MCP2515 not answering on SPI (CANSTAT=0x%02x). "
		       "Check module D9->D12 (MISO), D10->D11 (MOSI), D8->D13, CS, 3V3\n",
		       b->name, rx[2]);
		return false;
	}
	return true;
}

static bool bus_init(struct bus *b, enum canp_bus idx)
{
	gpio_pin_configure_dt(&b->tp_rx, GPIO_OUTPUT_INACTIVE);
	gpio_pin_configure_dt(&b->tp_tx, GPIO_OUTPUT_INACTIVE);

	if (!device_is_ready(b->dev)) {
		printk("%s: driver not ready\n", b->name);
		return false;
	}
	if (!spi_probe(b)) {
		return false;
	}
	if (can_set_mode(b->dev, CAN_MODE_NORMAL)) {
		printk("%s: set normal mode failed\n", b->name);
		return false;
	}
	for (size_t i = 0; i < ARRAY_SIZE(catalog_ids); i++) {
		const struct can_filter f = {
			.id = catalog_ids[i], .mask = CAN_STD_ID_MASK, .flags = 0,
		};

		if (can_add_rx_filter(b->dev, rx_handler, (void *)(uintptr_t)idx, &f) < 0) {
			printk("%s: add filter 0x%03x failed\n", b->name, catalog_ids[i]);
			return false;
		}
	}
	/* Bus-off recovery is automatic (CONFIG_CAN_MANUAL_RECOVERY_MODE off) */
	if (can_start(b->dev)) {
		printk("%s: can_start failed\n", b->name);
		return false;
	}
	return true;
}

int can_link_init(can_link_rx_cb_t rx_cb)
{
	int up = 0;

	user_rx_cb = rx_cb;
	for (int z = 0; z <= CANP_NUM_ZONES; z++) {
		for (int bus = 0; bus < CANP_NUM_BUSES; bus++) {
			last_hb_ms[z][bus] = -1;
		}
	}

	for (int bus = 0; bus < CANP_NUM_BUSES; bus++) {
		buses[bus].started = bus_init(&buses[bus], bus);
		printk("%s: %s\n", buses[bus].name, buses[bus].started ? "up" : "DOWN");
		up += buses[bus].started;
	}
	return up;
}
