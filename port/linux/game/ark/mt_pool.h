/* mt_pool.h

The multi-core steps' fork-join workers (mt_pool.c). */

#ifndef __HALO_MT_POOL_H
#define __HALO_MT_POOL_H

/* the worker threads (made at the first call; HALO_MT_WORKERS, 2 by
default; 0: none) */
int halo_mt_workers(void);

/* function(context, index) for each index 0..count-1, on the workers and
the caller; returns once all are done */
void halo_mt_run(long count, void (*function)(void *context, long index), void *context);

/* the same for the tick's own items, from the tick: each item sees the game
state as the tick does (render_epoch.h halo_epoch_tick_view) - for reads
only, while the tick waits for the batch */
void halo_mt_run_tick(long count, void (*function)(void *context, long index), void *context);

/* the same begun on the workers alone, the caller going on; halo_mt_wait
finishes it (any items left run on the waiter) - before the next batch, and
before anything the batch reads changes */
void halo_mt_start(long count, void (*function)(void *context, long index), void *context);
void halo_mt_wait(void);

/* (perf/cairo-30fps-2) the tick's futures: items the helpers - the workers,
and the game's thread while it waits at the tick's join - may run ahead of
the tick, which never waits for one not begun. From the tick: begin (0 when
the pool is taken: no futures, the tick runs its items itself), then for
each item it needs, claim (nonzero: no helper took it - the tick runs it
itself; zero: a helper ran it, waited for if under way), then end (every
item a helper has begun finished; none begun after). Each item sees the game
state as the tick does, for reads only; an item must write only its own
result. At most HALO_MT_FUTURES_MAXIMUM items. */
#define HALO_MT_FUTURES_MAXIMUM 64
int halo_mt_futures_begin(long count, void (*function)(void *context, long index), void *context);
int halo_mt_futures_claim(long index);
void halo_mt_futures_end(void);

/* (the game's thread, waiting at the join) the futures' generation open now,
0 when none; and a helper's turn - items run until none is left or they end */
unsigned long halo_mt_futures_open(void);
void halo_mt_futures_help(void);

/* (tick_thread.c) wakes the game's thread when it sleeps at the join */
void halo_tick_wake_game_for_futures(void);

#endif
