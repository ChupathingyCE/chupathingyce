/*
CAPTURE_CLIPS.C

Clips (capture.h): with clips.enabled, the game's picture and sound are
encoded as they are made and the last clips.seconds of them kept in memory,
and controls.clip saves them as an MP4 to <data root>/clips/, an instant
replay. The picture comes from the renderer (d3d8_gl.c scales the back
buffer on the GPU and reads it back behind a fence, the bgfx renderer reads
it back whole) and is lent here until copied; the sound is the mixer's
output (dsound_sdl.c), the HUD and menus in the picture and nothing of the
port's overlay (DELTA, toasts, the game list's screens). (A screenshot is
the window as shown, overlay and all, capture.c; a clip stays the game's
frame: the bgfx renderer can read the window only by waiting for the GPU,
which a frame of every clip cannot afford.)

One thread encodes: the picture with the system's hardware H.264 encoder
where FFmpeg can drive it (VideoToolbox on macOS), else FFmpeg's own MPEG-4
Part 2 at no more than 1280x720 and 30 frames a second; the sound as AAC.
What it encodes goes to a list of packets, cut a picture group at a time
from the front so that the oldest kept starts on a key frame. Saving copies
the list and writes the file on a thread of its own, so the encoder never
waits on the disk and the game never waits on either.

Off (the default), nothing here runs: no thread and no buffers, and the
renderer's and the mixer's calls return at their first test.

Built with the movies' FFmpeg (HALO_BINK_FFMPEG, tools/ffmpeg_build.py);
without it clips stay off and controls.clip says so.
*/

#include "platform.h"
#include "port_config.h"
#include "port_folders.h"
#include "capture.h"

#ifdef HALO_CAPTURE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef HALO_BINK_FFMPEG

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
#include <SDL3/SDL.h>

/* the frames lent and waiting for the encoder (the renderer has at most as
many in flight) */
#define CLIP_FRAMES 4
/* the mixer's sound waiting for the encoder: a second, stereo */
#define CLIP_AUDIO_RATE 48000
#define CLIP_AUDIO_RING (CLIP_AUDIO_RATE * 2)
/* a key frame every two seconds: the kept clip starts up to that much
earlier than clips.seconds */
#define CLIP_KEY_SECONDS 2

struct clip_frame
{
	const unsigned char *pixels;
	long width, height, pitch;
	int bgra;
	double time;
	volatile int *released;
};

/* a packet kept, its stream's (0 the picture, 1 the sound) */
struct clip_packet
{
	AVPacket *packet;
	int stream;
	struct clip_packet *next;
};

/* what saving takes: the packets, copied, and the streams' parameters */
struct clip_save
{
	struct clip_packet *first;
	AVCodecParameters *video, *audio;
	AVRational video_base, audio_base;
	char name[160];
};

static struct
{
	/* 0 not looked at yet, 1 off, 2 on, 3 failed to start, 4 starting */
	volatile int state;
	pthread_mutex_t lock;
	pthread_cond_t wake;

	/* the renderer's side */
	double next_time;
	double frame_seconds;
	long max_width, max_height;
	struct clip_frame frames[CLIP_FRAMES];
	int frame_count;

	/* the mixer's side: stereo frames, written by the audio thread alone
	and read by the encoder alone */
	float audio[CLIP_AUDIO_RING * 2];
	volatile unsigned long audio_put, audio_taken;
	/* when the mixer last put sound, and how much it had put then: the
	sound's place on the picture's clock */
	volatile double audio_time;
	volatile unsigned long audio_time_put;

	volatile int save_wanted;
	char save_name[160];

	/* the encoder's */
	double start;
	AVCodecContext *video, *audio_codec;
	AVFrame *picture, *sound;
	struct SwsContext *scale;
	int scale_width, scale_height, scale_bgra;
	int64_t last_video_pts;
	int64_t audio_pts;
	int audio_fill;
	int audio_started;
	struct clip_packet *first, *last;
	unsigned long bytes, most_bytes;
	unsigned long seconds;
	int fps;
} clips = { 0, PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER };

static double now_seconds(void)
{
	return (double)SDL_GetTicksNS() / 1e9;
}

/* ---------- the encoders */

static AVCodecContext *video_open(const char *name, long width, long height, int fps, enum AVPixelFormat format,
	long bit_rate)
{
	const AVCodec *codec = avcodec_find_encoder_by_name(name);
	AVCodecContext *context;

	if (!codec)
		return NULL;
	context = avcodec_alloc_context3(codec);
	if (!context)
		return NULL;
	context->width = (int)width;
	context->height = (int)height;
	context->pix_fmt = format;
	context->time_base = (AVRational){ 1, fps };
	context->framerate = (AVRational){ fps, 1 };
	context->gop_size = fps * CLIP_KEY_SECONDS;
	context->max_b_frames = 0;
	context->bit_rate = bit_rate;
	context->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
	context->colorspace = AVCOL_SPC_BT709;
	context->color_primaries = AVCOL_PRI_BT709;
	context->color_trc = AVCOL_TRC_BT709;
	context->color_range = AVCOL_RANGE_MPEG;
	if (!strcmp(name, "h264_videotoolbox"))
	{
		av_opt_set_int(context->priv_data, "realtime", 1, 0);
		av_opt_set_int(context->priv_data, "allow_sw", 0, 0);
		context->profile = AV_PROFILE_H264_HIGH;
	}
	else
	{
		/* (MPEG-4 Part 2: its quality kept up within the rate) */
		context->qmin = 2;
		context->qmax = 12;
	}
	if (avcodec_open2(context, codec, NULL) < 0)
	{
		avcodec_free_context(&context);
		return NULL;
	}
	return context;
}

static int encoders_open(void)
{
	const AVCodec *aac;

	/* the hardware's H.264 at up to 1920x1080 and 60 a second, else MPEG-4
	Part 2 at up to 1280x720 and 30 */
	clips.max_width = 1920;
	clips.max_height = 1080;
	clips.fps = 60;
	clips.video = NULL;
#ifdef __APPLE__
	if (!config_boolean("debug.clips_software"))
		clips.video = video_open("h264_videotoolbox", 1920, 1080, 60, AV_PIX_FMT_NV12, 12000000);
#endif
	if (clips.video)
	{
		/* (opened at the most it will be asked for; the real size is the
		first frame's, below) */
		avcodec_free_context(&clips.video);
		clips.video = NULL;
		platform_log("clips: H.264 (VideoToolbox), up to 1920x1080 at 60 a second");
	}
	else
	{
		clips.max_width = 1280;
		clips.max_height = 720;
		clips.fps = 30;
		platform_log("clips: MPEG-4 Part 2 (software), up to 1280x720 at 30 a second");
	}
	clips.frame_seconds = 1.0 / clips.fps;

	aac = avcodec_find_encoder_by_name("aac");
	if (!aac)
		return 0;
	clips.audio_codec = avcodec_alloc_context3(aac);
	if (!clips.audio_codec)
		return 0;
	clips.audio_codec->sample_fmt = AV_SAMPLE_FMT_FLTP;
	clips.audio_codec->sample_rate = CLIP_AUDIO_RATE;
	av_channel_layout_default(&clips.audio_codec->ch_layout, 2);
	clips.audio_codec->bit_rate = 160000;
	clips.audio_codec->time_base = (AVRational){ 1, CLIP_AUDIO_RATE };
	clips.audio_codec->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
	if (avcodec_open2(clips.audio_codec, aac, NULL) < 0)
	{
		avcodec_free_context(&clips.audio_codec);
		return 0;
	}
	clips.sound = av_frame_alloc();
	if (!clips.sound)
		return 0;
	clips.sound->format = AV_SAMPLE_FMT_FLTP;
	clips.sound->nb_samples = clips.audio_codec->frame_size ? clips.audio_codec->frame_size : 1024;
	clips.sound->sample_rate = CLIP_AUDIO_RATE;
	av_channel_layout_default(&clips.sound->ch_layout, 2);
	return av_frame_get_buffer(clips.sound, 0) >= 0;
}

/* the picture's encoder, for the first frame's size (a frame of another size
later, a resized window, is scaled to it) */
static int video_ready(long width, long height)
{
	if (clips.video)
		return 1;
	width &= ~1L;
	height &= ~1L;
	if (width < 16 || height < 16)
		return 0;
	if (clips.fps == 60)
		clips.video = video_open("h264_videotoolbox", width, height, 60, AV_PIX_FMT_NV12,
			width * height >= 1920L * 1080L ? 12000000 : 8000000);
	if (!clips.video)
	{
		if (width > 1280 || height > 720)
		{
			double scale = width * 720 > height * 1280 ? 1280.0 / width : 720.0 / height;

			width = (long)(width * scale) & ~1L;
			height = (long)(height * scale) & ~1L;
		}
		clips.fps = 30;
		clips.frame_seconds = 1.0 / 30;
		clips.video = video_open("mpeg4", width, height, 30, AV_PIX_FMT_YUV420P, 6000000);
	}
	if (!clips.video)
		return 0;
	clips.picture = av_frame_alloc();
	if (!clips.picture)
		return 0;
	clips.picture->format = clips.video->pix_fmt;
	clips.picture->width = clips.video->width;
	clips.picture->height = clips.video->height;
	if (av_frame_get_buffer(clips.picture, 0) < 0)
		return 0;
	platform_log("clips: %s %dx%d at %d a second, %lu seconds kept", clips.video->codec->name, clips.video->width,
		clips.video->height, clips.fps, clips.seconds);
	return 1;
}

/* ---------- the packets kept */

static void packet_list_free(struct clip_packet *packet)
{
	while (packet)
	{
		struct clip_packet *next = packet->next;

		av_packet_free(&packet->packet);
		free(packet);
		packet = next;
	}
}

static double packet_seconds(const struct clip_packet *packet)
{
	AVRational base = packet->stream ? clips.audio_codec->time_base : clips.video->time_base;

	return (double)packet->packet->pts * base.num / base.den;
}

/* the oldest picture group goes once the next one starts at or before the
cut: the clip kept is clips.seconds long, or up to a group longer */
static void packets_trim(double newest)
{
	double cut = newest - (double)clips.seconds;

	for (;;)
	{
		struct clip_packet *packet, *next_key = NULL;

		for (packet = clips.first ? clips.first->next : NULL; packet; packet = packet->next)
		{
			if (!packet->stream && (packet->packet->flags & AV_PKT_FLAG_KEY))
			{
				next_key = packet;
				break;
			}
		}
		if (!next_key || packet_seconds(next_key) > cut)
			break;
		while (clips.first != next_key)
		{
			struct clip_packet *gone = clips.first;

			clips.first = gone->next;
			clips.bytes -= (unsigned long)gone->packet->size;
			gone->next = NULL;
			packet_list_free(gone);
		}
	}
	/* (sound from before the first picture is no use) */
	while (clips.first && clips.first->stream)
	{
		struct clip_packet *gone = clips.first;

		clips.first = gone->next;
		clips.bytes -= (unsigned long)gone->packet->size;
		gone->next = NULL;
		packet_list_free(gone);
	}
	if (!clips.first)
		clips.last = NULL;
}

static void packets_take(AVCodecContext *codec, int stream)
{
	AVPacket *packet = av_packet_alloc();

	while (packet && avcodec_receive_packet(codec, packet) == 0)
	{
		struct clip_packet *kept = malloc(sizeof(*kept));

		if (!kept)
		{
			av_packet_unref(packet);
			continue;
		}
		kept->packet = packet;
		kept->stream = stream;
		kept->next = NULL;
		/* (the list starts on a picture's key frame) */
		if (!clips.first && (stream || !(packet->flags & AV_PKT_FLAG_KEY)))
		{
			av_packet_free(&packet);
			free(kept);
			packet = av_packet_alloc();
			continue;
		}
		if (clips.last)
			clips.last->next = kept;
		else
			clips.first = kept;
		clips.last = kept;
		clips.bytes += (unsigned long)packet->size;
		if (clips.bytes > clips.most_bytes)
			clips.most_bytes = clips.bytes;
		if (!stream)
			packets_trim(packet_seconds(kept));
		packet = av_packet_alloc();
	}
	av_packet_free(&packet);
}

/* ---------- saving */

static int write_packets(AVFormatContext *output, struct clip_save *save)
{
	struct clip_packet *packet;
	int64_t video_start = AV_NOPTS_VALUE;
	double start_seconds = 0.0;

	for (packet = save->first; packet; packet = packet->next)
	{
		if (!packet->stream)
		{
			video_start = packet->packet->pts;
			start_seconds = (double)video_start * save->video_base.num / save->video_base.den;
			break;
		}
	}
	if (video_start == AV_NOPTS_VALUE)
		return 0;
	for (packet = save->first; packet; packet = packet->next)
	{
		AVPacket *out = packet->packet;
		AVRational base = packet->stream ? save->audio_base : save->video_base;
		int64_t start = packet->stream ? (int64_t)(start_seconds * base.den / base.num + 0.5) : video_start;

		/* (sound from before the first picture is left out) */
		if (out->pts < start)
			continue;
		out->pts -= start;
		out->dts = out->dts == AV_NOPTS_VALUE ? out->pts : out->dts - start;
		if (out->dts < 0)
			out->dts = 0;
		out->stream_index = packet->stream;
		av_packet_rescale_ts(out, base, output->streams[packet->stream]->time_base);
		if (av_interleaved_write_frame(output, out) < 0)
			return 0;
	}
	return 1;
}

static void *save_main(void *data)
{
	struct clip_save *save = data;
	const char *folder = platform_folder(_port_folder_clips);
	char path[1024], temporary[1040], file_name[200];
	AVFormatContext *output = NULL;
	int saved = 0, attempt;
	FILE *exists;

	snprintf(file_name, sizeof(file_name), "%s.mp4", save->name);
	snprintf(path, sizeof(path), "%s/%s", folder, file_name);
	for (attempt = 2; (exists = fopen(path, "rb")) != NULL && attempt < 100; attempt++)
	{
		fclose(exists);
		snprintf(file_name, sizeof(file_name), "%s_%d.mp4", save->name, attempt);
		snprintf(path, sizeof(path), "%s/%s", folder, file_name);
	}
	if (exists)
		fclose(exists);
	snprintf(temporary, sizeof(temporary), "%s.part", path);
	if (avformat_alloc_output_context2(&output, NULL, "mp4", temporary) >= 0 && output)
	{
		AVStream *video = avformat_new_stream(output, NULL);
		AVStream *audio = avformat_new_stream(output, NULL);

		if (video && audio && avcodec_parameters_copy(video->codecpar, save->video) >= 0 &&
			avcodec_parameters_copy(audio->codecpar, save->audio) >= 0)
		{
			video->time_base = save->video_base;
			audio->time_base = save->audio_base;
			if (avio_open(&output->pb, temporary, AVIO_FLAG_WRITE) >= 0)
			{
				saved = avformat_write_header(output, NULL) >= 0 && write_packets(output, save) &&
					av_write_trailer(output) >= 0;
				avio_closep(&output->pb);
			}
		}
		avformat_free_context(output);
	}
	if (saved && rename(temporary, path) != 0)
		saved = 0;
	if (!saved)
		remove(temporary);
	if (saved)
	{
		platform_log("clips: saved %s", file_name);
		capture_notice("Clip saved", file_name);
	}
	else
	{
		platform_log("clips: could not write %s", file_name);
		capture_notice("Clip not saved", "Could not write the file");
	}
	packet_list_free(save->first);
	avcodec_parameters_free(&save->video);
	avcodec_parameters_free(&save->audio);
	free(save);
	return NULL;
}

/* the packets so far, copied (references to the same data: nothing is
copied but their headers), to a thread that writes them */
static void save_start(const char *name)
{
	struct clip_save *save = calloc(1, sizeof(*save));
	struct clip_packet *packet, **tail;
	pthread_t thread;

	if (!save || !clips.video || !clips.first)
	{
		free(save);
		capture_notice("Clip not saved", "Nothing recorded yet");
		return;
	}
	snprintf(save->name, sizeof(save->name), "%s", name);
	save->video = avcodec_parameters_alloc();
	save->audio = avcodec_parameters_alloc();
	save->video_base = clips.video->time_base;
	save->audio_base = clips.audio_codec->time_base;
	tail = &save->first;
	if (save->video && save->audio && avcodec_parameters_from_context(save->video, clips.video) >= 0 &&
		avcodec_parameters_from_context(save->audio, clips.audio_codec) >= 0)
	{
		for (packet = clips.first; packet; packet = packet->next)
		{
			struct clip_packet *copy = malloc(sizeof(*copy));

			if (!copy || !(copy->packet = av_packet_clone(packet->packet)))
			{
				free(copy);
				break;
			}
			copy->stream = packet->stream;
			copy->next = NULL;
			*tail = copy;
			tail = &copy->next;
		}
		if (save->first && pthread_create(&thread, NULL, save_main, save) == 0)
		{
			pthread_detach(thread);
			return;
		}
	}
	packet_list_free(save->first);
	avcodec_parameters_free(&save->video);
	avcodec_parameters_free(&save->audio);
	free(save);
	capture_notice("Clip not saved", "Out of memory");
}

/* ---------- the encoder's thread */

/* BGRA or RGBA to the encoder's NV12 or YUV 4:2:0, BT.709 at video range,
for a frame already at the encoder's size (the OpenGL renderer scales on
the GPU): a 2x2 block at a time, each pixel's luma and the block's averaged
chroma. Several times faster than swscale's portable C, which this FFmpeg
has (no assembly) */
static void convert_picture(const struct clip_frame *frame, AVFrame *picture)
{
	int nv12 = picture->format == AV_PIX_FMT_NV12;
	int red = frame->bgra ? 2 : 0, blue = frame->bgra ? 0 : 2;
	long x, y;

	for (y = 0; y + 1 < frame->height && y + 1 < picture->height; y += 2)
	{
		const unsigned char *top = frame->pixels + y * frame->pitch;
		const unsigned char *bottom = top + frame->pitch;
		uint8_t *luma_top = picture->data[0] + y * picture->linesize[0];
		uint8_t *luma_bottom = luma_top + picture->linesize[0];
		uint8_t *chroma_u = picture->data[1] + (y / 2) * picture->linesize[1];
		uint8_t *chroma_v = nv12 ? NULL : picture->data[2] + (y / 2) * picture->linesize[2];

		for (x = 0; x + 1 < frame->width && x + 1 < picture->width; x += 2)
		{
			const unsigned char *p0 = top + x * 4, *p1 = p0 + 4, *p2 = bottom + x * 4, *p3 = p2 + 4;
			int r = p0[red] + p1[red] + p2[red] + p3[red];
			int g = p0[1] + p1[1] + p2[1] + p3[1];
			int b = p0[blue] + p1[blue] + p2[blue] + p3[blue];
			int u = ((-26 * r - 87 * g + 112 * b + 512) >> 10) + 128;
			int v = ((112 * r - 102 * g - 10 * b + 512) >> 10) + 128;

			luma_top[x] = (uint8_t)(((47 * p0[red] + 157 * p0[1] + 16 * p0[blue] + 128) >> 8) + 16);
			luma_top[x + 1] = (uint8_t)(((47 * p1[red] + 157 * p1[1] + 16 * p1[blue] + 128) >> 8) + 16);
			luma_bottom[x] = (uint8_t)(((47 * p2[red] + 157 * p2[1] + 16 * p2[blue] + 128) >> 8) + 16);
			luma_bottom[x + 1] = (uint8_t)(((47 * p3[red] + 157 * p3[1] + 16 * p3[blue] + 128) >> 8) + 16);
			if (nv12)
			{
				chroma_u[x] = (uint8_t)u;
				chroma_u[x + 1] = (uint8_t)v;
			}
			else
			{
				chroma_u[x / 2] = (uint8_t)u;
				chroma_v[x / 2] = (uint8_t)v;
			}
		}
	}
}

static void encode_picture(struct clip_frame *frame)
{
	int64_t pts;

	if (!video_ready(frame->width, frame->height))
	{
		__atomic_store_n(frame->released, 1, __ATOMIC_RELEASE);
		return;
	}
	pts = (int64_t)((frame->time - clips.start) * clips.fps + 0.5);
	if (pts <= clips.last_video_pts)
	{
		/* (two in one frame's time: the second is not wanted) */
		__atomic_store_n(frame->released, 1, __ATOMIC_RELEASE);
		return;
	}
	if (frame->width == clips.video->width && frame->height == clips.video->height)
	{
		if (av_frame_make_writable(clips.picture) < 0)
		{
			__atomic_store_n(frame->released, 1, __ATOMIC_RELEASE);
			return;
		}
		convert_picture(frame, clips.picture);
	}
	else
	{
	/* (another size, the bgfx renderer's whole back buffer: swscale scales
	it) */
	if (!clips.scale || clips.scale_width != frame->width || clips.scale_height != frame->height ||
		clips.scale_bgra != frame->bgra)
	{
		sws_freeContext(clips.scale);
		clips.scale = sws_getContext((int)frame->width, (int)frame->height,
			frame->bgra ? AV_PIX_FMT_BGRA : AV_PIX_FMT_RGBA, clips.video->width, clips.video->height,
			clips.video->pix_fmt, SWS_BILINEAR, NULL, NULL, NULL);
		clips.scale_width = (int)frame->width;
		clips.scale_height = (int)frame->height;
		clips.scale_bgra = frame->bgra;
	}
	if (!clips.scale || av_frame_make_writable(clips.picture) < 0)
	{
		__atomic_store_n(frame->released, 1, __ATOMIC_RELEASE);
		return;
	}
	{
		const uint8_t *source[1] = { frame->pixels };
		int stride[1] = { (int)frame->pitch };

		sws_scale(clips.scale, source, stride, 0, (int)frame->height, clips.picture->data, clips.picture->linesize);
	}
	}
	/* (converted: the renderer may have its memory back) */
	__atomic_store_n(frame->released, 1, __ATOMIC_RELEASE);
	clips.picture->pts = pts;
	clips.last_video_pts = pts;
	if (avcodec_send_frame(clips.video, clips.picture) >= 0)
		packets_take(clips.video, 0);
}

/* the sound's place on the clip's clock (in samples) of the sample put as
number `index`: from when the mixer last put sound */
static int64_t sound_pts(unsigned long index)
{
	unsigned long put = __atomic_load_n(&clips.audio_time_put, __ATOMIC_ACQUIRE);
	double time = clips.audio_time;

	return (int64_t)((time - clips.start) * CLIP_AUDIO_RATE + 0.5) - (int64_t)(put - index);
}

static void encode_sound(void)
{
	unsigned long put = __atomic_load_n(&clips.audio_put, __ATOMIC_ACQUIRE);
	int frame_size = clips.sound->nb_samples;

	if (!clips.video)
	{
		/* (no picture yet: the sound so far is dropped) */
		__atomic_store_n(&clips.audio_taken, put, __ATOMIC_RELEASE);
		return;
	}
	while (clips.audio_taken != put)
	{
		unsigned long index = clips.audio_taken % CLIP_AUDIO_RING;
		float *left, *right;

		if (clips.audio_fill == 0)
		{
			/* the frame's time: the sound's own count, kept in step with the
			clock (a device that stopped for a while, or drifted 40 ms, starts
			again where the clock says) */
			int64_t expected = sound_pts(clips.audio_taken);

			if (!clips.audio_started || clips.audio_pts < expected - CLIP_AUDIO_RATE / 25)
				clips.audio_pts = expected;
			else if (clips.audio_pts > expected + CLIP_AUDIO_RATE / 25)
			{
				/* (ahead of the clock: this frame's sound is dropped) */
				__atomic_store_n(&clips.audio_taken, clips.audio_taken +
					((unsigned long)frame_size < put - clips.audio_taken ? (unsigned long)frame_size :
					put - clips.audio_taken), __ATOMIC_RELEASE);
				continue;
			}
			clips.audio_started = 1;
			if (av_frame_make_writable(clips.sound) < 0)
				return;
		}
		left = (float *)clips.sound->data[0];
		right = (float *)clips.sound->data[1];
		left[clips.audio_fill] = clips.audio[index * 2];
		right[clips.audio_fill] = clips.audio[index * 2 + 1];
		__atomic_store_n(&clips.audio_taken, clips.audio_taken + 1, __ATOMIC_RELEASE);
		if (++clips.audio_fill == frame_size)
		{
			clips.sound->pts = clips.audio_pts;
			clips.audio_pts += frame_size;
			clips.audio_fill = 0;
			if (avcodec_send_frame(clips.audio_codec, clips.sound) >= 0)
				packets_take(clips.audio_codec, 1);
		}
	}
}

static void *encoder_main(void *unused)
{
	unsigned long logged = 0;

	(void)unused;
	if (!encoders_open())
	{
		platform_log("clips: no encoder: clips are off");
		__atomic_store_n(&clips.state, 3, __ATOMIC_RELEASE);
		return NULL;
	}
	clips.start = now_seconds();
	clips.next_time = clips.start;
	__atomic_store_n(&clips.state, 2, __ATOMIC_RELEASE);
	pthread_mutex_lock(&clips.lock);
	for (;;)
	{
		struct clip_frame frame;
		int have_frame = 0, save = 0;
		char name[160];

		if (!clips.frame_count && !clips.save_wanted)
		{
			/* (the sound is taken every 20 ms whether or not a frame comes) */
			struct timespec deadline;

			clock_gettime(CLOCK_REALTIME, &deadline);
			deadline.tv_nsec += 20000000L;
			if (deadline.tv_nsec >= 1000000000L)
			{
				deadline.tv_sec++;
				deadline.tv_nsec -= 1000000000L;
			}
			pthread_cond_timedwait(&clips.wake, &clips.lock, &deadline);
		}
		if (clips.frame_count)
		{
			frame = clips.frames[0];
			memmove(clips.frames, clips.frames + 1, sizeof(clips.frames[0]) * (size_t)(clips.frame_count - 1));
			clips.frame_count--;
			have_frame = 1;
		}
		if (clips.save_wanted)
		{
			clips.save_wanted = 0;
			snprintf(name, sizeof(name), "%s", clips.save_name);
			save = 1;
		}
		pthread_mutex_unlock(&clips.lock);
		encode_sound();
		if (have_frame)
			encode_picture(&frame);
		if (save)
			save_start(name);
		/* (the most it has kept, each time it grows by 8 MB) */
		if (clips.most_bytes >> 23 > logged)
		{
			logged = clips.most_bytes >> 23;
			platform_log("clips: up to %lu MB kept", clips.most_bytes >> 20);
		}
		pthread_mutex_lock(&clips.lock);
	}
	return NULL;
}

/* clips.enabled, looked at once: the encoder's thread started if on, which
opens the encoders (a hardware encoder can take a while to open, which the
game's frame must not wait for) and then says it is on */
static int clips_on(void)
{
	pthread_t thread;
	long seconds;

	if (clips.state)
		return clips.state == 2;
	if (!config_boolean("clips.enabled"))
	{
		clips.state = 1;
		return 0;
	}
	seconds = config_integer("clips.seconds");
	clips.seconds = (unsigned long)(seconds < 5 ? 5 : seconds > 120 ? 120 : seconds);
	clips.last_video_pts = -1;
	clips.state = 4;
	if (pthread_create(&thread, NULL, encoder_main, NULL) != 0)
	{
		platform_log("clips: could not start the encoder's thread");
		clips.state = 3;
		return 0;
	}
	pthread_detach(thread);
	return 0;
}

/* ---------- public code */

int capture_clip_frame_due(long source_width, long source_height, long *width, long *height, double *time)
{
	double now;
	int room;

	if (clips.state != 2 && !clips_on())
		return 0;
	now = now_seconds();
	if (now < clips.next_time)
		return 0;
	/* (a frame late starts the count again from now) */
	clips.next_time = now - clips.next_time > clips.frame_seconds ? now + clips.frame_seconds :
		clips.next_time + clips.frame_seconds;
	pthread_mutex_lock(&clips.lock);
	room = clips.frame_count < CLIP_FRAMES;
	pthread_mutex_unlock(&clips.lock);
	if (!room || source_width < 16 || source_height < 16)
		return 0;
	*width = source_width;
	*height = source_height;
	if (*width > clips.max_width || *height > clips.max_height)
	{
		double scale = (double)*width * clips.max_height > (double)*height * clips.max_width ?
			(double)clips.max_width / *width : (double)clips.max_height / *height;

		*width = (long)(*width * scale);
		*height = (long)(*height * scale);
	}
	/* (once the encoder has its size, every frame is made at it) */
	if (clips.video)
	{
		*width = clips.video->width;
		*height = clips.video->height;
	}
	*width &= ~1L;
	*height &= ~1L;
	*time = now;
	return 1;
}

int capture_clip_frame_lend(const unsigned char *pixels, long width, long height, long pitch, int bgra,
	double time, volatile int *released)
{
	int taken = 0;

	*released = 0;
	pthread_mutex_lock(&clips.lock);
	if (clips.state == 2 && clips.frame_count < CLIP_FRAMES && pixels)
	{
		struct clip_frame *frame = &clips.frames[clips.frame_count++];

		frame->pixels = pixels;
		frame->width = width;
		frame->height = height;
		frame->pitch = pitch;
		frame->bgra = bgra;
		frame->time = time;
		frame->released = released;
		pthread_cond_signal(&clips.wake);
		taken = 1;
	}
	pthread_mutex_unlock(&clips.lock);
	if (!taken)
		*released = 1;
	return taken;
}

void capture_clip_audio(const float *samples, unsigned long frames, unsigned long channels)
{
	unsigned long put, taken, frame;

	if (clips.state != 2 || !channels)
		return;
	put = clips.audio_put;
	taken = __atomic_load_n(&clips.audio_taken, __ATOMIC_ACQUIRE);
	for (frame = 0; frame < frames && put - taken < CLIP_AUDIO_RING; frame++, put++)
	{
		const float *in = samples + frame * channels;
		float left = in[0], right = channels > 1 ? in[1] : in[0];

		/* (5.1 and 7.1 folded to stereo: the center and the surrounds at
		-3 dB, the LFE left out) */
		if (channels >= 6)
		{
			left += 0.7071f * (in[2] + in[4]);
			right += 0.7071f * (in[2] + in[5]);
			if (channels >= 8)
			{
				left += 0.7071f * in[6];
				right += 0.7071f * in[7];
			}
			left *= 0.5f;
			right *= 0.5f;
		}
		clips.audio[(put % CLIP_AUDIO_RING) * 2] = left;
		clips.audio[(put % CLIP_AUDIO_RING) * 2 + 1] = right;
	}
	clips.audio_time = now_seconds();
	__atomic_store_n(&clips.audio_time_put, put, __ATOMIC_RELEASE);
	__atomic_store_n(&clips.audio_put, put, __ATOMIC_RELEASE);
}

void capture_request_clip(void)
{
	char name[160];

	if (clips.state != 2)
	{
		capture_notice("Clips are off", "clips.enabled in config.toml");
		return;
	}
	/* (made here, on the game's thread, as every other of platform_folder's
	callers) */
	platform_folder(_port_folder_clips);
	capture_file_name(name, sizeof(name));
	pthread_mutex_lock(&clips.lock);
	snprintf(clips.save_name, sizeof(clips.save_name), "%s", name);
	clips.save_wanted = 1;
	pthread_cond_signal(&clips.wake);
	pthread_mutex_unlock(&clips.lock);
}

#else

/* (no FFmpeg in this build: clips stay off) */

void capture_request_clip(void)
{
	if (config_boolean("clips.enabled"))
		capture_notice("Clips are off", "This build has no video encoder");
}

int capture_clip_frame_due(long source_width, long source_height, long *width, long *height, double *time)
{
	(void)source_width, (void)source_height, (void)width, (void)height, (void)time;
	return 0;
}

int capture_clip_frame_lend(const unsigned char *pixels, long width, long height, long pitch, int bgra,
	double time, volatile int *released)
{
	(void)pixels, (void)width, (void)height, (void)pitch, (void)bgra, (void)time;
	*released = 1;
	return 0;
}

void capture_clip_audio(const float *samples, unsigned long frames, unsigned long channels)
{
	(void)samples, (void)frames, (void)channels;
}

#endif

#endif
