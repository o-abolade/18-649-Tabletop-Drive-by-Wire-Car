/*
 * cockpit.c — N1 cockpit zone for Lab 3 (Raspberry Pi, Linux SocketCAN).
 *
 *   UDP_RX    wheel/pedal state from the laptop proxy (UDP 8000, 20 ms)
 *   CTRL_TX   0x010 on every UDP update, on can0 (bus A) and can1 (bus B)
 *   BLINK_TX  0x200: the cockpit is the blinker master (state change + every
 *             500 ms toggle), so N2's and N3's lamps switch on the same frame
 *   HB_TX     0x100 every 20 ms on both buses (silent while LOCAL_FAIL)
 *   HB_MON    N2/N3 heartbeats per bus: DEG_A/DEG_B LEDs, error state
 *   FORCE_RX  0x300 from N2 (dummy counter this lab; printed only)
 *
 * Wire format, CRC-8 and duplicate rejection come from ../common/can_proto.c,
 * the same file the STM32 zones compile.
 *
 * Build:  make           (or: gcc -O2 -Wall -I../common -o cockpit cockpit.c ../common/can_proto.c -lgpiod)
 * Run:    ./cockpit [--rt] [--no-gpio]
 *   --rt       pin to CPU 3, SCHED_FIFO 80, mlockall (for Part 3 latency runs)
 *   --no-gpio  run without the test-point GPIOs
 * Keys (then Enter): b = send ONE out-of-range CTRL (bad-input demo),
 *                    s = toggle the per-second status line, q = quit
 */
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <fcntl.h>
#include <gpiod.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <netinet/in.h>
#include <poll.h>
#include <sched.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/timerfd.h>
#include <time.h>
#include <unistd.h>

#include "can_proto.h"
#include "state.h"   /* DIJOYSTATE2_t, Lab 2 Appendix A */

/* ---------------- configuration ---------------- */
#define UDP_PORT            8000
#define TICK_MS             10       /* HB_MON / blink / HB_TX scheduler tick */
#define HB_TIMEOUT_MS       (CANP_HB_MISSED_LIMIT * CANP_HB_PERIOD_MS)   /* 60 */
#define UDP_TIMEOUT_MS      100      /* no wheel update for this long = ERROR */
#define BTN_DEBOUNCE_MS     50
#define BTN_DOUBLE_MS       500
#define BLINK_HALF_MS       (CANP_BLINK_TOGGLE_MS)   /* 1 Hz, 50 % duty */
#define HAZARD_HALF_MS      250                      /* 2 Hz, as Lab 2 */
#define BLINK_MIN_GAP_MS    20       /* catalog: BLINK min interval */
#define FAULT_MIN_GAP_MS    20
#define TURN_THRESHOLD      400      /* self-cancel: past +-400 and back */
#define BAD_THROTTLE        150      /* injected out-of-range value (> 100) */

/* Wheel calibration (Lab 2 Part 1) */
#define STEER_RAW_LEFT      (-32768)
#define STEER_RAW_RIGHT     (32767)
#define PEDAL_RAW_REST      (32767)
#define PEDAL_RAW_MAX       (-32768)
#define BTN_RIGHT_TURN_SIG  4
#define BTN_LEFT_TURN_SIG   5
#define BTN_SELF_TEST_SIG   6

/* Test-point GPIOs (BCM numbers on /dev/gpiochip0). SPI0 (7-11) and the
 * MCP2515 INT lines (24, 25) are taken. UDP_RX/CMD_TX are Lab 2's pins. */
#define GPIO_CHIP_PATH      "/dev/gpiochip0"
enum tp { TP_UDP_RX, TP_CMD_TX, TP_CAN_TX_A, TP_CAN_TX_B, TP_DEG_A, TP_DEG_B, TP_COUNT };
static const unsigned int tp_offset[TP_COUNT] = {
	[TP_UDP_RX] = 18,     /* pin 12 */
	[TP_CMD_TX] = 27,     /* pin 13 */
	[TP_CAN_TX_A] = 22,   /* pin 15 */
	[TP_CAN_TX_B] = 23,   /* pin 16 */
	[TP_DEG_A] = 6,       /* pin 31, LED */
	[TP_DEG_B] = 5,       /* pin 29, LED */
};

/* Linux numbers can0/can1 in whatever order the two MCP2515s finish probing,
 * so the names swap between boots. Identify each bus by its SPI chip select
 * instead: CE0 (spi0.0) = CAN A module, CE1 (spi0.1) = CAN B module. */
static const char *const bus_spidev[CANP_NUM_BUSES] = { "spi0.0", "spi0.1" };
static char bus_ifname[CANP_NUM_BUSES][IFNAMSIZ];

/* ---------------- state ---------------- */
static struct gpiod_line_request *tp_req;
static int can_fd[CANP_NUM_BUSES] = { -1, -1 };

enum { SLOT_CTRL, SLOT_HB, SLOT_BLINK, SLOT_FAULT, NUM_TX_SLOTS };
static uint8_t tx_seq[NUM_TX_SLOTS];

/* RX dedup: HB_N2, HB_N3, FORCE, FAULT per zone */
enum { RX_HB_N2, RX_HB_N3, RX_FORCE, RX_FAULT_N2, RX_FAULT_N3, NUM_RX_SLOTS };
static struct canp_dedup dedup[NUM_RX_SLOTS];

static int64_t last_hb_ms[CANP_NUM_ZONES + 1][CANP_NUM_BUSES];
static uint8_t remote_state[CANP_NUM_ZONES + 1];
static int64_t fault_rx_ms[CANP_NUM_ZONES + 1] = { -1, -1, -1, -1 };
static int8_t last_force;
static uint32_t force_rx_count;

static enum canp_zone_state state = CANP_ZS_ERROR;   /* power up in error */
static bool local_fail;
static bool deg[CANP_NUM_BUSES];
static int64_t last_udp_ms = -1;

/* latest wheel values */
static long raw_steer, raw_throttle, raw_brake;   /* before scaling, for calibration */
static int64_t udp_max_gap_ms;
static int16_t cur_steer;
static uint8_t cur_throttle, cur_brake, cur_buttons;
static bool inject_bad;

/* blinker master */
static enum canp_blink_state driver_blink = CANP_BLINK_OFF;
static bool past_threshold;
static enum canp_blink_state sent_blink = CANP_BLINK_OFF;
static bool blink_phase;
static int64_t next_toggle_ms, last_blink_tx_ms = -1000;
static bool blink_pending;

static uint32_t tx_ok[CANP_NUM_BUSES], tx_fail[CANP_NUM_BUSES];

/* TX watchdog. Twice on hardware the mcp251x driver's TX queue stopped
 * draining for good ("No buffer space available") while RX on the same
 * interface kept working; only `ip link set canX down/up` cleared it. */
#define CAN_BITRATE        500000
#define TX_STUCK_MS        200     /* no successful write for this long ... */
#define TX_RESET_HOLDOFF_MS 1000   /* ... and at most one reset per second */
static int64_t last_tx_ok_ms[CANP_NUM_BUSES];
static int64_t last_reset_ms[CANP_NUM_BUSES];
static bool can_reset_ifaces;      /* needs root */
static uint32_t udp_count, ctrl_count;
static bool show_status = true;

/* ---------------- helpers ---------------- */
static int64_t now_ms(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void tp_set(enum tp t, bool on)
{
	if (tp_req) {
		gpiod_line_request_set_value(tp_req, tp_offset[t],
					     on ? GPIOD_LINE_VALUE_ACTIVE : GPIOD_LINE_VALUE_INACTIVE);
	}
}

static int gpio_init(void)
{
	struct gpiod_chip *chip = gpiod_chip_open(GPIO_CHIP_PATH);

	if (!chip) {
		perror("gpiod_chip_open");
		return -1;
	}
	struct gpiod_line_settings *s = gpiod_line_settings_new();
	struct gpiod_line_config *lc = gpiod_line_config_new();
	struct gpiod_request_config *rc = gpiod_request_config_new();

	gpiod_line_settings_set_direction(s, GPIOD_LINE_DIRECTION_OUTPUT);
	gpiod_line_settings_set_output_value(s, GPIOD_LINE_VALUE_INACTIVE);
	gpiod_line_config_add_line_settings(lc, tp_offset, TP_COUNT, s);
	gpiod_request_config_set_consumer(rc, "cockpit");
	tp_req = gpiod_chip_request_lines(chip, rc, lc);

	gpiod_request_config_free(rc);
	gpiod_line_config_free(lc);
	gpiod_line_settings_free(s);
	/* chip stays open for the life of the process, as in Lab 2 */
	if (!tp_req) {
		perror("gpiod_chip_request_lines");
		return -1;
	}
	return 0;
}

/* Find the canN interface whose parent device is `spidev` */
static bool find_can_iface(const char *spidev, char *out, size_t outlen)
{
	DIR *dir = opendir("/sys/class/net");
	struct dirent *e;
	bool found = false;

	if (!dir) {
		return false;
	}
	while (!found && (e = readdir(dir)) != NULL) {
		char link[PATH_MAX], target[PATH_MAX];

		if (strncmp(e->d_name, "can", 3) != 0) {
			continue;
		}
		snprintf(link, sizeof(link), "/sys/class/net/%s/device", e->d_name);
		ssize_t n = readlink(link, target, sizeof(target) - 1);

		if (n < 0) {
			continue;
		}
		target[n] = '\0';
		const char *base = strrchr(target, '/');

		size_t len = strlen(e->d_name);

		if (strcmp(base ? base + 1 : target, spidev) == 0 && len < outlen) {
			memcpy(out, e->d_name, len + 1);
			found = true;
		}
	}
	closedir(dir);
	return found;
}

static int open_can(const char *name)
{
	int fd = socket(PF_CAN, SOCK_RAW, CAN_RAW);

	if (fd < 0) {
		perror("socket(PF_CAN)");
		return -1;
	}
	struct ifreq ifr = { 0 };

	memcpy(ifr.ifr_name, name, strnlen(name, IFNAMSIZ - 1));   /* ifr zeroed: stays terminated */
	if (ioctl(fd, SIOCGIFINDEX, &ifr) < 0) {
		fprintf(stderr, "%s: no such interface (run can_up.sh?)\n", name);
		close(fd);
		return -1;
	}
	struct sockaddr_can addr = { .can_family = AF_CAN, .can_ifindex = ifr.ifr_ifindex };

	if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		perror("bind can");
		close(fd);
		return -1;
	}
	/* Only what N1 consumes: heartbeats from N2/N3, FAULT, FORCE */
	const struct can_filter f[] = {
		{ CANP_ID_HB_N2, CAN_SFF_MASK | CAN_EFF_FLAG | CAN_RTR_FLAG },
		{ CANP_ID_HB_N3, CAN_SFF_MASK | CAN_EFF_FLAG | CAN_RTR_FLAG },
		{ CANP_ID_FAULT, CAN_SFF_MASK | CAN_EFF_FLAG | CAN_RTR_FLAG },
		{ CANP_ID_FORCE, CAN_SFF_MASK | CAN_EFF_FLAG | CAN_RTR_FLAG },
	};

	setsockopt(fd, SOL_CAN_RAW, CAN_RAW_FILTER, f, sizeof(f));
	fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
	return fd;
}

static int open_udp(void)
{
	int fd = socket(AF_INET, SOCK_DGRAM, 0);
	struct sockaddr_in addr = {
		.sin_family = AF_INET, .sin_addr.s_addr = INADDR_ANY, .sin_port = htons(UDP_PORT),
	};

	if (fd < 0 || bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		perror("udp bind (is pi_bridge or wheel_monitor still running?)");
		exit(1);
	}
	return fd;
}

/* ---------------- CAN TX ---------------- */
static bool send_on(enum canp_bus bus, uint32_t id, const uint8_t d[CANP_DLC])
{
	if (can_fd[bus] < 0) {
		return false;
	}
	struct can_frame f = { .can_id = id, .len = CANP_DLC };

	memcpy(f.data, d, CANP_DLC);
	tp_set(bus == CANP_BUS_A ? TP_CAN_TX_A : TP_CAN_TX_B, true);
	/* Non-blocking: a bus that isn't ACKing fills its queue (ENOBUFS) and
	 * must not delay the copy on the other bus */
	bool ok = write(can_fd[bus], &f, sizeof(f)) == (ssize_t)sizeof(f);

	tp_set(bus == CANP_BUS_A ? TP_CAN_TX_A : TP_CAN_TX_B, false);
	if (ok) {
		tx_ok[bus]++;
		last_tx_ok_ms[bus] = now_ms();
	} else {
		tx_fail[bus]++;
	}
	return ok;
}

/* Same frame back to back on A and B */
static void send_both(uint32_t id, const uint8_t d[CANP_DLC])
{
	send_on(CANP_BUS_A, id, d);
	send_on(CANP_BUS_B, id, d);
}

static uint8_t next_seq(int slot)
{
	return tx_seq[slot]++ & CANP_SEQ_MASK;
}

static void send_fault(enum canp_fault_reason reason)
{
	static int64_t last = -FAULT_MIN_GAP_MS;
	uint8_t d[CANP_DLC];

	if (now_ms() - last < FAULT_MIN_GAP_MS) {
		return;
	}
	canp_encode_fault(d, next_seq(SLOT_FAULT), CANP_ZONE_N1, reason);
	send_both(CANP_ID_FAULT, d);
	last = now_ms();
	printf("N1: FAULT sent (reason %d)\n", reason);
}

/* ---------------- blinker master ---------------- */
static void blink_buttons(uint8_t pressed_edges, int16_t steer)
{
	if (pressed_edges & CANP_BTN_LEFT) {
		driver_blink = (driver_blink == CANP_BLINK_LEFT) ? CANP_BLINK_OFF : CANP_BLINK_LEFT;
		past_threshold = false;
	}
	if (pressed_edges & CANP_BTN_RIGHT) {
		driver_blink = (driver_blink == CANP_BLINK_RIGHT) ? CANP_BLINK_OFF : CANP_BLINK_RIGHT;
		past_threshold = false;
	}
	/* Self-cancel: once the wheel has gone past the threshold in the turn
	 * direction, returning inside it cancels (Lab 2 behaviour) */
	if (driver_blink == CANP_BLINK_LEFT) {
		if (steer <= -TURN_THRESHOLD) {
			past_threshold = true;
		} else if (past_threshold) {
			driver_blink = CANP_BLINK_OFF;
		}
	} else if (driver_blink == CANP_BLINK_RIGHT) {
		if (steer >= TURN_THRESHOLD) {
			past_threshold = true;
		} else if (past_threshold) {
			driver_blink = CANP_BLINK_OFF;
		}
	}
}

static void blink_tick(int64_t now)
{
	enum canp_blink_state want = (state != CANP_ZS_NORMAL) ? CANP_BLINK_HAZARD : driver_blink;
	int half = (want == CANP_BLINK_HAZARD) ? HAZARD_HALF_MS : BLINK_HALF_MS;

	if (want != sent_blink) {
		/* R2.4: start blinking right away, lamps ON on the first frame */
		sent_blink = want;
		blink_phase = (want != CANP_BLINK_OFF);
		next_toggle_ms = now + half;
		blink_pending = true;
	} else if (want != CANP_BLINK_OFF && now >= next_toggle_ms) {
		blink_phase = !blink_phase;
		next_toggle_ms += half;
		if (now > next_toggle_ms) {
			next_toggle_ms = now + half;   /* resync after a stall */
		}
		blink_pending = true;
	}

	if (blink_pending && now - last_blink_tx_ms >= BLINK_MIN_GAP_MS) {
		uint8_t d[CANP_DLC];

		canp_encode_blink(d, next_seq(SLOT_BLINK), sent_blink, blink_phase);
		send_both(CANP_ID_BLINK, d);
		last_blink_tx_ms = now;
		blink_pending = false;
	}
}

/* ---------------- UDP_RX -> CTRL_TX ---------------- */
static int16_t scale_steering(long raw)
{
	long s = ((raw - STEER_RAW_LEFT) * 2000) / (STEER_RAW_RIGHT - STEER_RAW_LEFT) - 1000;

	return (int16_t)(s < -1000 ? -1000 : s > 1000 ? 1000 : s);
}

static uint8_t scale_pedal(long raw)
{
	long s = ((raw - PEDAL_RAW_REST) * 100) / (PEDAL_RAW_MAX - PEDAL_RAW_REST);

	return (uint8_t)(s < 0 ? 0 : s > 100 ? 100 : s);
}

static void handle_test_button(int64_t now)
{
	static int64_t last_press = -BTN_DOUBLE_MS - 1;

	if (now - last_press < BTN_DEBOUNCE_MS) {
		return;
	}
	if (local_fail && now - last_press <= BTN_DOUBLE_MS) {
		local_fail = false;
		printf("N1: wheel test button double press -> restore\n");
	} else {
		local_fail = true;
		state = CANP_ZS_LOCAL_FAIL;
		printf("N1: wheel test button -> LOCAL_FAIL (HB_N1 stops)\n");
		send_fault(CANP_FAULT_TEST_BUTTON);
	}
	last_press = now;
}

static void handle_udp(int fd)
{
	uint8_t packet[4 + sizeof(DIJOYSTATE2_t)];
	ssize_t n = recvfrom(fd, packet, sizeof(packet), 0, NULL, NULL);

	tp_set(TP_UDP_RX, true);
	if (n != (ssize_t)sizeof(packet)) {
		tp_set(TP_UDP_RX, false);
		return;
	}

	int64_t now = now_ms();
	const DIJOYSTATE2_t *js = (const DIJOYSTATE2_t *)(packet + 4);
	uint8_t buttons = 0;

	if (js->rgbButtons[BTN_LEFT_TURN_SIG] & 0x80) {
		buttons |= CANP_BTN_LEFT;
	}
	if (js->rgbButtons[BTN_RIGHT_TURN_SIG] & 0x80) {
		buttons |= CANP_BTN_RIGHT;
	}
	if (js->rgbButtons[BTN_SELF_TEST_SIG] & 0x80) {
		buttons |= CANP_BTN_SELF_TEST;
	}
	uint8_t edges = buttons & ~cur_buttons;   /* pressed this update */

	raw_steer = js->lX;
	raw_throttle = js->lY;
	raw_brake = js->lRz;
	cur_steer = scale_steering(js->lX);
	cur_throttle = scale_pedal(js->lY);
	cur_brake = scale_pedal(js->lRz);
	cur_buttons = buttons;
	if (last_udp_ms >= 0 && now - last_udp_ms > udp_max_gap_ms) {
		udp_max_gap_ms = now - last_udp_ms;
	}
	last_udp_ms = now;
	udp_count++;

	/* R4.4: a CTRL for every UDP update */
	struct canp_ctrl c = {
		.steer = cur_steer, .throttle = cur_throttle,
		.brake = cur_brake, .buttons = cur_buttons,
	};

	if (inject_bad) {
		c.throttle = BAD_THROTTLE;   /* valid CRC, value out of range */
		inject_bad = false;
		printf("N1: injected out-of-range CTRL (throttle=%d)\n", BAD_THROTTLE);
	}
	uint8_t d[CANP_DLC];

	canp_encode_ctrl(d, next_seq(SLOT_CTRL), &c);
	send_both(CANP_ID_CTRL, d);
	ctrl_count++;
	tp_set(TP_CMD_TX, true);
	tp_set(TP_UDP_RX, false);
	tp_set(TP_CMD_TX, false);

	/* Off the brake path: blinker and test button after CTRL is out */
	if (edges & CANP_BTN_SELF_TEST) {
		handle_test_button(now);
	}
	blink_buttons(edges, cur_steer);
	blink_tick(now);
}

/* ---------------- CAN RX (HB_MON / FORCE_RX) ---------------- */
static int rx_slot(uint32_t id, const uint8_t *d)
{
	switch (id) {
	case CANP_ID_HB_N2: return RX_HB_N2;
	case CANP_ID_HB_N3: return RX_HB_N3;
	case CANP_ID_FORCE: return RX_FORCE;
	case CANP_ID_FAULT:
		return canp_fault_zone(d) == CANP_ZONE_N2 ? RX_FAULT_N2 :
		       canp_fault_zone(d) == CANP_ZONE_N3 ? RX_FAULT_N3 : -1;
	default:            return -1;
	}
}

static void handle_can(enum canp_bus bus)
{
	struct can_frame f;

	while (read(can_fd[bus], &f, sizeof(f)) == (ssize_t)sizeof(f)) {
		if ((f.can_id & (CAN_EFF_FLAG | CAN_RTR_FLAG | CAN_ERR_FLAG)) || f.len != CANP_DLC) {
			continue;
		}
		uint32_t id = f.can_id & CAN_SFF_MASK;
		int slot = rx_slot(id, f.data);
		int64_t now = now_ms();

		if (slot < 0) {
			continue;
		}
		int zone = canp_hb_zone(id);

		/* Per-bus liveness counts every copy, direct or relayed */
		if (zone) {
			last_hb_ms[zone][bus] = now;
			remote_state[zone] = canp_hb_state(f.data);
		}
		if (!canp_dedup_accept(&dedup[slot], canp_seq(f.data), now)) {
			continue;
		}
		if (id == CANP_ID_FORCE) {
			last_force = canp_force(f.data);
			force_rx_count++;
		} else if (id == CANP_ID_FAULT) {
			fault_rx_ms[canp_fault_zone(f.data)] = now;
			printf("N1: FAULT from N%u (reason %u)\n", canp_fault_zone(f.data),
			       canp_fault_reason(f.data));
		}
	}
}

static bool hb_seen(int zone, int bus, int64_t now)
{
	return last_hb_ms[zone][bus] > 0 && now - last_hb_ms[zone][bus] <= HB_TIMEOUT_MS;
}

static const char *state_name(enum canp_zone_state s)
{
	return s == CANP_ZS_NORMAL ? "NORMAL" : s == CANP_ZS_LOCAL_FAIL ? "LOCAL_FAIL" : "ERROR";
}

static void hb_mon(int64_t now)
{
	bool zone_lost = false, fault_block = false;
	bool bus_seen[CANP_NUM_BUSES] = { false, false };

	for (int z = CANP_ZONE_N2; z <= CANP_ZONE_N3; z++) {
		bool alive = false;

		for (int b = 0; b < CANP_NUM_BUSES; b++) {
			if (hb_seen(z, b, now)) {
				bus_seen[b] = alive = true;
			}
		}
		zone_lost |= !alive;
		if (fault_rx_ms[z] >= 0) {
			int64_t hb = last_hb_ms[z][0] > last_hb_ms[z][1] ? last_hb_ms[z][0] : last_hb_ms[z][1];

			fault_block |= hb <= fault_rx_ms[z];
		}
	}
	for (int b = 0; b < CANP_NUM_BUSES; b++) {
		deg[b] = !bus_seen[b];
	}
	tp_set(TP_DEG_A, deg[CANP_BUS_A]);
	tp_set(TP_DEG_B, deg[CANP_BUS_B]);

	bool udp_ok = last_udp_ms >= 0 && now - last_udp_ms <= UDP_TIMEOUT_MS;
	enum canp_zone_state next = local_fail ? CANP_ZS_LOCAL_FAIL :
				    (zone_lost || fault_block || !udp_ok) ? CANP_ZS_ERROR :
				    CANP_ZS_NORMAL;

	if (next != state) {
		printf("N1: %s -> %s%s%s%s\n", state_name(state), state_name(next),
		       zone_lost ? " (zone lost)" : "", fault_block ? " (FAULT)" : "",
		       !udp_ok ? " (no wheel UDP)" : "");
		state = next;
	}
}

/* A bus whose writes have all failed for TX_STUCK_MS while we still HEAR
 * other zones on it isn't unplugged (then RX would be dead too): the driver's
 * TX path is wedged. Reset the interface, like can_up.sh does. */
static void tx_watchdog(int64_t now)
{
	for (int b = 0; b < CANP_NUM_BUSES; b++) {
		bool rx_alive = hb_seen(CANP_ZONE_N2, b, now) || hb_seen(CANP_ZONE_N3, b, now);

		if (can_fd[b] < 0 || !rx_alive || now - last_tx_ok_ms[b] < TX_STUCK_MS ||
		    now - last_reset_ms[b] < TX_RESET_HOLDOFF_MS) {
			continue;
		}
		last_reset_ms[b] = now;
		if (!can_reset_ifaces) {
			printf("N1: bus %c (%s) TX stuck %lld ms while RX alive: run ./can_up.sh "
			       "(or start cockpit with sudo for automatic recovery)\n",
			       'A' + b, bus_ifname[b], (long long)(now - last_tx_ok_ms[b]));
			continue;
		}
		char cmd[160];

		/* bus_ifname came from /sys/class/net and is a plain canN name */
		snprintf(cmd, sizeof(cmd),
			 "ip link set %s down && ip link set %s type can bitrate %d restart-ms 100 "
			 "&& ip link set %s up &",
			 bus_ifname[b], bus_ifname[b], CAN_BITRATE, bus_ifname[b]);
		printf("N1: bus %c (%s) TX stuck %lld ms while RX alive -> resetting interface\n",
		       'A' + b, bus_ifname[b], (long long)(now - last_tx_ok_ms[b]));
		if (system(cmd) != 0) {
			printf("N1: reset of %s failed\n", bus_ifname[b]);
		}
		last_tx_ok_ms[b] = now;   /* give the reset time to take effect */
	}
}

static void hb_tx(void)
{
	if (state == CANP_ZS_LOCAL_FAIL) {
		return;   /* go silent so N2/N3 detect the failed cockpit (R2.5) */
	}
	uint8_t d[CANP_DLC];
	uint8_t health = (deg[CANP_BUS_A] ? 0 : CANP_HEALTH_A) | (deg[CANP_BUS_B] ? 0 : CANP_HEALTH_B);

	canp_encode_hb(d, next_seq(SLOT_HB), state, health);
	send_both(CANP_ID_HB_N1, d);
}

static void print_status(int64_t now)
{
	printf("N1 %-10s | A(%s) %s%s B(%s) %s%s | N2:%c%c(%s) N3:%c%c(%s) | UDP %u/s age %lldms | "
	       "blink %d/%d | force %d (%u/s) | tx A %u/%u B %u/%u\n",
	       state_name(state),
	       bus_ifname[0], can_fd[0] < 0 ? "DOWN" : "up", deg[0] ? "/DEG" : "",
	       bus_ifname[1], can_fd[1] < 0 ? "DOWN" : "up", deg[1] ? "/DEG" : "",
	       hb_seen(2, 0, now) ? 'A' : '-', hb_seen(2, 1, now) ? 'B' : '-',
	       state_name(remote_state[2]),
	       hb_seen(3, 0, now) ? 'A' : '-', hb_seen(3, 1, now) ? 'B' : '-',
	       state_name(remote_state[3]),
	       udp_count, last_udp_ms < 0 ? -1LL : (long long)(now - last_udp_ms),
	       sent_blink, blink_phase, last_force, force_rx_count,
	       tx_ok[0], tx_fail[0], tx_ok[1], tx_fail[1]);
	printf("   wheel raw X %ld Y %ld Rz %ld -> steer %d thr %u%% brk %u%% btn 0x%02x | UDP max gap %lldms\n",
	       raw_steer, raw_throttle, raw_brake, cur_steer, cur_throttle, cur_brake,
	       cur_buttons, (long long)udp_max_gap_ms);
	udp_max_gap_ms = 0;
	udp_count = force_rx_count = 0;
	memset(tx_ok, 0, sizeof(tx_ok));
	memset(tx_fail, 0, sizeof(tx_fail));
}

static void handle_stdin(void)
{
	char line[64];

	if (!fgets(line, sizeof(line), stdin)) {
		return;
	}
	switch (line[0]) {
	case 'b': inject_bad = true; printf("N1: next CTRL will be out of range\n"); break;
	case 's': show_status = !show_status; break;
	case 'q': exit(0);
	default:  printf("keys: b = inject bad CTRL, s = status on/off, q = quit\n"); break;
	}
}

static void go_realtime(void)
{
	cpu_set_t set;
	struct sched_param sp = { .sched_priority = 80 };

	CPU_ZERO(&set);
	CPU_SET(3, &set);
	if (sched_setaffinity(0, sizeof(set), &set) != 0) {
		perror("sched_setaffinity");
	}
	if (sched_setscheduler(0, SCHED_FIFO, &sp) != 0) {
		perror("sched_setscheduler (run with sudo)");
	}
	if (mlockall(MCL_CURRENT | MCL_FUTURE) != 0) {
		perror("mlockall");
	}
	printf("N1: pinned to CPU 3, SCHED_FIFO 80\n");
}

int main(int argc, char **argv)
{
	bool use_gpio = true;

	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--rt")) {
			go_realtime();
		} else if (!strcmp(argv[i], "--no-gpio")) {
			use_gpio = false;
		} else {
			fprintf(stderr, "usage: %s [--rt] [--no-gpio]\n", argv[0]);
			return 1;
		}
	}
	setvbuf(stdout, NULL, _IOLBF, 0);

	if (use_gpio && gpio_init() != 0) {
		fprintf(stderr, "N1: continuing without test-point GPIOs\n");
	}
	for (int b = 0; b < CANP_NUM_BUSES; b++) {
		if (!find_can_iface(bus_spidev[b], bus_ifname[b], sizeof(bus_ifname[b]))) {
			fprintf(stderr, "N1: no CAN interface on %s (bus %c): check dmesg | grep mcp251\n",
				bus_spidev[b], 'A' + b);
			snprintf(bus_ifname[b], sizeof(bus_ifname[b]), "-");
			continue;
		}
		can_fd[b] = open_can(bus_ifname[b]);
		printf("N1: bus %c = %s (%s) %s\n", 'A' + b, bus_ifname[b], bus_spidev[b],
		       can_fd[b] < 0 ? "DOWN" : "up");
	}
	can_reset_ifaces = (geteuid() == 0);
	printf("N1: TX watchdog %s\n", can_reset_ifaces ? "will reset a stuck interface automatically"
			       : "can only warn (not root: run with sudo for auto-reset)");
	for (int b = 0; b < CANP_NUM_BUSES; b++) {
		last_tx_ok_ms[b] = now_ms();
		last_reset_ms[b] = -TX_RESET_HOLDOFF_MS;
	}
	int udp_fd = open_udp();
	int tfd = timerfd_create(CLOCK_MONOTONIC, 0);
	struct itimerspec its = {
		.it_interval = { 0, TICK_MS * 1000000L }, .it_value = { 0, TICK_MS * 1000000L },
	};

	timerfd_settime(tfd, 0, &its, NULL);
	printf("N1 cockpit: listening on UDP %d, started in ERROR. Keys: b, s, q\n", UDP_PORT);

	struct pollfd fds[5] = {
		{ .fd = udp_fd, .events = POLLIN },
		{ .fd = can_fd[0], .events = POLLIN },   /* fd -1 is ignored by poll */
		{ .fd = can_fd[1], .events = POLLIN },
		{ .fd = tfd, .events = POLLIN },
		{ .fd = STDIN_FILENO, .events = POLLIN },
	};
	int64_t next_hb = now_ms(), next_status = now_ms() + 1000;

	while (1) {
		if (poll(fds, 5, -1) < 0) {
			if (errno == EINTR) {
				continue;
			}
			perror("poll");
			return 1;
		}
		/* UDP first: it's the start of the brake chain */
		if (fds[0].revents & POLLIN) {
			handle_udp(udp_fd);
		}
		for (int b = 0; b < CANP_NUM_BUSES; b++) {
			if (fds[1 + b].revents & POLLIN) {
				handle_can(b);
			}
		}
		if (fds[3].revents & POLLIN) {
			uint64_t expirations;
			int64_t now = now_ms();

			(void)!read(tfd, &expirations, sizeof(expirations));
			hb_mon(now);
			tx_watchdog(now);
			if (now >= next_hb) {
				hb_tx();
				next_hb += CANP_HB_PERIOD_MS;   /* absolute schedule, no drift */
				if (now > next_hb) {
					next_hb = now + CANP_HB_PERIOD_MS;
				}
			}
			blink_tick(now);
			if (now >= next_status) {
				if (show_status) {
					print_status(now);
				}
				next_status += 1000;
			}
		}
		if (fds[4].revents & POLLIN) {
			handle_stdin();
		}
	}
}
