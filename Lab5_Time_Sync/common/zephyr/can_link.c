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
#include "raft_proto.h"
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
 * with its own counter, so it gets one slot per sending zone. Lab 5: one slot
 * per Raft frame kind per sender (each sender has its own ID). */
enum {
	SLOT_CTRL,
	SLOT_HB_N1, SLOT_HB_N2, SLOT_HB_N3,
	SLOT_BLINK,
	SLOT_FORCE,
	SLOT_FAULT_N1, SLOT_FAULT_N2, SLOT_FAULT_N3,
	SLOT_RAFT_FIRST,
	NUM_SLOTS = SLOT_RAFT_FIRST + (RAFTP_KIND_COUNT - 1) * RAFT_N,
};

/* Receive filters (software, in the MCP2515 driver): the Lab 3 catalog plus
 * the Lab 5 Raft ranges (base + sender 1..3) */
static const struct {
	uint32_t id;
	uint32_t mask;
} rx_filters[] = {
	{ CANP_ID_CTRL,  CAN_STD_ID_MASK },
	{ CANP_ID_FAULT, CAN_STD_ID_MASK },
	{ CANP_ID_HB_N1, 0x7FC },                     /* 0x100-0x103 */
	{ CANP_ID_BLINK, CAN_STD_ID_MASK },
	{ CANP_ID_FORCE, CAN_STD_ID_MASK },
	{ RAFTP_ID_REF_BASE,      0x7FC },            /* 0x024-0x027 */
	{ RAFTP_ID_REQ_VOTE_BASE, 0x7F8 },            /* REQ_VOTE + VOTE 0x030-0x037 */
	{ RAFTP_ID_APPEND_BASE,   0x7F8 },            /* APPEND + ACK 0x040-0x047 */
	{ RAFTP_ID_SNAPSHOT_BASE, 0x7FC },            /* 0x050-0x053 */
	{ RAFTP_ID_EVENT_BASE,    0x7F8 },            /* EVENT + RESP 0x060-0x067 */
};

struct bus {
	const char *name;
	const struct device *dev;
	struct spi_dt_spec spi;
	struct gpio_dt_spec tp_rx;
	struct gpio_dt_spec tp_tx;
	struct gpio_dt_spec intr;   /* MCP2515 INT, read by the stuck-INT check */
	int int_low_polls;
	bool started;
	/* Lab 5 timestamps */
	struct gpio_callback int_cb;
	uint64_t edge_cyc;          /* cycle count at the last INT falling edge */
	uint64_t tx_done_cyc;       /* last successful TX-complete callback */
};

static struct bus buses[CANP_NUM_BUSES] = {
	[CANP_BUS_A] = {
		.name = "CAN A",
		.dev = DEVICE_DT_GET(DT_ALIAS(can_a)),
		.spi = SPI_DT_SPEC_GET(DT_ALIAS(can_a), PROBE_SPI_OP),
		.tp_rx = GPIO_DT_SPEC_GET(DT_ALIAS(tp_can_rx_a), gpios),
		.tp_tx = GPIO_DT_SPEC_GET(DT_ALIAS(tp_can_tx_a), gpios),
		.intr = GPIO_DT_SPEC_GET(DT_ALIAS(can_a), int_gpios),
	},
	[CANP_BUS_B] = {
		.name = "CAN B",
		.dev = DEVICE_DT_GET(DT_ALIAS(can_b)),
		.spi = SPI_DT_SPEC_GET(DT_ALIAS(can_b), PROBE_SPI_OP),
		.tp_rx = GPIO_DT_SPEC_GET(DT_ALIAS(tp_can_rx_b), gpios),
		.tp_tx = GPIO_DT_SPEC_GET(DT_ALIAS(tp_can_tx_b), gpios),
		.intr = GPIO_DT_SPEC_GET(DT_ALIAS(can_b), int_gpios),
	},
};

static can_link_rx_cb_t user_rx_cb;
static can_link_sent_cb_t sent_cb;   /* RELAY's RELAY_TX test point */
static can_link_stamp_cb_t rx_stamp_cb;
static can_link_stamp_cb_t tx_stamp_cb;

/*
 * Lab 5 frame timestamps (time sync, design.md section 7).
 *
 * The MCP2515 has no hardware timestamp, and the driver delivers frames from
 * a thread after SPI reads, 50-150 us after the frame ends. Instead, an ISR on
 * the INT line records the cycle count of each falling edge: INT falls when
 * a received frame lands in a buffer (RXnIF) or a transmission completes
 * (TXnIF), i.e. at the end of the frame on the wire, on the sender and on
 * the receivers alike. The driver's own callback on the line is untouched.
 *
 * INT gives no edge if it is still low from an earlier, unhandled frame. The
 * edge is then older than that frame, and frames on one bus end at least one
 * frame time (>= 222 us at 500 kbit/s, DLC 8) apart, so a stamp older than
 * STAMP_MAX_AGE_US when the driver delivers the frame is not trusted.
 */
#define STAMP_MAX_AGE_US 200

static struct k_spinlock stamp_lock;   /* edge_cyc, tx_done_cyc */

/* Guards dedup, last_hb_ms, tx_seq and stats. Touched from both MCP2515 RX
 * threads and from app threads; never held across an SPI transaction. */
static struct k_spinlock lock;
static struct canp_dedup dedup[NUM_SLOTS];
static uint8_t tx_seq[NUM_SLOTS];
static int64_t last_hb_ms[CANP_NUM_ZONES + 1][CANP_NUM_BUSES];
static int64_t last_rx_ms[CANP_NUM_BUSES] = { -1, -1 };
static struct can_link_stats stats;
/* Driver thread run time at the end of the last rx_handler, per bus (WCET) */
static uint64_t drv_mark[CANP_NUM_BUSES];

static int slot_of(uint32_t id, const uint8_t *data)
{
	unsigned sender;
	enum raftp_kind kind = raftp_classify(id, &sender);

	if (kind != RAFTP_NONE) {
		return SLOT_RAFT_FIRST + (kind - 1) * RAFT_N + (sender - 1);
	}
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

static void int_edge_isr(const struct device *port, struct gpio_callback *cb, uint32_t pins)
{
	struct bus *b = CONTAINER_OF(cb, struct bus, int_cb);
	uint64_t now = k_cycle_get_64();
	k_spinlock_key_t key = k_spin_lock(&stamp_lock);

	ARG_UNUSED(port);
	ARG_UNUSED(pins);
	b->edge_cyc = now;
	k_spin_unlock(&stamp_lock, key);
}

/* The last INT edge on `bus`, and whether it can belong to the frame being
 * handled now. The edge must also be later than `after_cyc`. */
static uint64_t last_edge(enum canp_bus bus, uint64_t after_cyc, bool *ok)
{
	uint64_t now = k_cycle_get_64();
	k_spinlock_key_t key = k_spin_lock(&stamp_lock);
	uint64_t edge = buses[bus].edge_cyc;

	k_spin_unlock(&stamp_lock, key);
	*ok = edge != 0 && edge > after_cyc &&
	      k_cyc_to_us_floor64(now - edge) <= STAMP_MAX_AGE_US;
	return edge;
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

	if (drv_mark[bus] != 0) {
		wcet_end(WCET_DRV_FRAME, drv_mark[bus]);
	}

	ARG_UNUSED(dev);
	gpio_pin_set_dt(tp, 1);

	int slot = slot_of(frame->id, frame->data);

	/* Lab 5: every copy on every bus, before dedup (time sync pairs a REF
	 * with its send time on the same bus) */
	if (rx_stamp_cb && slot >= 0 && frame->dlc == CANP_DLC &&
	    !(frame->flags & CAN_FRAME_IDE)) {
		bool ok;
		uint64_t edge = last_edge(bus, 0, &ok);

		rx_stamp_cb(bus, frame->id, frame->data, edge, ok);
	}
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
	drv_mark[bus] = wcet_begin();
}

/* One frame handed to the MCP2515, for the TX-complete stamp. At most one
 * frame sits in the hardware buffer and one more waits in can_send(), so a
 * ring of TX_RECS per bus is never overrun. */
#define TX_RECS 4

struct tx_rec {
	enum canp_bus bus;
	uint32_t id;
	uint8_t data[CANP_DLC];
};

static struct tx_rec tx_recs[CANP_NUM_BUSES][TX_RECS];

/* Runs in the MCP2515 driver's thread for this bus */
static void tx_done(const struct device *dev, int error, void *user_data)
{
	const struct tx_rec *rec = user_data;
	enum canp_bus bus = rec->bus;

	ARG_UNUSED(dev);
	if (error) {
		k_spinlock_key_t key = k_spin_lock(&lock);

		stats.tx_done_err[bus]++;
		k_spin_unlock(&lock, key);
		return;
	}

	/* This frame's TXnIF edge comes after the previous frame's TX-complete
	 * was handled: the single buffer was busy until then */
	k_spinlock_key_t key = k_spin_lock(&stamp_lock);
	uint64_t prev_done = buses[bus].tx_done_cyc;

	buses[bus].tx_done_cyc = k_cycle_get_64();
	k_spin_unlock(&stamp_lock, key);

	if (tx_stamp_cb) {
		bool ok;
		uint64_t edge = last_edge(bus, prev_done, &ok);

		tx_stamp_cb(bus, rec->id, rec->data, edge, ok);
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
#define RESTART_RETRY_MS 100   /* retry a failed can_start this often */

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
	unsigned rec_next = 0;
	bool start_pending = false;   /* can_start failed after a restart */

	while (1) {
		int got = k_sem_take(&q->ready, start_pending ? K_MSEC(RESTART_RETRY_MS) : K_FOREVER);

		/* Seen on hardware: after a restart, can_start failed to put the
		 * MCP2515 back in normal mode (wire disturbed while probing) and
		 * the bus stayed STOPPED until reset. Keep retrying instead, so
		 * the bus comes back on its own (R4.8 f). */
		if (start_pending) {
			int ret = can_start(b->dev);

			if (ret == 0 || ret == -EALREADY) {
				start_pending = false;
				printk("%s: controller restarted\n", b->name);
			}
		}
		if (got != 0) {
			continue;
		}

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
		struct tx_rec *rec = &tx_recs[bus][rec_next];

		rec_next = (rec_next + 1) % TX_RECS;
		rec->bus = bus;
		rec->id = f.id;
		memcpy(rec->data, f.data, CANP_DLC);

		gpio_pin_set_dt(&b->tp_tx, 1);
		int ret = can_send(b->dev, &f, K_MSEC(TX_BUF_WAIT_MS), tx_done, rec);

		tp_end(&b->tp_tx, start);
		wcet_end(WCET_CAN_TX, w0);
		if (ret == 0) {
			stuck_waits = 0;
			if (sent_cb) {
				sent_cb(bus, f.id, f.data);
			}
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
			int sret = can_start(b->dev);

			if (sret != 0 && sret != -EALREADY) {
				printk("%s: can_start failed (%d), retrying every %d ms\n",
				       b->name, sret, RESTART_RETRY_MS);
				start_pending = true;
			}
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

int can_link_send_each(uint32_t id, uint8_t data[CANP_NUM_BUSES][CANP_DLC])
{
	int slot = slot_of(id, data[0]);
	int mask = 0;

	if (slot < 0 || id == CANP_ID_CTRL) {
		return -EINVAL;
	}

	k_spinlock_key_t key = k_spin_lock(&lock);
	uint8_t seq = tx_seq[slot]++ & CANP_SEQ_MASK;

	k_spin_unlock(&lock, key);

	for (int bus = 0; bus < CANP_NUM_BUSES; bus++) {
		data[bus][0] = canp_flags(data[bus]) | seq;
		if (can_link_send_on(bus, id, data[bus]) == 0) {
			mask |= BIT(bus);
		}
	}
	return mask;
}

void can_link_set_stamp_hooks(can_link_stamp_cb_t rx_cb, can_link_stamp_cb_t tx_cb)
{
	rx_stamp_cb = rx_cb;
	tx_stamp_cb = tx_cb;
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

/*
 * Stuck-INT recovery. Zephyr's MCP2515 driver wakes only on the falling edge
 * of INT. Seen on hardware after bus B's CANL was shorted to ground: during
 * a controller restart INT was already low, no edge ever came, and RX and TX
 * on that bus stayed dead (chip still reading ACTIVE 0/0) until a reset.
 * The driver clears INT within ~1 ms, so INT held low for INT_STUCK_POLLS
 * HB_MON polls means it is not running. Clearing CANINTF over SPI releases
 * INT; the next frame then gives the driver a fresh edge.
 */
#define INT_STUCK_POLLS      5     /* x 10 ms HB_MON period = 50 ms */
#define MCP2515_OP_WRITE     0x02
#define MCP2515_REG_CANINTF  0x2C

void can_link_poll(void)
{
	for (int bus = 0; bus < CANP_NUM_BUSES; bus++) {
		struct bus *b = &buses[bus];

		if (!b->started) {
			continue;
		}
		if (gpio_pin_get_dt(&b->intr) <= 0) {
			b->int_low_polls = 0;
			continue;
		}
		if (++b->int_low_polls < INT_STUCK_POLLS) {
			continue;
		}
		uint8_t tx[3] = { MCP2515_OP_WRITE, MCP2515_REG_CANINTF, 0x00 };
		const struct spi_buf buf = { .buf = tx, .len = sizeof(tx) };
		const struct spi_buf_set set = { .buffers = &buf, .count = 1 };

		(void)spi_write_dt(&b->spi, &set);
		b->int_low_polls = 0;

		k_spinlock_key_t key = k_spin_lock(&lock);

		stats.int_recover[bus]++;
		k_spin_unlock(&lock, key);
		printk("%s: INT stuck low -> cleared interrupt flags\n", b->name);
	}
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
	/* Called from the lowest-priority status loop. The SPI bus lock is a
	 * plain semaphore (no priority inheritance): if this thread were
	 * preempted while holding it, CAN_TX and the MCP2515 threads would wait
	 * behind every middle-priority task. Not preemptible here instead, so
	 * the blocking it causes is just these SPI reads (WCET_STATE_SPI). */
	uint64_t w0 = wcet_begin();

	k_sched_lock();
	int ret = can_get_state(buses[bus].dev, &state, &cnt);

	k_sched_unlock();
	wcet_end(WCET_STATE_SPI, w0);
	if (ret) {
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

	uint32_t t0 = k_cycle_get_32();

	if (spi_transceive_dt(&b->spi, &tx_set, &rx_set)) {
		return false;
	}
	/* Part 3: one register read, the driver's smallest SPI transaction */
	printk("%s: 3-byte SPI read took %u us\n", b->name,
	       k_cyc_to_us_ceil32(k_cycle_get_32() - t0));
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
	for (size_t i = 0; i < ARRAY_SIZE(rx_filters); i++) {
		const struct can_filter f = {
			.id = rx_filters[i].id, .mask = rx_filters[i].mask, .flags = 0,
		};

		if (can_add_rx_filter(b->dev, rx_handler, (void *)(uintptr_t)idx, &f) < 0) {
			printk("%s: add filter 0x%03x failed\n", b->name, rx_filters[i].id);
			return false;
		}
	}
	/* Lab 5: our own ISR on the INT line, beside the driver's */
	gpio_init_callback(&b->int_cb, int_edge_isr, BIT(b->intr.pin));
	if (gpio_add_callback(b->intr.port, &b->int_cb)) {
		printk("%s: INT timestamp callback failed\n", b->name);
	}
	/* Bus-off recovery is automatic (CONFIG_CAN_MANUAL_RECOVERY_MODE off) */
	if (can_start(b->dev)) {
		printk("%s: can_start failed\n", b->name);
		return false;
	}
	return true;
}

void can_link_set_sent_hook(can_link_sent_cb_t cb)
{
	sent_cb = cb;
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
