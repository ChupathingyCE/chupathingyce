/*
CONTENT_AUDIO.H

IMA ADPCM's step, and its tables (the Xbox's ADPCM is IMA's), one copy for the
desktop mixer's decoder (dsound_sdl.c) and MCC's coder (mcc_audio.c).
content_audio.c. The retail game's own copy stays in source/sound/ima_adpcm.c,
and tools/test_content_golden.py checks these are the same numbers.
*/

#ifndef CONTENT_AUDIO_H
#define CONTENT_AUDIO_H

#define CONTENT_IMA_STEP_COUNT 89

/* each step index's step size */
extern const int content_ima_steps[CONTENT_IMA_STEP_COUNT];
/* the step index's change after each code */
extern const int content_ima_changes[16];

/* one code (a 4-bit nibble) applied to a sample and its step index, both
kept between 16-bit and 0..88: the new sample */
int content_ima_expand(int code, int *value, int *step);

#endif
