#ifndef PID_CONTROLLER_H
#define PID_CONTROLLER_H

#include <stdbool.h>

/*
 * Small, unit-agnostic PID controller.  The caller chooses the units for
 * setpoint and measurement; the output is clamped to the configured range.
 */
struct pid_controller {
	float kp;
	float ki;
	float kd;
	float integral;
	float previous_error;
	float output_min;
	float output_max;
	bool have_previous_error;
};

void pid_controller_init(struct pid_controller *controller, float kp, float ki,
			 float kd, float output_min, float output_max);
void pid_controller_reset(struct pid_controller *controller);
void pid_controller_set_gains(struct pid_controller *controller, float kp,
			      float ki, float kd);
float pid_controller_update(struct pid_controller *controller, float setpoint,
			    float measurement, float dt_seconds);

#endif /* PID_CONTROLLER_H */
