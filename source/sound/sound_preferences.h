/*
SOUND_PREFERENCES.H

header included in hcex build.
*/

#ifndef __SOUND_PREFERENCES_H
#define __SOUND_PREFERENCES_H
#pragma once

/* ---------- constants */

/* ---------- macros */

/* ---------- structures */

struct sound_preferences
{
	short platform;
#ifdef HALO_CUSTOM_EDITION
	/* (per channel type, sound_channel_type_flags: the Xbox's four of Xbox
	ADPCM, then the port's four of 16-bit PCM, for Halo PC's Ogg Vorbis sounds,
	decoded) */
	short actual_channel_counts[8];
	short virtual_channel_counts[8];
#else
	short actual_channel_counts[4];
	short virtual_channel_counts[4];
#endif
	short unused;
};

/* ---------- prototypes/SOUND_PREFERENCES.C */

void read_sound_preferences(
	struct sound_preferences **preferences);
void write_sound_preferences(
	void);

/* ---------- globals */

#ifdef HALO_CUSTOM_EDITION
extern short sound_channel_type_flags[8];
#else
extern short sound_channel_type_flags[4];
#endif

/* ---------- public code */

#endif // __SOUND_PREFERENCES_H
