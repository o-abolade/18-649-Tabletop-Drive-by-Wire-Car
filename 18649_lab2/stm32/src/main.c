#include <errno.h>

#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/printk.h>
#include "pi_stm32_uart.h"
#include "blinker.h"
#include "encoder.h"
#include "motor_control.h"

#define SLEEP_LED_TIME_MS   400
#define ENCODER_MONITOR_PERIOD_MS 100

#define STATE_INIT     0
#define STATE_NORMAL   1
#define STATE_FAILSAFE 2
#define STATE_SELFTEST 3

#define FAILSAFE_TIMEOUT_MS 150

static atomic_t zone_state = ATOMIC_INIT(STATE_INIT);

/*
 * Bench-only command: `motor_pulse <a|b>`.
 * Each invocation is a 50%, 300 ms pulse and ends in the safe state.
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

        // TODO Part 3: replace with real ADC current-sensor readings
        st.motor1_current = 0;
        st.motor2_current = 0;
        st.servo_current  = 0;

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
        cmd_frame_t cmd;
        bool have = pi_stm32_uart_get_latest_cmd(&cmd);
        uint32_t age = pi_stm32_uart_ms_since_last_cmd();

        if (!have || age > FAILSAFE_TIMEOUT_MS) {
            if (atomic_get(&zone_state) != STATE_FAILSAFE) {
                printk("*** ENTERING FAILSAFE (age=%ums) ***\n", age);
            }
            atomic_set(&zone_state, STATE_FAILSAFE);

            /* Safe now: PWM=0 and direction pins low. Dynamic braking is a
             * separate, board-verified state to add after bench testing. */
            if (!motor_control_manual_test_active()) {
                motor_control_safe_stop();
            }

        } else {
            if (atomic_get(&zone_state) == STATE_FAILSAFE) {
                printk("*** RECOVERED TO NORMAL ***\n");
            }
            atomic_set(&zone_state, STATE_NORMAL);

            printk("seq=%u steer=%d thr=%u brk=%u btn=0x%02x age=%ums\n",
                   cmd.seq, cmd.steering, cmd.throttle, cmd.brake,
                   cmd.buttons, age);

            /* No drive command exists yet, so a valid command is safe too. */
            if (!motor_control_manual_test_active()) {
                motor_control_safe_stop();
            }
        }

        k_sleep(K_MSEC(10));
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
