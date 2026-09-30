#include <errno.h>
#include <stdlib.h>

#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/printk.h>
#include "pi_stm32_uart.h"
#include "blinker.h"
#include "encoder.h"
#include "motor_control.h"
#include "current.h"
#include "pid_controller.h"
#include "steering.h"

#define ENCODER_MONITOR_PERIOD_MS 100
#define CONTROL_PERIOD_MS 10U
#define CONTROL_DT_SECONDS ((float)CONTROL_PERIOD_MS / 1000.0f)
#define BRAKE_ACTIVE_THRESHOLD 50U
#define BLINK_PERIOD_MS 500U

/* Tune these only after the encoder wiring produces valid signed counts. */
#define PID_MAX_SPEED_TRANSITIONS_PER_SECOND 2000.0f
#define PID_KP_DEFAULT 0.02f
#define PID_KI_DEFAULT 0.0f
#define PID_KD_DEFAULT 0.0f

#define STATE_INIT     0
#define STATE_NORMAL   1
#define STATE_FAILSAFE 2
#define STATE_SELFTEST 3

#define FAILSAFE_TIMEOUT_MS 150

static atomic_t zone_state = ATOMIC_INIT(STATE_INIT);
static atomic_t application_initialized = ATOMIC_INIT(0);
static atomic_t pid_enabled = ATOMIC_INIT(0);
static struct pid_controller right_pid;
static struct pid_controller left_pid;
static int32_t previous_right_count;
static int32_t previous_left_count;
static bool have_speed_sample;

static int cmd_pid_enable(const struct shell *sh, size_t argc, char **argv)
{
	if (argc != 2 || (argv[1][0] != '0' && argv[1][0] != '1') ||
	    argv[1][1] != '\0') {
		shell_error(sh, "usage: pid_enable <0|1>");
		return -EINVAL;
	}

	atomic_set(&pid_enabled, argv[1][0] == '1');
	pid_controller_reset(&right_pid);
	pid_controller_reset(&left_pid);
	shell_print(sh, "PID speed control %s", argv[1][0] == '1' ? "enabled" : "disabled");
	if (argv[1][0] == '1') {
		shell_warn(sh, "Enable only after both encoder counts are valid; bad feedback can command full PWM");
	}
	return 0;
}
SHELL_CMD_ARG_REGISTER(pid_enable, NULL,
			       "Enable PID only after encoder validation: pid_enable <0|1>",
			       cmd_pid_enable, 2, 0);

static int cmd_pid_gains(const struct shell *sh, size_t argc, char **argv)
{
	char *end;
	float kp;
	float ki;
	float kd;

	if (argc != 4) {
		shell_error(sh, "usage: pid_gains <kp> <ki> <kd>");
		return -EINVAL;
	}

	kp = strtof(argv[1], &end);
	if (*end != '\0') {
		return -EINVAL;
	}
	ki = strtof(argv[2], &end);
	if (*end != '\0') {
		return -EINVAL;
	}
	kd = strtof(argv[3], &end);
	if (*end != '\0') {
		return -EINVAL;
	}

	pid_controller_set_gains(&right_pid, kp, ki, kd);
	pid_controller_set_gains(&left_pid, kp, ki, kd);
	pid_controller_reset(&right_pid);
	pid_controller_reset(&left_pid);
	shell_print(sh, "PID gains set: Kp=%g Ki=%g Kd=%g", (double)kp,
		    (double)ki, (double)kd);
	return 0;
}
SHELL_CMD_ARG_REGISTER(pid_gains, NULL, "Set both motor PID gains: pid_gains <kp> <ki> <kd>",
			       cmd_pid_gains, 4, 0);

/*
 * Bench-only command: `motor_pulse <a|b>`.
 * Each invocation uses the configured duty/duration and ends in the safe state.
 */
static int cmd_motor_pulse(const struct shell *sh, size_t argc, char **argv)
{
    enum motor_control_channel channel;
    int rc;

    if (argc != 2 || (argv[1][0] != 'a' && argv[1][0] != 'b') || argv[1][1] != '\0') {
        shell_error(sh, "usage: motor_pulse <a|b>");
        return -EINVAL;
    }

    channel = argv[1][0] == 'a' ? MOTOR_CHANNEL_A : MOTOR_CHANNEL_B;
    shell_print(sh, "Pulsing motor channel %c at %u%% for %u ms", argv[1][0],
                MOTOR_TEST_DUTY_PERCENT, MOTOR_TEST_DURATION_MS);
    rc = motor_control_pulse(channel);
    if (rc != 0) {
        shell_error(sh, "pulse failed: %d", rc);
        return rc;
    }

    shell_print(sh, "Pulse finished; motor outputs are safe");
    return 0;
}
SHELL_CMD_ARG_REGISTER(motor_pulse, NULL,
                       "Bench pulse: motor_pulse <a|b>",
                       cmd_motor_pulse, 2, 0);

static int cmd_encoder_status(const struct shell *sh, size_t argc, char **argv)
{
	int32_t right_count;
	int32_t left_count;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	encoder_get_counts(&right_count, &left_count);
	shell_print(sh, "Encoder counts: right=%d left=%d", right_count, left_count);
	return 0;
}
SHELL_CMD_REGISTER(encoder_status, NULL,
			   "Show signed encoder counts", cmd_encoder_status);

static int cmd_encoder_zero(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	encoder_zero_counts();
	shell_print(sh, "Encoder counts reset to zero");
	return 0;
}
SHELL_CMD_REGISTER(encoder_zero, NULL,
			   "Reset both encoder counts", cmd_encoder_zero);

static int cmd_encoder_levels(const struct shell *sh, size_t argc, char **argv)
{
	uint8_t right_a;
	uint8_t right_b;
	uint8_t left_a;
	uint8_t left_b;
	int rc;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	rc = encoder_get_levels(&right_a, &right_b, &left_a, &left_b);
	if (rc != 0) {
		shell_error(sh, "encoder read failed: %d", rc);
		return rc;
	}

	shell_print(sh, "Encoder levels: right A=%u B=%u; left A=%u B=%u",
		    right_a, right_b, left_a, left_b);
	return 0;
}
SHELL_CMD_REGISTER(encoder_levels, NULL,
			   "Show live A/B logic levels", cmd_encoder_levels);

/*
 * Print the instantaneous A/B levels without requiring a new shell command
 * for every sample.  shell_readline() keeps the shell responsive to Ctrl-C;
 * it returns -ECANCELED when Ctrl-C is received.
 */
static int cmd_encoder_monitor(const struct shell *sh, size_t argc, char **argv)
{
	uint8_t input[32];
	uint8_t right_a;
	uint8_t right_b;
	uint8_t left_a;
	uint8_t left_b;
	int rc;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	shell_print(sh, "Monitoring encoder levels every %u ms; press Ctrl-C to stop",
		    ENCODER_MONITOR_PERIOD_MS);

	while (true) {
		rc = encoder_get_levels(&right_a, &right_b, &left_a, &left_b);
		if (rc != 0) {
			shell_error(sh, "encoder read failed: %d", rc);
			return rc;
		}

		shell_print(sh, "Encoder levels: right A=%u B=%u; left A=%u B=%u",
			    right_a, right_b, left_a, left_b);

		rc = shell_readline(sh, input, sizeof(input),
				    K_MSEC(ENCODER_MONITOR_PERIOD_MS));
		if (rc == -ECANCELED) {
			break;
		}
		if (rc < 0 && rc != -ETIMEDOUT) {
			shell_error(sh, "monitor input failed: %d", rc);
			return rc;
		}
	}

	shell_print(sh, "Encoder monitor stopped");
	return 0;
}
SHELL_CMD_REGISTER(encoder_monitor, NULL,
			   "Continuously show A/B levels; Ctrl-C stops it", cmd_encoder_monitor);

static int cmd_encoder_edges(const struct shell *sh, size_t argc, char **argv)
{
	int32_t right_a;
	int32_t right_b;
	int32_t left_a;
	int32_t left_b;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	encoder_get_edge_counts(&right_a, &right_b, &left_a, &left_b);
	shell_print(sh, "Raw encoder edges: right A=%d B=%d; left A=%d B=%d",
		    right_a, right_b, left_a, left_b);
	return 0;
}
SHELL_CMD_REGISTER(encoder_edges, NULL,
			   "Show raw A/B GPIO edge counts", cmd_encoder_edges);

/* Continuously show accumulated transitions instead of sampled pin levels. */
static int cmd_encoder_count_monitor(const struct shell *sh, size_t argc,
				     char **argv)
{
	uint8_t input[32];
	int32_t right_count;
	int32_t left_count;
	int32_t right_a_edges;
	int32_t right_b_edges;
	int32_t left_a_edges;
	int32_t left_b_edges;
	int rc;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	shell_print(sh, "Monitoring encoder transition counts every %u ms; press Ctrl-C to stop",
		    ENCODER_MONITOR_PERIOD_MS);

	while (true) {
		encoder_get_counts(&right_count, &left_count);
		encoder_get_edge_counts(&right_a_edges, &right_b_edges,
					&left_a_edges, &left_b_edges);

		shell_print(sh,
			    "Counts: right=%d (A=%d B=%d); left=%d (A=%d B=%d)",
			    right_count, right_a_edges, right_b_edges,
			    left_count, left_a_edges, left_b_edges);

		rc = shell_readline(sh, input, sizeof(input),
				    K_MSEC(ENCODER_MONITOR_PERIOD_MS));
		if (rc == -ECANCELED) {
			break;
		}
		if (rc < 0 && rc != -ETIMEDOUT) {
			shell_error(sh, "monitor input failed: %d", rc);
			return rc;
		}
	}

	shell_print(sh, "Encoder count monitor stopped");
	return 0;
}
SHELL_CMD_REGISTER(encoder_count_monitor, NULL,
			   "Continuously show transition counts; Ctrl-C stops it",
			   cmd_encoder_count_monitor);

static uint16_t current_status_value(enum current_id id)
{
	int raw = current_read_raw(id);

	return raw < 0 ? 0U : (uint16_t)raw;
}

static void set_blinkers(bool left_request, bool right_request, bool hazards)
{
	bool on = ((k_uptime_get_32() / BLINK_PERIOD_MS) & 1U) == 0U;
	bool left_on = on && (hazards || left_request);
	bool right_on = on && (hazards || right_request);

	blinker_set(BLINKER_FL, left_on);
	blinker_set(BLINKER_RL, left_on);
	blinker_set(BLINKER_FR, right_on);
	blinker_set(BLINKER_RR, right_on);
}

static void reset_speed_controllers(void)
{
	pid_controller_reset(&right_pid);
	pid_controller_reset(&left_pid);
	have_speed_sample = false;
}

static void apply_throttle(uint16_t throttle)
{
	int32_t right_count;
	int32_t left_count;
	float right_speed;
	float left_speed;
	float target_speed;
	unsigned int right_duty;
	unsigned int left_duty;

	if (throttle > 1000U) {
		throttle = 1000U;
	}

	if (atomic_get(&pid_enabled) == 0) {
		/* Encoder-independent fallback used until feedback is verified. */
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

	right_speed = (float)(right_count - previous_right_count) / CONTROL_DT_SECONDS;
	left_speed = (float)(left_count - previous_left_count) / CONTROL_DT_SECONDS;
	previous_right_count = right_count;
	previous_left_count = left_count;
	target_speed = ((float)throttle / 1000.0f) *
		       PID_MAX_SPEED_TRANSITIONS_PER_SECOND;

	right_duty = (unsigned int)pid_controller_update(&right_pid, target_speed,
								 right_speed,
								 CONTROL_DT_SECONDS);
	left_duty = (unsigned int)pid_controller_update(&left_pid, target_speed,
							       left_speed,
							       CONTROL_DT_SECONDS);
	(void)motor_control_set_state(MOTOR_CHANNEL_A, MOTOR_CONTROL_FORWARD,
				      right_duty);
	(void)motor_control_set_state(MOTOR_CHANNEL_B, MOTOR_CONTROL_FORWARD,
				      left_duty);
}

/*
 * Part 3 implementation plan (pseudocode only -- no motor pins are driven yet)
 *
 * encoder_init():
 *   configure LEFT_A, LEFT_B, RIGHT_A, RIGHT_B as GPIO interrupt inputs
 *   initialize atomic encoder counts to zero
 *
 * encoder ISR for each A/B edge:
 *   read both signals
 *   use the previous and current A/B state to add +1 or -1 to that wheel count
 *   do no printing, sleeping, or control calculation in the ISR
 *
 * every CONTROL_PERIOD_MS:
 *   left_delta  = atomic exchange(left_count_since_last_sample, 0)
 *   right_delta = atomic exchange(right_count_since_last_sample, 0)
 *   left_speed/right_speed = delta / CONTROL_PERIOD_MS
 *   vehicle_speed = (left_speed + right_speed) / 2
 *
 *   if command is stale OR brake is active:
 *       PWM = 0; H-bridge = dynamic braking; hazards = on
 *   else:
 *       target_speed = monotonic_map(throttle, 0..1000, 0..MAX_SPEED)
 *       error = target_speed - vehicle_speed
 *       pwm_request = clamp(KP * error + optional_integral + optional_derivative,
 *                           0, MAX_PWM)
 *       write PWM and the forward direction pins to the L298N
 *
 * Pin names, voltage checks, encoder polarity, PWM frequency, MAX_SPEED, MAX_PWM,
 * and gains remain intentionally unassigned until the physical wiring is verified.
 */

// Status heartbeat thread: sends every 20ms
static void status_thread_fn(void *a, void *b, void *c)
{
    ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);

    while (1) {
        status_frame_t st;
        st.state = (uint8_t)atomic_get(&zone_state);

        /* Raw 12-bit ADC samples; calibration remains in current.c. */
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
        if (pi_stm32_uart_get_latest_cmd(&last_cmd)) {
            st.seq = last_cmd.seq;
        } else {
            st.seq = 0;   /* no command ever received yet */
        }

        pi_stm32_uart_send_status(&st);
        k_sleep(K_MSEC(20));
    }
}
K_THREAD_DEFINE(status_tid, 512, status_thread_fn, NULL, NULL, NULL, 7, 0, 0);

// Control thread: reads latest command, checks failsafe
static void control_thread_fn(void *a, void *b, void *c)
{
    ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);

    while (1) {
		if (atomic_get(&application_initialized) == 0) {
			k_sleep(K_MSEC(CONTROL_PERIOD_MS));
			continue;
		}

        cmd_frame_t cmd;
        bool have = pi_stm32_uart_get_latest_cmd(&cmd);
        uint32_t age = pi_stm32_uart_ms_since_last_cmd();

        if (!have || age > FAILSAFE_TIMEOUT_MS) {
            if (atomic_get(&zone_state) != STATE_FAILSAFE) {
                printk("*** ENTERING FAILSAFE (age=%ums) ***\n", age);
            }
            atomic_set(&zone_state, STATE_FAILSAFE);

            if (!motor_control_manual_test_active()) {
				(void)motor_control_dynamic_brake();
            }
			reset_speed_controllers();
			set_blinkers(false, false, true);

        } else {
            if (atomic_get(&zone_state) == STATE_FAILSAFE) {
                printk("*** RECOVERED TO NORMAL ***\n");
            }
            atomic_set(&zone_state, STATE_NORMAL);

            printk("seq=%u steer=%d thr=%u brk=%u btn=0x%02x age=%ums\n",
                   cmd.seq, cmd.steering, cmd.throttle, cmd.brake,
                   cmd.buttons, age);

            if (!motor_control_manual_test_active()) {
				(void)set_wheel_angle(cmd.steering);
				if (cmd.brake >= BRAKE_ACTIVE_THRESHOLD) {
					(void)motor_control_dynamic_brake();
					reset_speed_controllers();
				} else {
					apply_throttle(cmd.throttle);
				}
            }
			set_blinkers((cmd.buttons & BIT(0)) != 0U,
				     (cmd.buttons & BIT(1)) != 0U, false);
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

	pid_controller_init(&right_pid, PID_KP_DEFAULT, PID_KI_DEFAULT,
			    PID_KD_DEFAULT, 0.0f, 100.0f);
	pid_controller_init(&left_pid, PID_KP_DEFAULT, PID_KI_DEFAULT,
			    PID_KD_DEFAULT, 0.0f, 100.0f);
	if (motor_rc == 0) {
		atomic_set(&application_initialized, 1);
	}
    printk("Lab 2 STM32 online. Waiting for commands...\n");

    // printk("lab2 up\n");
    
	// bool led_state = true;

    // blinker_init(BLINKER_FL);
    // while (1) {
    //     blinker_set(BLINKER_FL, led_state);
    //     printk("Toggling LED, in test mode\n");
	// 	led_state = !led_state;
	// 	k_msleep(SLEEP_TIME_MS);
	// }
    
    return 0;
}
