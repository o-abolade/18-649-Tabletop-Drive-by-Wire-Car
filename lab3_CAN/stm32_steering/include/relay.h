#ifndef RELAY_H
#define RELAY_H

#include <stdint.h>

#include "can_proto.h"

/* zone_config.rx_hook for N2: queues frames for the RELAY thread */
void relay_rx_hook(uint32_t id, const uint8_t data[CANP_DLC], enum canp_bus bus);

void relay_get_stats(uint32_t *n_relayed, uint32_t *n_overflow);

#endif /* RELAY_H */
