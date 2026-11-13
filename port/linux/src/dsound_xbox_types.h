/* SPDX-FileCopyrightText: OpenH1 contributors
 * SPDX-FileCopyrightText: chestahh (GulchCE)
 * SPDX-FileCopyrightText: 2026 The Chupathingy contributors
 * SPDX-FileCopyrightText: 2026 Milenko x Horrible Dev
 * SPDX-License-Identifier: GPL-3.0-or-later
 * The Xbox models: docs/reading/xbox_audio.md. */

/* port: the HRTF's constants and parameters (dsound_xbox_hrtf.inl), as
chester brought them into GulchCE from OpenH1; the rest of that mixer's
constants (its resamplers, level floor, Doppler law, ramps) stay out, this
mixer keeping its own */

#ifndef DSOUND_XBOX_TYPES_H
#define DSOUND_XBOX_TYPES_H

/* the HRTF (hrtf): the frames each ear keeps for its delay, the distance
within which a sound's direction fades out (world units), the bands each
ear's level is set in (HRTF_BANDS, split at hrtf_crossovers), and the
directions the table (hrtf_table) holds: every 15 degrees round from ahead
to behind, at 60, 40, 20 and 0 degrees below and above, with separate pole responses. Measured on an Xbox
(see the HRTF's comment below) */
#define HRTF_FRAMES 64
#define HRTF_NEAR 0.25f
#define HRTF_BANDS 9
#define HRTF_AZIMUTHS 13
#define HRTF_ELEVATIONS 7
/* the crossfade between the pan and the HRTF when the choice changes
(audio.hrtf): 100 ms */
#define HRTF_FADE_FRAMES 4800

struct hrtf_parameters
{
	/* each ear's delay (frames; left, right), and its weights of the
	bands' low passes and of the sound itself (hrtf_process: each band at
	its level, HRTF_BANDS of them told apart by HRTF_BANDS - 1 low passes) */
	float delay[2];
	float weights[2][HRTF_BANDS];
	/* (each ear's level in each band, dB: for the log, not ramped) */
	float decibels[2][HRTF_BANDS];
	/* each ear's gain: the pan's in the middle (-3 dB), with the voice's
	distance and direct path, the head's filters doing the rest */
	float gain;
	/* the gains of the front pair of speakers and the back pair (on
	stereo 1 and 0; on 5.1 DirectSound's split, surround_split) */
	float pair[2];
};

#endif
