/*
H2_EMBLEM_PROTOCOL.C

See h2_emblem_protocol.h. From the Halo 2 decompilation (halo2-decomp,
GPL-3.0-or-later): function_153750 (src/unknown_153750.cpp), the appearance's
checks, and player_profile_initialize's and player_profile_make's values
(src/player_profile_storage.cpp).
*/

#include "h2_emblem_protocol.h"

#include <string.h>

void h2_emblem_default(struct h2_emblem *emblem)
{
	memset(emblem, 0, sizeof(*emblem));
	/* (colours 10, 0, 0 and 10: cobalt, white, white, cobalt) */
	emblem->colors[0] = emblem->colors[3] = 10;
}

int h2_emblem_valid(struct h2_emblem const *emblem)
{
	int i;

	for (i = 0; i < H2_EMBLEM_CHANGE_COLORS; i++)
	{
		if (emblem->colors[i] >= H2_EMBLEM_COLORS)
			return 0;
	}
	return emblem->foreground < H2_EMBLEM_FOREGROUNDS && emblem->background < H2_EMBLEM_BACKGROUNDS &&
		emblem->model < H2_EMBLEM_MODELS &&
		!(emblem->flags & H2_EMBLEM_FLAGS_INVALID);
}

void h2_emblem_from_seed(unsigned long seed, struct h2_emblem *emblem)
{
	uint32_t state = (uint32_t)seed * 2654435761u + 1013904223u;
	int i;

	for (i = 0; i < H2_EMBLEM_CHANGE_COLORS; i++)
	{
		state = state * 1664525u + 1013904223u;
		emblem->colors[i] = (uint8_t)((state >> 16) % 16);
	}
	state = state * 1664525u + 1013904223u;
	emblem->foreground = (uint8_t)((state >> 16) % H2_EMBLEM_FOREGROUNDS);
	state = state * 1664525u + 1013904223u;
	emblem->background = (uint8_t)((state >> 16) % H2_EMBLEM_BACKGROUNDS);
	emblem->flags = 0;
	emblem->model = 0;
}

void emblem_net_encode(struct emblem_net_message *message, int machine, int controller, int present,
	struct h2_emblem const *emblem)
{
	memset(message, 0, sizeof(*message));
	message->version = EMBLEM_NET_VERSION;
	message->machine = (uint8_t)machine;
	message->controller = (uint8_t)controller;
	message->present = (uint8_t)(present != 0);
	if (present && emblem)
	{
		message->foreground = emblem->foreground;
		message->background = emblem->background;
		message->flags = (uint8_t)(emblem->flags & H2_EMBLEM_FLAGS_KEPT);
		message->model = emblem->model;
		memcpy(message->colors, emblem->colors, H2_EMBLEM_CHANGE_COLORS);
	}
}

int emblem_net_decode(struct emblem_net_message const *message, int *machine, int *controller, int *present,
	struct h2_emblem *emblem)
{
	if (message->version != EMBLEM_NET_VERSION || message->machine >= EMBLEM_NET_MACHINES ||
		message->controller >= EMBLEM_NET_PLAYERS || message->present > 1 || message->reserved)
	{
		return 0;
	}
	memset(emblem, 0, sizeof(*emblem));
	if (message->present)
	{
		emblem->foreground = message->foreground;
		emblem->background = message->background;
		emblem->flags = message->flags;
		emblem->model = message->model;
		memcpy(emblem->colors, message->colors, H2_EMBLEM_CHANGE_COLORS);
		if (!h2_emblem_valid(emblem))
			return 0;
		emblem->flags &= H2_EMBLEM_FLAGS_KEPT;
	}
	else
	{
		static uint8_t const zeros[H2_EMBLEM_CHANGE_COLORS];

		if (message->foreground || message->background || message->flags || message->model ||
			memcmp(message->colors, zeros, H2_EMBLEM_CHANGE_COLORS))
		{
			return 0;
		}
	}
	*machine = message->machine;
	*controller = message->controller;
	*present = message->present;
	return 1;
}

void emblem_net_table_reset(struct emblem_net_table *table)
{
	memset(table, 0, sizeof(*table));
}

static int emblem_net_in_range(int machine, int controller)
{
	return machine >= 0 && machine < EMBLEM_NET_MACHINES && controller >= 0 && controller < EMBLEM_NET_PLAYERS;
}

int emblem_net_table_set(struct emblem_net_table *table, int machine, int controller, struct h2_emblem const *emblem)
{
	struct emblem_net_entry *entry;

	if (!emblem_net_in_range(machine, controller) || !h2_emblem_valid(emblem))
		return 0;
	entry = &table->entries[machine][controller];
	if (entry->known && !memcmp(&entry->emblem, emblem, sizeof(*emblem)))
		return 0;
	entry->known = 1;
	entry->emblem = *emblem;
	entry->serial = ++table->serial;
	return 1;
}

int emblem_net_table_clear(struct emblem_net_table *table, int machine, int controller)
{
	struct emblem_net_entry *entry;

	if (!emblem_net_in_range(machine, controller))
		return 0;
	entry = &table->entries[machine][controller];
	if (!entry->known)
		return 0;
	entry->known = 0;
	memset(&entry->emblem, 0, sizeof(entry->emblem));
	entry->serial = ++table->serial;
	return 1;
}

int emblem_net_table_clear_machine(struct emblem_net_table *table, int machine)
{
	int controller, cleared = 0;

	for (controller = 0; controller < EMBLEM_NET_PLAYERS; controller++)
		cleared |= emblem_net_table_clear(table, machine, controller);
	return cleared;
}

struct h2_emblem const *emblem_net_table_get(struct emblem_net_table const *table, int machine, int controller)
{
	if (!emblem_net_in_range(machine, controller) || !table->entries[machine][controller].known)
		return 0;
	return &table->entries[machine][controller].emblem;
}

int emblem_net_rate_take(struct emblem_net_rate *rate, uint32_t now_ms)
{
	uint32_t elapsed;

	if (!rate->started)
	{
		rate->started = 1;
		rate->tokens = EMBLEM_NET_BURST;
		rate->last_ms = now_ms;
	}
	elapsed = now_ms - rate->last_ms;
	if (elapsed >= EMBLEM_NET_REFILL_MS)
	{
		uint32_t gained = elapsed / EMBLEM_NET_REFILL_MS;

		rate->tokens = gained >= EMBLEM_NET_BURST ? EMBLEM_NET_BURST : rate->tokens + (int32_t)gained;
		if (rate->tokens > EMBLEM_NET_BURST)
			rate->tokens = EMBLEM_NET_BURST;
		rate->last_ms += gained * EMBLEM_NET_REFILL_MS;
	}
	if (rate->tokens <= 0)
		return 0;
	rate->tokens--;
	return 1;
}
