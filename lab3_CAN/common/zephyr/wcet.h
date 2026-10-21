/*
 * Per-task WCET measurement for the Part 3 node RTA (handout: replace the
 * placeholder C values with values measured on your own node).
 *
 * Method: at the start and end of one job (one activation of a task's body)
 * read the CURRENT THREAD's accumulated run time (Zephyr thread runtime
 * stats, k_cycle_get_32 units = 84 MHz CPU cycles). The difference is the
 * time the thread itself executed, so preemption by other threads is
 * excluded; ISR time that hit during the job is included (conservative).
 * The max over all jobs since boot is kept per task.
 *
 * The MCP2515 driver's RX/interrupt threads can't be instrumented without
 * editing Zephyr, so for them we report Zephyr's peak_cycles: the longest
 * single uninterrupted run of that thread (one interrupt's worth of SPI reads
 * plus our RX dispatch).
 */
#ifndef WCET_H
#define WCET_H

#include <stdint.h>

enum wcet_id {
	WCET_RX_DISPATCH,   /* can_link rx_handler: dedup + zone dispatch (+ relay hook) */
	WCET_CAN_TX,        /* per-bus TX thread: one frame handed to the MCP2515 (SPI load) */
	WCET_ACT,           /* STEER (N2) / DRIVE (N3): one CTRL -> actuator */
	WCET_BLINK,         /* BLINK_F / BLINK_R: one wake of the lamp thread */
	WCET_BTN,           /* TEST_BTN thread: one button edge */
	WCET_PID,           /* N3 PID: one 10 ms tick */
	WCET_HB_MON,        /* one 10 ms HB_MON poll */
	WCET_HB_TX,         /* one heartbeat queued on both buses */
	WCET_RELAY,         /* N2 RELAY: one forwarded frame */
	WCET_FORCE_TX,      /* N2 FORCE_TX: one force frame */
	WCET_COUNT,
};

/* Thread-local execution time so far, in cycles */
uint64_t wcet_begin(void);

/* Record one job of `id` that started at `start` (from wcet_begin) */
void wcet_end(enum wcet_id id, uint64_t start);

/* Print max/count per task, plus the MCP2515 driver threads' peak run */
void wcet_print(void);

#endif /* WCET_H */
