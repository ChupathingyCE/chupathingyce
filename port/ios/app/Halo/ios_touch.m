// Puts the touch controls over the game once its window and OpenGL ES view
// exist (port/linux/src/sdl_platform.c calls ios_window_created). The
// controls appear to SDL as a game controller, which xinput_sdl.c reads like
// any other pad.

#import <UIKit/UIKit.h>
#include <SDL3/SDL.h>
#import "HaloTouchControls.h"

static HaloTouchControls *touch_controls;

void ios_window_created(SDL_Window *window)
{
    UIWindow *uiwindow = (__bridge UIWindow *)SDL_GetPointerProperty(SDL_GetWindowProperties(window),
                                                                     SDL_PROP_WINDOW_UIKIT_WINDOW_POINTER, NULL);
    UIView *view = uiwindow.rootViewController.view;

    if (touch_controls || !view || !HaloTouchControls.isSupported)
        return;
    touch_controls = [[HaloTouchControls alloc] init];
    [touch_controls attachToView:view];
}
