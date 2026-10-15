#ifndef HALO_NAVIGATION_PROBE_H
#define HALO_NAVIGATION_PROBE_H

#include "cseries/cseries.h"

struct player_action;

/* Called only for Bot 1 after the ordinary solo-host and living-unit gates. */
boolean navigation_probe_action(long unit_index, struct player_action *action);
#endif
