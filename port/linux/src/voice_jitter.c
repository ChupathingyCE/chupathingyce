/*
VOICE_JITTER.C

A speaker's jitter buffer for voice chat (voice_jitter.h).
*/

#include "voice_jitter.h"

#include <string.h>

/* how far b is after a, in sequence (negative: before) */
static int sequence_after(unsigned short a, unsigned short b)
{
	return (int)(short)(unsigned short)(b - a);
}

void voice_jitter_reset(struct voice_jitter *jitter)
{
	memset(jitter, 0, sizeof(*jitter));
}

int voice_jitter_count(const struct voice_jitter *jitter)
{
	int index, count = 0;

	for (index = 0; index < VOICE_JITTER_SLOTS; index++)
		count += jitter->slots[index].used;
	return count;
}

static struct voice_jitter_slot *slot_of(struct voice_jitter *jitter, unsigned short sequence)
{
	struct voice_jitter_slot *slot = &jitter->slots[sequence % VOICE_JITTER_SLOTS];

	return slot->used && slot->sequence == sequence ? slot : NULL;
}

int voice_jitter_put(struct voice_jitter *jitter, unsigned long now, unsigned short sequence,
	const unsigned char *data, int length)
{
	struct voice_jitter_slot *slot;
	int index;

	if (!data || length < 1 || length > VOICE_JITTER_PACKET_SIZE)
		return 0;
	if (jitter->playing)
	{
		int ahead = sequence_after(jitter->next, sequence);

		if (ahead < 0)
		{
			jitter->late++;
			return 0;
		}
		/* (too far ahead: the speaker started over, or a long gap) */
		if (ahead >= VOICE_JITTER_SLOTS)
		{
			unsigned long late = jitter->late, lost = jitter->lost, skipped = jitter->skipped;

			voice_jitter_reset(jitter);
			jitter->late = late;
			jitter->lost = lost;
			jitter->skipped = skipped;
		}
	}
	else
	{
		/* (a frame far from those waiting: they are from before a gap) */
		for (index = 0; index < VOICE_JITTER_SLOTS; index++)
		{
			struct voice_jitter_slot *other = &jitter->slots[index];
			int distance = other->used ? sequence_after(other->sequence, sequence) : 0;

			if (other->used && (distance >= VOICE_JITTER_SLOTS || distance <= -VOICE_JITTER_SLOTS))
				other->used = 0;
		}
	}
	slot = &jitter->slots[sequence % VOICE_JITTER_SLOTS];
	if (slot->used && slot->sequence == sequence)
		return 0;
	slot->used = 1;
	slot->sequence = sequence;
	slot->length = (unsigned char)length;
	memcpy(slot->data, data, (size_t)length);
	if (!jitter->has_first)
	{
		jitter->has_first = 1;
		jitter->first_time = now;
	}
	return 1;
}

/* the oldest frame kept (playing starts there) */
static int oldest(const struct voice_jitter *jitter, unsigned short *sequence)
{
	int index, found = 0;

	for (index = 0; index < VOICE_JITTER_SLOTS; index++)
	{
		const struct voice_jitter_slot *slot = &jitter->slots[index];

		if (slot->used && (!found || sequence_after(*sequence, slot->sequence) < 0))
		{
			*sequence = slot->sequence;
			found = 1;
		}
	}
	return found;
}

int voice_jitter_take(struct voice_jitter *jitter, unsigned long now, unsigned char *data, int *length)
{
	struct voice_jitter_slot *slot;
	int count = voice_jitter_count(jitter);

	*length = 0;
	if (!jitter->playing)
	{
		if (!count || (count < VOICE_JITTER_START_FRAMES &&
			(!jitter->has_first || now - jitter->first_time < (unsigned long)VOICE_JITTER_START_TIME)))
		{
			return _voice_jitter_wait;
		}
		oldest(jitter, &jitter->next);
		jitter->playing = 1;
	}
	if (!count)
	{
		jitter->playing = 0;
		jitter->has_first = 0;
		return _voice_jitter_end;
	}
	/* (too much waiting: the oldest skipped) */
	while (count > VOICE_JITTER_MOST_WAITING)
	{
		slot = slot_of(jitter, jitter->next);
		if (slot)
		{
			slot->used = 0;
			count--;
		}
		jitter->next++;
		jitter->skipped++;
	}
	slot = slot_of(jitter, jitter->next);
	if (slot)
	{
		memcpy(data, slot->data, slot->length);
		*length = slot->length;
		slot->used = 0;
		jitter->next++;
		return _voice_jitter_frame;
	}
	jitter->lost++;
	slot = slot_of(jitter, (unsigned short)(jitter->next + 1));
	jitter->next++;
	if (slot)
	{
		/* (the next frame stays to be played after) */
		memcpy(data, slot->data, slot->length);
		*length = slot->length;
		return _voice_jitter_fec;
	}
	return _voice_jitter_lost;
}
