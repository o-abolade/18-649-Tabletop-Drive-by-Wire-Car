#include <errno.h>

#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/kernel.h>
#include <zephyr/spinlock.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/printk.h>

#include "can_link.h"
#include "zone_config.h"
#include "zone_core.h"

#define STACK_SIZE 1024

static const struct gpio_dt_spec led_deg[CANP_NUM_BUSES] = {
	[CANP_BUS_A] = GPIO_DT_SPEC_GET(DT_ALIAS(led_deg_a), gpios),
	[CANP_BUS_B] = GPIO_DT_SPEC_GET(DT_ALIAS(led_deg_b), gpios),
};
static const struct gpio_dt_spec led_err = GPIO_DT_SPEC_GET(DT_ALIAS(led_err), gpios);
static const struct gpio_dt_spec lamp_left = GPIO_DT_SPEC_GET(DT_ALIAS(blinker_left), gpios);
static const struct gpio_dt_spec lamp_right = GPIO_DT_SPEC_GET(DT_ALIAS(blinker_right), gpios);
static const struct gpio_dt_spec test_btn = GPIO_DT_SPEC_GET(DT_ALIAS(test_btn), gpios);
static const struct device *const wdt = DEVICE_DT_GET(DT_ALIAS(zone_wdt));

static const struct zone_config *cfg;

/* ---- Shared state. The RX path writes from the MCP2515 threads, the
 * zone threads read it; every access goes through `lock`. ---- */
static struct k_spinlock lock;
static enum canp_zone_state state = CANP_ZS_ERROR;   /* power up in error */
static bool local_fail;
static struct canp_ctrl last_ctrl;
static int64_t ctrl_ok_ms = -1;      /* last valid CTRL */
static int64_t ctrl_bad_ms = -1;     /* last out-of-range CTRL */
static int64_t fault_rx_ms[CANP_NUM_ZONES + 1] = { -1, -1, -1, -1 };
static enum canp_blink_state blink_state;
static bool blink_lamps_on;
static bool deg[CANP_NUM_BUSES];
static uint32_t ctrl_bad_crc;

/* FAULT this zone still has to send (set by RX path / button, sent by HB_MON) */
static atomic_t fault_pending;       /* enum canp_fault_reason, 0 = none */

static K_SEM_DEFINE(ctrl_sem, 0, 1);
static K_SEM_DEFINE(blink_sem, 0, 1);
static K_SEM_DEFINE(hbmon_sem, 0, 1);   /* wake HB_MON early (FAULT, bad CTRL) */
static K_SEM_DEFINE(btn_sem, 0, 1);

static struct gpio_callback btn_cb_data;
static int wdt_channel = -1;

const char *zone_state_name(enum canp_zone_state s)
{
	switch (s) {
	case CANP_ZS_NORMAL:     return "NORMAL";
	case CANP_ZS_LOCAL_FAIL: return "LOCAL_FAIL";
	default:                 return "ERROR";
	}
}

enum canp_zone_state zone_state(void)
{
	k_spinlock_key_t key = k_spin_lock(&lock);
	enum canp_zone_state s = state;

	k_spin_unlock(&lock, key);
	return s;
}

int zone_wait_ctrl(struct canp_ctrl *out, k_timeout_t timeout)
{
	if (k_sem_take(&ctrl_sem, timeout) != 0) {
		return -EAGAIN;
	}
	k_spinlock_key_t key = k_spin_lock(&lock);

	*out = last_ctrl;
	k_spin_unlock(&lock, key);
	return 0;
}

static void set_state(enum canp_zone_state next)
{
	k_spinlock_key_t key = k_spin_lock(&lock);
	enum canp_zone_state prev = state;

	/* HB_MON may have sampled local_fail just before TEST_BTN preempted it */
	if (local_fail) {
		next = CANP_ZS_LOCAL_FAIL;
	}
	state = next;
	k_spin_unlock(&lock, key);

	if (prev == next) {
		return;
	}
	gpio_pin_set_dt(&led_err, next != CANP_ZS_NORMAL);
	if (next != CANP_ZS_NORMAL && cfg->enter_safe) {
		cfg->enter_safe();
	}
	k_sem_give(&blink_sem);   /* switch lamps between BLINK and local hazard */
	printk("ZONE N%d: %s -> %s\n", cfg->zone, zone_state_name(prev), zone_state_name(next));
}

/* ---- RX dispatch: MCP2515 cooperative thread, already deduplicated ---- */
static void on_frame(uint32_t id, const uint8_t data[CANP_DLC], enum canp_bus bus)
{
	int64_t now = k_uptime_get();

	switch (id) {
	case CANP_ID_CTRL: {
		struct canp_ctrl c;
		enum canp_ctrl_result r = canp_decode_ctrl(data, &c);
		k_spinlock_key_t key = k_spin_lock(&lock);

		if (r == CANP_CTRL_OK) {
			last_ctrl = c;
			ctrl_ok_ms = now;
		} else if (r == CANP_CTRL_OUT_OF_RANGE) {
			ctrl_bad_ms = now;
		} else {
			ctrl_bad_crc++;
		}
		k_spin_unlock(&lock, key);

		if (r == CANP_CTRL_OK) {
			k_sem_give(&ctrl_sem);
		} else if (r == CANP_CTRL_OUT_OF_RANGE) {
			atomic_set(&fault_pending, CANP_FAULT_CTRL_RANGE);
			k_sem_give(&hbmon_sem);
		}
		break;
	}
	case CANP_ID_BLINK: {
		k_spinlock_key_t key = k_spin_lock(&lock);

		blink_state = canp_blink_state(data);
		blink_lamps_on = canp_blink_lamps_on(data);
		k_spin_unlock(&lock, key);
		k_sem_give(&blink_sem);
		break;
	}
	case CANP_ID_FAULT: {
		uint8_t z = canp_fault_zone(data);

		if (z >= 1 && z <= CANP_NUM_ZONES && z != cfg->zone) {
			k_spinlock_key_t key = k_spin_lock(&lock);

			fault_rx_ms[z] = now;
			k_spin_unlock(&lock, key);
			k_sem_give(&hbmon_sem);
		}
		break;
	}
	default:
		break;   /* heartbeats: can_link already timestamps them per bus */
	}

	if (cfg->rx_hook) {
		cfg->rx_hook(id, data, bus);
	}
}

/* ---- HB_MON: liveness per zone per bus, DEG LEDs, state machine, WDT ---- */
static void send_fault(enum canp_fault_reason reason)
{
	static int64_t last_sent_ms = -ZONE_FAULT_MIN_INTERVAL_MS;
	uint8_t d[CANP_DLC];
	int64_t now = k_uptime_get();

	if (now - last_sent_ms < ZONE_FAULT_MIN_INTERVAL_MS) {
		atomic_set(&fault_pending, reason);   /* retry next poll */
		return;
	}
	canp_encode_fault(d, 0, cfg->zone, reason);
	can_link_send(CANP_ID_FAULT, d);
	last_sent_ms = now;
	printk("ZONE N%d: FAULT sent (reason %d)\n", cfg->zone, reason);
}

static bool hb_seen(int zone, enum canp_bus bus, int64_t now)
{
	int64_t t = can_link_last_hb_ms(zone, bus);

	return t >= 0 && (now - t) <= ZONE_HB_TIMEOUT_MS;
}

static void hb_mon_thread(void *a, void *b, void *c)
{
	ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
	int64_t next = k_uptime_get();

	while (1) {
		next += ZONE_HB_MON_PERIOD_MS;
		/* Absolute deadline: no drift; a FAULT or bad CTRL wakes us early */
		(void)k_sem_take(&hbmon_sem, K_TIMEOUT_ABS_MS(next));
		if (k_uptime_get() > next + ZONE_HB_MON_PERIOD_MS) {
			next = k_uptime_get();   /* resync after a long stall */
		}

		int64_t now = k_uptime_get();
		bool zone_lost = false;
		bool fault_block = false;
		bool bus_seen[CANP_NUM_BUSES] = { false, false };

		for (int z = 1; z <= CANP_NUM_ZONES; z++) {
			if (z == cfg->zone) {
				continue;
			}
			bool alive = false;

			for (int bus = 0; bus < CANP_NUM_BUSES; bus++) {
				if (hb_seen(z, bus, now)) {
					bus_seen[bus] = true;
					alive = true;
				}
			}
			zone_lost |= !alive;

			/* A FAULT from zone z holds us in ERROR until z sends a newer heartbeat */
			k_spinlock_key_t key = k_spin_lock(&lock);
			int64_t f = fault_rx_ms[z];

			k_spin_unlock(&lock, key);
			if (f >= 0) {
				int64_t hb = MAX(can_link_last_hb_ms(z, CANP_BUS_A),
						 can_link_last_hb_ms(z, CANP_BUS_B));
				fault_block |= hb <= f;
			}
		}

		k_spinlock_key_t key = k_spin_lock(&lock);
		bool ctrl_ok = ctrl_ok_ms >= 0 && (now - ctrl_ok_ms) <= ZONE_CTRL_TIMEOUT_MS &&
			       ctrl_ok_ms > ctrl_bad_ms;
		bool lfail = local_fail;

		for (int bus = 0; bus < CANP_NUM_BUSES; bus++) {
			deg[bus] = !bus_seen[bus];
		}
		k_spin_unlock(&lock, key);

		for (int bus = 0; bus < CANP_NUM_BUSES; bus++) {
			gpio_pin_set_dt(&led_deg[bus], deg[bus]);
		}

		int reason = atomic_clear(&fault_pending);

		if (reason) {
			send_fault(reason);
		}

		if (lfail) {
			set_state(CANP_ZS_LOCAL_FAIL);
		} else if (zone_lost || fault_block || !ctrl_ok) {
			set_state(CANP_ZS_ERROR);
		} else {
			set_state(CANP_ZS_NORMAL);
		}

		if (wdt_channel >= 0) {
			wdt_feed(wdt, wdt_channel);
		}
	}
}

/* ---- HB_TX: every 20 ms on both buses, absolute schedule ---- */
static void hb_tx_thread(void *a, void *b, void *c)
{
	ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
	int64_t next = k_uptime_get();

	while (1) {
		next += CANP_HB_PERIOD_MS;
		k_sleep(K_TIMEOUT_ABS_MS(next));

		k_spinlock_key_t key = k_spin_lock(&lock);
		enum canp_zone_state s = state;
		uint8_t health = (deg[CANP_BUS_A] ? 0 : CANP_HEALTH_A) |
				 (deg[CANP_BUS_B] ? 0 : CANP_HEALTH_B);

		k_spin_unlock(&lock, key);

		/* A locally failed zone goes silent so the others detect it (R2.5) */
		if (s == CANP_ZS_LOCAL_FAIL) {
			continue;
		}
		uint8_t d[CANP_DLC];

		canp_encode_hb(d, 0, s, health);
		can_link_send(CANP_ID_HB(cfg->zone), d);
	}
}

/* ---- Blinker lamps: follow BLINK in NORMAL, local hazard otherwise ---- */
static void set_lamps(bool left, bool right)
{
	gpio_pin_set_dt(&lamp_left, left);
	gpio_pin_set_dt(&lamp_right, right);
}

static void blink_thread(void *a, void *b, void *c)
{
	ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
	bool hazard_on = false;
	int64_t next_toggle = 0;

	while (1) {
		if (zone_state() != CANP_ZS_NORMAL) {
			/* Own schedule: BLINK frames waking us must not add toggles */
			if (k_uptime_get() >= next_toggle) {
				hazard_on = !hazard_on;
				set_lamps(hazard_on, hazard_on);
				next_toggle = k_uptime_get() + ZONE_HAZARD_HALF_MS;
			}
			(void)k_sem_take(&blink_sem, K_TIMEOUT_ABS_MS(next_toggle));
			continue;
		}
		next_toggle = 0;

		/* Edges come only from BLINK frames, so N2 and N3 stay in sync */
		k_spinlock_key_t key = k_spin_lock(&lock);
		enum canp_blink_state s = blink_state;
		bool on = blink_lamps_on;

		k_spin_unlock(&lock, key);
		set_lamps(on && (s == CANP_BLINK_LEFT || s == CANP_BLINK_HAZARD),
			  on && (s == CANP_BLINK_RIGHT || s == CANP_BLINK_HAZARD));
		hazard_on = false;
		k_sem_take(&blink_sem, K_FOREVER);
	}
}

/* ---- TEST_BTN: ISR only timestamps and wakes the thread ---- */
static void btn_isr(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	ARG_UNUSED(dev); ARG_UNUSED(cb); ARG_UNUSED(pins);
	k_sem_give(&btn_sem);
}

static void btn_thread(void *a, void *b, void *c)
{
	ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
	int64_t last_press_ms = -ZONE_BTN_DOUBLE_MS - 1;
	bool held = false;   /* press accepted, stable release not seen yet */

	while (1) {
		/* While held, also wake every debounce period to look for a stable
		 * release: a release only counts once the pin reads released AND
		 * no edge arrived for a full debounce period. Otherwise release
		 * bounce would look like a second press = a false "double press". */
		int ret = k_sem_take(&btn_sem, held ? K_MSEC(ZONE_BTN_DEBOUNCE_MS) : K_FOREVER);
		int64_t now = k_uptime_get();
		bool pressed = gpio_pin_get_dt(&test_btn) > 0;

		if (held) {
			if (ret != 0 && !pressed) {
				held = false;
			}
			continue;   /* bounce while pressed or while releasing */
		}
		if (!pressed) {
			continue;   /* glitch: pin already back to released */
		}
		/* First edge of a real press: act now (R2.5), ignore what follows */
		held = true;

		k_spinlock_key_t key = k_spin_lock(&lock);
		bool restore = local_fail && (now - last_press_ms) <= ZONE_BTN_DOUBLE_MS;

		local_fail = !restore;
		k_spin_unlock(&lock, key);
		last_press_ms = now;

		if (restore) {
			printk("ZONE N%d: test button double press -> restore\n", cfg->zone);
			k_sem_give(&hbmon_sem);   /* re-evaluate state now */
		} else {
			/* R2.5: act locally right here, not on the next HB_MON poll */
			set_state(CANP_ZS_LOCAL_FAIL);
			atomic_set(&fault_pending, CANP_FAULT_TEST_BUTTON);
			k_sem_give(&hbmon_sem);
		}
	}
}

K_THREAD_DEFINE(hb_mon_tid, STACK_SIZE, hb_mon_thread, NULL, NULL, NULL,
		ZONE_PRIO_HB_MON, 0, SYS_FOREVER_MS);
K_THREAD_DEFINE(hb_tx_tid, STACK_SIZE, hb_tx_thread, NULL, NULL, NULL,
		ZONE_PRIO_HB_TX, 0, SYS_FOREVER_MS);
K_THREAD_DEFINE(blink_tid, STACK_SIZE, blink_thread, NULL, NULL, NULL,
		ZONE_PRIO_BLINK, 0, SYS_FOREVER_MS);
K_THREAD_DEFINE(btn_tid, STACK_SIZE, btn_thread, NULL, NULL, NULL,
		ZONE_PRIO_BTN, 0, SYS_FOREVER_MS);

static void watchdog_init(void)
{
	const struct wdt_timeout_cfg wcfg = {
		.window = { .min = 0, .max = ZONE_WDT_TIMEOUT_MS },
		.flags = WDT_FLAG_RESET_SOC,
	};

	if (!device_is_ready(wdt)) {
		printk("ZONE: watchdog not ready\n");
		return;
	}
	wdt_channel = wdt_install_timeout(wdt, &wcfg);
	if (wdt_channel < 0 || wdt_setup(wdt, WDT_OPT_PAUSE_HALTED_BY_DBG) != 0) {
		printk("ZONE: watchdog setup failed (%d)\n", wdt_channel);
		wdt_channel = -1;
	}
}

int zone_core_init(const struct zone_config *config)
{
	cfg = config;

	gpio_pin_configure_dt(&led_deg[CANP_BUS_A], GPIO_OUTPUT_INACTIVE);
	gpio_pin_configure_dt(&led_deg[CANP_BUS_B], GPIO_OUTPUT_INACTIVE);
	gpio_pin_configure_dt(&led_err, GPIO_OUTPUT_ACTIVE);   /* start in ERROR */
	gpio_pin_configure_dt(&lamp_left, GPIO_OUTPUT_INACTIVE);
	gpio_pin_configure_dt(&lamp_right, GPIO_OUTPUT_INACTIVE);

	gpio_pin_configure_dt(&test_btn, GPIO_INPUT);
	gpio_init_callback(&btn_cb_data, btn_isr, BIT(test_btn.pin));
	gpio_add_callback(test_btn.port, &btn_cb_data);
	/* Both edges: the thread tracks release too (see btn_thread) */
	gpio_pin_interrupt_configure_dt(&test_btn, GPIO_INT_EDGE_BOTH);

	if (cfg->enter_safe) {
		cfg->enter_safe();
	}

	int up = can_link_init(on_frame);

	watchdog_init();
	k_thread_start(hb_mon_tid);
	k_thread_start(hb_tx_tid);
	k_thread_start(blink_tid);
	k_thread_start(btn_tid);
	printk("ZONE N%d: started in ERROR, %d CAN bus(es) up\n", cfg->zone, up);
	return up;
}

void zone_print_status(void)
{
	struct can_link_stats st;
	uint8_t tec[2], rec[2];
	const char *bs[2];
	int64_t now = k_uptime_get();

	can_link_get_stats(&st);
	for (int bus = 0; bus < CANP_NUM_BUSES; bus++) {
		bs[bus] = can_link_bus_state(bus, &tec[bus], &rec[bus]);
	}

	k_spinlock_key_t key = k_spin_lock(&lock);
	enum canp_zone_state s = state;
	bool da = deg[CANP_BUS_A], db = deg[CANP_BUS_B];
	int64_t ctrl_age = ctrl_ok_ms < 0 ? -1 : now - ctrl_ok_ms;
	uint32_t bad_crc = ctrl_bad_crc;

	k_spin_unlock(&lock, key);

	printk("N%d %-10s | A %s%s T%u R%u | B %s%s T%u R%u | HB",
	       cfg->zone, zone_state_name(s),
	       bs[0], da ? "/DEG" : "", tec[0], rec[0],
	       bs[1], db ? "/DEG" : "", tec[1], rec[1]);
	for (int z = 1; z <= CANP_NUM_ZONES; z++) {
		if (z == cfg->zone) {
			continue;
		}
		printk(" N%d:%c%c", z,
		       hb_seen(z, CANP_BUS_A, now) ? 'A' : '-',
		       hb_seen(z, CANP_BUS_B, now) ? 'B' : '-');
	}
	printk(" | CTRL age %lldms badcrc %u | rx %u dup %u | txbusy A%u B%u\n",
	       ctrl_age, bad_crc, st.rx_accepted, st.rx_dropped,
	       st.tx_busy[CANP_BUS_A], st.tx_busy[CANP_BUS_B]);
}
