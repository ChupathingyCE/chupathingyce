/*
FSB_CELT.H

(port) FMOD's CELT, the codec of the dialogue, music and streamed sounds of
Halo: The Master Chief Collection's Halo CE (halo1\sound\pc\sounds_dialogs.fsb,
sounds_music.fsb, sounds_stream.fsb, sounds_debug.fsb; fsb5.h): libcelt 0.11
(bitstream 0x80000010, port/third_party/celt) in a mode of 512 samples a
frame at 44100 Hz, each frame in the bank an 8-byte header (a big-endian
0x17C30DF3 and the payload's size, little-endian: fsb5_celt_frame_check)
and its payload, decoded with the library's pitch postfilter on and its
signalling off (FMOD's frames carry no table-of-contents byte; celt_port.h).
A stream is its frames one after another,
each 512 frames of its 1 or 2 channels; its sample count is its frames times
512.

The mode is made once, on the first decoder's start, from a few kilobytes
the library asks for (celt_port.h); a decoder is the caller's memory
(fsb_celt_decoder_bytes), so any number may run on any threads.

Built without a decoder (HALO_OGG_SOUND_NO_DECODER: the Android guest),
every decoder fails to start and the sounds stay silent.
*/

#ifndef __FSB_CELT_H
#define __FSB_CELT_H

#include <stddef.h>
#include <stdint.h>

enum
{
	FSB_CELT_RATE = 44100,
	FSB_CELT_FRAME_SAMPLES = 512,
	FSB_CELT_HEADER_BYTES = 8,
	/* the largest payload taken (FMOD's are 370 bytes a mono frame and 464
	a stereo one in MCC's banks; CELT's own limit is 1275) */
	FSB_CELT_MAXIMUM_PAYLOAD_BYTES = 1275,
	/* the most a decoder of 1 or 2 channels takes (fsb_celt_decoder_bytes
	is not more) */
	FSB_CELT_DECODER_MAXIMUM_BYTES = 24 * 1024
};

/* the memory a decoder of `channels` (1 or 2) takes, 0 when none can be
made (no mode, or no decoder in this build) */
size_t fsb_celt_decoder_bytes(int channels);

/* starts a decoder of `channels` in `memory` (`bytes`, 8-byte aligned):
nonzero on success; again for a new stream */
int fsb_celt_decoder_start(void *memory, size_t bytes, int channels);

/* decodes one frame's payload (`payload_bytes`, the header's size, not the
header) into `pcm` (FSB_CELT_FRAME_SAMPLES frames of the decoder's channels,
interleaved): FSB_CELT_FRAME_SAMPLES, or a negative number when the frame
is damaged (its samples then silence) */
int fsb_celt_decode(void *decoder, uint8_t const *payload, uint32_t payload_bytes, int16_t *pcm);

#endif
