/*
HOST_TOUCH.C

The on-screen touch controls between the overlay and the game. The overlay
(app/.../TouchControls.java) is an Android view over SDL's surface: it
hands its stick, buttons and view swipes over JNI on the UI thread, and the
game reads them through host imports when it reads port 0's controller
(port/linux/src/touch_input.c). The game hands back what the overlay needs
to know: whether a menu or a cinematic is up and the input.touch_controls
setting (host_touch_scene), and how hard port 0 rumbles. It asks the
activity for the system's gesture insets (host_gesture_insets).

The native 64-bit build (tools/android64_build.py) links this file too: the
game calls these functions directly there.
*/

#include "host.h"

#include <SDL3/SDL.h>
#include <android/log.h>
#include <jni.h>
#include <pthread.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

/* a press shorter than this still reaches the game: it reads the
controller once a frame and acts at 30 ticks a second, and a quick tap can
go down and up between two reads */
#define TOUCH_TAP_NS 60000000LL
/* view motion and rumble older than this are dropped: left over from the
menus, a cinematic or the app in the background */
#define TOUCH_STALE_NS 150000000LL

/* SDL's 15 gamepad buttons, then the left and right triggers */
#define TOUCH_BUTTONS 15
#define TOUCH_INPUTS (TOUCH_BUTTONS + 2)

static pthread_mutex_t touch_lock = PTHREAD_MUTEX_INITIALIZER;
/* the SDL axes (left x, y, right x, y, left trigger, right trigger), then
the SDL button bits */
static int32_t touch_state[7];
static int64_t touch_pressed_ns[TOUCH_INPUTS];
static float look_delta[2];
static int64_t look_ns;
static int rumble_amplitude;
static int64_t rumble_ns;
static volatile int touch_scene;

static int64_t now_ns(void)
{
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	return (int64_t)now.tv_sec * 1000000000LL + now.tv_nsec;
}

/* the inputs of a state as bits: buttons, then the triggers */
static uint32_t touch_inputs(const int32_t *state)
{
	return ((uint32_t)state[6] & ((1u << TOUCH_BUTTONS) - 1)) | (state[4] ? 1u << TOUCH_BUTTONS : 0) |
		(state[5] ? 1u << (TOUCH_BUTTONS + 1) : 0);
}

JNIEXPORT void JNICALL Java_com_halo_decomp_TouchControls_nativeState(
	JNIEnv *env, jclass cls, jint lx, jint ly, jint rx, jint ry,
	jint lt, jint rt, jint buttons)
{
	int32_t next[] = { lx, ly, rx, ry, lt, rt, buttons };
	uint32_t pressed;
	int64_t now = now_ns();
	int input;

	(void)env;
	(void)cls;
	pthread_mutex_lock(&touch_lock);
	pressed = touch_inputs(next) & ~touch_inputs(touch_state);
	for (input = 0; input < TOUCH_INPUTS; input++)
	{
		if (pressed & (1u << input))
			touch_pressed_ns[input] = now;
	}
	memcpy(touch_state, next, sizeof(next));
	pthread_mutex_unlock(&touch_lock);
}

/* the guest's: the overlay's controller state, with each press held for
at least TOUCH_TAP_NS */
void host_touch_read(int32_t *state)
{
	int64_t now = now_ns();
	int input;

	pthread_mutex_lock(&touch_lock);
	memcpy(state, touch_state, sizeof(touch_state));
	for (input = 0; input < TOUCH_INPUTS; input++)
	{
		if (touch_pressed_ns[input] && now - touch_pressed_ns[input] < TOUCH_TAP_NS)
		{
			if (input < TOUCH_BUTTONS)
				state[6] |= 1 << input;
			else if (!state[4 + input - TOUCH_BUTTONS])
				state[4 + input - TOUCH_BUTTONS] = 32767;
		}
	}
	pthread_mutex_unlock(&touch_lock);
}

JNIEXPORT void JNICALL Java_com_halo_decomp_TouchControls_nativeLook(
	JNIEnv *env, jclass cls, jfloat dx, jfloat dy)
{
	(void)env;
	(void)cls;
	pthread_mutex_lock(&touch_lock);
	look_delta[0] += dx;
	look_delta[1] += dy;
	look_ns = now_ns();
	pthread_mutex_unlock(&touch_lock);
}

JNIEXPORT void JNICALL Java_com_halo_decomp_TouchControls_nativeLookReset(JNIEnv *env, jclass cls)
{
	(void)env;
	(void)cls;
	pthread_mutex_lock(&touch_lock);
	look_delta[0] = look_delta[1] = 0.0f;
	pthread_mutex_unlock(&touch_lock);
}

/* the guest's: the view swipe since the last read, in the overlay's
logical pixels with its sensitivity applied */
void host_touch_look_read(float *delta)
{
	int64_t now = now_ns();

	pthread_mutex_lock(&touch_lock);
	memcpy(delta, look_delta, sizeof(look_delta));
	if (now - look_ns > TOUCH_STALE_NS)
		delta[0] = delta[1] = 0.0f;
	look_delta[0] = look_delta[1] = 0.0f;
	pthread_mutex_unlock(&touch_lock);
}

/* the guest's: port 0's motors (XInputSetState), each 0..65535; the game
sends them every frame, and zero while paused or with the profile's
vibration off */
void host_touch_rumble(unsigned int low, unsigned int high)
{
	unsigned int strength = low > high ? low : high;

	pthread_mutex_lock(&touch_lock);
	rumble_amplitude = strength ? 64 + (int)(strength * 191u / 65535u) : 0;
	rumble_ns = now_ns();
	pthread_mutex_unlock(&touch_lock);
}

/* the phone's vibration strength, 0 or 64..255 */
JNIEXPORT jint JNICALL Java_com_halo_decomp_TouchControls_nativeRumble(JNIEnv *env, jclass cls)
{
	int amplitude;

	(void)env;
	(void)cls;
	pthread_mutex_lock(&touch_lock);
	amplitude = now_ns() - rumble_ns > TOUCH_STALE_NS ? 0 : rumble_amplitude;
	pthread_mutex_unlock(&touch_lock);
	return amplitude;
}

/* the guest's, at every read of port 0: _touch_scene_* bits
(port/linux/src/touch_input.c) */
void host_touch_scene(int scene)
{
	touch_scene = scene;
}

/* 0 until the game has read its controller once */
JNIEXPORT jint JNICALL Java_com_halo_decomp_TouchControls_nativeScene(JNIEnv *env, jclass cls)
{
	(void)env;
	(void)cls;
	return touch_scene;
}

/* asks the activity for Android's system gesture insets via JNI
(HaloActivity.getSystemGestureInsetsPixels); the game calls it at every
finger down because the insets change when the phone rotates; the method is
found on the activity's own class because the game's native thread has the
system's class loader, which does not know the app's classes; a change is
logged so the log shows the value the game works with; all 0 on any failure
(the touch controls then use the whole screen) */
void host_gesture_insets(int *insets)
{
	static int logged[4] = { -1, -1, -1, -1 };
	JNIEnv *env = (JNIEnv *)SDL_GetAndroidJNIEnv();
	jobject activity = (jobject)SDL_GetAndroidActivity();
	jint values[4] = { 0, 0, 0, 0 };
	int index;

	if (env && activity)
	{
		jclass activity_class = (*env)->GetObjectClass(env, activity);
		jmethodID method = activity_class ? (*env)->GetMethodID(env, activity_class, "getSystemGestureInsetsPixels", "()[I") : NULL;
		jintArray array = method ? (jintArray)(*env)->CallObjectMethod(env, activity, method) : NULL;

		if (array && !(*env)->ExceptionCheck(env) && (*env)->GetArrayLength(env, array) == 4)
			(*env)->GetIntArrayRegion(env, array, 0, 4, values);
		/* a Java exception left pending would break the next JNI call of the
		thread, and a failed read must leave zeros */
		if ((*env)->ExceptionCheck(env))
		{
			(*env)->ExceptionClear(env);
			for (index = 0; index < 4; index++)
				values[index] = 0;
		}
		if (array)
			(*env)->DeleteLocalRef(env, array);
		if (activity_class)
			(*env)->DeleteLocalRef(env, activity_class);
		(*env)->DeleteLocalRef(env, activity);
	}
	for (index = 0; index < 4; index++)
		insets[index] = (int)values[index];
	if (memcmp(logged, insets, sizeof(logged)))
	{
		memcpy(logged, insets, sizeof(logged));
		__android_log_print(ANDROID_LOG_INFO, HOST_LOG_TAG, "system gesture insets %d,%d,%d,%d", insets[0], insets[1], insets[2], insets[3]);
	}
}
