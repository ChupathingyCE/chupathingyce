/*
MCC_SOUND.H

(port) The sounds of MCC's own Halo CE maps (docs/mcc_cea_maps.md): MCC
keeps them in FMOD banks beside the maps (halo1\sound\pc\*.fsb, fsb5.h),
and in lists (halo1\sound\pc\lst\sounds_*.lst.bin, mcc_sound_list.h) that
say which streams of a bank each sound tag is.

The sound effects' bank (sounds_adpcm.fsb) is IMA ADPCM, the Xbox's own
format, so its streams are the permutations' samples as they lie, read
from the bank by the sound cache like a sounds.map's. The dialogue
(sounds_dialogs.fsb), the music (sounds_music.fsb), the streamed ambience,
cinematic music and foley (sounds_stream.fsb) and the Anniversary-only
sound effects (sounds_debug.fsb) are banks of FMOD's CELT (fsb_celt.h):
their permutations are made Xbox ADPCM as the sound cache loads them
(custom_edition_sounds.c, kept in the map's sound cache file), read from the
bank's file (mcc_sound_celt_read). A sound is the classic one MCC plays in
its classic mode where the lists have it (sound\old_sfx\... for
sound\sfx\..., sound\old_music\... for the music and the cinematics'
sound\...), else the path's own.

The lists and the banks' tables are opened when an MCC map's tags are
converted, closed straight after (mcc_sound_open, mcc_sound_close); the
CELT banks' files stay open for the decoding from the first read on.
*/

#ifndef __MCC_SOUND_H
#define __MCC_SOUND_H

#include "cache_file_formats.h"

/* the banks and lists, in the content folder (d:\mcc\) */
#define MCC_SOUND_DIRECTORY "d:\\mcc\\halo1\\sound\\pc\\"
#define MCC_SOUND_ADPCM_BANK "sounds_adpcm.fsb"
#define MCC_SOUND_ADPCM_LIST "lst\\sounds_adpcm.lst.bin"

/* the banks (a CELT permutation's compression carries the number:
CUSTOM_EDITION_PERMUTATION_MCC_CELT) */
enum
{
	MCC_SOUND_BANK_ADPCM,
	MCC_SOUND_BANK_DIALOGS,
	MCC_SOUND_BANK_MUSIC,
	MCC_SOUND_BANK_STREAM,
	MCC_SOUND_BANK_DEBUG,
	NUMBER_OF_MCC_SOUND_BANKS
};

/* Opens the sound effects' bank and the lists; `combined_offset` is where
the bank starts in the combined offset space and `bank_bytes` its size (the
file the sounds slot has open). FALSE, `source` empty, with `reason` (a
sentence, may be NULL) when the bank or its list is not there or not whole. */
boolean mcc_sound_open(
	struct mcc_sound_source *source,
	uint32_t combined_offset,
	uint32_t bank_bytes,
	char *reason,
	long reason_size);
void mcc_sound_close(void);

/* What the map's sounds got, for the log. */
void mcc_sound_log(struct custom_edition_conversion_report const *report);

/* the bank's file name ("sounds_dialogs.fsb") */
char const *mcc_sound_bank_name(int bank);

/* Reads `size` bytes at `offset` of CELT bank `bank`'s file (opened the
first time, kept open; any thread): nonzero when all were read. */
int mcc_sound_celt_read(int bank, uint32_t offset, uint32_t size, void *buffer);


#endif
