/*
XBOX_DSOUND.C

The game's DirectSound calls, as the January 2002 SDK declared them, on a
later SDK's DirectSound library.

The January SDK named a voice's mix bins with a mask, one bit per bin
(DSBUFFERDESC and DSSTREAMDESC's dwMixBinMask, SetMixBins(mask),
SetMixBinVolumes(mask, volumes), SetMixBinHeadroom(mask, headroom)); later
SDKs take a DSMIXBINS list of bin and volume pairs. Bit N of a January mask
is bin N of the later list (port/include/xdk/xdk_dsound.h). The game's calls
to these are renamed to the halo_xbox_ functions here by
port/xbox/include/halo_xbox_prefix.h; everything else goes to the library
directly, its declarations being the same in both SDKs.

DirectSoundStopStream and DirectSoundGetStreamVoiceStatus came from a
stream helper linked with the January game (dsstrmh), not the library.

This file is compiled with the later SDK's headers, not the game's.
*/

#include <xtl.h>

/* ---------- the January SDK's structures */

struct january_buffer_description
{
	DWORD size;
	DWORD flags;
	DWORD buffer_bytes;
	LPWAVEFORMATEX format;
	DWORD mix_bin_mask;
	DWORD input_mix_bin_mask;
};

struct january_stream_description
{
	DWORD flags;
	DWORD maximum_attached_packets;
	LPWAVEFORMATEX format;
	LPFNXMEDIAOBJECTCALLBACK callback;
	LPVOID context;
	DWORD mix_bin_mask;
};

/* ---------- mix bins */

#define MAXIMUM_MIX_BINS 32

struct mix_bins
{
	DSMIXBINS bins;
	DSMIXBINVOLUMEPAIR pairs[MAXIMUM_MIX_BINS];
};

/* The bins of a January mask, with the volumes given in bin order (or full
volume). */
static LPCDSMIXBINS mix_bins_from_mask(
	struct mix_bins *out,
	DWORD mask,
	const LONG *volumes)
{
	DWORD bin;

	out->bins.dwMixBinCount= 0;
	out->bins.lpMixBinVolumePairs= out->pairs;
	for (bin= 0; bin<MAXIMUM_MIX_BINS; bin++)
	{
		if (mask & (1UL<<bin))
		{
			DSMIXBINVOLUMEPAIR *pair= &out->pairs[out->bins.dwMixBinCount];

			pair->dwMixBin= bin;
			pair->lVolume= volumes ? volumes[out->bins.dwMixBinCount] : DSBVOLUME_MAX;
			out->bins.dwMixBinCount++;
		}
	}

	return &out->bins;
}

/* ---------- buffers and streams */

static void buffer_description_from_january(
	DSBUFFERDESC *description,
	struct mix_bins *bins,
	const struct january_buffer_description *january)
{
	ZeroMemory(description, sizeof(*description));
	description->dwSize= sizeof(*description);
	description->dwFlags= january->flags;
	description->dwBufferBytes= january->buffer_bytes;
	description->lpwfxFormat= january->format;
	description->lpMixBins= january->mix_bin_mask ? mix_bins_from_mask(bins, january->mix_bin_mask, NULL) : NULL;
	description->dwInputMixBin= 0;
	if (january->input_mix_bin_mask)
	{
		DWORD bin;

		for (bin= 0; !(january->input_mix_bin_mask & (1UL<<bin)); bin++);
		description->dwInputMixBin= bin;
	}
}

HRESULT WINAPI halo_xbox_DirectSoundCreateBuffer(
	const struct january_buffer_description *january,
	LPDIRECTSOUNDBUFFER *buffer)
{
	DSBUFFERDESC description;
	struct mix_bins bins;

	buffer_description_from_january(&description, &bins, january);
	return DirectSoundCreateBuffer(&description, buffer);
}

HRESULT WINAPI halo_xbox_IDirectSound_CreateSoundBuffer(
	LPDIRECTSOUND direct_sound,
	const struct january_buffer_description *january,
	LPDIRECTSOUNDBUFFER *buffer,
	LPUNKNOWN outer)
{
	DSBUFFERDESC description;
	struct mix_bins bins;

	buffer_description_from_january(&description, &bins, january);
	return IDirectSound_CreateSoundBuffer(direct_sound, &description, buffer, outer);
}

HRESULT WINAPI halo_xbox_IDirectSound_CreateSoundStream(
	LPDIRECTSOUND direct_sound,
	const struct january_stream_description *january,
	LPDIRECTSOUNDSTREAM *stream,
	LPUNKNOWN outer)
{
	DSSTREAMDESC description;
	struct mix_bins bins;

	ZeroMemory(&description, sizeof(description));
	description.dwFlags= january->flags;
	description.dwMaxAttachedPackets= january->maximum_attached_packets;
	description.lpwfxFormat= january->format;
	description.lpfnCallback= january->callback;
	description.lpvContext= january->context;
	description.lpMixBins= january->mix_bin_mask ? mix_bins_from_mask(&bins, january->mix_bin_mask, NULL) : NULL;
	return IDirectSound_CreateSoundStream(direct_sound, &description, stream, outer);
}

HRESULT WINAPI halo_xbox_IDirectSoundStream_SetMixBins(
	LPDIRECTSOUNDSTREAM stream,
	DWORD mask)
{
	struct mix_bins bins;

	return IDirectSoundStream_SetMixBins(stream, mix_bins_from_mask(&bins, mask, NULL));
}

HRESULT WINAPI halo_xbox_IDirectSoundStream_SetMixBinVolumes(
	LPDIRECTSOUNDSTREAM stream,
	DWORD mask,
	const LONG *volumes)
{
	struct mix_bins bins;

	return IDirectSoundStream_SetMixBinVolumes(stream, mix_bins_from_mask(&bins, mask, volumes));
}

HRESULT WINAPI halo_xbox_IDirectSound_SetMixBinHeadroom(
	LPDIRECTSOUND direct_sound,
	DWORD mask,
	DWORD headroom)
{
	HRESULT result= DS_OK;
	DWORD bin;

	for (bin= 0; bin<MAXIMUM_MIX_BINS && SUCCEEDED(result); bin++)
	{
		if (mask & (1UL<<bin))
		{
			result= IDirectSound_SetMixBinHeadroom(direct_sound, bin, headroom);
		}
	}

	return result;
}

/* The game carries the January SDK's effects image (reverb and crosstalk
DSP code), built for that SDK's DirectSound; this library's image format is
not known to match, so no image is downloaded and the game plays without
reverb. The game only checks the result. */
HRESULT WINAPI halo_xbox_IDirectSound_DownloadEffectsImage(
	LPDIRECTSOUND direct_sound,
	LPCVOID image,
	DWORD image_size,
	LPCDSEFFECTIMAGELOC location,
	LPDSEFFECTIMAGEDESC *description)
{
	(void)direct_sound;
	(void)image;
	(void)image_size;
	(void)location;
	if (description)
	{
		*description= NULL;
	}

	return DS_OK;
}

/* ---------- the stream helper */

void WINAPI DirectSoundStopStream(
	LPDIRECTSOUNDSTREAM stream)
{
	IDirectSoundStream_FlushEx(stream, 0, DSSTREAMFLUSHEX_ASYNC);
}

DWORD WINAPI DirectSoundGetStreamVoiceStatus(
	LPDIRECTSOUNDSTREAM stream)
{
	DWORD status= 0;

	if (FAILED(IDirectSoundStream_GetStatus(stream, &status)))
	{
		return 0;
	}

	return (status & DSSTREAMSTATUS_PLAYING) ? 1 : 0;
}
