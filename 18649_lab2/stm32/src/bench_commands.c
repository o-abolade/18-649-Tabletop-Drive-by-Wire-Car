#include <errno.h>
#include <stdlib.h>

#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>

#include "encoder.h"
#include "motor_control.h"
#include "steering.h"
#include "control_state.h"

#define ENCODER_MONITOR_PERIOD_MS 100U
#define STEERING_TEST_MAGNITUDE   500
#define STEERING_TEST_HOLD_MS     1000U

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
	float kp, ki, kd;

	if (argc != 4) {
		shell_error(sh, "usage: pid_gains <kp> <ki> <kd>");
		return -EINVAL;
	}

	kp = strtof(argv[1], &end);
	if (*end != '\0') return -EINVAL;
	ki = strtof(argv[2], &end);
	if (*end != '\0') return -EINVAL;
	kd = strtof(argv[3], &end);
	if (*end != '\0') return -EINVAL;

	pid_controller_set_gains(&right_pid, kp, ki, kd);
	pid_controller_set_gains(&left_pid, kp, ki, kd);
	pid_controller_reset(&right_pid);
	pid_controller_reset(&left_pid);
	shell_print(sh, "PID gains set: Kp=%g Ki=%g Kd=%g", (double)kp, (double)ki, (double)kd);
	return 0;
}
SHELL_CMD_ARG_REGISTER(pid_gains, NULL, "Set both motor PID gains: pid_gains <kp> <ki> <kd>",
			cmd_pid_gains, 4, 0);

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
SHELL_CMD_ARG_REGISTER(motor_pulse, NULL, "Bench pulse: motor_pulse <a|b>", cmd_motor_pulse, 2, 0);

static int cmd_steering_test(const struct shell *sh, size_t argc, char **argv)
{
	int rc;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	shell_print(sh, "Steering test: center, right, center, left, center");

	rc = set_wheel_angle(0);
	if (rc != 0) return rc;
	k_sleep(K_MSEC(STEERING_TEST_HOLD_MS));

	rc = set_wheel_angle(STEERING_TEST_MAGNITUDE);
	if (rc != 0) return rc;
	k_sleep(K_MSEC(STEERING_TEST_HOLD_MS));

	rc = set_wheel_angle(0);
	if (rc != 0) return rc;
	k_sleep(K_MSEC(STEERING_TEST_HOLD_MS));

	rc = set_wheel_angle(-STEERING_TEST_MAGNITUDE);
	if (rc != 0) return rc;
	k_sleep(K_MSEC(STEERING_TEST_HOLD_MS));

	rc = set_wheel_angle(0);
	if (rc == 0) {
		shell_print(sh, "Steering test complete; wheels centered");
	}
	return rc;
}
SHELL_CMD_REGISTER(steering_test, NULL, "Center/right/left steering bench test", cmd_steering_test);

static int cmd_encoder_status(const struct shell *sh, size_t argc, char **argv)
{
	int32_t right_count, left_count;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	encoder_get_counts(&right_count, &left_count);
	shell_print(sh, "Encoder counts: right=%d left=%d", right_count, left_count);
	return 0;
}
SHELL_CMD_REGISTER(encoder_status, NULL, "Show signed encoder counts", cmd_encoder_status);

static int cmd_encoder_zero(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	encoder_zero_counts();
	shell_print(sh, "Encoder counts reset to zero");
	return 0;
}
SHELL_CMD_REGISTER(encoder_zero, NULL, "Reset both encoder counts", cmd_encoder_zero);

static int cmd_encoder_levels(const struct shell *sh, size_t argc, char **argv)
{
	uint8_t right_a, right_b, left_a, left_b;
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
SHELL_CMD_REGISTER(encoder_levels, NULL, "Show live A/B logic levels", cmd_encoder_levels);

static int cmd_encoder_monitor(const struct shell *sh, size_t argc, char **argv)
{
	uint8_t input[32];
	uint8_t right_a, right_b, left_a, left_b;
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

		rc = shell_readline(sh, input, sizeof(input), K_MSEC(ENCODER_MONITOR_PERIOD_MS));
		if (rc == -ECANCELED) break;
		if (rc < 0 && rc != -ETIMEDOUT) {
			shell_error(sh, "monitor input failed: %d", rc);
			return rc;
		}
	}

	shell_print(sh, "Encoder monitor stopped");
	return 0;
}
SHELL_CMD_REGISTER(encoder_monitor, NULL, "Continuously show A/B levels; Ctrl-C stops it",
		    cmd_encoder_monitor);

static int cmd_encoder_edges(const struct shell *sh, size_t argc, char **argv)
{
	int32_t right_a, right_b, left_a, left_b;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	encoder_get_edge_counts(&right_a, &right_b, &left_a, &left_b);
	shell_print(sh, "Raw encoder edges: right A=%d B=%d; left A=%d B=%d",
		    right_a, right_b, left_a, left_b);
	return 0;
}
SHELL_CMD_REGISTER(encoder_edges, NULL, "Show raw A/B GPIO edge counts", cmd_encoder_edges);

static int cmd_encoder_count_monitor(const struct shell *sh, size_t argc, char **argv)
{
	uint8_t input[32];
	int32_t right_count, left_count;
	int32_t right_a_edges, right_b_edges, left_a_edges, left_b_edges;
	int rc;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	shell_print(sh, "Monitoring encoder transition counts every %u ms; press Ctrl-C to stop",
		    ENCODER_MONITOR_PERIOD_MS);

	while (true) {
		encoder_get_counts(&right_count, &left_count);
		encoder_get_edge_counts(&right_a_edges, &right_b_edges, &left_a_edges, &left_b_edges);

		shell_print(sh, "Counts: right=%d (A=%d B=%d); left=%d (A=%d B=%d)",
			    right_count, right_a_edges, right_b_edges,
			    left_count, left_a_edges, left_b_edges);

		rc = shell_readline(sh, input, sizeof(input), K_MSEC(ENCODER_MONITOR_PERIOD_MS));
		if (rc == -ECANCELED) break;
		if (rc < 0 && rc != -ETIMEDOUT) {
			shell_error(sh, "monitor input failed: %d", rc);
			return rc;
		}
	}

	shell_print(sh, "Encoder count monitor stopped");
	return 0;
}
SHELL_CMD_REGISTER(encoder_count_monitor, NULL, "Continuously show transition counts; Ctrl-C stops it",
		    cmd_encoder_count_monitor);