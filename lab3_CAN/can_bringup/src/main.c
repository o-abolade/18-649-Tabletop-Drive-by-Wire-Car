/*
 * Lab 3 Part 0 bring-up: proves each MCP2515 on this Nucleo works on its own,
 * before any bus wire is connected.
 *
 *  1. SPI probe        -> raw register read proves the chip answers on MISO.
 *                         (The driver's init only writes, so "device ready"
 *                         alone does NOT prove the module is there.)
 *  2. loopback TX->RX  -> SPI read/write AND the INT pin -> driver thread path
 *  3. normal mode      -> can_get_state reports ERROR-ACTIVE (handout checkpoint)
 */

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/can.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/sys/printk.h>

#define LOOPBACK_TEST_ID 0x7AB
#define STATUS_PERIOD_MS 1000

#define MCP2515_OP_READ      0x03
#define MCP2515_REG_CANSTAT  0x0E   /* CANCTRL (0x0F) follows via auto-increment */
#define MCP2515_OPMOD_CONFIG 0x4    /* CANSTAT[7:5] after reset */

#define PROBE_SPI_OP (SPI_WORD_SET(8) | SPI_TRANSFER_MSB | SPI_OP_MODE_CONTROLLER)

struct can_port {
	const char *name;
	const struct device *dev;
	struct spi_dt_spec spi;
	struct k_msgq *rx_q;
	bool ok;
};

CAN_MSGQ_DEFINE(rx_q_a, 2);
CAN_MSGQ_DEFINE(rx_q_b, 2);

static struct can_port ports[] = {
	{ "CAN A", DEVICE_DT_GET(DT_ALIAS(can_a)),
	  SPI_DT_SPEC_GET(DT_ALIAS(can_a), PROBE_SPI_OP), &rx_q_a, false },
	{ "CAN B", DEVICE_DT_GET(DT_ALIAS(can_b)),
	  SPI_DT_SPEC_GET(DT_ALIAS(can_b), PROBE_SPI_OP), &rx_q_b, false },
};

/* Read CANSTAT and CANCTRL directly. The driver leaves the chip in
 * configuration mode after reset, so a live MCP2515 reads CANSTAT = 0x80
 * (reset value of CANCTRL is 0x87). */
static bool spi_probe(const struct can_port *p)
{
	uint8_t tx[4] = { MCP2515_OP_READ, MCP2515_REG_CANSTAT, 0x00, 0x00 };
	uint8_t rx[4] = { 0 };
	const struct spi_buf tx_buf = { .buf = tx, .len = sizeof(tx) };
	const struct spi_buf rx_buf = { .buf = rx, .len = sizeof(rx) };
	const struct spi_buf_set tx_set = { .buffers = &tx_buf, .count = 1 };
	const struct spi_buf_set rx_set = { .buffers = &rx_buf, .count = 1 };
	int ret = spi_transceive_dt(&p->spi, &tx_set, &rx_set);

	if (ret) {
		printk("%s: SPI transfer error (%d)\n", p->name, ret);
		return false;
	}

	uint8_t canstat = rx[2];
	uint8_t canctrl = rx[3];

	printk("%s: SPI probe CANSTAT=0x%02x CANCTRL=0x%02x -> ", p->name, canstat, canctrl);

	if (canstat == 0x00 && canctrl == 0x00) {
		printk("chip NOT answering (all zeros). Check 3V3/GND, SCK (D8->D13), "
		       "MISO (module D9 -> Nucleo D12), MOSI (module D10 -> Nucleo D11; "
		       "handout figure has D9/D10 backwards), CS\n");
		return false;
	}
	if (canstat == 0xFF && canctrl == 0xFF) {
		printk("MISO floating high (all ones). Check MISO and CS wiring\n");
		return false;
	}
	if ((canstat >> 5) != MCP2515_OPMOD_CONFIG) {
		printk("unexpected value (expect 0x80): loose wire or noise on SPI\n");
		return false;
	}
	printk("OK (configuration mode)\n");
	return true;
}

static const char *state_name(enum can_state s)
{
	switch (s) {
	case CAN_STATE_ERROR_ACTIVE:  return "ERROR-ACTIVE";
	case CAN_STATE_ERROR_WARNING: return "ERROR-WARNING";
	case CAN_STATE_ERROR_PASSIVE: return "ERROR-PASSIVE";
	case CAN_STATE_BUS_OFF:       return "BUS-OFF";
	case CAN_STATE_STOPPED:       return "STOPPED";
	default:                      return "UNKNOWN";
	}
}

static void print_state(const struct can_port *p)
{
	enum can_state state;
	struct can_bus_err_cnt cnt;
	int ret = can_get_state(p->dev, &state, &cnt);

	if (ret) {
		printk("%s: can_get_state failed (%d)\n", p->name, ret);
		return;
	}
	printk("%s: %s  TEC=%u REC=%u\n", p->name, state_name(state),
	       cnt.tx_err_cnt, cnt.rx_err_cnt);
}

static bool loopback_test(const struct can_port *p)
{
	const struct can_filter filter = {
		.id = LOOPBACK_TEST_ID,
		.mask = CAN_STD_ID_MASK,
		.flags = 0,
	};
	struct can_frame tx = {
		.id = LOOPBACK_TEST_ID,
		.dlc = 8,
		.data = { 0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x02, 0x03, 0x04 },
	};
	struct can_frame rx;
	bool ok = false;
	int ret;

	ret = can_set_mode(p->dev, CAN_MODE_LOOPBACK);
	if (ret) {
		printk("%s: set loopback mode failed (%d)\n", p->name, ret);
		return false;
	}

	int filter_id = can_add_rx_filter_msgq(p->dev, p->rx_q, &filter);

	if (filter_id < 0) {
		printk("%s: add rx filter failed (%d)\n", p->name, filter_id);
		return false;
	}

	ret = can_start(p->dev);
	if (ret) {
		printk("%s: can_start (loopback) failed (%d)\n", p->name, ret);
		goto out;
	}

	ret = can_send(p->dev, &tx, K_MSEC(100), NULL, NULL);
	if (ret) {
		printk("%s: loopback send failed (%d)\n", p->name, ret);
		goto stop;
	}

	/* RX only completes if the MCP2515 INT line reaches the STM32 EXTI */
	ret = k_msgq_get(p->rx_q, &rx, K_MSEC(100));
	if (ret) {
		printk("%s: loopback frame NOT received -> check INT wiring/pin\n", p->name);
		goto stop;
	}

	ok = (rx.id == tx.id) && (rx.dlc == tx.dlc) &&
	     (memcmp(rx.data, tx.data, tx.dlc) == 0);
	printk("%s: loopback %s (id=0x%03x dlc=%u)\n", p->name,
	       ok ? "OK" : "MISMATCH", rx.id, rx.dlc);

stop:
	can_stop(p->dev);
out:
	can_remove_rx_filter(p->dev, filter_id);
	return ok;
}

static void bring_up(struct can_port *p)
{
	uint32_t core_clock = 0;
	int ret;

	if (!device_is_ready(p->dev)) {
		printk("%s: driver init failed (see log above)\n", p->name);
		return;
	}

	if (!spi_probe(p)) {
		printk("%s: skipping CAN tests until the SPI probe passes\n", p->name);
		return;
	}

	/* Computed from osc-freq in devicetree, not read from the chip */
	(void)can_get_core_clock(p->dev, &core_clock);
	printk("%s: core clock %u Hz (expect 8000000 = 16 MHz osc / 2)\n",
	       p->name, core_clock);

	if (!loopback_test(p)) {
		printk("%s: loopback failed; skipping normal mode\n", p->name);
		return;
	}

	ret = can_set_mode(p->dev, CAN_MODE_NORMAL);
	if (ret) {
		printk("%s: set normal mode failed (%d)\n", p->name, ret);
		return;
	}
	ret = can_start(p->dev);
	if (ret) {
		printk("%s: can_start (normal) failed (%d)\n", p->name, ret);
		return;
	}
	p->ok = true;
	print_state(p);
}

int main(void)
{
	printk("\n=== Lab 3 CAN bring-up (500 kbit/s, 16 MHz osc) ===\n");

	for (size_t i = 0; i < ARRAY_SIZE(ports); i++) {
		bring_up(&ports[i]);
	}

	/* Nothing transmits in normal mode: a lone node gets no ACK and would
	 * walk itself to ERROR-PASSIVE. Just report state for the checkpoint. */
	while (1) {
		k_msleep(STATUS_PERIOD_MS);
		for (size_t i = 0; i < ARRAY_SIZE(ports); i++) {
			if (ports[i].ok) {
				print_state(&ports[i]);
			}
		}
	}
	return 0;
}
