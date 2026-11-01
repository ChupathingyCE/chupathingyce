/*
HALO_CONSOLE_MEMORY.H

A console's memory class: the one thing the shared code asks about a
console's memory, so that any console port can answer it (the original
Xbox's answer is port/xbox/src/xbox_memory.c). A port that answers it
defines HALO_CONSOLE_MEMORY_CLASS (on its compile lines: warthog-xbox's
port/xbox/build.py) and implements the two calls below; every other build
(the desktop builds, a console port that does not) gets its baseline from
HALO_CONSOLE_MEMORY_EXTRAS.

The class is the megabytes a console's game may plan on: the original
Xbox's 64 (a stock console) or 128 (the common upgrade, and a devkit), an
Xbox 360's 512. Each console has a baseline, its smallest, and everything
must run there. A console above its baseline gets "extras": larger caches
and render buffers, more room in its own queues, a heavy feature that
asks first. Never anything of the simulation or the network: the game
state, the objects, the players and what goes on the wire stay the same on
every console and every PC, so all of them play the same game
(docs/cross-play.md).

The class is known from the first line of main, before anything is
allocated, and does not change while the game runs. A player may hold a
console to its baseline (the original Xbox's console.memory_class).
*/

#ifndef __HALO_CONSOLE_MEMORY_H
#define __HALO_CONSOLE_MEMORY_H

#ifdef HALO_CONSOLE_MEMORY_CLASS
/* this console's class, in megabytes (64, 128, 256, 512), as the game uses
it: held to the baseline if the player or the executable asks; 0 if it
cannot say (then it is treated as the baseline) */
int console_memory_class(void);
/* nonzero if this console has room above its baseline for the extras:
what a feature that only a bigger console can carry checks */
int console_memory_extras(void);

/* the baseline's size of something, or the extras' on a console that has
them (read at run time: make it once, at its start) */
#define HALO_CONSOLE_MEMORY_EXTRAS(baseline, extras) (console_memory_extras() ? (extras) : (baseline))
#else
#define HALO_CONSOLE_MEMORY_EXTRAS(baseline, extras) (baseline)
#endif

#endif /* __HALO_CONSOLE_MEMORY_H */
