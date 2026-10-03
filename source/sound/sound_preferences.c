/*
SOUND_PREFERENCES.C

symbols in this file:
001BF310 0010:
	_read_sound_preferences (0000)
001BF320 0010:
	_write_sound_preferences (0000)
00317A84 001c:
	_data_00317a84 (0000)
	_sound_channel_type_flags (0014)
*/

/* ---------- headers */

#include "sound_preferences.h"

/* ---------- constants */

/* ---------- macros */

/* ---------- structures */

/* ---------- prototypes */

/* ---------- globals */

static struct sound_preferences default_sound_preferences =
{
	0,
#ifdef HALO_CUSTOM_EDITION
	{ 10, 51, 10, 10, 2, 4, 4, 1 },
	{ 9, 46, 9, 9, 2, 4, 4, 1 },
#else
	{ 10, 51, 10, 10 },
	{ 9, 46, 9, 9 },
#endif
	0,
};

#ifdef HALO_CUSTOM_EDITION
/* the channels' types (sound_dsound_xbox.c: 3D, stereo, 44 kHz, compressed):
the Xbox's, of Xbox ADPCM (mono, mono 3D, stereo, stereo 44 kHz), then the
port's, the same of 16-bit PCM, which Halo PC's Ogg Vorbis sounds play as
(port/linux/game/ce_resources.c decodes them) */
short sound_channel_type_flags[8] = { 8, 9, 10, 14, 0, 1, 2, 6 };
#else
short sound_channel_type_flags[4] = { 8, 9, 10, 14 };
#endif

/* ---------- public code */

void read_sound_preferences(struct sound_preferences **preferences)
{
	*preferences = &default_sound_preferences;
}

void write_sound_preferences(void)
{
}

/* ---------- private code */
