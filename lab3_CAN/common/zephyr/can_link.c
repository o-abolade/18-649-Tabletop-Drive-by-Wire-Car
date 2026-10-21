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
#include "wcet.h"
#include "zone_config.h"

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
static int64_t last_rx_ms[CANP_NUM_BUSES] = { -1, -1 };
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
	uint64_t w0 = wcet_begin();
	uint32_t start = k_cycle_get_32();
	bool accept = false;

	ARG_UNUSED(dev);
	gpio_pin_set_dt(tp, 1);

	int slot = slot_of(frame->id, frame->data);
	int64_t now = k_uptime_get();
	k_spinlock_key_t key = k_spin_lock(&lock);

	last_rx_ms[bus] = now;   /* the bus is alive: someone else is talking */
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
	wcet_end(WCET_RX_DISPATCH, w0);   /* includes the >=10 us test-point hold */
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

/*
 * Per-bus TX queue, ordered by CAN ID.
 *
 * Zephyr's MCP2515 driver uses only ONE of the chip's three TX buffers
 * (MCP2515_TX_CNT = 1 in drivers/can/can_mcp2515.h), so each controller holds
 * one pending frame. Calling can_send() without waiting dropped any frame
 * queued while another was pending (seen: every other FORCE frame lost when
 * it coincided with HB_TX). Instead, senders put frames in this queue and a
 * per-bus TX thread feeds the single hardware buffer:
 *  - lowest ID first, so a relayed CTRL overtakes queued HB/FORCE the way bus
 *    arbitration would; the only priority inversion left is the one frame
 *    already in the hardware buffer (<= 1 frame time, 270 us);
 *  - a dead bus only backs up its own queue, never the other bus's copy.
 */
#define TXQ_DEPTH        8
#define TX_BUF_WAIT_MS   10    /* give up on one frame if the bus isn't ACKing */

/* Stuck-buffer recovery. Seen on hardware: N3's bus A TX buffer stayed "busy"
 * forever (every can_send timed out, TEC stayed 0, RX on that bus still
 * worked) - the driver never got the TX-complete interrupt, and with one
 * buffer that blocks the bus's TX permanently. `can stop`/`can start`
 * cleared it (stop reports the pending frame done with -ENETDOWN). If the
 * buffer stays busy this many waits in a row while we are still RECEIVING
 * on that bus (so it isn't simply unplugged), restart the controller. */
#define TX_STUCK_WAITS   3     /* 3 x 10 ms */
#define RX_ALIVE_MS      100

struct tx_item {
	uint32_t id;
	uint8_t data[CANP_DLC];
};

struct txq {
	struct k_spinlock lock;
	struct tx_item item[TXQ_DEPTH];   /* sorted, item[0] = lowest ID */
	int count;
	struct k_sem ready;
};

static struct txq txq[CANP_NUM_BUSES];

int can_link_send_on(enum canp_bus bus, uint32_t id, const uint8_t data[CANP_DLC])
{
	struct txq *q = &txq[bus];
	int ret = 0;

	if (!buses[bus].started) {
		return -ENETDOWN;
	}

	k_spinlock_key_t key = k_spin_lock(&q->lock);

	if (q->count == TXQ_DEPTH) {
		/* Full (bus not ACKing): keep the higher-priority frames */
		if (id >= q->item[TXQ_DEPTH - 1].id) {
			ret = -ENOBUFS;
		} else {
			q->count--;   /* evict the lowest-priority (highest ID) entry */
		}
	}
	if (ret == 0) {
		int pos = q->count;

		/* Stable insertion: after equal IDs, so same-ID frames stay FIFO */
		while (pos > 0 && q->item[pos - 1].id > id) {
			q->item[pos] = q->item[pos - 1];
			pos--;
		}
		q->item[pos].id = id;
		memcpy(q->item[pos].data, data, CANP_DLC);
		q->count++;
	}
	k_spin_unlock(&q->lock, key);

	key = k_spin_lock(&lock);
	if (ret == 0) {
		stats.tx_queued[bus]++;
	} else {
		stats.tx_busy[bus]++;
	}
	k_spin_unlock(&lock, key);

	if (ret == 0) {
		k_sem_give(&q->ready);
	}
	return ret;
}

static void tx_thread(void *p1, void *p2, void *p3)
{
	enum canp_bus bus = (enum canp_bus)(uintptr_t)p1;
	struct bus *b = &buses[bus];
	struct txq *q = &txq[bus];

	ARG_UNUSED(p2); ARG_UNUSED(p3);
	int stuck_waits = 0;

	while (1) {
		k_sem_take(&q->ready, K_FOREVER);

		struct can_frame f = { .dlc = CANP_DLC };
		k_spinlock_key_t key = k_spin_lock(&q->lock);

		if (q->count == 0) {
			k_spin_unlock(&q->lock, key);
			continue;
		}
		f.id = q->item[0].id;
		memcpy(f.data, q->item[0].data, CANP_DLC);
		q->count--;
		memmove(&q->item[0], &q->item[1], q->count * sizeof(q->item[0]));
		k_spin_unlock(&q->lock, key);

		/* CAN_TX_x test point: rising edge = this frame's turn, falling
		 * edge = handed to the controller (after waiting for the one
		 * hardware buffer). */
		uint32_t start = k_cycle_get_32();
		/* Execution only: time asleep waiting for the TX buffer isn't counted */
		uint64_t w0 = wcet_begin();

		gpio_pin_set_dt(&b->tp_tx, 1);
		int ret = can_send(b->dev, &f, K_MSEC(TX_BUF_WAIT_MS), tx_done, (void *)(uintptr_t)bus);

		tp_end(&b->tp_tx, start);
		wcet_end(WCET_CAN_TX, w0);
		if (ret == 0) {
			stuck_waits = 0;
			continue;
		}

		key = k_spin_lock(&lock);
		stats.tx_busy[bus]++;
		bool rx_alive = last_rx_ms[bus] >= 0 &&
				(k_uptime_get() - last_rx_ms[bus]) <= RX_ALIVE_MS;
		k_spin_unlock(&lock, key);

		/* -EAGAIN = the single TX buffer is still busy after 10 ms */
		stuck_waits = (ret == -EAGAIN) ? stuck_waits + 1 : 0;
		if (stuck_waits >= TX_STUCK_WAITS && rx_alive) {
			printk("%s: TX buffer stuck while RX alive -> restarting controller\n", b->name);
			(void)can_stop(b->dev);   /* aborts the frame, frees the buffer */
			(void)can_start(b->dev);
			key = k_spin_lock(&lock);
			stats.tx_recover[bus]++;
			k_spin_unlock(&lock, key);
			stuck_waits = 0;
		}
	}
}

K_THREAD_DEFINE(can_tx_a_tid, 1024, tx_thread, (void *)CANP_BUS_A, NULL, NULL,
		ZONE_PRIO_CAN_TX, 0, SYS_FOREVER_MS);
K_THREAD_DEFINE(can_tx_b_tid, 1024, tx_thread, (void *)CANP_BUS_B, NULL, NULL,
		ZONE_PRIO_CAN_TX, 0, SYS_FOREVER_MS);

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
		k_sem_init(&txq[bus].ready, 0, TXQ_DEPTH);
		buses[bus].started = bus_init(&buses[bus], bus);
		printk("%s: %s\n", buses[bus].name, buses[bus].started ? "up" : "DOWN");
		up += buses[bus].started;
	}
	k_thread_start(can_tx_a_tid);
	k_thread_start(can_tx_b_tid);
	return up;
}
