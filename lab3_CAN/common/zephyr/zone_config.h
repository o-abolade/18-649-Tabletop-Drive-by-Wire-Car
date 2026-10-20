/*
 * Every timing threshold and thread priority for the STM32 zones, in one
 * place (handout Part 5). The Part 3 analysis uses these numbers.
 */
#ifndef ZONE_CONFIG_H
#define ZONE_CONFIG_H

#include "can_proto.h"

/* ---- HB_MON (R4.5, R4.7) ---- */
/* A zone's heartbeat is "missing" on a bus once none arrived for this long:
 * three 20 ms periods. Missing on one bus = that bus degraded (DEG LED);
 * missing on both = zone failed (system error). */
#define ZONE_HB_TIMEOUT_MS      (CANP_HB_MISSED_LIMIT * CANP_HB_PERIOD_MS)
/* HB_MON polls faster than the 20 ms heartbeat so detection lands at most
 * one poll after the timeout. */
#define ZONE_HB_MON_PERIOD_MS   10

/* ---- CTRL ---- */
/* No valid CTRL for this long (5 missed 20 ms updates) = error state */
#define ZONE_CTRL_TIMEOUT_MS    100

/* ---- FAULT ---- */
#define ZONE_FAULT_MIN_INTERVAL_MS 20

/* ---- Test button (R2.5) ---- */
#define ZONE_BTN_DEBOUNCE_MS    50
#define ZONE_BTN_DOUBLE_MS      500   /* 2nd press within this restores */

/* ---- Local hazard while in ERROR / LOCAL_FAIL (no BLINK needed) ---- */
#define ZONE_HAZARD_HALF_MS     250   /* 2 Hz, same as Lab 2 */

/* ---- Independent watchdog (R4.5), fed by HB_MON every 10 ms ----
 * IWDG runs from the LSI RC oscillator (17-47 kHz on F401), so leave margin. */
#define ZONE_WDT_TIMEOUT_MS     200

/* ---- Console status line ---- */
#define ZONE_STATUS_PERIOD_MS   1000

/* ---- Thread priorities (lower = higher). Deadline-monotonic start point.
 * The MCP2515 RX threads are cooperative (CONFIG_CAN_MCP2515_INT_THREAD_PRIO
 * = 2, i.e. K_PRIO_COOP(2)) and outrank all of these. */
#define ZONE_PRIO_RELAY     0   /* N2 only: on the relayed brake path */
#define ZONE_PRIO_ACT       1   /* DRIVE (N3) / STEER (N2): CTRL -> actuator */
#define ZONE_PRIO_BLINK     2   /* BLINK_F / BLINK_R: 0.5 ms from reception */
#define ZONE_PRIO_BTN       3   /* TEST_BTN: local fail within 10 ms */
#define ZONE_PRIO_PID       4   /* N3 only: 10 ms */
#define ZONE_PRIO_HB_MON    5   /* 10 ms poll, 20 ms deadline */
#define ZONE_PRIO_HB_TX     6   /* 20 ms */
#define ZONE_PRIO_FORCE_TX  7   /* N2 only: 50 ms */

#endif /* ZONE_CONFIG_H */
