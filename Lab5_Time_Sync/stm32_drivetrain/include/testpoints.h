#ifndef TESTPOINTS_H
#define TESTPOINTS_H

#include <stdbool.h>

/* N3-only synthetic test points. The CAN test points live in can_link. */
enum testpoint_id {
    TP_PWM_SET,
    TESTPOINT_COUNT
};

int testpoints_init(void);
void testpoint_set(enum testpoint_id id, bool on);

#endif /* TESTPOINTS_H */
