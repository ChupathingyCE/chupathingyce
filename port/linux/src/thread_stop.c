/*
THREAD_STOP.C

The platform layer's threads, ended together at the game's end
(thread_stop.h). The threads are counted in a table of their names, under
one lock; the end sets the signal, wakes the sleepers (thread_stop_sleep)
and waits on the same condition for the counted ones to leave.
*/

#include "thread_stop.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

void platform_log(const char *format, ...) __attribute__((format(printf, 1, 2)));

enum
{
	THREAD_STOP_SLOTS = 32,
	THREAD_STOP_NAME_SIZE = 48,
};

struct thread_stop_slot
{
	char name[THREAD_STOP_NAME_SIZE];
	int used;
	int waited;
};

struct thread_stop_start
{
	void *(*main)(void *);
	void *argument;
	int slot;
};

static pthread_mutex_t thread_stop_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t thread_stop_condition = PTHREAD_COND_INITIALIZER;
static struct thread_stop_slot thread_stop_slots[THREAD_STOP_SLOTS];
static volatile int thread_stop_asked;

/* a CLOCK_REALTIME deadline milliseconds from now (pthread_cond_timedwait's
clock) */
static void deadline_after(unsigned long milliseconds, struct timespec *deadline)
{
	clock_gettime(CLOCK_REALTIME, deadline);
	deadline->tv_sec += (time_t)(milliseconds / 1000);
	deadline->tv_nsec += (long)(milliseconds % 1000) * 1000000L;
	if (deadline->tv_nsec >= 1000000000L)
	{
		deadline->tv_sec++;
		deadline->tv_nsec -= 1000000000L;
	}
}

static unsigned long milliseconds_since(const struct timespec *start)
{
	struct timespec now;
	long long elapsed;

	clock_gettime(CLOCK_REALTIME, &now);
	elapsed = (long long)(now.tv_sec - start->tv_sec) * 1000 + (now.tv_nsec - start->tv_nsec) / 1000000L;
	return elapsed > 0 ? (unsigned long)elapsed : 0;
}

int thread_stop_enter(const char *name, int waited)
{
	int slot;

	pthread_mutex_lock(&thread_stop_lock);
	for (slot = 0; slot < THREAD_STOP_SLOTS && thread_stop_slots[slot].used; slot++)
		;
	if (thread_stop_asked || slot == THREAD_STOP_SLOTS)
	{
		pthread_mutex_unlock(&thread_stop_lock);
		return -1;
	}
	snprintf(thread_stop_slots[slot].name, sizeof(thread_stop_slots[slot].name), "%s", name);
	thread_stop_slots[slot].used = 1;
	thread_stop_slots[slot].waited = waited;
	pthread_mutex_unlock(&thread_stop_lock);
	return slot;
}

void thread_stop_leave(int slot)
{
	if (slot < 0 || slot >= THREAD_STOP_SLOTS)
		return;
	pthread_mutex_lock(&thread_stop_lock);
	thread_stop_slots[slot].used = 0;
	pthread_cond_broadcast(&thread_stop_condition);
	pthread_mutex_unlock(&thread_stop_lock);
}

static void *thread_stop_main(void *context)
{
	struct thread_stop_start start = *(struct thread_stop_start *)context;

	free(context);
	start.main(start.argument);
	thread_stop_leave(start.slot);
	return NULL;
}

int thread_stop_create(const char *name, void *(*main)(void *), void *argument)
{
	struct thread_stop_start *start;
	pthread_t thread;
	int slot = thread_stop_enter(name, 1);

	/* (a full table still starts the thread, uncounted; only the end stops
	new ones) */
	if (slot < 0 && thread_stop_asked)
		return -1;
	start = malloc(sizeof(*start));
	if (!start)
	{
		thread_stop_leave(slot);
		return -1;
	}
	start->main = main;
	start->argument = argument;
	start->slot = slot;
	if (pthread_create(&thread, NULL, thread_stop_main, start) != 0)
	{
		free(start);
		thread_stop_leave(slot);
		return -1;
	}
	pthread_detach(thread);
	return 0;
}

int thread_stop_requested(void)
{
	return thread_stop_asked;
}

int thread_stop_sleep(unsigned long milliseconds)
{
	struct timespec deadline;
	int asked;

	deadline_after(milliseconds, &deadline);
	pthread_mutex_lock(&thread_stop_lock);
	while (!thread_stop_asked &&
		pthread_cond_timedwait(&thread_stop_condition, &thread_stop_lock, &deadline) == 0)
	{
		;
	}
	asked = thread_stop_asked;
	pthread_mutex_unlock(&thread_stop_lock);
	return asked;
}

void thread_stop_all(unsigned long milliseconds)
{
	struct
	{
		char name[THREAD_STOP_NAME_SIZE];
		int slot;
		int waited;
		int stopped;
		unsigned long after;
	} counted[THREAD_STOP_SLOTS];
	struct timespec start, deadline;
	int count = 0, index, slot;

	pthread_mutex_lock(&thread_stop_lock);
	if (thread_stop_asked)
	{
		pthread_mutex_unlock(&thread_stop_lock);
		return;
	}
	thread_stop_asked = 1;
	for (slot = 0; slot < THREAD_STOP_SLOTS; slot++)
	{
		if (!thread_stop_slots[slot].used)
			continue;
		memcpy(counted[count].name, thread_stop_slots[slot].name, sizeof(counted[count].name));
		counted[count].slot = slot;
		counted[count].waited = thread_stop_slots[slot].waited;
		counted[count].stopped = 0;
		counted[count].after = 0;
		count++;
	}
	pthread_cond_broadcast(&thread_stop_condition);
	clock_gettime(CLOCK_REALTIME, &start);
	deadline_after(milliseconds, &deadline);
	/* (no thread is counted after the signal, so a slot that empties is
	the thread that was in it) */
	for (index = 0; index < count; index++)
	{
		if (!counted[index].waited)
			continue;
		while (thread_stop_slots[counted[index].slot].used &&
			pthread_cond_timedwait(&thread_stop_condition, &thread_stop_lock, &deadline) == 0)
		{
			;
		}
		counted[index].stopped = !thread_stop_slots[counted[index].slot].used;
		counted[index].after = milliseconds_since(&start);
	}
	for (index = 0; index < count; index++)
	{
		if (!counted[index].waited)
			counted[index].stopped = !thread_stop_slots[counted[index].slot].used;
	}
	pthread_mutex_unlock(&thread_stop_lock);

	platform_log("exit: %d thread%s to end", count, count == 1 ? "" : "s");
	for (index = 0; index < count; index++)
	{
		if (counted[index].stopped)
			platform_log("exit: %s stopped", counted[index].name);
		else if (counted[index].waited)
			platform_log("exit: %s still running after %lu ms", counted[index].name, counted[index].after);
		else
			platform_log("exit: %s still running (the game's own: not asked to stop)", counted[index].name);
	}
}
