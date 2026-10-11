/*
BINK_FFMPEG.C

The Bink video SDK entry points bink_playback.c uses, decoded with FFmpeg
instead of the proprietary RAD SDK (bink_null.c keeps the stubs for builds
without FFmpeg; HALO_BINK_FFMPEG picks this one).

Why here and not in the Android app: the game already drives the movies
(bink_playback.c opens them, waits on their clock, copies every frame into a
locked texture and the renderer draws it), and its sound is the port's own
DirectSound. Implementing the SDK's own API means every port gets the movies
through the renderer and at the game's volume, with no per-platform player.

The contract (from bink_playback.c): the handle's first five members are read
directly by the game, and BinkCopyToBuffer writes into a linear X8R8G8B8
texture it has locked.
*/

#ifdef HALO_BINK_FFMPEG

#include "platform.h"

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
#include <SDL3/SDL.h>

#include <stdlib.h>
#include <string.h>
#include <stdint.h>

typedef void *(__stdcall *rad_memory_allocate_proc)(unsigned long size);
typedef void (__stdcall *rad_memory_free_proc)(void *memory);
typedef void *(__stdcall *bink_sound_system_open_proc)(unsigned long param);

/* the five the game reads, in its order; the rest is ours */
struct BINK
{
	unsigned long Width;
	unsigned long Height;
	unsigned long Frames;
	unsigned long FrameNum;
	unsigned long LastFrameNum;

	AVFormatContext *format;
	AVCodecContext *video;
	AVCodecContext *audio;
	int video_stream;
	int audio_stream;
	AVPacket *packet;
	AVFrame *decoded;
	AVFrame *rgba;
	AVFrame *audio_frame;
	struct SwsContext *to_rgba;
	unsigned char *pixels;      /* one frame, X8R8G8B8, Width x Height */
	long pixels_pitch;
	int decoded_frame;          /* the frame in pixels now */
	int submitted;              /* the frame BinkDoFrame was asked for */
	uint64_t start_ticks;       /* when the movie started, for BinkWait */
	double frame_seconds;
	SwrContext *to_sdl;
	SDL_AudioStream *stream;
	SDL_AudioDeviceID device;
	uint8_t *audio_out;
	int audio_out_size;
	int sound_system_ready;
};

typedef struct BINK *HBINK;

void __stdcall RADSetMemory(rad_memory_allocate_proc allocate, rad_memory_free_proc release)
{
	(void)allocate;
	(void)release;
}

void __stdcall BinkSetIOSize(unsigned long io_size)
{
	(void)io_size;
}

/* The game hands Bink its DirectSound (the port's own, dsound_sdl.c) and Bink
mixes the movie into it. FFmpeg's audio goes to SDL's output here, scaled by the
game's own volume, which is what the game means by "the game's volume". */
void *__stdcall BinkOpenDirectSound(unsigned long param)
{
	(void)param;
	return (void *)1;
}

long __stdcall BinkSetSoundSystem(bink_sound_system_open_proc open, unsigned long param)
{
	(void)open;
	(void)param;
	return 1;
}

static int bink_audio_open(struct BINK *bink)
{
	SDL_AudioSpec have;
	SDL_AudioSpec want;

	if (bink->audio_stream < 0)
		return 0;
	SDL_zero(want);
	want.format = SDL_AUDIO_F32;
	want.channels = 2;
	want.freq = 48000;
	bink->device = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &want);
	if (!bink->device)
	{
		platform_log("Bink video: no audio output (%s); playing the movie without sound", SDL_GetError());
		return 0;
	}
	if (!SDL_GetAudioDeviceFormat(bink->device, &have, NULL))
	{
		SDL_CloseAudioDevice(bink->device);
		bink->device = 0;
		return 0;
	}
	bink->stream = SDL_OpenAudioDeviceStream(bink->device, &have, NULL, NULL);
	if (!bink->stream)
	{
		SDL_CloseAudioDevice(bink->device);
		bink->device = 0;
		return 0;
	}
	SDL_ResumeAudioStreamDevice(bink->stream);
	return 1;
}

HBINK __stdcall BinkOpen(const char *name, unsigned long flags)
{
	struct BINK *bink;
	const AVCodec *codec;
	double fps;

	(void)flags;
	if (!name)
		return NULL;
	bink = calloc(1, sizeof(*bink));
	if (!bink)
		return NULL;
	bink->video_stream = -1;
	bink->audio_stream = -1;
	bink->decoded_frame = -1;
	bink->submitted = -1;
	if (avformat_open_input(&bink->format, name, NULL, NULL) < 0)
	{
		platform_log("Bink video: \"%s\" cannot be opened", name);
		free(bink);
		return NULL;
	}
	if (avformat_find_stream_info(bink->format, NULL) < 0)
		goto fail;
	bink->video_stream = av_find_best_stream(bink->format, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
	if (bink->video_stream < 0)
		goto fail;
	codec = avcodec_find_decoder(bink->format->streams[bink->video_stream]->codecpar->codec_id);
	if (!codec)
		goto fail;
	bink->video = avcodec_alloc_context3(codec);
	if (!bink->video)
		goto fail;
	if (avcodec_parameters_to_context(bink->video, bink->format->streams[bink->video_stream]->codecpar) < 0)
		goto fail;
	if (avcodec_open2(bink->video, codec, NULL) < 0)
		goto fail;

	bink->Width = (unsigned long)bink->video->width;
	bink->Height = (unsigned long)bink->video->height;
	fps = av_guess_frame_rate(bink->format, bink->format->streams[bink->video_stream], NULL).num /
		(double)av_guess_frame_rate(bink->format, bink->format->streams[bink->video_stream], NULL).den;
	bink->frame_seconds = fps > 0.0 ? 1.0 / fps : 1.0 / 15.0;
	bink->Frames = bink->format->streams[bink->video_stream]->nb_frames > 0 ?
		(unsigned long)bink->format->streams[bink->video_stream]->nb_frames :
		(unsigned long)(bink->format->duration / (AV_TIME_BASE * bink->frame_seconds) + 0.5);

	bink->pixels_pitch = (long)bink->Width * 4;
	bink->pixels = malloc((size_t)bink->pixels_pitch * bink->Height);
	if (!bink->pixels)
		goto fail;
	memset(bink->pixels, 0, (size_t)bink->pixels_pitch * bink->Height);
	bink->packet = av_packet_alloc();
	bink->decoded = av_frame_alloc();
	bink->rgba = av_frame_alloc();
	bink->audio_frame = av_frame_alloc();
	if (!bink->packet || !bink->decoded || !bink->rgba || !bink->audio_frame)
		goto fail;
	bink->rgba->format = AV_PIX_FMT_BGRA;
	bink->rgba->width = (int)bink->Width;
	bink->rgba->height = (int)bink->Height;
	if (av_frame_get_buffer(bink->rgba, 0) < 0)
		goto fail;
	bink->to_rgba = sws_getContext((int)bink->Width, (int)bink->Height, bink->video->pix_fmt,
		(int)bink->Width, (int)bink->Height, AV_PIX_FMT_BGRA, SWS_BILINEAR, NULL, NULL, NULL);
	if (!bink->to_rgba)
		goto fail;

	/* the movie's own sound, if the disc's file carries one */
	bink->audio_stream = av_find_best_stream(bink->format, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
	if (bink->audio_stream >= 0)
	{
		const AVCodec *acodec = avcodec_find_decoder(
			bink->format->streams[bink->audio_stream]->codecpar->codec_id);

		if (acodec)
		{
			bink->audio = avcodec_alloc_context3(acodec);
			if (bink->audio &&
				avcodec_parameters_to_context(bink->audio,
					bink->format->streams[bink->audio_stream]->codecpar) >= 0 &&
				avcodec_open2(bink->audio, acodec, NULL) >= 0)
			{
				AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
				AVChannelLayout in_layout = bink->audio->ch_layout;

				if (in_layout.nb_channels <= 0)
					av_channel_layout_default(&in_layout, 2);
				if (swr_alloc_set_opts2(&bink->to_sdl, &stereo, AV_SAMPLE_FMT_FLT, 48000,
						&in_layout, bink->audio->sample_fmt, bink->audio->sample_rate,
						0, NULL) >= 0 && swr_init(bink->to_sdl) >= 0)
				{
					bink->sound_system_ready = bink_audio_open(bink) != 0;
				}
			}
		}
	}
	bink->start_ticks = SDL_GetTicks();
	return bink;

fail:
	platform_log("Bink video: \"%s\" cannot be decoded", name);
	if (bink)
	{
		if (bink->format)
			avformat_close_input(&bink->format);
		avcodec_free_context(&bink->video);
		avcodec_free_context(&bink->audio);
		av_packet_free(&bink->packet);
		av_frame_free(&bink->decoded);
		av_frame_free(&bink->rgba);
		free(bink->pixels);
		free(bink);
	}
	return NULL;
}

/* decodes into pixels the frame the movie is on, reading ahead when needed */
static void bink_decode_to(struct BINK *bink, int frame)
{
	while (bink->decoded_frame < frame)
	{
		int got = 0;

		if (av_read_frame(bink->format, bink->packet) < 0)
			return;
		if (bink->packet->stream_index == bink->video_stream)
		{
			if (avcodec_send_packet(bink->video, bink->packet) >= 0 &&
				avcodec_receive_frame(bink->video, bink->decoded) >= 0)
			{
				sws_scale(bink->to_rgba, (const uint8_t *const *)bink->decoded->data,
					bink->decoded->linesize, 0, (int)bink->Height,
					bink->rgba->data, bink->rgba->linesize);
				memcpy(bink->pixels, bink->rgba->data[0], (size_t)bink->pixels_pitch * bink->Height);
				bink->decoded_frame++;
				got = 1;
			}
		}
		else if (bink->packet->stream_index == bink->audio_stream && bink->sound_system_ready)
		{
			if (avcodec_send_packet(bink->audio, bink->packet) >= 0 &&
				avcodec_receive_frame(bink->audio, bink->audio_frame) >= 0)
			{
				int wanted = bink->audio_frame->nb_samples * 2;

				if (wanted > bink->audio_out_size)
				{
					uint8_t *grown = realloc(bink->audio_out, (size_t)wanted);

					if (grown)
					{
						bink->audio_out = grown;
						bink->audio_out_size = wanted;
					}
				}
				if (bink->audio_out && bink->to_sdl)
				{
					uint8_t *out[1] = { bink->audio_out };
					int got_audio = swr_convert(bink->to_sdl, out, wanted,
						(const uint8_t **)bink->audio_frame->extended_data,
						bink->audio_frame->nb_samples);

					if (got_audio > 0)
						SDL_PutAudioStreamData(bink->stream, bink->audio_out, got_audio * 2 * sizeof(float));
				}
			}
		}
		av_packet_unref(bink->packet);
		(void)got;
	}
}

long __stdcall BinkDoFrame(HBINK bink)
{
	if (!bink)
		return 1;
	bink_decode_to(bink, (int)bink->FrameNum);
	bink->submitted = (int)bink->FrameNum;
	return 0;
}

long __stdcall BinkWait(HBINK bink)
{
	uint64_t due;
	uint64_t now;

	if (!bink)
		return 0;
	due = bink->start_ticks + (uint64_t)(bink->FrameNum * bink->frame_seconds * 1000.0);
	now = SDL_GetTicks();
	if (now >= due)
		return 0;
	/* (the game polls this; a short nap keeps its frame loop from spinning) */
	SDL_Delay((uint32_t)(due - now > 8 ? 8 : due - now));
	return (long)(due - now);
}

void __stdcall BinkNextFrame(HBINK bink)
{
	if (!bink)
		return;
	bink->LastFrameNum = bink->FrameNum;
	bink->FrameNum++;
}

long __stdcall BinkCopyToBuffer(HBINK bink, void *destination, long destination_pitch,
	unsigned long destination_height, unsigned long destination_x, unsigned long destination_y,
	unsigned long flags)
{
	unsigned long row;
	const unsigned char *source;
	unsigned char *target;
	unsigned long copy_width;

	(void)flags;
	if (!bink || !destination || !bink->pixels)
		return 1;
	if (bink->decoded_frame < (int)bink->submitted)
		bink_decode_to(bink, bink->submitted);
	copy_width = bink->Width;
	if (destination_x + copy_width > (unsigned long)(destination_pitch / 4))
		copy_width = (unsigned long)(destination_pitch / 4) - destination_x;
	if (destination_y >= destination_height)
		return 0;
	if (destination_y + bink->Height > destination_height)
		row = destination_height - destination_y;
	else
		row = bink->Height;
	source = bink->pixels;
	target = (unsigned char *)destination + destination_y * destination_pitch + destination_x * 4;
	for (unsigned long y = 0; y < row; y++)
	{
		memcpy(target, source, copy_width * 4);
		target += destination_pitch;
		source += bink->pixels_pitch;
	}
	return 0;
}

void __stdcall BinkGetSummary(HBINK bink, void *summary)
{
	unsigned long *fields = summary;

	if (!fields)
		return;
	/* (Width, Height, TotalTime and a few more come first; the game's debug
	log reads the frame numbers) */
	fields[0] = bink ? bink->Width : 0;
	fields[1] = bink ? bink->Height : 0;
	fields[8] = bink ? bink->FrameNum : 0;
	fields[9] = bink ? bink->Frames : 0;
}

void __stdcall BinkGetRealtime(HBINK bink, void *realtime, unsigned long frame_count)
{
	unsigned long *fields = realtime;

	(void)frame_count;
	if (!fields)
		return;
	fields[0] = bink ? bink->FrameNum : 0;
	fields[1] = 1;
	fields[3] = bink ? bink->Frames : 0;
}

void __stdcall BinkClose(HBINK bink)
{
	if (!bink)
		return;
	if (bink->stream)
		SDL_DestroyAudioStream(bink->stream);
	if (bink->device)
		SDL_CloseAudioDevice(bink->device);
	swr_free(&bink->to_sdl);
	sws_freeContext(bink->to_rgba);
	av_frame_free(&bink->rgba);
	av_frame_free(&bink->decoded);
	av_packet_free(&bink->packet);
	avcodec_free_context(&bink->video);
	avcodec_free_context(&bink->audio);
	if (bink->format)
		avformat_close_input(&bink->format);
	free(bink->audio_out);
	free(bink->pixels);
	free(bink);
}


#endif /* HALO_BINK_FFMPEG */
