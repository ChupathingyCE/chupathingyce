/*
BINKPLAYER.C

An open-source Bink player for the Android port.

The port has no Bink decoder (the RAD SDK is proprietary) and Android's own
player cannot read a .bik, so this library links FFmpeg's open-source Bink
decoder (libavformat's bink demuxer, libavcodec's binkvideo and binkaudio
decoders) and plays the movie a Halo disc carries: the frames are scaled to
RGBA and drawn straight to a Surface (ANativeWindow), the audio is resampled
and written to the device's output (AAudio). The .bik files come from the
disc the player extracted (its bink folder), so nothing of the game is
packaged with the app.

The guest (port/linux/src/bink_null.c) only asks which movie to play and
reads how far it is; Java (BinkPlayer.java, MoviePlayer.java) drives this.
*/

#include <jni.h>
#include <android/log.h>
#include <android/native_window_jni.h>
#include <aaudio/AAudio.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <libavutil/channel_layout.h>
#include <libavutil/imgutils.h>

#define TAG "BinkPlayer"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

typedef struct
{
	AVFormatContext *format;
	AVCodecContext *video;
	AVCodecContext *audio;
	int video_stream;
	int audio_stream;
	struct SwsContext *scaler;
	struct SwrContext *resampler;
	AVFrame *frame;
	AVFrame *sound;
	AVPacket *packet;
	/* the size the scaler is set up for (the picture inside the window) */
	int scaler_width;
	int scaler_height;

	ANativeWindow *window;
	int width;
	int height;

	AAudioStream *output;
	int output_rate;

	pthread_t thread;
	pthread_mutex_t lock;
	atomic_int stop;
	atomic_int finished;
	atomic_int position_ms;
	int duration_ms;

	int64_t start_ms;
	int64_t audio_frames;
} BinkPlayer;

static int64_t bink_now_ms(void)
{
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

/* the presentation time of a frame, in milliseconds */
static int64_t bink_frame_ms(BinkPlayer *player, AVFrame *frame, AVRational time_base)
{
	int64_t stamp = frame->best_effort_timestamp;

	if (stamp == AV_NOPTS_VALUE)
		stamp = frame->pts;
	if (stamp == AV_NOPTS_VALUE)
		return -1;

	return av_rescale_q(stamp, time_base, (AVRational){ 1, 1000 });
}

static void bink_wait_for(BinkPlayer *player, int64_t ms)
{
	while (!atomic_load(&player->stop) && !atomic_load(&player->finished))
	{
		int64_t clock;

		if (player->output && player->output_rate > 0)
			clock = player->audio_frames * 1000 / player->output_rate;
		else
			clock = bink_now_ms() - player->start_ms;

		if (clock + 30 >= ms)
			break;

		usleep(2000);
	}
}

/* the decoded frame to the Surface: the picture's own shape kept (black
around it), scaled into the window the view gives */
static void bink_draw(BinkPlayer *player, AVFrame *frame)
{
	ANativeWindow_Buffer buffer;
	int width, height, x, y;

	if (!player->window || !player->video)
		return;

	if (ANativeWindow_lock(player->window, &buffer, NULL) != 0)
		return;

	if (buffer.format != WINDOW_FORMAT_RGBA_8888 || buffer.width <= 0 || buffer.height <= 0)
	{
		ANativeWindow_unlockAndPost(player->window);
		return;
	}

	/* the picture fits inside the window, the side that limits it */
	if (buffer.width * player->height <= buffer.height * player->width)
	{
		width = buffer.width;
		height = player->height * buffer.width / player->width;
	}
	else
	{
		height = buffer.height;
		width = player->width * buffer.height / player->height;
	}
	if (width < 1)
		width = 1;
	if (height < 1)
		height = 1;
	x = (buffer.width - width) / 2;
	y = (buffer.height - height) / 2;
	if (x < 0)
		x = 0;
	if (y < 0)
		y = 0;

	if (player->scaler && (player->scaler_width != width || player->scaler_height != height))
	{
		sws_freeContext(player->scaler);
		player->scaler = NULL;
	}
	if (!player->scaler)
	{
		player->scaler = sws_getContext(player->width, player->height, player->video->pix_fmt,
			width, height, AV_PIX_FMT_RGBA, SWS_BILINEAR, NULL, NULL, NULL);
		player->scaler_width = width;
		player->scaler_height = height;
	}

	if (buffer.bits && player->scaler)
	{
		uint8_t *rows[4] = {
			(uint8_t *)buffer.bits + ((size_t)y * buffer.stride + x) * 4, NULL, NULL, NULL
		};
		int pitches[4] = { buffer.stride * 4, 0, 0, 0 };

		memset(buffer.bits, 0, (size_t)buffer.stride * buffer.height * 4);
		sws_scale(player->scaler, (const uint8_t *const *)frame->data, frame->linesize,
			0, player->height, rows, pitches);
	}

	ANativeWindow_unlockAndPost(player->window);
}

static void bink_sound(BinkPlayer *player, AVFrame *frame)
{
	uint8_t *out[1];
	int capacity;
	int converted = 0;

	if (!player->output || !player->resampler)
		return;

	capacity = swr_get_out_samples(player->resampler, frame->nb_samples);
	out[0] = av_malloc((size_t)capacity * 2 * 2);
	if (!out[0])
		return;

	converted = swr_convert(player->resampler, out, capacity,
		(const uint8_t *const *)frame->data, frame->nb_samples);
	if (converted > 0)
	{
		int written = 0;

		while (written < converted && !atomic_load(&player->stop))
		{
			int result = AAudioStream_write(player->output,
				(int16_t *)out[0] + (size_t)written * 2, converted - written, 1000000000);

			if (result < 0)
				break;
			written += result;
		}
		player->audio_frames += written;
	}

	av_free(out[0]);
}

static void bink_decode(BinkPlayer *player, AVCodecContext *context, AVFrame *frame, int is_video)
{
	if (avcodec_send_packet(context, player->packet) < 0)
		return;

	while (avcodec_receive_frame(context, frame) == 0)
	{
		if (is_video)
		{
			int64_t ms = bink_frame_ms(player, frame, player->format->streams[player->video_stream]->time_base);

			if (ms >= 0)
			{
				bink_wait_for(player, ms);
				atomic_store(&player->position_ms, (int)ms);
			}
			bink_draw(player, frame);
		}
		else
		{
			bink_sound(player, frame);
		}
		av_frame_unref(frame);
	}
}

static void *bink_thread(void *argument)
{
	BinkPlayer *player = argument;
	int read = 0;

	player->start_ms = bink_now_ms();

	while (!atomic_load(&player->stop))
	{
		read = av_read_frame(player->format, player->packet);
		if (read < 0)
			break;

		if (player->packet->stream_index == player->video_stream && player->video)
			bink_decode(player, player->video, player->frame, 1);
		else if (player->packet->stream_index == player->audio_stream && player->audio)
			bink_decode(player, player->audio, player->sound, 0);

		av_packet_unref(player->packet);
	}

	/* (the buffered tails: the last frames and the last of the sound) */
	if (!atomic_load(&player->stop))
	{
		if (player->video)
		{
			avcodec_send_packet(player->video, NULL);
			while (avcodec_receive_frame(player->video, player->frame) == 0)
			{
				bink_draw(player, player->frame);
				av_frame_unref(player->frame);
			}
		}
		if (player->audio)
		{
			avcodec_send_packet(player->audio, NULL);
			while (avcodec_receive_frame(player->audio, player->sound) == 0)
			{
				bink_sound(player, player->sound);
				av_frame_unref(player->sound);
			}
		}
	}

	atomic_store(&player->finished, 1);
	LOGI("the movie ended");
	return NULL;
}

static void bink_free(BinkPlayer *player)
{
	if (!player)
		return;

	atomic_store(&player->stop, 1);
	if (player->thread)
		pthread_join(player->thread, NULL);

	if (player->output)
	{
		AAudioStream_requestStop(player->output);
		AAudioStream_close(player->output);
	}
	if (player->window)
		ANativeWindow_release(player->window);
	if (player->resampler)
		swr_free(&player->resampler);
	if (player->scaler)
		sws_freeContext(player->scaler);
	av_frame_free(&player->frame);
	av_frame_free(&player->sound);
	av_packet_free(&player->packet);
	avcodec_free_context(&player->video);
	avcodec_free_context(&player->audio);
	if (player->format)
		avformat_close_input(&player->format);
	pthread_mutex_destroy(&player->lock);
	free(player);
}

/* ---------- the player (BinkPlayer.java) opens one */

static int bink_open_audio(BinkPlayer *player)
{
	AAudioStreamBuilder *builder = NULL;
	AVChannelLayout out_layout;
	int rate = 0;

	if (player->audio_stream < 0)
		return 0;

	if (AAudio_createStreamBuilder(&builder) != AAUDIO_OK)
		return 0;

	AAudioStreamBuilder_setDirection(builder, AAUDIO_DIRECTION_OUTPUT);
	AAudioStreamBuilder_setSharingMode(builder, AAUDIO_SHARING_MODE_SHARED);
	AAudioStreamBuilder_setPerformanceMode(builder, AAUDIO_PERFORMANCE_MODE_NONE);
	AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_I16);
	AAudioStreamBuilder_setChannelCount(builder, 2);

	if (AAudioStreamBuilder_openStream(builder, &player->output) != AAUDIO_OK)
		player->output = NULL;
	AAudioStreamBuilder_delete(builder);

	if (!player->output)
	{
		LOGE("no audio output; the movie plays without sound");
		return 0;
	}

	rate = AAudioStream_getSampleRate(player->output);
	player->output_rate = rate > 0 ? rate : 48000;

	av_channel_layout_default(&out_layout, 2);
	if (swr_alloc_set_opts2(&player->resampler,
		&out_layout, AV_SAMPLE_FMT_S16, player->output_rate,
		&player->audio->ch_layout, player->audio->sample_fmt, player->audio->sample_rate,
		0, NULL) < 0 || swr_init(player->resampler) < 0)
	{
		LOGE("the sound could not be set up");
		swr_free(&player->resampler);
		return 0;
	}

	AAudioStream_requestStart(player->output);
	return 1;
}

JNIEXPORT jlong JNICALL Java_com_halo_decomp_BinkPlayer_nativeOpen(
	JNIEnv *env, jclass cls, jstring path, jobject surface)
{
	BinkPlayer *player;
	const char *file;
	const AVCodec *codec;
	int found;

	(void)cls;

	if (!path || !surface)
		return 0;

	player = calloc(1, sizeof(*player));
	if (!player)
		return 0;

	player->video_stream = -1;
	player->audio_stream = -1;
	pthread_mutex_init(&player->lock, NULL);

	file = (*env)->GetStringUTFChars(env, path, NULL);
	if (!file)
	{
		bink_free(player);
		return 0;
	}

	if (avformat_open_input(&player->format, file, NULL, NULL) < 0)
	{
		LOGE("the movie could not be opened: %s", file);
		(*env)->ReleaseStringUTFChars(env, path, file);
		bink_free(player);
		return 0;
	}
	(*env)->ReleaseStringUTFChars(env, path, file);

	if (avformat_find_stream_info(player->format, NULL) < 0)
	{
		LOGE("the movie's streams could not be read");
		bink_free(player);
		return 0;
	}

	found = av_find_best_stream(player->format, AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0);
	if (found < 0)
	{
		LOGE("the movie has no video");
		bink_free(player);
		return 0;
	}
	player->video_stream = found;
	player->video = avcodec_alloc_context3(codec);
	if (!player->video ||
		avcodec_parameters_to_context(player->video, player->format->streams[found]->codecpar) < 0 ||
		avcodec_open2(player->video, codec, NULL) < 0)
	{
		LOGE("the movie's video could not be set up");
		bink_free(player);
		return 0;
	}
	player->width = player->video->width;
	player->height = player->video->height;

	found = av_find_best_stream(player->format, AVMEDIA_TYPE_AUDIO, -1, -1, &codec, 0);
	if (found >= 0)
	{
		player->audio_stream = found;
		player->audio = avcodec_alloc_context3(codec);
		if (!player->audio ||
			avcodec_parameters_to_context(player->audio, player->format->streams[found]->codecpar) < 0 ||
			avcodec_open2(player->audio, codec, NULL) < 0)
		{
			LOGE("the movie's sound could not be set up; it plays without it");
			avcodec_free_context(&player->audio);
			player->audio_stream = -1;
		}
	}

	player->frame = av_frame_alloc();
	player->sound = av_frame_alloc();
	player->packet = av_packet_alloc();

	player->window = ANativeWindow_fromSurface(env, surface);
	if (player->window)
		ANativeWindow_setBuffersGeometry(player->window, 0, 0, WINDOW_FORMAT_RGBA_8888);

	if (!player->frame || !player->sound || !player->packet)
	{
		LOGE("the movie could not be set up");
		bink_free(player);
		return 0;
	}

	if (player->format->duration > 0)
		player->duration_ms = (int)(player->format->duration * 1000 / AV_TIME_BASE);

	bink_open_audio(player);

	LOGI("opened: %dx%d, %d ms", player->width, player->height, player->duration_ms);
	return (jlong)(intptr_t)player;
}

JNIEXPORT void JNICALL Java_com_halo_decomp_BinkPlayer_nativeStart(
	JNIEnv *env, jclass cls, jlong handle)
{
	BinkPlayer *player = (BinkPlayer *)(intptr_t)handle;

	(void)env;
	(void)cls;

	if (!player)
		return;

	if (pthread_create(&player->thread, NULL, bink_thread, player) != 0)
	{
		LOGE("the movie's thread could not be started");
		atomic_store(&player->finished, 1);
	}
}

JNIEXPORT jint JNICALL Java_com_halo_decomp_BinkPlayer_nativePositionMs(
	JNIEnv *env, jclass cls, jlong handle)
{
	BinkPlayer *player = (BinkPlayer *)(intptr_t)handle;

	(void)env;
	(void)cls;

	return player ? atomic_load(&player->position_ms) : 0;
}

JNIEXPORT jint JNICALL Java_com_halo_decomp_BinkPlayer_nativeDurationMs(
	JNIEnv *env, jclass cls, jlong handle)
{
	BinkPlayer *player = (BinkPlayer *)(intptr_t)handle;

	(void)env;
	(void)cls;

	return player ? player->duration_ms : 0;
}

JNIEXPORT jboolean JNICALL Java_com_halo_decomp_BinkPlayer_nativeFinished(
	JNIEnv *env, jclass cls, jlong handle)
{
	BinkPlayer *player = (BinkPlayer *)(intptr_t)handle;

	(void)env;
	(void)cls;

	return (player && atomic_load(&player->finished)) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL Java_com_halo_decomp_BinkPlayer_nativeStop(
	JNIEnv *env, jclass cls, jlong handle)
{
	BinkPlayer *player = (BinkPlayer *)(intptr_t)handle;

	(void)env;
	(void)cls;

	if (player)
		atomic_store(&player->stop, 1);
}

JNIEXPORT void JNICALL Java_com_halo_decomp_BinkPlayer_nativeClose(
	JNIEnv *env, jclass cls, jlong handle)
{
	(void)env;
	(void)cls;

	bink_free((BinkPlayer *)(intptr_t)handle);
}
