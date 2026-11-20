/*
STALL_TRACE.H

(port, debug: HALO_STALL_TRACE=1) the frame-stall trace - a ring of the
last events that can hold a frame up (the render worker's slow commands, the
GPU's fence wait, the display queue, texture creates, decodes and memory
frees, long waits for the cache lock, the derived bitmaps' makes and the
derive arena's blocks), each with its time and thread, logged whole when the
game thread's present waits over 500 ms (HALO_STALL_TRACE_MS). Off: one
test of a flag a call.
*/

#ifndef __STALL_TRACE_H
#define __STALL_TRACE_H

unsigned long long vita_host_time_us(void);
extern int halo_stall_trace_enabled;   /* (-1 not read yet) */

int halo_stall_trace_on(void);
/* an event: what (a static string) and two figures */
void halo_stall_note(const char *what, unsigned long a, unsigned long b);
/* (the game thread's present) the frame's wait; the ring logged if long */
void halo_stall_frame(unsigned long long waited_us, unsigned long frame);

#define HALO_STALL_ON() (halo_stall_trace_enabled > 0 || (halo_stall_trace_enabled < 0 && halo_stall_trace_on()))

#endif
