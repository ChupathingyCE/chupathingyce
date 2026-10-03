// Puts the touch controls over the game once its window and OpenGL ES view
// exist (port/linux/src/sdl_platform.c calls ios_window_created). The
// controls appear to SDL as a game controller, which xinput_sdl.c reads like
// any other pad; their right stick looks through ios_look.m instead. A gear
// button at the top opens the look settings.

#import <UIKit/UIKit.h>
#include <SDL3/SDL.h>
#import "HaloTouchControls.h"

void ios_look_present_settings(UIViewController *presenter);   // ios_look.m

static HaloTouchControls *touch_controls;

HaloTouchControls *ios_touch_controls(void)
{
    return touch_controls;
}

@interface HaloSettingsButtonTarget : NSObject
@end

@implementation HaloSettingsButtonTarget
- (void)open:(UIButton *)sender
{
    ios_look_present_settings(sender.window.rootViewController);
}
@end

static HaloSettingsButtonTarget *settings_target;

static void add_settings_button(UIView *view)
{
    UIButton *gear = [UIButton buttonWithType:UIButtonTypeSystem];
    UIImageSymbolConfiguration *size = [UIImageSymbolConfiguration configurationWithPointSize:17 weight:UIImageSymbolWeightSemibold];
    [gear setImage:[UIImage systemImageNamed:@"gearshape.fill" withConfiguration:size] forState:UIControlStateNormal];
    gear.tintColor = [UIColor colorWithWhite:1.0 alpha:0.85];
    gear.backgroundColor = [UIColor colorWithWhite:0.0 alpha:0.35];
    gear.layer.cornerRadius = 20;
    gear.frame = CGRectMake(0, 0, 40, 40);
    // top center, between the corner buttons (HaloTouchControls.m's table)
    gear.center = CGPointMake(CGRectGetMidX(view.bounds), view.safeAreaInsets.top + 28);
    gear.autoresizingMask = UIViewAutoresizingFlexibleLeftMargin | UIViewAutoresizingFlexibleRightMargin |
                            UIViewAutoresizingFlexibleBottomMargin;
    gear.accessibilityLabel = @"Touch control settings";
    settings_target = [[HaloSettingsButtonTarget alloc] init];
    [gear addTarget:settings_target action:@selector(open:) forControlEvents:UIControlEventTouchUpInside];
    [view addSubview:gear];   // above the touch overlay, so it gets its own taps
}

void ios_window_created(SDL_Window *window)
{
    UIWindow *uiwindow = (__bridge UIWindow *)SDL_GetPointerProperty(SDL_GetWindowProperties(window),
                                                                     SDL_PROP_WINDOW_UIKIT_WINDOW_POINTER, NULL);
    UIView *view = uiwindow.rootViewController.view;
    static BOOL attached;

    if (attached || !view)
        return;
    attached = YES;
    if (HaloTouchControls.isSupported) {
        touch_controls = [[HaloTouchControls alloc] init];
        [touch_controls attachToView:view];
    }
    add_settings_button(view);
}
