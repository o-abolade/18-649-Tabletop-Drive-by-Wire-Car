#ifndef CONTROL_STATE_H
#define CONTROL_STATE_H

#include <zephyr/kernel.h>
#include "pid_controller.h"

/* Shared between main.c (owns/updates these) and bench_commands.c
 * (shell commands that need to read/reset/retune them). */
extern atomic_t pid_enabled;
extern struct pid_controller right_pid;
extern struct pid_controller left_pid;

void reset_speed_controllers(void);

#endif