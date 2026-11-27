/*
LATENCY_METER.H

The ping meter of a network game (latency_meter.c).
*/

#ifndef __LATENCY_METER_H
#define __LATENCY_METER_H

/* draws the meter over the whole screen, when display.ping_meter is on and a
network game is being played (interface.c's full screen overlays) */
void latency_meter_draw(
	void);

#endif // __LATENCY_METER_H
