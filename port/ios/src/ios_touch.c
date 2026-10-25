/*
IOS_TOUCH.C

The touch controls' hooks (port/linux/src/touch_input.c, under HALO_ANDROID),
which chupathingyce's port/android/host/host_touch.c serves from Android's
overlay of the screen. iOS has no overlay yet: the hooks report no touches,
and the game is played with a controller (SDL's gamepads: a DualSense or an
Xbox controller over Bluetooth, an MFi controller).
*/

#include <string.h>

/* port 0's controller as the overlay holds it: the SDL axes (left x, y,
right x, y, left trigger, right trigger), then the SDL button bits */
void host_touch_read(int *state)
{
	memset(state, 0, 7 * sizeof(*state));
}

/* the view swipe since the last read */
void host_touch_look_read(float *delta)
{
	delta[0] = delta[1] = 0.0f;
}

/* port 0's motors: the overlay's vibration, none here (a controller's own
rumble is SDL's) */
void host_touch_rumble(unsigned int low, unsigned int high)
{
	(void)low;
	(void)high;
}

/* whether a menu or a cinematic is up, for the overlay */
void host_touch_scene(int scene)
{
	(void)scene;
}

/* the system's gesture insets (left, top, right, bottom): none, so the
touch controls would use the whole screen */
void host_gesture_insets(int *insets)
{
	insets[0] = insets[1] = insets[2] = insets[3] = 0;
}
