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
texture it has locked. The sound goes back the other way: the game hands Bink
its own DirectSound (BinkSetSoundSystem, BinkOpenDirectSound) and the movie's
decoded sound is queued into a stream of it, as the game's own sounds are.

A movie that cannot be played is skipped as the Xbox skips a missing one:
BinkOpen answers NULL with one line in the log, and bink_playback.c goes on
to what follows (the main menu after the intro, the menu's countdown after
an attract movie). One that stops decoding partway (a truncated file) ends
there instead of holding its last frame until its header's length runs out.
This is the one movie implementation of the desktop and Android builds (the
consoles keep bink_null.c's stubs for now).

Only Bink is opened: the file goes to FFmpeg's bink demuxer by name (no
probing for another format), through its file protocol only, and its streams
must be Bink video and Bink audio; anything else is skipped as a damaged
movie is. The FFmpeg the builds make has nothing else in it either
(tools/ffmpeg_build.py, tools/android_ffmpeg.py).
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

#include <stdio.h>
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
	SwrContext *to_sound;
	LPDIRECTSOUNDSTREAM stream;  /* one of the game's DirectSound streams */
	uint8_t *audio_out;
	int audio_out_size;
	int sound_system_ready;
	int ended;                  /* the decoder has nothing more to give */
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

/* BINK OPEN DIRECT SOUND

The game hands Bink its DirectSound (the port's own software mixer,
dsound_sdl.c, the one its own sounds play through) before opening a movie:
the movie's decoded sound is queued into a stream of it, so it is mixed
with the game's sounds and at the game's volume (port_config's
audio.volume). A game with no DirectSound plays the movie without sound,
as it did before there was a decoder.

The game passes the object as an unsigned long, which a 64-bit build cuts to
32 bits; the object itself is the game's own (dsound_get), so it is taken
from there, and the parameter only says whether there is one. */
LPDIRECTSOUND dsound_get(void);

static LPDIRECTSOUND bink_dsound;

void *__stdcall BinkOpenDirectSound(unsigned long param)
{
	bink_dsound = param ? dsound_get() : NULL;
	return bink_dsound ? (void *)bink_dsound : NULL;
}

long __stdcall BinkSetSoundSystem(bink_sound_system_open_proc open, unsigned long param)
{
	/* (the game's check: true when the sound system was taken) */
	return (open && open(param)) ? 1 : 0;
}

/* the movie's sound, as a stream of the game's DirectSound: 16-bit PCM,
stereo, at the movie's own rate, the format the port's mixer takes for a
voice (it resamples it as it does the game's sounds) */
static int bink_audio_open(struct BINK *bink)
{
	DSSTREAMDESC description;
	tWAVEFORMATEX format;

	if (!bink_dsound || !bink->audio || bink->audio->sample_rate <= 0)
	{
		platform_log("Bink video: no DirectSound from the game; playing the movie without sound");
		return 0;
	}
	memset(&format, 0, sizeof(format));
	format.wFormatTag = WAVE_FORMAT_PCM;
	format.nChannels = 2;
	format.nSamplesPerSec = (unsigned long)bink->audio->sample_rate;
	format.wBitsPerSample = 16;
	format.nBlockAlign = (unsigned short)(format.nChannels * format.wBitsPerSample / 8);
	format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;
	memset(&description, 0, sizeof(description));
	/* (a few packets ahead is enough: the game retires one per frame) */
	description.dwMaxAttachedPackets = 8;
	description.lpwfxFormat = &format;
	if (IDirectSound_CreateSoundStream(bink_dsound, &description, &bink->stream, NULL) < 0 ||
		!bink->stream)
	{
		platform_log("Bink video: the game's DirectSound refused the movie's stream; playing it without sound");
		bink->stream = NULL;
		return 0;
	}
	return 1;
}

/* The frame the audio decoder just produced, queued into the game's stream
as the game queues its own sounds: from here the mixer owns the packet (it
resamples it, mixes it with the game's sounds and retires it once played,
from DirectSoundDoWork, which the game calls every frame). */
static void bink_audio_queue(struct BINK *bink)
{
	unsigned long status;
	XMEDIAPACKET packet;
	uint8_t *out[1];
	int samples = bink->audio_frame->nb_samples * 2;  /* (the stream is stereo) */
	int got;

	if (samples <= 0 || !bink->to_sound || !bink->stream)
		return;
	if (samples * (int)sizeof(short) > bink->audio_out_size)
	{
		uint8_t *grown = realloc(bink->audio_out, (size_t)samples * sizeof(short));

		if (!grown)
			return;
		bink->audio_out = grown;
		bink->audio_out_size = samples * (int)sizeof(short);
	}
	out[0] = bink->audio_out;
	got = swr_convert(bink->to_sound, out, samples,
		(const uint8_t **)bink->audio_frame->extended_data, bink->audio_frame->nb_samples);
	if (got <= 0)
		return;
	if (IDirectSoundStream_GetStatus(bink->stream, &status) < 0 ||
		!(status & XMO_STATUSF_ACCEPT_INPUT_DATA))
	{
		/* (the game has not played out a queued packet yet: its frame clock
		is ahead of the movie's sound, which an underrun here would sound
		like; the frame is dropped rather than piled up late) */
		return;
	}
	memset(&packet, 0, sizeof(packet));
	packet.pvBuffer = bink->audio_out;
	packet.dwMaxSize = (unsigned long)(got * 2 * (int)sizeof(short));
	if (IDirectSoundStream_Process(bink->stream, &packet, NULL) < 0)
		platform_log("Bink video: the game's DirectSound refused an audio packet");
}


/* the movie's file: the game's own path ("d:\bink\intro_es.bik") through the
port's file layer, which knows where the game's data is (the disc's folder on
Linux and macOS, the app's assets on Android); a movie that is not there is
also looked for in assets/bink/, where a checkout keeps the disc's */
static int bink_movie_path(const char *name, char *path, size_t path_size)
{
	const char *base;
	FILE *file;

	if (name[0] && name[1] == ':')
		platform_translate_path(name, path, (unsigned long)path_size);
	else
		snprintf(path, path_size, "%s", name);
	file = fopen(path, "rb");
	if (file)
	{
		fclose(file);
		return 1;
	}
	base = strrchr(name, '\\');
	if (!base)
		base = strrchr(name, '/');
	base = base ? base + 1 : name;
	snprintf(path, path_size, "assets/bink/%s", base);
	file = fopen(path, "rb");
	if (file)
	{
		fclose(file);
		return 1;
	}
	return 0;
}

/* the movie, skipped: one line, and the game goes on as with a missing file */
static HBINK bink_skip(struct BINK *bink, const char *name, const char *reason)
{
	platform_log("Bink video: skipping \"%s\" (%s)", name, reason);
	if (bink)
	{
		swr_free(&bink->to_sound);
		sws_freeContext(bink->to_rgba);
		if (bink->format)
			avformat_close_input(&bink->format);
		avcodec_free_context(&bink->video);
		avcodec_free_context(&bink->audio);
		av_packet_free(&bink->packet);
		av_frame_free(&bink->decoded);
		av_frame_free(&bink->rgba);
		av_frame_free(&bink->audio_frame);
		free(bink->pixels);
		free(bink);
	}
	return NULL;
}

HBINK __stdcall BinkOpen(const char *name, unsigned long flags)
{
	struct BINK *bink;
	const AVCodec *codec;
	AVRational rate;
	double fps;
	char path[1024];

	(void)flags;
	/* (no movie at all, in any language: attract_mode.c has said so already) */
	if (!name || !name[0])
		return NULL;
	/* (FFmpeg's own messages about a damaged file would be one per packet in
	the console; the one line below is the movie's) */
	av_log_set_level(AV_LOG_QUIET);
	if (!bink_movie_path(name, path, sizeof(path)))
		return bink_skip(NULL, name, "not found");
	bink = calloc(1, sizeof(*bink));
	if (!bink)
		return NULL;
	bink->video_stream = -1;
	bink->audio_stream = -1;
	bink->decoded_frame = -1;
	bink->submitted = -1;
	/* (the bink demuxer forced, the file protocol and that demuxer the only
	ones FFmpeg may use for it) */
	{
		const AVInputFormat *bink_format = av_find_input_format("bink");
		AVDictionary *options = NULL;
		int opened;

		if (!bink_format)
			return bink_skip(bink, name, "no Bink demuxer in this build");
		bink->format = avformat_alloc_context();
		if (!bink->format)
			return bink_skip(bink, name, "out of memory");
		av_dict_set(&options, "protocol_whitelist", "file", 0);
		av_dict_set(&options, "format_whitelist", "bink", 0);
		av_dict_set(&options, "codec_whitelist", "binkvideo,binkaudio_rdft,binkaudio_dct", 0);
		opened = avformat_open_input(&bink->format, path, bink_format, &options);
		av_dict_free(&options);
		/* (a failed open frees the context and clears the pointer) */
		if (opened < 0)
			return bink_skip(bink, name, "not a Bink movie");
	}
	if (!bink->format->iformat || strcmp(bink->format->iformat->name, "bink") != 0)
		return bink_skip(bink, name, "not a Bink movie");
	if (avformat_find_stream_info(bink->format, NULL) < 0)
		return bink_skip(bink, name, "no stream information");
	bink->video_stream = av_find_best_stream(bink->format, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
	if (bink->video_stream < 0)
		return bink_skip(bink, name, "no video");
	if (bink->format->streams[bink->video_stream]->codecpar->codec_id != AV_CODEC_ID_BINKVIDEO)
		return bink_skip(bink, name, "its video is not Bink video");
	codec = avcodec_find_decoder(bink->format->streams[bink->video_stream]->codecpar->codec_id);
	if (!codec)
		return bink_skip(bink, name, "no decoder for its video");
	bink->video = avcodec_alloc_context3(codec);
	if (!bink->video ||
		avcodec_parameters_to_context(bink->video, bink->format->streams[bink->video_stream]->codecpar) < 0 ||
		avcodec_open2(bink->video, codec, NULL) < 0 ||
		bink->video->width <= 0 || bink->video->height <= 0)
	{
		return bink_skip(bink, name, "its video cannot be decoded");
	}

	bink->Width = (unsigned long)bink->video->width;
	bink->Height = (unsigned long)bink->video->height;
	rate = av_guess_frame_rate(bink->format, bink->format->streams[bink->video_stream], NULL);
	fps = rate.den ? rate.num / (double)rate.den : 0.0;
	bink->frame_seconds = fps > 0.0 ? 1.0 / fps : 1.0 / 15.0;
	/* (the Bink header's own count, which the demuxer reads) */
	bink->Frames = bink->format->streams[bink->video_stream]->nb_frames > 0 ?
		(unsigned long)bink->format->streams[bink->video_stream]->nb_frames :
		(unsigned long)(bink->format->duration / (AV_TIME_BASE * bink->frame_seconds) + 0.5);
	if (bink->Frames < 1)
		return bink_skip(bink, name, "no frames");

	bink->pixels_pitch = (long)bink->Width * 4;
	bink->pixels = malloc((size_t)bink->pixels_pitch * bink->Height);
	if (!bink->pixels)
		return bink_skip(bink, name, "out of memory");
	memset(bink->pixels, 0, (size_t)bink->pixels_pitch * bink->Height);
	bink->packet = av_packet_alloc();
	bink->decoded = av_frame_alloc();
	bink->rgba = av_frame_alloc();
	bink->audio_frame = av_frame_alloc();
	if (!bink->packet || !bink->decoded || !bink->rgba || !bink->audio_frame)
		return bink_skip(bink, name, "out of memory");
	bink->rgba->format = AV_PIX_FMT_BGRA;
	bink->rgba->width = (int)bink->Width;
	bink->rgba->height = (int)bink->Height;
	if (av_frame_get_buffer(bink->rgba, 0) < 0)
		return bink_skip(bink, name, "out of memory");
	bink->to_rgba = sws_getContext((int)bink->Width, (int)bink->Height, bink->video->pix_fmt,
		(int)bink->Width, (int)bink->Height, AV_PIX_FMT_BGRA, SWS_BILINEAR, NULL, NULL, NULL);
	if (!bink->to_rgba)
		return bink_skip(bink, name, "no conversion for its pixels");

	/* the movie's own sound, if the disc's file carries one */
	bink->audio_stream = av_find_best_stream(bink->format, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
	if (bink->audio_stream >= 0 &&
		bink->format->streams[bink->audio_stream]->codecpar->codec_id != AV_CODEC_ID_BINKAUDIO_RDFT &&
		bink->format->streams[bink->audio_stream]->codecpar->codec_id != AV_CODEC_ID_BINKAUDIO_DCT)
	{
		/* (a sound that is not Bink's: the movie plays silent) */
		bink->audio_stream = -1;
	}
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
				if (swr_alloc_set_opts2(&bink->to_sound, &stereo, AV_SAMPLE_FMT_S16,
						bink->audio->sample_rate, &in_layout, bink->audio->sample_fmt,
						bink->audio->sample_rate, 0, NULL) >= 0 && swr_init(bink->to_sound) >= 0)
				{
					bink->sound_system_ready = bink_audio_open(bink) != 0;
				}
			}
		}
	}
	platform_log("Bink video: playing \"%s\" (%ux%u, %u frames)", name,
		(unsigned int)bink->Width, (unsigned int)bink->Height, (unsigned int)bink->Frames);
	bink->start_ticks = SDL_GetTicks();
	return bink;
}

/* The audio packet into the game's stream. The decoder holds a frame back,
so it is drained after every packet (that frame is the movie's sound):
sending first and asking later loses it, because the decoder answers EAGAIN
while it still has one to give, and the packet would be dropped unheard. */
static void bink_audio_decode(struct BINK *bink)
{
	int sent = avcodec_send_packet(bink->audio, bink->packet);

	if (sent < 0 && sent != AVERROR(EAGAIN))
		return;
	while (avcodec_receive_frame(bink->audio, bink->audio_frame) >= 0)
		bink_audio_queue(bink);
	if (sent == AVERROR(EAGAIN) && avcodec_send_packet(bink->audio, bink->packet) >= 0)
		/* (already drained: the packet fits now) */
		while (avcodec_receive_frame(bink->audio, bink->audio_frame) >= 0)
			bink_audio_queue(bink);
}

/* the next video frame, decoded into pixels: the decoder may hold frames the
game has not asked for yet, so it is drained first and fed only when it asks
for more (a frame per packet, as FFmpeg's decoders delay by one) */
static int bink_next_video_frame(struct BINK *bink)
{
	for (;;)
	{
		int received = avcodec_receive_frame(bink->video, bink->decoded);

		if (received == 0)
		{
			sws_scale(bink->to_rgba, (const uint8_t *const *)bink->decoded->data,
				bink->decoded->linesize, 0, (int)bink->Height, bink->rgba->data, bink->rgba->linesize);
			memcpy(bink->pixels, bink->rgba->data[0], (size_t)bink->pixels_pitch * bink->Height);
			bink->decoded_frame++;
			return 1;
		}
		if (received != AVERROR(EAGAIN))
		{
			/* (the end, or a frame it cannot decode: the movie ends here) */
			bink->ended = 1;
			return 0;
		}

		/* (EAGAIN: it wants another packet) */
		for (;;)
		{
			if (av_read_frame(bink->format, bink->packet) < 0)
			{
				/* (the file's end, early when it is truncated: what the decoder
				still holds comes out, and then the movie ends) */
				if (avcodec_send_packet(bink->video, NULL) < 0)
				{
					bink->ended = 1;
					return 0;
				}
				break;
			}
			if (bink->packet->stream_index == bink->video_stream)
			{
				int sent = avcodec_send_packet(bink->video, bink->packet);

				av_packet_unref(bink->packet);
				if (sent < 0 && sent != AVERROR(EAGAIN))
				{
					bink->ended = 1;
					return 0;
				}
				break;
			}
			if (bink->packet->stream_index == bink->audio_stream && bink->sound_system_ready)
				bink_audio_decode(bink);
			av_packet_unref(bink->packet);
		}
	}
}


/* decodes into pixels until the frame the game asked for is the one there */
static void bink_decode_to(struct BINK *bink, int frame)
{
	while (bink->decoded_frame < frame && !bink->ended)
		if (!bink_next_video_frame(bink))
			return;
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
	/* a movie that stopped decoding before its header's length (a truncated
	file) ends now: bink_playback.c stops at FrameNum == Frames-1, and holding
	the last frame for the rest of the length would look like a hang */
	if (bink->ended && bink->FrameNum + 1 < bink->Frames)
		bink->Frames = bink->FrameNum + 1;
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
		/* (Release flushes what it has queued and takes it out of the mixer) */
		IDirectSoundStream_Release(bink->stream);
	swr_free(&bink->to_sound);
	sws_freeContext(bink->to_rgba);
	av_frame_free(&bink->rgba);
	av_frame_free(&bink->decoded);
	av_frame_free(&bink->audio_frame);
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
