/*
 * N3 drivetrain zone (rear).
 *   DRIVE  on CTRL: brake -> dynamic brake now (DIR_A = IN4); throttle ->
 *          feed-forward duty written now (PWM_SET), so R2.1/R2.2 don't wait
 *          for the PID period.
 *   PID    10 ms timer: per-wheel speed correction added on top of the
 *          feed-forward duty, with real elapsed dt (Lab 2 lesson).
 *   BLINK_R, HB_TX, HB_MON, TEST_BTN, error state: common/zephyr/zone_core.c
 *
 * Every motor command goes through motor_lock: DRIVE, PID and the safe-state
 * hook all touch the H-bridge, and motor_control_set_state() must not be
 * interleaved (it disables PWM before changing direction).
 */
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "can_link.h"
#include "encoder.h"
#include "motor_control.h"
#include "pid_controller.h"
#include "testpoints.h"
#include "zone_config.h"
#include "zone_core.h"

#define PID_PERIOD_MS            10
#define BRAKE_ACTIVE_PCT         5     /* Lab 2: 50/1000 */
#define MOTOR_MIN_EFFECTIVE_DUTY 55U   /* measured breakaway duty (Lab 2) */
#define PID_TRIM_LIMIT           40.0f /* PID adds -40..+40 % to feed-forward */

/* Lab 2 measured values. Gains need re-tuning: PID is now a trim, 10 ms */
#define PID_MAX_SPEED            6500.0f   /* encoder transitions/s at 100 % */
#define PID_KP                   0.15f
#define PID_KI                   0.04f
#define PID_KD                   0.0f
#define RIGHT_ENCODER_SIGN       1
#define LEFT_ENCODER_SIGN        (-1)

static K_MUTEX_DEFINE(motor_lock);

/* Protected by motor_lock */
static bool driving;
static uint8_t target_throttle;          /* 0..100 % */
static float trim_left, trim_right;
static unsigned int duty_left, duty_right;
static struct pid_controller pid_left, pid_right;
static int32_t prev_right_count, prev_left_count;
static int64_t prev_pid_ms;
static bool have_sample;

static unsigned int feed_forward(uint8_t throttle)
{
	if (throttle == 0) {
		return 0;
	}
	return MOTOR_MIN_EFFECTIVE_DUTY +
	       ((100U - MOTOR_MIN_EFFECTIVE_DUTY) * throttle) / 100U;
}

static unsigned int clamp_duty(float d)
{
	if (d < (float)MOTOR_MIN_EFFECTIVE_DUTY) {
		return MOTOR_MIN_EFFECTIVE_DUTY;
	}
	return d > 100.0f ? 100U : (unsigned int)d;
}

/* Caller holds motor_lock */
static void brake_locked(void)
{
	(void)motor_control_dynamic_brake();
	driving = false;
	target_throttle = 0;
	trim_left = trim_right = 0.0f;
	pid_controller_reset(&pid_left);
	pid_controller_reset(&pid_right);
	have_sample = false;
}

/* Caller holds motor_lock. Only rewrite a channel whose duty changed, so the
 * H-bridge isn't glitched to 0 % every 20 ms. */
static void apply_duty_locked(unsigned int left, unsigned int right, bool force)
{
	if (force || left != duty_left) {
		(void)motor_control_set_state(MOTOR_CHANNEL_A, MOTOR_CONTROL_FORWARD, left);
		duty_left = left;
	}
	if (force || right != duty_right) {
		(void)motor_control_set_state(MOTOR_CHANNEL_B, MOTOR_CONTROL_FORWARD, right);
		duty_right = right;
	}
}

static void drive_safe(void)
{
	k_mutex_lock(&motor_lock, K_FOREVER);
	brake_locked();
	k_mutex_unlock(&motor_lock);
}

static void drive_thread(void *a, void *b, void *c)
{
	ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
	struct canp_ctrl cmd;

	while (1) {
		if (zone_wait_ctrl(&cmd, K_MSEC(ZONE_CTRL_TIMEOUT_MS)) != 0) {
			continue;   /* HB_MON moves us to ERROR and brakes via drive_safe */
		}

		k_mutex_lock(&motor_lock, K_FOREVER);
		if (zone_state() != CANP_ZS_NORMAL ||
		    cmd.brake >= BRAKE_ACTIVE_PCT || cmd.throttle == 0) {
			if (driving || duty_left != 100U) {
				brake_locked();
				duty_left = duty_right = 100U;   /* brake = enables at 100 % */
			}
		} else {
			bool start = !driving;
			unsigned int ff = feed_forward(cmd.throttle);

			target_throttle = cmd.throttle;
			driving = true;
			apply_duty_locked(clamp_duty(ff + trim_left), clamp_duty(ff + trim_right), start);
		}
		k_mutex_unlock(&motor_lock);
	}
}

static void pid_thread(void *a, void *b, void *c)
{
	ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
	int64_t next = k_uptime_get();

	while (1) {
		next += PID_PERIOD_MS;
		k_sleep(K_TIMEOUT_ABS_MS(next));

		k_mutex_lock(&motor_lock, K_FOREVER);
		if (!driving) {
			k_mutex_unlock(&motor_lock);
			continue;
		}

		int64_t now = k_uptime_get();
		int32_t right_count, left_count;

		encoder_get_counts(&right_count, &left_count);
		if (have_sample) {
			/* Real elapsed dt, never the nominal period */
			float dt = (float)(now - prev_pid_ms) / 1000.0f;
			float target = (target_throttle / 100.0f) * PID_MAX_SPEED;
			float right_speed = RIGHT_ENCODER_SIGN * (right_count - prev_right_count) / dt;
			float left_speed = LEFT_ENCODER_SIGN * (left_count - prev_left_count) / dt;
			unsigned int ff = feed_forward(target_throttle);

			trim_right = pid_controller_update(&pid_right, target, right_speed, dt);
			trim_left = pid_controller_update(&pid_left, target, left_speed, dt);
			apply_duty_locked(clamp_duty(ff + trim_left), clamp_duty(ff + trim_right), false);
		}
		prev_right_count = right_count;
		prev_left_count = left_count;
		prev_pid_ms = now;
		have_sample = true;
		k_mutex_unlock(&motor_lock);
	}
}

K_THREAD_DEFINE(drive_tid, 1024, drive_thread, NULL, NULL, NULL,
		ZONE_PRIO_ACT, 0, SYS_FOREVER_MS);
K_THREAD_DEFINE(pid_tid, 1024, pid_thread, NULL, NULL, NULL,
		ZONE_PRIO_PID, 0, SYS_FOREVER_MS);

static const struct zone_config n3_cfg = {
	.zone = CANP_ZONE_N3,
	.enter_safe = drive_safe,
	.rx_hook = NULL,   /* N3 doesn't relay */
};

int main(void)
{
	printk("\n=== Lab 3 N3 drivetrain zone ===\n");

	if (testpoints_init() != 0) {
		printk("N3: test points not ready\n");
	}
	pid_controller_init(&pid_left, PID_KP, PID_KI, PID_KD, -PID_TRIM_LIMIT, PID_TRIM_LIMIT);
	pid_controller_init(&pid_right, PID_KP, PID_KI, PID_KD, -PID_TRIM_LIMIT, PID_TRIM_LIMIT);
	if (motor_control_init() != 0) {
		printk("N3: motor outputs not ready\n");
	}
	if (encoder_init() != 0) {
		printk("N3: encoders not ready\n");
	}

	zone_core_init(&n3_cfg);   /* calls drive_safe: start braked */
	k_thread_start(drive_tid);
	k_thread_start(pid_tid);

	while (1) {
		k_msleep(ZONE_STATUS_PERIOD_MS);
		zone_print_status();

		k_mutex_lock(&motor_lock, K_FOREVER);
		printk("   drive: %s thr %u%% duty L%u R%u\n",
		       driving ? "DRIVE" : "BRAKE", target_throttle, duty_left, duty_right);
		k_mutex_unlock(&motor_lock);
	}
	return 0;
}
