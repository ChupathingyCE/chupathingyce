/* mt_pool.c

The multi-core steps' workers (mt_pool.h): a few threads that take part in
fork-join work the game thread or the tick thread hands them - each item of
a batch run by whichever thread takes it next, the caller among them, and
the call returning once every item is done. A step uses them only while the
state it reads stands still (its own switch, and HALO_TICK_HASH proving the
game's ticks unchanged: tools/tick_hash_ab.sh). HALO_MT_WORKERS: how many (2
by default; 0 none - every batch then runs on its caller). On the Vita they
are pinned to the fourth core (the helpers') and the second (the render
worker's), whose spare time they take. */

#include <pthread.h>
#include <stdlib.h>

#include "mt_pool.h"
#include "render_epoch.h"

/* (render_epoch.h; perf/parallel-tick-2: a count - this pool's batch and
mt_spin.c's phase may be under way at once) */
volatile int halo_mt_parallel;

void platform_log(const char *format, ...);
void vita_host_pin_current_thread(int core) __attribute__((weak));

#define MT_POOL_MAXIMUM_WORKERS 3

static struct
{
	int checked;
	int count;
	pthread_t threads[MT_POOL_MAXIMUM_WORKERS];
	pthread_mutex_t lock;
	pthread_cond_t wake, finished;
	/* the batch: its generation (a worker takes part once each), function,
	context and items, the next item to take and the items done */
	unsigned long generation;
	void (*function)(void *context, long index);
	void *context;
	long count_items;
	volatile long next;
	volatile long done;
	long workers_in;
} pool = { 0, 0, { 0 }, PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, PTHREAD_COND_INITIALIZER };

/* (any thread) the batch's items until none is left */
static void run_items(void (*function)(void *context, long index), void *context, long count)
{
	long index;

	while ((index = __atomic_fetch_add(&pool.next, 1, __ATOMIC_ACQ_REL)) < count)
	{
		function(context, index);
		__atomic_add_fetch(&pool.done, 1, __ATOMIC_ACQ_REL);
	}
}

static void *worker(void *argument)
{
	unsigned long seen = 0;
	int core = (int)(long)argument;

	if (vita_host_pin_current_thread && core >= 0)
		vita_host_pin_current_thread(core);
	for (;;)
	{
		void (*function)(void *context, long index);
		void *context;
		long count;

		pthread_mutex_lock(&pool.lock);
		while (pool.generation == seen)
			pthread_cond_wait(&pool.wake, &pool.lock);
		seen = pool.generation;
		/* (perf/cairo-30fps) a batch already finished - its caller gone,
		which it may be once every item is done and no worker is in - is
		not joined: its items counter is the next batch's once that begins,
		and a worker that joined late ran the next batch's items with this
		one's count (the firing positions' lines of sight: an item past the
		batch's end) */
		if (__atomic_load_n(&pool.done, __ATOMIC_ACQUIRE) >= pool.count_items)
		{
			pthread_mutex_unlock(&pool.lock);
			continue;
		}
		function = pool.function;
		context = pool.context;
		count = pool.count_items;
		pool.workers_in++;
		pthread_mutex_unlock(&pool.lock);
		run_items(function, context, count);
		pthread_mutex_lock(&pool.lock);
		pool.workers_in--;
		pthread_cond_broadcast(&pool.finished);
		pthread_mutex_unlock(&pool.lock);
	}
	return NULL;
}

/* HALO_MT_BENCH=<batches>: what a fork-join costs on this machine - that
many batches of as many empty items as there are threads, timed, and of
items of about 20 us each (the per-biped node work's size on the Vita),
against the same work on the caller alone; logged once at the pool's start */
unsigned long long vita_host_time_us(void) __attribute__((weak));
static void mt_pool_bench(int batches);

static volatile unsigned long bench_sink;

static void bench_empty(void *context, long index)
{
	(void)context;
	(void)index;
}

static void bench_work(void *context, long index)
{
	unsigned long x = (unsigned long)index + 1, k, rounds = (unsigned long)(long)context;

	for (k = 0; k < rounds; k++)
		x = x * 1103515245UL + 12345UL;
	__atomic_add_fetch(&bench_sink, x, __ATOMIC_RELAXED);
}

int halo_mt_workers(void)
{
	if (!pool.checked)
	{
		char const *setting = getenv("HALO_MT_WORKERS");
		int wanted = setting ? atoi(setting) : 2, k;

		pool.checked = 1;
		if (wanted > MT_POOL_MAXIMUM_WORKERS)
			wanted = MT_POOL_MAXIMUM_WORKERS;
		for (k = 0; k < wanted; k++)
		{
			pthread_attr_t attributes;

			pthread_attr_init(&attributes);
			pthread_attr_setstacksize(&attributes, 1024 * 1024);
			/* (the Vita: the fourth core, then the second) */
			if (pthread_create(&pool.threads[k], &attributes, worker, (void *)(long)(k == 0 ? 3 : 1)) != 0)
			{
				pthread_attr_destroy(&attributes);
				break;
			}
			pthread_attr_destroy(&attributes);
			pool.count++;
		}
		platform_log("multi-core: %d worker threads (HALO_MT_WORKERS)", pool.count);
		if (getenv("HALO_MT_BENCH") && atoi(getenv("HALO_MT_BENCH")) > 0)
			mt_pool_bench(atoi(getenv("HALO_MT_BENCH")));
	}
	return pool.count;
}

/* (one batch at a time: a batch asked for by another thread while one is
under way - the Halo 2 level's make runs on a thread of its own while the
game thread draws the loading screen - runs on its caller alone) */
static int pool_busy;
static pthread_t pool_owner;

/* (mt_spin.c: the two pools' batches one at a time - a spin phase while a
batch runs here, or the reverse, falls back to its caller) */
int halo_mt_pool_busy_take(void)
{
	return !__atomic_exchange_n(&pool_busy, 1, __ATOMIC_ACQ_REL);
}

void halo_mt_pool_busy_release(void)
{
	__atomic_store_n(&pool_busy, 0, __ATOMIC_RELEASE);
}

void halo_mt_run(long count, void (*function)(void *context, long index), void *context)
{
	if (count <= 0)
		return;
	if (count == 1 || !halo_mt_workers() || __atomic_exchange_n(&pool_busy, 1, __ATOMIC_ACQ_REL))
	{
		long index;

		for (index = 0; index < count; index++)
			function(context, index);
		return;
	}
	/* (the markers locked and the tick's caches the caller's alone from
here: render_epoch.h) */
	__atomic_add_fetch(&halo_mt_parallel, 1, __ATOMIC_SEQ_CST);
	pthread_mutex_lock(&pool.lock);
	pool.function = function;
	pool.context = context;
	pool.count_items = count;
	pool.next = 0;
	pool.done = 0;
	pool.generation++;
	pthread_cond_broadcast(&pool.wake);
	pthread_mutex_unlock(&pool.lock);
	run_items(function, context, count);
	/* (every item done, and every worker out of the batch: the next batch
	may change what this one's function reads) */
	pthread_mutex_lock(&pool.lock);
	while (__atomic_load_n(&pool.done, __ATOMIC_ACQUIRE) < count || pool.workers_in)
		pthread_cond_wait(&pool.finished, &pool.lock);
	pthread_mutex_unlock(&pool.lock);
	__atomic_sub_fetch(&halo_mt_parallel, 1, __ATOMIC_SEQ_CST);
	__atomic_store_n(&pool_busy, 0, __ATOMIC_RELEASE);
}

/* (perf/cairo-30fps) a batch of the tick's own items: each run with the
tick's view of the game state (render_epoch.h halo_epoch_tick_view) */
struct tick_items
{
	void (*function)(void *context, long index);
	void *context;
};

static void tick_item(void *context, long index)
{
	struct tick_items const *items = (struct tick_items const *)context;
	int was = halo_epoch_tick_view;

	halo_epoch_tick_view_set(1);
	items->function(items->context, index);
	halo_epoch_tick_view_set(was);
}

void halo_mt_run_tick(long count, void (*function)(void *context, long index), void *context)
{
	struct tick_items items;

	items.function = function;
	items.context = context;
	halo_mt_run(count, tick_item, &items);
}

/* a batch begun on the workers alone, the caller going on (one at a time:
halo_mt_wait before the next, and before anything changes what it reads) */
static int started_batch;

void halo_mt_start(long count, void (*function)(void *context, long index), void *context)
{
	if (count <= 0)
		return;
	if (!halo_mt_workers() || __atomic_exchange_n(&pool_busy, 1, __ATOMIC_ACQ_REL))
	{
		long index;

		for (index = 0; index < count; index++)
			function(context, index);
		return;
	}
	__atomic_add_fetch(&halo_mt_parallel, 1, __ATOMIC_SEQ_CST);
	pthread_mutex_lock(&pool.lock);
	pool.function = function;
	pool.context = context;
	pool.count_items = count;
	pool.next = 0;
	pool.done = 0;
	pool.generation++;
	started_batch = 1;
	pool_owner = pthread_self();
	pthread_cond_broadcast(&pool.wake);
	pthread_mutex_unlock(&pool.lock);
}

void halo_mt_wait(void)
{
	/* (none begun, or begun by another thread: not this one's to finish) */
	if (!started_batch || !pthread_equal(pool_owner, pthread_self()))
		return;
	/* (what no worker has taken yet, the caller's) */
	run_items(pool.function, pool.context, pool.count_items);
	pthread_mutex_lock(&pool.lock);
	while (__atomic_load_n(&pool.done, __ATOMIC_ACQUIRE) < pool.count_items || pool.workers_in)
		pthread_cond_wait(&pool.finished, &pool.lock);
	started_batch = 0;
	pthread_mutex_unlock(&pool.lock);
	__atomic_sub_fetch(&halo_mt_parallel, 1, __ATOMIC_SEQ_CST);
	__atomic_store_n(&pool_busy, 0, __ATOMIC_RELEASE);
}

/* ---------- (perf/cairo-30fps-2) the tick's futures (mt_pool.h) */

static struct
{
	/* the open generation (0: none), the items, the next a helper takes, each
	item's state (0 free, 1 taken by the game's thread, 3 by a worker, 2
	done), the helpers inside */
	volatile unsigned long open;
	unsigned long generation;
	void (*function)(void *context, long index);
	void *context;
	long count;
	volatile long next;
	volatile unsigned char state[HALO_MT_FUTURES_MAXIMUM];
	volatile int helpers_in;
	int used_pool;
} futures;

/* (the futures' counts, logged every 256 ends: items the tick ran, ran by
the game's thread, by a worker, a worker's run again by the tick, waited
for) */
static struct
{
	unsigned long ends, items, tick, game, worker, again, waited;
	unsigned long long end_wait_us;
} futures_stats;

unsigned long halo_mt_futures_open(void)
{
	return __atomic_load_n(&futures.open, __ATOMIC_ACQUIRE);
}

static void futures_help(unsigned char taken)
{
	unsigned long generation = __atomic_load_n(&futures.open, __ATOMIC_SEQ_CST);
	int was;

	if (!generation)
		return;
	__atomic_add_fetch(&futures.helpers_in, 1, __ATOMIC_SEQ_CST);
	/* (ended meanwhile - or another begun -: not this helper's) */
	if (__atomic_load_n(&futures.open, __ATOMIC_SEQ_CST) != generation)
	{
		__atomic_sub_fetch(&futures.helpers_in, 1, __ATOMIC_SEQ_CST);
		return;
	}
	was = halo_epoch_tick_view;
	halo_epoch_tick_view_set(1);
	while (__atomic_load_n(&futures.open, __ATOMIC_SEQ_CST) == generation)
	{
		long index = __atomic_fetch_add(&futures.next, 1, __ATOMIC_ACQ_REL);
		unsigned char expected = 0;

		if (index >= futures.count)
			break;
		if (__atomic_compare_exchange_n(&futures.state[index], &expected, taken, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
		{
			futures.function(futures.context, index);
			__atomic_store_n(&futures.state[index], 2, __ATOMIC_RELEASE);
			__atomic_add_fetch(taken == 1 ? &futures_stats.game : &futures_stats.worker, 1, __ATOMIC_RELAXED);
		}
	}
	halo_epoch_tick_view_set(was);
	__atomic_sub_fetch(&futures.helpers_in, 1, __ATOMIC_SEQ_CST);
}

void halo_mt_futures_help(void)
{
	futures_help(1);
}

static void futures_helper_item(void *context, long index)
{
	(void)context;
	(void)index;
	futures_help(3);
}

int halo_mt_futures_begin(long count, void (*function)(void *context, long index), void *context)
{
	long index;
	int workers = halo_mt_workers();

	if (count <= 0 || count > HALO_MT_FUTURES_MAXIMUM || futures.open ||
		__atomic_exchange_n(&pool_busy, 1, __ATOMIC_ACQ_REL))
	{
		return 0;
	}
	futures.function = function;
	futures.context = context;
	futures.count = count;
	futures.next = 0;
	futures.helpers_in = 0;
	for (index = 0; index < count; index++)
		futures.state[index] = 0;
	/* (the markers locked and the tick's caches the caller's alone from
	here: render_epoch.h) */
	__atomic_add_fetch(&halo_mt_parallel, 1, __ATOMIC_SEQ_CST);
	if (++futures.generation == 0)
		futures.generation = 1;
	__atomic_store_n(&futures.open, futures.generation, __ATOMIC_SEQ_CST);
	futures.used_pool = workers > 0;
	if (futures.used_pool)
	{
		/* (each worker one helper's turn) */
		pthread_mutex_lock(&pool.lock);
		pool.function = futures_helper_item;
		pool.context = NULL;
		pool.count_items = workers;
		pool.next = 0;
		pool.done = 0;
		pool.generation++;
		pthread_cond_broadcast(&pool.wake);
		pthread_mutex_unlock(&pool.lock);
	}
	halo_tick_wake_game_for_futures();
	return 1;
}

int halo_mt_futures_claim(long index)
{
	unsigned char expected = 0;

	if (index < 0 || index >= futures.count)
		return 1;
	if (__atomic_compare_exchange_n(&futures.state[index], &expected, 4, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
	{
		futures_stats.tick++;
		return 1;
	}
	/* (a worker's under way: run again by the tick - a worker's core may be
	taken from it for long, the render worker's or the system's -, its
	answer the same and unused; the game's thread's, on a core the game owns
	and begun before the tick came to it, waited for) */
	if (expected == 3)
	{
		futures_stats.again++;
		return 1;
	}
	if (expected != 2)
		futures_stats.waited++;
	while (__atomic_load_n(&futures.state[index], __ATOMIC_ACQUIRE) != 2)
		;
	return 0;
}

void halo_mt_futures_end(void)
{
	unsigned long long end_from = vita_host_time_us ? vita_host_time_us() : 0;

	if (!futures.open)
		return;
	__atomic_store_n(&futures.open, 0, __ATOMIC_SEQ_CST);
	/* (the items under way finished: they read what the tick changes next) */
	while (__atomic_load_n(&futures.helpers_in, __ATOMIC_SEQ_CST))
		;
	if (futures.used_pool)
	{
		/* (the workers' turns: those not taken end at once here) */
		run_items(pool.function, pool.context, pool.count_items);
		pthread_mutex_lock(&pool.lock);
		while (__atomic_load_n(&pool.done, __ATOMIC_ACQUIRE) < pool.count_items || pool.workers_in)
			pthread_cond_wait(&pool.finished, &pool.lock);
		pthread_mutex_unlock(&pool.lock);
	}
	futures_stats.ends++;
	futures_stats.items += futures.count;
	if (end_from)
		futures_stats.end_wait_us += vita_host_time_us() - end_from;
	if (futures_stats.ends % 256 == 0)
		platform_log("multi-core futures: %lu ends, %lu items: the tick ran %lu, the game's thread %lu, the workers %lu "
			"(%lu run again by the tick), %lu waited for; %.1f us a end", futures_stats.ends, futures_stats.items,
			futures_stats.tick, futures_stats.game, futures_stats.worker, futures_stats.again, futures_stats.waited,
			(double)futures_stats.end_wait_us / futures_stats.ends);
	__atomic_sub_fetch(&halo_mt_parallel, 1, __ATOMIC_SEQ_CST);
	__atomic_store_n(&pool_busy, 0, __ATOMIC_RELEASE);
}

static void mt_pool_bench(int batches)
{
	unsigned long long started, empty_us, alone_us, parallel_us, calibrate_us;
	long items = pool.count + 1, rounds = 2000000, k;

	if (!vita_host_time_us || batches <= 0)
		return;
	/* (rounds for an item of about 20 us) */
	started = vita_host_time_us();
	bench_work((void *)rounds, 0);
	calibrate_us = vita_host_time_us() - started;
	if (calibrate_us > 0)
		rounds = (long)((double)rounds * 20.0 / (double)calibrate_us);
	if (rounds < 1)
		rounds = 1;
	started = vita_host_time_us();
	for (k = 0; k < batches; k++)
		halo_mt_run(items, bench_empty, NULL);
	empty_us = vita_host_time_us() - started;
	started = vita_host_time_us();
	for (k = 0; k < batches; k++)
	{
		long index;

		for (index = 0; index < items; index++)
			bench_work((void *)rounds, index);
	}
	alone_us = vita_host_time_us() - started;
	started = vita_host_time_us();
	for (k = 0; k < batches; k++)
		halo_mt_run(items, bench_work, (void *)rounds);
	parallel_us = vita_host_time_us() - started;
	platform_log("multi-core bench: a fork-join of %ld empty items %.1f us; %ld items of ~20 us: %.1f us alone, %.1f us "
		"on the workers and the caller (%d batches)", items, (double)empty_us / batches, items, (double)alone_us / batches,
		(double)parallel_us / batches, batches);
}
