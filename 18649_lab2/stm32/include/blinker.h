#ifndef BLINKER_H
#define BLINKER_H

#include <stdbool.h>

enum blinker_id {
    BLINKER_FL,
    BLINKER_FR,
    BLINKER_RL,
    BLINKER_RR,
    BLINKER_COUNT
};

int  blinker_init_all(void);                          /* configures all four, returns 0 or -errno */
int  blinker_init(enum blinker_id id);
void blinker_set(enum blinker_id id, bool on);

#endif