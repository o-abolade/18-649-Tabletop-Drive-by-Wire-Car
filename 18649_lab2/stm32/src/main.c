#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include "pi_stm32_uart.h"
#include "blinker.h"
#include "blinker_ctrl.h"
#include "encoder.h"
#include "motor_control.h"
#include "current.h"
#include "pid_controller.h"
#include "steering.h"
#include "control_state.h"

#define CONTROL_PERIOD_MS 10U
#define CONTROL_DT_SECONDS ((float)CONTROL_PERIOD_MS / 1000.0f)
#define BRAKE_ACTIVE_THRESHOLD 50U

#define PID_MAX_SPEED_TRANSITIONS_PER_SECOND 6500.0f   // 14454.0 counts / 2 seconds = 7227 max -> 6500 = ~90% of measured 7227 max
#define PID_KP_DEFAULT 0.02f
#define PID_KI_DEFAULT 0.01f
#define PID_KD_DEFAULT 0.0f

#define RIGHT_ENCODER_SIGN  1   /* set to -1 if right wheel reads backward */
#define LEFT_ENCODER_SIGN   -1   /* set to -1 if left wheel reads backward */

#define MOTOR_MIN_EFFECTIVE_DUTY 55U

#define STATE_INIT     0
#define STATE_NORMAL   1
#define STATE_FAILSAFE 2
#define STATE_SELFTEST 3

#define FAILSAFE_TIMEOUT_MS 150

#define LEFT_TURN_BIT  0x01
#define RIGHT_TURN_BIT 0x02

static atomic_t zone_state = ATOMIC_INIT(STATE_INIT);
static atomic_t application_initialized = ATOMIC_INIT(0);

/* Shared with bench_commands.c via control_state.h */
atomic_t pid_enabled = ATOMIC_INIT(1);
struct pid_controller right_pid;
struct pid_controller left_pid;

static int32_t previous_right_count;
static int32_t previous_left_count;
static bool have_speed_sample;

static uint16_t current_status_value(enum current_id id)
{
	int raw = current_read_raw(id);
	return raw < 0 ? 0U : (uint16_t)raw;
}

void reset_speed_controllers(void)
{
	pid_controller_reset(&right_pid);
	pid_controller_reset(&left_pid);
	have_speed_sample = false;
}

static void apply_throttle(uint16_t throttle)
{
	int32_t right_count, left_count;
	float right_speed, left_speed, target_speed;
	unsigned int right_duty, left_duty;

	if (throttle > 1000U) {
		throttle = 1000U;
	}
	if (throttle == 0U) {
		(void)motor_control_dynamic_brake();
		reset_speed_controllers();
		return;
	}

	if (atomic_get(&pid_enabled) == 0) {
		(void)motor_control_drive_forward(throttle / 10U);
		return;
	}

	encoder_get_counts(&right_count, &left_count);
	if (!have_speed_sample) {
		previous_right_count = right_count;
		previous_left_count = left_count;
		have_speed_sample = true;
		(void)motor_control_drive_forward(0U);
		return;
	}

	right_speed = RIGHT_ENCODER_SIGN  * (float)(right_count - previous_right_count) / CONTROL_DT_SECONDS;
	left_speed = LEFT_ENCODER_SIGN * (float)(left_count - previous_left_count) / CONTROL_DT_SECONDS;
	previous_right_count = right_count;
	previous_left_count = left_count;
	target_speed = ((float)throttle / 1000.0f) * PID_MAX_SPEED_TRANSITIONS_PER_SECOND;

	right_duty = (unsigned int)pid_controller_update(&right_pid, target_speed, right_speed, CONTROL_DT_SECONDS);
	left_duty  = (unsigned int)pid_controller_update(&left_pid, target_speed, left_speed, CONTROL_DT_SECONDS);

	if (right_duty > 0 && right_duty < MOTOR_MIN_EFFECTIVE_DUTY) {
		right_duty = MOTOR_MIN_EFFECTIVE_DUTY;
	}
	if (left_duty > 0 && left_duty < MOTOR_MIN_EFFECTIVE_DUTY) {
		left_duty = MOTOR_MIN_EFFECTIVE_DUTY;
	}

	(void)motor_control_set_state(MOTOR_CHANNEL_A, MOTOR_CONTROL_FORWARD, left_duty);
	(void)motor_control_set_state(MOTOR_CHANNEL_B, MOTOR_CONTROL_FORWARD, right_duty);

	// Prints what PID is doing
	static uint32_t debug_counter = 0;
	if (++debug_counter % 20 == 0) {  /* print roughly every 200ms, not every 10ms */
		printk("target=%.1f right_spd=%.1f right_duty=%u | left_spd=%.1f left_duty=%u\n",
			(double)target_speed, (double)right_speed, right_duty,
			(double)left_speed, left_duty);
	}
}

// Status heartbeat thread: sends every 20ms
static void status_thread_fn(void *a, void *b, void *c)
{
	ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);

	while (1) {
		status_frame_t st;
		st.state = (uint8_t)atomic_get(&zone_state);

		if (atomic_get(&application_initialized) != 0) {
			st.motor1_current = current_status_value(CURRENT_MOTOR_A);
			st.motor2_current = current_status_value(CURRENT_MOTOR_B);
			st.servo_current  = current_status_value(CURRENT_SERVO);
		} else {
			st.motor1_current = 0;
			st.motor2_current = 0;
			st.servo_current  = 0;
		}

		cmd_frame_t last_cmd;
		st.seq = pi_stm32_uart_get_latest_cmd(&last_cmd) ? last_cmd.seq : 0;

		pi_stm32_uart_send_status(&st);
		k_sleep(K_MSEC(20));
	}
}
K_THREAD_DEFINE(status_tid, 1024, status_thread_fn, NULL, NULL, NULL, 7, 0, 0);

// Control thread: reads latest command, checks failsafe, drives actuators
static void control_thread_fn(void *a, void *b, void *c)
{
	ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);

	uint8_t prev_buttons = 0;

	while (1) {
		if (atomic_get(&application_initialized) == 0) {
			atomic_set(&zone_state, STATE_FAILSAFE);
			blinker_set_hazard(true);
			k_sleep(K_MSEC(CONTROL_PERIOD_MS));
			continue;
		}

		cmd_frame_t cmd;
		bool have = pi_stm32_uart_get_latest_cmd(&cmd);
		uint32_t age = pi_stm32_uart_ms_since_last_cmd();

		if (!have || age > FAILSAFE_TIMEOUT_MS) {
			if (atomic_get(&zone_state) != STATE_FAILSAFE) {
				printk("*** ENTERING FAILSAFE (age=%ums) ***\n", age);
				blinker_set_hazard(true);
			}
			atomic_set(&zone_state, STATE_FAILSAFE);

			if (!motor_control_manual_test_active()) {
				(void)motor_control_dynamic_brake();
			}
			reset_speed_controllers();

		} else {
			if (atomic_get(&zone_state) == STATE_FAILSAFE) {
				printk("*** RECOVERED TO NORMAL ***\n");
				blinker_set_hazard(false);
			}
			atomic_set(&zone_state, STATE_NORMAL);

			// printk("seq=%u steer=%d thr=%u brk=%u btn=0x%02x age=%ums\n",
			//        cmd.seq, cmd.steering, cmd.throttle, cmd.brake, cmd.buttons, age);

			if (!motor_control_manual_test_active()) {
				(void)set_wheel_angle(cmd.steering);
				if (cmd.brake >= BRAKE_ACTIVE_THRESHOLD) {
					(void)motor_control_dynamic_brake();
					reset_speed_controllers();
				} else {
					apply_throttle(cmd.throttle);
				}
			}

			bool left_now   = cmd.buttons & LEFT_TURN_BIT;
			bool right_now  = cmd.buttons & RIGHT_TURN_BIT;
			bool left_prev  = prev_buttons & LEFT_TURN_BIT;
			bool right_prev = prev_buttons & RIGHT_TURN_BIT;

			if (left_now && !left_prev) {
				blinker_signal_left_pressed();
			}
			if (right_now && !right_prev) {
				blinker_signal_right_pressed();
			}
			prev_buttons = cmd.buttons;

			blinker_update_steering(cmd.steering);
		}

		k_sleep(K_MSEC(CONTROL_PERIOD_MS));
	}
}
K_THREAD_DEFINE(control_tid, 1024, control_thread_fn, NULL, NULL, NULL, 5, 0, 0);

int main(void)
{
	pi_stm32_uart_init();

	int motor_rc = motor_control_init();
	if (motor_rc != 0) {
		atomic_set(&zone_state, STATE_FAILSAFE);
		printk("Motor safe-output initialization failed: %d\n", motor_rc);
	}

	int encoder_rc = encoder_init();
	if (encoder_rc != 0) {
		printk("Encoder initialization failed: %d\n", encoder_rc);
	}

	int steering_rc = servo_init();
	if (steering_rc != 0) {
		printk("Steering initialization failed: %d\n", steering_rc);
	}

	int current_rc = current_init();
	if (current_rc != 0) {
		printk("Current-sense initialization failed: %d\n", current_rc);
	}

	int blinker_rc = blinker_init_all();
	if (blinker_rc != 0) {
		printk("Blinker initialization failed: %d\n", blinker_rc);
	}

	pid_controller_init(&right_pid, PID_KP_DEFAULT, PID_KI_DEFAULT, PID_KD_DEFAULT, 0.0f, 100.0f);
	pid_controller_init(&left_pid, PID_KP_DEFAULT, PID_KI_DEFAULT, PID_KD_DEFAULT, 0.0f, 100.0f);

	if (motor_rc == 0) {
		atomic_set(&application_initialized, 1);
	}

	printk("Lab 2 STM32 online. Waiting for commands...\n");
	return 0;
}
