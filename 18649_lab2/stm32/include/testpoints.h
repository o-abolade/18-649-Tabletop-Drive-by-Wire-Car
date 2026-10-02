#ifndef TESTPOINTS_H
#define TESTPOINTS_H

#include <stdbool.h>

enum testpoint_id {
    TP_CMD_RX,
    TP_PWM_SET,
    TESTPOINT_COUNT
};

int testpoints_init(void);
void testpoint_set(enum testpoint_id id, bool on);
void testpoint_pulse(enum testpoint_id id);

#endif /* TESTPOINTS_H */