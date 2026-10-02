#ifndef TESTPOINTS_H
#define TESTPOINTS_H

#include <stdbool.h>

/*
 * Optional timing instrumentation hooks.  No test-point GPIO aliases are in
 * the current Lab 2 wiring, so these are deliberately harmless no-ops until
 * the team maps physical test-point pins.
 */
enum testpoint_id {
	TP_CMD_RX,
	TP_PWM_SET,
};

int testpoints_init(void);
void testpoint_set(enum testpoint_id id, bool asserted);

#endif /* TESTPOINTS_H */
