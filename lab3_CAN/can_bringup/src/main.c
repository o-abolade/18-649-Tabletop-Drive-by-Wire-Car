/*
 * Lab 3 Part 0 bring-up: proves each MCP2515 on this Nucleo works on its own,
 * before any bus wire is connected.
 *
 *  1. device ready     -> SPI wiring + CS + crystal-driven reset succeeded
 *  2. loopback TX->RX  -> SPI read/write AND the INT pin -> driver thread path
 *  3. normal mode      -> can_get_state reports ERROR-ACTIVE (handout checkpoint)
 */

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/can.h>
#include <zephyr/sys/printk.h>

#define LOOPBACK_TEST_ID 0x7AB
#define STATUS_PERIOD_MS 1000

struct can_port {
	const char *name;
	const struct device *dev;
	struct k_msgq *rx_q;
};

CAN_MSGQ_DEFINE(rx_q_a, 2);
CAN_MSGQ_DEFINE(rx_q_b, 2);

static struct can_port ports[] = {
	{ "CAN A", DEVICE_DT_GET(DT_ALIAS(can_a)), &rx_q_a },
	{ "CAN B", DEVICE_DT_GET(DT_ALIAS(can_b)), &rx_q_b },
};

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

static void bring_up(const struct can_port *p)
{
	uint32_t core_clock = 0;
	int ret;

	if (!device_is_ready(p->dev)) {
		printk("%s: device NOT ready -> MCP2515 reset over SPI failed "
		       "(SCK/MOSI/MISO/CS/3V3/GND wiring?)\n", p->name);
		return;
	}

	(void)can_get_core_clock(p->dev, &core_clock);
	printk("%s: ready, core clock %u Hz (expect 8000000 = 16 MHz osc / 2)\n",
	       p->name, core_clock);

	(void)loopback_test(p);

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
			if (device_is_ready(ports[i].dev)) {
				print_state(&ports[i]);
			}
		}
	}
	return 0;
}
