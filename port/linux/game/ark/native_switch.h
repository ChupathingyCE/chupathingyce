/*
NATIVE_SWITCH.H

(perf/vita-native) The switches of the tick's exact-result savings (the
same game state, bit for bit - the tick hash - reached with fewer memory
walks): each has its own variable (0: the old code; 2, where there is one:
both, compared), HALO_NATIVE_TICK=0 turns off all of those not set
themselves, and none set means on. Read again when the settings change
(@set, env.txt: halo_settings_generation), so one run can time both ways.
*/

#ifndef HALO_NATIVE_SWITCH_H
#define HALO_NATIVE_SWITCH_H

#include <stdlib.h>

/* TRUE when the switch's value changed (the first read included): the
caller forgets what it kept under the old value */
static __inline int halo_native_switch(const char *name, int *mode, unsigned long *seen)
{
	extern volatile unsigned long halo_settings_generation;

	if (*mode < 0 || *seen != halo_settings_generation)
	{
		const char *setting = getenv(name);
		const char *all = getenv("HALO_NATIVE_TICK");
		int value = setting && *setting ? atoi(setting) : (all && *all ? atoi(all) != 0 : 1);

		*seen = halo_settings_generation;
		if (value < 0)
			value = 0;
		if (value != *mode)
		{
			*mode = value;
			return 1;
		}
	}
	return 0;
}

#endif
