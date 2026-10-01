#include "pid_controller.h"

static float clamp(float value, float minimum, float maximum)
{
	if (value < minimum) {
		return minimum;
	}
	if (value > maximum) {
		return maximum;
	}
	return value;
}

void pid_controller_init(struct pid_controller *controller, float kp, float ki,
			 float kd, float output_min, float output_max)
{
	controller->kp = kp;
	controller->ki = ki;
	controller->kd = kd;
	controller->output_min = output_min;
	controller->output_max = output_max;
	pid_controller_reset(controller);
}

void pid_controller_reset(struct pid_controller *controller)
{
	controller->integral = 0.0f;
	controller->previous_error = 0.0f;
	controller->have_previous_error = false;
}

void pid_controller_set_gains(struct pid_controller *controller, float kp,
			      float ki, float kd)
{
	controller->kp = kp;
	controller->ki = ki;
	controller->kd = kd;
}

float pid_controller_update(struct pid_controller *controller, float setpoint,
			    float measurement, float dt_seconds)
{
	float error = setpoint - measurement;
	float derivative = 0.0f;
	float candidate_integral;
	float output;

	if (dt_seconds <= 0.0f) {
		return clamp(controller->kp * error, controller->output_min,
			     controller->output_max);
	}

	candidate_integral = controller->integral + error * dt_seconds;
	if (controller->have_previous_error) {
		derivative = (error - controller->previous_error) / dt_seconds;
	}

	output = controller->kp * error + controller->ki * candidate_integral +
		 controller->kd * derivative;

	/* Integrate only when the output is not driving farther into saturation. */
	if ((output <= controller->output_max || error < 0.0f) &&
	    (output >= controller->output_min || error > 0.0f)) {
		controller->integral = candidate_integral;
	}

	controller->previous_error = error;
	controller->have_previous_error = true;
	return clamp(output, controller->output_min, controller->output_max);
}
