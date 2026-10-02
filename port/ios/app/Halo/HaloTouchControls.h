// HaloTouchControls: Halo CE on-screen controls built on Apple's TouchController
// framework (iOS 26+). The overlay shows up to the game as a GCController, so
// SDL's gamepad layer reads it like any physical pad. The layout follows the
// port's gamepad mapping (port/linux/src/xinput_sdl.c); see docs/SPEC.md.
//
// This header is the contract the Halo iOS app will use. Do not change it
// without updating docs/SPEC.md.

#import <UIKit/UIKit.h>
#import <GameController/GameController.h>

NS_ASSUME_NONNULL_BEGIN

@interface HaloTouchControls : NSObject

/// NO on devices or OS versions without TouchController support.
@property (class, nonatomic, readonly, getter=isSupported) BOOL supported;

/// The virtual controller. nil until -attachToView: has run.
@property (nonatomic, readonly, nullable) GCController *controller;

/// YES while the overlay is visible and the virtual controller is connected.
/// Goes NO while a physical controller is connected, and back to YES when the
/// last physical controller disconnects.
@property (nonatomic, readonly, getter=isActive) BOOL active;

/// Adds a transparent full-screen overlay on top of `hostView`, builds the
/// Halo layout and connects the virtual controller. Call once.
- (void)attachToView:(UIView *)hostView;

/// Removes the overlay and disconnects the virtual controller.
- (void)detach;

/// Rebuilds the layout from the host view's current size and safe area.
/// Call from viewDidLayoutSubviews / viewSafeAreaInsetsDidChange.
- (void)relayout;

@end

NS_ASSUME_NONNULL_END
