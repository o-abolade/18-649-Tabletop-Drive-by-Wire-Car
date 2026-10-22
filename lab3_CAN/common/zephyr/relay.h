#ifndef RELAY_H
#define RELAY_H

#include <stdint.h>

#include "can_proto.h"

/* Call before zone_core_init(): records this zone (its own frames are never
 * relayed) and hooks the RELAY_TX test point into can_link. */
void relay_init(enum canp_zone zone);

/* zone_config.rx_hook: queues first copies from other zones for RELAY */
void relay_rx_hook(uint32_t id, const uint8_t data[CANP_DLC], enum canp_bus bus);

void relay_get_stats(uint32_t *n_relayed, uint32_t *n_overflow);

#endif /* RELAY_H */
