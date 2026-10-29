/*
IOS_TOUCH.M

The on-screen touch controls, and the hooks the game reads them through
(port/linux/src/touch_input.c, under HALO_ANDROID), which chupathingyce's
port/android/host/host_touch.c serves on Android from its overlay
(port/android/app/.../TouchControls.java). This is that overlay for iOS: a
view over SDL's, with the stick, the controller's buttons and swipe aiming,
laid out in the safe area: smoked glass, Xbox-coloured face buttons, symbols on
the small ones (the table below, and "how the controls look").

It shows only in a game, never over the menus or a cinematic (which take
taps themselves, through SDL's view below), and with input.touch_controls
"auto" only while no controller is connected; while hidden it lets every
finger through. The game reads its controller on the main thread, where
UIKit hands over the fingers, so nothing here needs a lock.

Not yet Android's: the layout editor and its options, gyroscope aiming and
the phone's rumble.

On a large screen (an iPad, iPhone Duo's inner display) the controls keep
about a phone's size and still spread to the corners, under the thumbs.

iPhone Duo (iOS 27.1): closed, it is a phone, and the outer display's
layout is a phone's; open flat, the inner display is one big screen. Held
part open like a laptop (the fold across the screen, say at 70 degrees),
the picture goes in the half above the fold (host_present_area: the game's
platform_video_present_area) and the controls in the half below it, a
controller's deck; part open like a book (the fold up the screen), no
control sits on the fold. The fold is UIKit's division region, and a hinge
interaction says when to look again; both are reached through the
Objective-C runtime, so the overlay builds with any SDK and does nothing
where iOS has neither.

On the Apple TV (HALO_TVOS) there is no touch screen: the hooks are here,
and answer nothing, and the game is played with a controller.
*/

#import <UIKit/UIKit.h>
#import <GameController/GameController.h>
#include <SDL3/SDL.h>

#include <math.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include "halo_ios.h"

#ifdef HALO_TVOS

/* ---------- the Apple TV: no overlay */

void host_touch_read(int *state)
{
	memset(state, 0, 7 * sizeof(*state));
}

void host_touch_look_read(float *delta)
{
	delta[0] = delta[1] = 0.0f;
}

void host_touch_rumble(unsigned int low, unsigned int high)
{
	(void)low;
	(void)high;
}

void host_touch_scene(int scene)
{
	(void)scene;
}

void host_gesture_insets(int *insets)
{
	insets[0] = insets[1] = insets[2] = insets[3] = 0;
}

void host_present_area(float *area)
{
	area[0] = area[1] = 0.0f;
	area[2] = area[3] = 1.0f;
}

#else

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
SDL's trigger axes 4 and 5. Their places are this port's own, at 960x540:
the face buttons a diamond under the right thumb, Fire and Grenade above
them where an index finger or the thumb reaches up, the stick under the
left thumb with the D-pad above it, and the rarely pressed small ones as
pills along the top and bottom edges. Each shows a big glyph (a letter, a
word) or an SF Symbol, with a caption under it on the round ones: a pill's
radius is half its height, and half_width its half width (0: a circle) */
#define CONTROL_COUNT 18
#define CONTROL_STICK 16
static const struct
{
	const char *label;
	const char *glyph, *symbol, *caption;
	/* the glyph's or symbol's colour, 0xRRGGBB */
	unsigned int tint;
	int bit, trigger;
	float x, y, radius, half_width;
} controls[CONTROL_COUNT] =
{
	{ "A, jump", "A", NULL, "JUMP", 0x6fd16f, 0, -1, 862, 452, 36, 0 },
	{ "B, melee", "B", NULL, "MELEE", 0xf0625a, 1, -1, 926, 384, 31, 0 },
	{ "X, reload", "X", NULL, "RELOAD", 0x58a6ff, 2, -1, 798, 384, 31, 0 },
	{ "Y, switch weapon", "Y", NULL, "WEAPON", 0xf5c542, 3, -1, 862, 318, 31, 0 },
	{ "Fire", "FIRE", NULL, NULL, 0xffffff, -1, 5, 906, 212, 44, 0 },
	{ "Grenade", "GRENADE", NULL, NULL, 0xffffff, -1, 4, 796, 232, 34, 0 },
	{ "Crouch", "CROUCH", NULL, NULL, 0xffffff, 7, -1, 252, 474, 30, 0 },
	{ "Zoom", NULL, "scope", "ZOOM", 0xffffff, 8, -1, 700, 474, 30, 0 },
	{ "Flashlight", "LIGHT", "flashlight.on.fill", NULL, 0xffffff, 9, -1, 398, 508, 17, 48 },
	{ "Switch grenade", "SWAP", "arrow.triangle.2.circlepath", NULL, 0xffffff, 10, -1, 562, 508, 17, 48 },
	{ "Pause", "PAUSE", "pause.fill", NULL, 0xffffff, 6, -1, 594, 34, 17, 48 },
	{ "Back", "BACK", "chevron.backward", NULL, 0xffffff, 4, -1, 366, 34, 17, 48 },
	{ "Up", NULL, "chevron.up", NULL, 0xffffff, 11, -1, 190, 214, 22, 0 },
	{ "Down", NULL, "chevron.down", NULL, 0xffffff, 12, -1, 190, 286, 22, 0 },
	{ "Left", NULL, "chevron.left", NULL, 0xffffff, 13, -1, 154, 250, 22, 0 },
	{ "Right", NULL, "chevron.right", NULL, 0xffffff, 14, -1, 226, 250, 22, 0 },
	{ "Move", NULL, NULL, "MOVE", 0xffffff, -1, -1, 138, 408, 68, 0 },
	{ "Fire", "FIRE", NULL, NULL, 0xffffff, -1, 5, 72, 206, 40, 0 },
};

/* the hide toggle: a pill at the top's middle */
#define TOGGLE_RADIUS 17
#define TOGGLE_HALF_WIDTH 36

/* how far a control reaches each way from its centre (a pill wider than tall) */
static CGFloat control_half_width(int index)
{
	return MAX(controls[index].radius, controls[index].half_width);
}

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

/* points per logical unit at most (the 960x540 layout's): a third more
than a big phone's in landscape (0.75), where an iPad's would be 1.25 and up */
#define MAX_TOUCH_SCALE 1.0

/* ---------- iPhone Duo's fold and hinge (iOS 27.1's UIKit)

As Apple declares them (UIView's reservedRegionsOfKind:, the division
region kind, UIViewReservedRegion's frame and isActive, UIHingeInteraction's
initWithUpdateHandler:, its update's hinge and the hinge's angle, in
radians), declared here so that the SDK need not have them: each is
looked up before it is called */

@protocol HaloRegionView <NSObject>
- (NSArray *)reservedRegionsOfKind:(id)kind;
@end

@protocol HaloRegionKind <NSObject>
+ (id)divisionRegionKind;
@end

@protocol HaloRegion <NSObject>
- (CGRect)frame;
- (BOOL)isActive;
@end

@protocol HaloHingeInteraction <NSObject>
- (id)initWithUpdateHandler:(void (^)(id interaction, id update))handler;
@end

@protocol HaloHingeUpdate <NSObject>
- (id)hinge;
@end

@protocol HaloHinge <NSObject>
- (CGFloat)angle;
@end

/* the fold across a view, in its points, while the device is part open
(Apple's division region is active only then); NO when there is none: not
an iPhone Duo, not iOS 27.1, or open flat */
static BOOL fold_in_view(UIView *view, CGRect *fold)
{
	Class kind = NSClassFromString(@"UIViewReservedRegionKind");
	NSArray *regions;

	if (!kind || ![kind respondsToSelector:@selector(divisionRegionKind)] ||
		![view respondsToSelector:@selector(reservedRegionsOfKind:)])
		return NO;
	regions = [(id<HaloRegionView>)view reservedRegionsOfKind:[(Class<HaloRegionKind>)kind divisionRegionKind]];
	for (id<HaloRegion> region in regions)
	{
		if ([region respondsToSelector:@selector(isActive)] && [region isActive] &&
			[region respondsToSelector:@selector(frame)])
		{
			*fold = [region frame];
			return YES;
		}
	}
	return NO;
}

/* the part of the window the picture is shown in, as fractions of it (x,
y, width, height from the top left): all of it, but the half above the fold
in the laptop posture (host_present_area) */
static float present_area[4] = { 0.0f, 0.0f, 1.0f, 1.0f };

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
	/* iPhone Duo: the fold the controls were laid out around, whether there
	was one, and whether it ran across the screen (the laptop posture: the
	controls below it); the hinge's angle last reported, in degrees */
	CGRect laid_fold;
	BOOL laid_folded;
	BOOL tabletop;
	BOOL hinge_watched;
	double hinge_degrees;
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
	CGRect area = UIEdgeInsetsInsetRect(self.bounds, self.safeAreaInsets);
	CGRect fold = CGRectZero;
	BOOL folded = fold_in_view(self, &fold), was_tabletop = tabletop;
	CGFloat width, height;
	int index;

	[super layoutSubviews];
	[self reset];
	[self watchHinge];
	laid_fold = fold;
	laid_folded = folded;
	/* the laptop posture: a fold across the screen. The picture above it,
	the controls below it */
	tabletop = folded && fold.size.width > fold.size.height && self.bounds.size.height > 0 &&
		CGRectGetMinY(fold) > 0 && CGRectGetMaxY(fold) < CGRectGetMaxY(area);
	if (tabletop)
	{
		CGFloat top = MAX(CGRectGetMinY(area), CGRectGetMaxY(fold));

		area.size.height = CGRectGetMaxY(area) - top;
		area.origin.y = top;
		present_area[0] = 0.0f;
		present_area[1] = 0.0f;
		present_area[2] = 1.0f;
		present_area[3] = (float)(CGRectGetMinY(fold) / self.bounds.size.height);
	}
	else
	{
		present_area[0] = present_area[1] = 0.0f;
		present_area[2] = present_area[3] = 1.0f;
	}
	if (tabletop != was_tabletop)
	{
		if (tabletop)
			SDL_Log("touch: iPhone Duo part open like a laptop (hinge at %.0f degrees): the picture above the fold, "
				"the controls below", hinge_degrees);
		else
			SDL_Log("touch: the whole screen again");
	}
	width = MAX(1.0, area.size.width);
	height = MAX(1.0, area.size.height);
	/* circles keep their proportions, the controls spread over the whole
	safe area (TouchControls.java layoutControls), or the deck below the
	fold; no larger than a big phone's, so that an iPad's are not a hand's
	width */
	scale = MAX(0.01, MIN(MAX_TOUCH_SCALE, MIN(width / 960.0, height / 540.0)));
	offset = area.origin;
	logical = CGSizeMake(width / scale, height / scale);
	for (index = 0; index < CONTROL_COUNT; index++)
	{
		CGFloat radius = controls[index].radius, half_width = control_half_width(index);

		position[index].x = MAX(half_width, MIN(logical.width - half_width, controls[index].x * logical.width / 960.0));
		position[index].y = MAX(radius, MIN(logical.height - radius, controls[index].y * logical.height / 540.0));
	}
	[self avoidFold];
	[self setNeedsDisplay];
}

/* iPhone Duo part open like a book: a control over the fold moves off it,
to the side its centre is on (in the laptop posture the controls are all
below the fold already) */
- (void)avoidFold
{
	CGRect fold;
	int index;

	if (!laid_folded || tabletop)
		return;
	/* (in logical units, as the controls are) */
	fold = CGRectMake((laid_fold.origin.x - offset.x) / scale, (laid_fold.origin.y - offset.y) / scale,
		laid_fold.size.width / scale, laid_fold.size.height / scale);
	for (index = 0; index < CONTROL_COUNT; index++)
	{
		CGFloat radius = controls[index].radius, half_width = control_half_width(index);
		CGPoint *at = &position[index];

		if (!CGRectIntersectsRect(fold, CGRectMake(at->x - half_width, at->y - radius, half_width * 2, radius * 2)))
			continue;
		/* a fold up the screen moves controls sideways, one across it up or down */
		if (fold.size.height >= fold.size.width)
			at->x = at->x < CGRectGetMidX(fold) ? CGRectGetMinX(fold) - half_width : CGRectGetMaxX(fold) + half_width;
		else
			at->y = at->y < CGRectGetMidY(fold) ? CGRectGetMinY(fold) - radius : CGRectGetMaxY(fold) + radius;
		at->x = MAX(half_width, MIN(logical.width - half_width, at->x));
		at->y = MAX(radius, MIN(logical.height - radius, at->y));
	}
}

/* the fold, looked at again: laid out anew only when it comes, goes or
moves (the hinge reports as it turns, and a layout lets go of every finger) */
- (void)foldMaybeChanged
{
	CGRect fold = CGRectZero;
	BOOL folded = fold_in_view(self, &fold);

	if (folded != laid_folded || (folded && !CGRectEqualToRect(fold, laid_fold)))
		[self setNeedsLayout];
}

/* iPhone Duo's hinge, once: each report looks at the fold again, now and a
moment later (the region can follow the hinge by a little) */
- (void)watchHinge
{
	Class class = NSClassFromString(@"UIHingeInteraction");
	__weak HaloTouchControls *weak = self;
	id interaction;

	if (hinge_watched || !class || ![class instancesRespondToSelector:@selector(initWithUpdateHandler:)])
		return;
	hinge_watched = YES;
	interaction = [(id<HaloHingeInteraction>)[class alloc] initWithUpdateHandler:^(id unused, id update) {
		[weak hingeMoved:update];
	}];
	if ([interaction conformsToProtocol:@protocol(UIInteraction)])
	{
		[self addInteraction:interaction];
		SDL_Log("touch: watching iPhone Duo's hinge");
	}
}

- (void)hingeMoved:(id)update
{
	id hinge = [update respondsToSelector:@selector(hinge)] ? [(id<HaloHingeUpdate>)update hinge] : nil;
	__weak HaloTouchControls *weak = self;

	if ([hinge respondsToSelector:@selector(angle)])
		hinge_degrees = [(id<HaloHinge>)hinge angle] * 180.0 / M_PI;
	[self foldMaybeChanged];
	dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 250 * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{
		[weak foldMaybeChanged];
	});
}

/* (UIKit runs this again when the regions it read change) */
+ (BOOL)requiresConstraintBasedLayout
{
	return YES;
}

- (void)updateConstraints
{
	[self foldMaybeChanged];
	[super updateConstraints];
}

- (CGPoint)logicalPoint:(UITouch *)touch
{
	CGPoint point = [touch locationInView:self];

	return CGPointMake((point.x - offset.x) / scale, (point.y - offset.y) / scale);
}

- (CGPoint)togglePosition
{
	return CGPointMake(logical.width / 2, 34);
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

/* whether a point is in a circle, or a pill (half_width past its radius) */
static BOOL inside(CGPoint point, CGPoint centre, CGFloat radius, CGFloat half_width)
{
	CGFloat dx = fabs(point.x - centre.x) - MAX(0.0, half_width - radius), dy = point.y - centre.y;

	dx = MAX(0.0, dx);
	return dx * dx + dy * dy <= radius * radius;
}

/* the control under a finger going down, else HELD_NONE */
- (int)hit:(CGPoint)point
{
	if (inside(point, [self togglePosition], TOGGLE_RADIUS + 6, TOGGLE_HALF_WIDTH + 6))
		return HELD_TOGGLE;
	if (!visible)
		return HELD_NONE;
	for (int index = 0; index < CONTROL_COUNT; index++)
	{
		if (index != CONTROL_STICK &&
			inside(point, position[index], controls[index].radius, controls[index].half_width))
			return index;
	}
	/* the stick takes a little more than its circle */
	if (![self held:CONTROL_STICK] &&
		inside(point, position[CONTROL_STICK], controls[CONTROL_STICK].radius * 1.28, 0))
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

/* ---------- how the controls look

Smoked glass: a dark fill the game shows through, a sheen down from the
top, a thin light rim; pressed, the glass lights up in the launcher's green.
Fire's rim is the launcher's pink (art/background.svg's neon). */

static UIColor *colour(unsigned int rgb, CGFloat alpha)
{
	return [UIColor colorWithRed:((rgb >> 16) & 0xff) / 255.0 green:((rgb >> 8) & 0xff) / 255.0
		blue:(rgb & 0xff) / 255.0 alpha:alpha];
}

#define ACCENT 0x6fd16f
#define NEON 0xff8fb0

/* the glass of one control, a circle or a pill */
static void glass(CGContextRef context, CGPoint centre, CGFloat radius, CGFloat half_width,
	BOOL active, unsigned int rim)
{
	CGFloat width = MAX(radius, half_width);
	CGRect box = CGRectMake(centre.x - width, centre.y - radius, width * 2, radius * 2);
	UIBezierPath *shape = [UIBezierPath bezierPathWithRoundedRect:box cornerRadius:radius];
	CGFloat sheen[] = { 1, 1, 1, active ? 0.22 : 0.13, 1, 1, 1, 0 };
	CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
	CGGradientRef gradient = CGGradientCreateWithColorComponents(space, sheen, NULL, 2);

	[(active ? colour(ACCENT, 0.42) : colour(0x0b1014, 0.34)) setFill];
	[shape fill];
	CGContextSaveGState(context);
	[shape addClip];
	CGContextDrawLinearGradient(context, gradient, CGPointMake(centre.x, box.origin.y),
		CGPointMake(centre.x, centre.y + radius * 0.1), 0);
	CGContextRestoreGState(context);
	CGGradientRelease(gradient);
	CGColorSpaceRelease(space);
	[(active ? colour(ACCENT, 1.0) : colour(rim, rim == 0xffffff ? 0.5 : 0.85)) setStroke];
	shape.lineWidth = rim == 0xffffff ? 1.25 : 2.0;
	[shape stroke];
}

static NSDictionary *text_style(CGFloat size, UIFontWeight weight, UIColor *colour, CGFloat kerning)
{
	return @{
		NSFontAttributeName: [UIFont systemFontOfSize:size weight:weight],
		NSForegroundColorAttributeName: colour,
		NSKernAttributeName: @(kerning),
	};
}

/* text centred on a point, made smaller to fit a width */
static void centred_text(const char *text, CGPoint centre, CGFloat size, UIFontWeight weight, UIColor *colour,
	CGFloat kerning, CGFloat fit)
{
	NSString *string = [NSString stringWithUTF8String:text];
	NSDictionary *style = text_style(size, weight, colour, kerning);
	CGSize measured = [string sizeWithAttributes:style];

	if (measured.width > fit)
	{
		style = text_style(size * fit / measured.width, weight, colour, kerning * fit / measured.width);
		measured = [string sizeWithAttributes:style];
	}
	[string drawAtPoint:CGPointMake(centre.x - measured.width / 2, centre.y - measured.height / 2) withAttributes:style];
}

/* an SF Symbol, its size points tall at most, centred on a point */
static CGFloat symbol(const char *name, CGPoint centre, CGFloat size, UIColor *colour)
{
	UIImageSymbolConfiguration *configuration =
		[UIImageSymbolConfiguration configurationWithPointSize:size weight:UIImageSymbolWeightSemibold];
	UIImage *image = [[UIImage systemImageNamed:[NSString stringWithUTF8String:name] withConfiguration:configuration]
		imageWithTintColor:colour renderingMode:UIImageRenderingModeAlwaysOriginal];

	if (!image)
		return 0;
	[image drawInRect:CGRectMake(centre.x - image.size.width / 2, centre.y - image.size.height / 2,
		image.size.width, image.size.height)];
	return image.size.width;
}

- (void)drawControl:(int)index context:(CGContextRef)context
{
	CGPoint centre = position[index];
	CGFloat radius = controls[index].radius, half_width = controls[index].half_width;
	BOOL active = [self held:index];
	UIColor *tint = colour(controls[index].tint, 0.95);

	glass(context, centre, radius, half_width, active, controls[index].trigger == 5 ? NEON : 0xffffff);
	if (half_width > radius)
	{
		/* a pill: its symbol, then its word, side by side */
		NSString *word = [NSString stringWithUTF8String:controls[index].glyph];
		NSDictionary *style = text_style(10.5, UIFontWeightBold, colour(0xffffff, 0.92), 1.2);
		CGFloat text = [word sizeWithAttributes:style].width, icon = 15, gap = 6;
		CGFloat left = centre.x - (icon + gap + text) / 2;

		symbol(controls[index].symbol, CGPointMake(left + icon / 2, centre.y), 11, tint);
		centred_text(controls[index].glyph, CGPointMake(left + icon + gap + text / 2, centre.y), 10.5,
			UIFontWeightBold, colour(0xffffff, 0.92), 1.2, half_width * 2);
		return;
	}
	if (controls[index].caption)
	{
		/* a letter or symbol over a small caption */
		CGPoint top = CGPointMake(centre.x, centre.y - radius * 0.16);

		if (controls[index].glyph)
			centred_text(controls[index].glyph, top, radius * 0.78, UIFontWeightHeavy, tint, 0, radius * 1.4);
		else
			symbol(controls[index].symbol, top, radius * 0.5, tint);
		centred_text(controls[index].caption, CGPointMake(centre.x, centre.y + radius * 0.5), 7.5,
			UIFontWeightBold, colour(0xffffff, 0.7), 0.9, radius * 1.5);
	}
	else if (controls[index].glyph)
		centred_text(controls[index].glyph, centre, radius * 0.36, UIFontWeightHeavy, tint, 1.4, radius * 1.55);
	else
		symbol(controls[index].symbol, centre, radius * 0.62, tint);
}

/* the stick: a ringed base with its caption, and the knob where it is */
- (void)drawStick:(CGContextRef)context
{
	CGPoint centre = position[CONTROL_STICK];
	CGFloat radius = controls[CONTROL_STICK].radius;
	BOOL active = [self held:CONTROL_STICK];
	CGPoint knob = CGPointMake(centre.x + stick[0] / 32767.0 * radius, centre.y + stick[1] / 32767.0 * radius);

	glass(context, centre, radius, 0, NO, 0xffffff);
	[colour(0xffffff, 0.14) setStroke];
	CGContextSetLineWidth(context, 1);
	CGContextStrokeEllipseInRect(context, CGRectInset(CGRectMake(centre.x - radius, centre.y - radius,
		radius * 2, radius * 2), radius * 0.42, radius * 0.42));
	centred_text(controls[CONTROL_STICK].caption, CGPointMake(centre.x, centre.y + radius * 0.74), 7.5,
		UIFontWeightBold, colour(0xffffff, 0.6), 1.4, radius);
	glass(context, knob, radius * 0.4, 0, active, 0xffffff);
}

- (void)drawRect:(CGRect)rect
{
	CGContextRef context = UIGraphicsGetCurrentContext();
	CGPoint toggle;

	if (![self shown])
		return;
	if (tabletop)
	{
		/* the deck below the fold: the launcher's night, and a hairline
		where the screen bends */
		CGRect deck = CGRectMake(0, CGRectGetMaxY(laid_fold), self.bounds.size.width,
			self.bounds.size.height - CGRectGetMaxY(laid_fold));

		[colour(0x0d1114, 1.0) setFill];
		UIRectFill(deck);
		[colour(0xffffff, 0.08) setFill];
		UIRectFill(CGRectMake(0, CGRectGetMaxY(laid_fold), self.bounds.size.width, 1));
	}
	CGContextSaveGState(context);
	CGContextTranslateCTM(context, offset.x, offset.y);
	CGContextScaleCTM(context, scale, scale);
	/* the hide toggle, always: dimmer while the controls are hidden */
	toggle = [self togglePosition];
	glass(context, toggle, TOGGLE_RADIUS, TOGGLE_HALF_WIDTH, NO, 0xffffff);
	symbol(visible ? "eye.slash" : "hand.tap", CGPointMake(toggle.x - 17, toggle.y), 11, colour(0xffffff, 0.9));
	centred_text(visible ? "HIDE" : "SHOW", CGPointMake(toggle.x + 9, toggle.y), 10.5, UIFontWeightBold,
		colour(0xffffff, 0.92), 1.2, 40);
	if (visible)
	{
		[self drawStick:context];
		for (int index = 0; index < CONTROL_COUNT; index++)
		{
			if (index != CONTROL_STICK)
				[self drawControl:index context:context];
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

/* the part of the window the picture is shown in (sdl_platform.c,
platform_video_present_area): the half above iPhone Duo's fold in the
laptop posture, else all of it; read each frame, on the main thread */
void host_present_area(float *area)
{
	overlay_attach();
	memcpy(area, present_area, sizeof(present_area));
}

#endif
