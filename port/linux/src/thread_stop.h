/*
THREAD_STOP.H

The end of the platform layer's own threads, asked for by a port whose
system wants them ended before the game is (the Wii U's proc_end,
port/wiiu/docs/exit.md in Warthog for the Wii U): one signal for all of
them, which each sees in its loop, and a wait for them that gives up after a
time, each thread named in the log as it stops or as it is left running.

The desktop builds never ask: their threads run until the process ends, as
before. No SDL or game types.
*/

#ifndef THREAD_STOP_H
#define THREAD_STOP_H

/* a thread of the platform layer's own, started detached and counted by
name until it returns (the name is copied). 0 when it started; nonzero when
it did not, or when the end has been asked for (no thread starts after) */
int thread_stop_create(const char *name, void *(*main)(void *), void *argument);

/* a thread started some other way (the game's own, xbox_kernel.c's
CreateThread), counted by name until thread_stop_leave with what this
returns. waited: whether the end waits for it (a thread whose loop sees the
signal), or only names it if it is still there. -1 when it is not counted
(no room, or the end has been asked for) */
int thread_stop_enter(const char *name, int waited);
void thread_stop_leave(int slot);

/* the end has been asked for: a thread's loop returns */
int thread_stop_requested(void);

/* sleeps up to milliseconds, and returns at once when the end is asked for;
nonzero once it has been */
int thread_stop_sleep(unsigned long milliseconds);

/* the end: asked for, then each counted thread waited for, all together,
up to milliseconds; each logged ("exit: <name> stopped", "exit: <name>
still running after N ms"). Once; later calls return at once */
void thread_stop_all(unsigned long milliseconds);

#endif
