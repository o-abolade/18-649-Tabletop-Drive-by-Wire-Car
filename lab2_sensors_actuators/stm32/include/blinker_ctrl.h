#ifndef BLINKER_CTRL_H
#define BLINKER_CTRL_H

#include <stdint.h>
#include <stdbool.h>

enum blink_state {
    BLINK_OFF,
    BLINK_LEFT,
    BLINK_RIGHT,
    BLINK_HAZARD,
};

/* Starts the blinker thread */
void blinker_control_start(void);

/* Called from control thread each time a new command frame is processed. */
void blinker_signal_left_pressed(void);
void blinker_signal_right_pressed(void);

/* Called from control thread every cycle with the current steering value,
 * to detect threshold crossings for self-cancel. */
void blinker_update_steering(int16_t steering);

/* Called to force hazards on/off (self-test / system error state). */
void blinker_set_hazard(bool on);

#endif