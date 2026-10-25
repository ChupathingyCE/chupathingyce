/*
IOS_TOUCH.M

The on-screen touch controls, and the hooks the game reads them through
(port/linux/src/touch_input.c, under HALO_ANDROID), which chupathingyce's
port/android/host/host_touch.c serves on Android from its overlay
(port/android/app/.../TouchControls.java). This is that overlay for iOS: a
view over SDL's, with the stick, the controller's buttons and swipe aiming,
laid out as Android's defaults (TouchLayout.java) in the safe area.

It shows only in a game, never over the menus or a cinematic (which take
taps themselves, through SDL's view below), and with input.touch_controls
"auto" only while no controller is connected; while hidden it lets every
finger through. The game reads its controller on the main thread, where
UIKit hands over the fingers, so nothing here needs a lock.

Not yet Android's: the layout editor and its options, gyroscope aiming and
the phone's rumble.
*/

#import <UIKit/UIKit.h>
#import <GameController/GameController.h>
#include <SDL3/SDL.h>

#include <math.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

/* a press shorter than this still reaches the game: it reads the
controller once a frame and acts at 30 ticks a second, and a quick tap can
go down and up between two reads */
#define TOUCH_TAP_NS 60000000LL
/* view motion older than this is dropped: left over from the menus or a
cinematic */
#define TOUCH_STALE_NS 150000000LL

/* SDL's 15 gamepad buttons, then the left and right triggers */
#define TOUCH_BUTTONS 15
#define TOUCH_INPUTS (TOUCH_BUTTONS + 2)

/* the game's _touch_scene_* bits (touch_input.c) */
enum
{
	_scene_known = 1 << 0,
	_scene_menus = 1 << 1,
	_scene_on = 1 << 2,
	_scene_off = 1 << 3,
};

/* the controls, in TouchLayout.java's order: SDL_GamepadButton bits, or
SDL's trigger axes 4 and 5; at 960x540, as Android lays them out */
#define CONTROL_COUNT 18
#define CONTROL_STICK 16
static const struct
{
	const char *label;
	int bit, trigger;
	float x, y, radius;
} controls[CONTROL_COUNT] =
{
	{ "A / Jump", 0, -1, 856, 447, 36 },
	{ "B / Melee", 1, -1, 913, 377, 32 },
	{ "X / Reload", 2, -1, 794, 377, 34 },
	{ "Y / Weapon", 3, -1, 850, 312, 32 },
	{ "Fire", -1, 5, 915, 239, 39 },
	{ "Grenade", -1, 4, 802, 239, 35 },
	{ "Crouch", 7, -1, 236, 449, 32 },
	{ "Zoom", 8, -1, 691, 449, 32 },
	{ "Light", 9, -1, 360, 490, 27 },
	{ "Gren. type", 10, -1, 438, 490, 29 },
	{ "Pause", 6, -1, 570, 36, 28 },
	{ "Back", 4, -1, 390, 36, 28 },
	{ "Up", 11, -1, 100, 237, 25 },
	{ "Down", 12, -1, 100, 335, 25 },
	{ "Left", 13, -1, 51, 286, 25 },
	{ "Right", 14, -1, 149, 286, 25 },
	{ "Move", -1, -1, 115, 440, 64 },
	{ "Fire", -1, 5, 255, 239, 39 },
};

/* what a finger holds: a control, the view, or the hide toggle */
#define HELD_NONE (-1)
#define HELD_LOOK (-2)
#define HELD_TOGGLE (-3)
#define FINGER_COUNT 10

/* the SDL axes (left x, y, right x, y, left trigger, right trigger), then
the SDL button bits */
static int32_t touch_state[7];
static int64_t touch_pressed_ns[TOUCH_INPUTS];
static float look_delta[2];
static int64_t look_ns;
static int touch_scene;

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

static void touch_state_set(const int32_t *next)
{
	uint32_t pressed = touch_inputs(next) & ~touch_inputs(touch_state);
	int64_t now = now_ns();
	int input;

	for (input = 0; input < TOUCH_INPUTS; input++)
	{
		if (pressed & (1u << input))
			touch_pressed_ns[input] = now;
	}
	memcpy(touch_state, next, sizeof(touch_state));
}

/* ---------- the overlay */

@interface HaloTouchControls : UIView
- (void)sceneChanged;
@end

@implementation HaloTouchControls
{
	struct
	{
		__unsafe_unretained UITouch *touch;
		int held;
		/* where a button's finger went down: dragged far enough, it aims */
		CGPoint origin;
	} fingers[FINGER_COUNT];
	__unsafe_unretained UITouch *look_touch;
	CGPoint look_last;
	/* the left stick's deflection, -32767..32767 */
	int stick[2];
	/* the controls at the overlay's size, in its logical units */
	CGPoint position[CONTROL_COUNT];
	/* points per logical unit, and the safe area's corner */
	CGFloat scale;
	CGPoint offset;
	CGSize logical;
	BOOL visible;
	BOOL was_shown;
	float sensitivity;
}

- (instancetype)initWithFrame:(CGRect)frame
{
	self = [super initWithFrame:frame];
	if (self)
	{
		self.opaque = NO;
		self.backgroundColor = UIColor.clearColor;
		self.multipleTouchEnabled = YES;
		self.contentMode = UIViewContentModeRedraw;
		self.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
		self.accessibilityLabel = @"Halo touch controller";
		visible = YES;
		sensitivity = 1.0f;
		scale = 1.0;
		[self reset];
	}
	return self;
}

/* whether the controls are on screen: in a game (no menu or cinematic),
and with input.touch_controls "auto" only while no controller is
connected */
- (BOOL)shown
{
	if (!(touch_scene & _scene_known) || (touch_scene & (_scene_menus | _scene_off)))
		return NO;
	return (touch_scene & _scene_on) || GCController.controllers.count == 0;
}

- (void)layoutSubviews
{
	UIEdgeInsets safe = self.safeAreaInsets;
	CGFloat width = MAX(1.0, self.bounds.size.width - safe.left - safe.right);
	CGFloat height = MAX(1.0, self.bounds.size.height - safe.top - safe.bottom);
	int index;

	[super layoutSubviews];
	[self reset];
	/* circles keep their proportions, the controls spread over the whole
	safe area (TouchControls.java layoutControls) */
	scale = MAX(0.01, MIN(width / 960.0, height / 540.0));
	offset = CGPointMake(safe.left, safe.top);
	logical = CGSizeMake(width / scale, height / scale);
	for (index = 0; index < CONTROL_COUNT; index++)
	{
		CGFloat radius = controls[index].radius;

		position[index].x = MAX(radius, MIN(logical.width - radius, controls[index].x * logical.width / 960.0));
		position[index].y = MAX(radius, MIN(logical.height - radius, controls[index].y * logical.height / 540.0));
	}
	[self setNeedsDisplay];
}

- (CGPoint)logicalPoint:(UITouch *)touch
{
	CGPoint point = [touch locationInView:self];

	return CGPointMake((point.x - offset.x) / scale, (point.y - offset.y) / scale);
}

- (CGPoint)togglePosition
{
	return CGPointMake(logical.width / 2, 38);
}

- (void)reset
{
	memset(fingers, 0, sizeof(fingers));
	for (int index = 0; index < FINGER_COUNT; index++)
		fingers[index].held = HELD_NONE;
	look_touch = nil;
	stick[0] = stick[1] = 0;
	look_delta[0] = look_delta[1] = 0.0f;
	[self publish];
	[self setNeedsDisplay];
}

- (BOOL)held:(int)control
{
	for (int index = 0; index < FINGER_COUNT; index++)
	{
		if (fingers[index].touch && fingers[index].held == control)
			return YES;
	}
	return NO;
}

- (void)publish
{
	int32_t next[7] = { stick[0], stick[1], 0, 0, 0, 0, 0 };

	for (int index = 0; index < FINGER_COUNT; index++)
	{
		int held = fingers[index].held;

		if (!fingers[index].touch || held < 0 || held == CONTROL_STICK)
			continue;
		if (controls[held].bit >= 0)
			next[6] |= 1 << controls[held].bit;
		if (controls[held].trigger >= 0)
			next[controls[held].trigger] = 32767;
	}
	touch_state_set(next);
}

static BOOL inside(CGPoint point, CGPoint centre, CGFloat radius)
{
	CGFloat dx = point.x - centre.x, dy = point.y - centre.y;

	return dx * dx + dy * dy <= radius * radius;
}

/* the control under a finger going down, else HELD_NONE */
- (int)hit:(CGPoint)point
{
	if (inside(point, [self togglePosition], 28))
		return HELD_TOGGLE;
	if (!visible)
		return HELD_NONE;
	for (int index = 0; index < CONTROL_COUNT; index++)
	{
		if (index != CONTROL_STICK && inside(point, position[index], controls[index].radius))
			return index;
	}
	/* the stick takes a little more than its circle */
	if (![self held:CONTROL_STICK] &&
		inside(point, position[CONTROL_STICK], controls[CONTROL_STICK].radius * 1.28))
		return CONTROL_STICK;
	return HELD_NONE;
}

- (void)moveStick:(CGPoint)point
{
	CGFloat radius = controls[CONTROL_STICK].radius;
	CGFloat dx = (point.x - position[CONTROL_STICK].x) / radius;
	CGFloat dy = (point.y - position[CONTROL_STICK].y) / radius;
	CGFloat length = sqrt(dx * dx + dy * dy);

	if (length < 0.12)
		dx = dy = 0;
	else if (length > 1)
	{
		dx /= length;
		dy /= length;
	}
	stick[0] = (int)lround(dx * 32767);
	stick[1] = (int)lround(dy * 32767);
}

- (int)fingerOf:(UITouch *)touch
{
	for (int index = 0; index < FINGER_COUNT; index++)
	{
		if (fingers[index].touch == touch)
			return index;
	}
	return -1;
}

/* every finger is the game's while the controls are hidden */
- (UIView *)hitTest:(CGPoint)point withEvent:(UIEvent *)event
{
	return [self shown] ? [super hitTest:point withEvent:event] : nil;
}

- (void)touchesBegan:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event
{
	if (![self shown])
		return;
	for (UITouch *touch in touches)
	{
		CGPoint point = [self logicalPoint:touch];
		int control = [self hit:point];
		int finger = [self fingerOf:nil];

		if (finger < 0)
			break;
		if (control == HELD_TOGGLE)
		{
			[self reset];
			visible = !visible;
			continue;
		}
		if (control == HELD_NONE)
		{
			if (!visible || look_touch)
				continue;
			look_touch = touch;
			look_last = [touch locationInView:self];
			control = HELD_LOOK;
		}
		else if (control == CONTROL_STICK)
			[self moveStick:point];
		fingers[finger].touch = touch;
		fingers[finger].held = control;
		fingers[finger].origin = [touch locationInView:self];
	}
	[self publish];
	[self setNeedsDisplay];
}

- (void)touchesMoved:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event
{
	/* (a swipe draws nothing: only the stick's knob moves) */
	BOOL redraw = NO;

	if (![self shown])
		return;
	for (UITouch *touch in touches)
	{
		int finger = [self fingerOf:touch];
		CGPoint now = [touch locationInView:self];

		if (finger < 0)
			continue;
		if (fingers[finger].held == CONTROL_STICK)
		{
			[self moveStick:[self logicalPoint:touch]];
			redraw = YES;
		}
		/* a held button can aim too, either Fire among them */
		if (!look_touch && fingers[finger].held >= 0 && fingers[finger].held != CONTROL_STICK &&
			hypot(now.x - fingers[finger].origin.x, now.y - fingers[finger].origin.y) > 10 * scale)
		{
			look_touch = touch;
			look_last = fingers[finger].origin;
		}
		if (touch == look_touch)
		{
			/* in logical units, as Android's view swipe */
			look_delta[0] += (float)((now.x - look_last.x) / scale) * sensitivity;
			look_delta[1] += (float)((now.y - look_last.y) / scale) * sensitivity;
			look_ns = now_ns();
			look_last = now;
		}
	}
	[self publish];
	if (redraw)
		[self setNeedsDisplay];
}

- (void)touchesEnded:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event
{
	for (UITouch *touch in touches)
	{
		int finger = [self fingerOf:touch];

		if (finger < 0)
			continue;
		if (fingers[finger].held == CONTROL_STICK)
			stick[0] = stick[1] = 0;
		if (touch == look_touch)
			look_touch = nil;
		fingers[finger].touch = nil;
		fingers[finger].held = HELD_NONE;
	}
	[self publish];
	[self setNeedsDisplay];
}

- (void)touchesCancelled:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event
{
	[self reset];
}

/* at every read of the game's controller: shown or hidden anew, it starts
over (a finger held across the change must not press anything) */
- (void)sceneChanged
{
	BOOL shown = [self shown];

	if (shown != was_shown)
	{
		was_shown = shown;
		[self reset];
	}
}

/* ---------- drawing (TouchControls.java circle, stick) */

- (void)circle:(CGContextRef)context at:(CGPoint)centre radius:(CGFloat)radius
	label:(const char *)label active:(BOOL)active
{
	CGRect box = CGRectMake(centre.x - radius, centre.y - radius, radius * 2, radius * 2);

	if (active)
		CGContextSetRGBFillColor(context, 0x83 / 255.0, 0xd9 / 255.0, 1.0, 0x99 / 255.0);
	else
		CGContextSetRGBFillColor(context, 0x30 / 255.0, 0x40 / 255.0, 0x50 / 255.0, 0x55 / 255.0);
	CGContextFillEllipseInRect(context, box);
	if (active)
		CGContextSetRGBStrokeColor(context, 0xae / 255.0, 0xe7 / 255.0, 1.0, 1.0);
	else
		CGContextSetRGBStrokeColor(context, 1.0, 1.0, 1.0, 1.0);
	CGContextSetLineWidth(context, 2);
	CGContextStrokeEllipseInRect(context, box);
	if (label && *label)
	{
		NSString *text = [NSString stringWithUTF8String:label];
		NSDictionary *attributes = @{
			NSFontAttributeName: [UIFont systemFontOfSize:11 weight:UIFontWeightSemibold],
			NSForegroundColorAttributeName: UIColor.whiteColor,
		};
		CGSize size = [text sizeWithAttributes:attributes];

		[text drawAtPoint:CGPointMake(centre.x - size.width / 2, centre.y - size.height / 2)
			withAttributes:attributes];
	}
}

- (void)drawRect:(CGRect)rect
{
	CGContextRef context = UIGraphicsGetCurrentContext();

	if (![self shown])
		return;
	CGContextSaveGState(context);
	CGContextTranslateCTM(context, offset.x, offset.y);
	CGContextScaleCTM(context, scale, scale);
	[self circle:context at:[self togglePosition] radius:28 label:visible ? "Hide" : "Touch" active:NO];
	if (visible)
	{
		CGPoint centre = position[CONTROL_STICK];
		CGFloat radius = controls[CONTROL_STICK].radius;

		[self circle:context at:centre radius:radius label:"Move" active:NO];
		[self circle:context at:CGPointMake(centre.x + stick[0] / 32767.0 * radius, centre.y + stick[1] / 32767.0 * radius)
			radius:24 label:"" active:[self held:CONTROL_STICK]];
		for (int index = 0; index < CONTROL_COUNT; index++)
		{
			if (index != CONTROL_STICK)
				[self circle:context at:position[index] radius:controls[index].radius
					label:controls[index].label active:[self held:index]];
		}
	}
	CGContextRestoreGState(context);
}

@end

/* ---------- the overlay over SDL's view */

static HaloTouchControls *overlay;

/* puts the overlay over SDL's window once it has one */
static void overlay_attach(void)
{
	SDL_Window **windows;
	UIWindow *window = nil;
	UIView *view;
	int count = 0;

	if (overlay && overlay.superview)
		return;
	windows = SDL_GetWindows(&count);
	if (windows && count > 0)
		window = (__bridge UIWindow *)SDL_GetPointerProperty(SDL_GetWindowProperties(windows[0]),
			SDL_PROP_WINDOW_UIKIT_WINDOW_POINTER, NULL);
	SDL_free(windows);
	view = window.rootViewController.view ?: window;
	if (!view)
		return;
	overlay = [[HaloTouchControls alloc] initWithFrame:view.bounds];
	[view addSubview:overlay];
	SDL_Log("touch: on-screen controls over SDL's view");
}

/* ---------- the game's hooks */

/* port 0's controller as the overlay holds it: the SDL axes (left x, y,
right x, y, left trigger, right trigger), then the SDL button bits, with
each press held for at least TOUCH_TAP_NS */
void host_touch_read(int *state)
{
	int64_t now = now_ns();
	int input;

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
}

/* the view swipe since the last read, in the overlay's logical units */
void host_touch_look_read(float *delta)
{
	if (now_ns() - look_ns > TOUCH_STALE_NS)
		delta[0] = delta[1] = 0.0f;
	else
	{
		delta[0] = look_delta[0];
		delta[1] = look_delta[1];
	}
	look_delta[0] = look_delta[1] = 0.0f;
}

/* port 0's motors: the phone's vibration on Android, none here yet (a
controller's own rumble is SDL's) */
void host_touch_rumble(unsigned int low, unsigned int high)
{
	(void)low;
	(void)high;
}

/* whether a menu or a cinematic is up, for the overlay: at every read of
port 0, on the main thread */
void host_touch_scene(int scene)
{
	overlay_attach();
	touch_scene = scene;
	/* (a controller connecting changes it too) */
	[overlay sceneChanged];
}

/* the system's gesture insets (left, top, right, bottom) for the menus'
taps: none, iOS keeps its gestures to the very edges */
void host_gesture_insets(int *insets)
{
	insets[0] = insets[1] = insets[2] = insets[3] = 0;
}
