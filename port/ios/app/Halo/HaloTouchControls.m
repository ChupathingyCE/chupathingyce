// HaloTouchControls.m — Part A: on-screen touch controls built on Apple's
// TouchController framework. Contract: HaloTouchControls.h. Spec: docs/SPEC.md.
//
// Every layout number lives in kHTCLayout below so it can be tuned in one
// place after testing on the phone.

#import "HaloTouchControls.h"
#import <MetalKit/MetalKit.h>
#import <TouchController/TouchController.h>

#pragma mark - Layout table

typedef NS_ENUM(NSInteger, HTCControlKind) {
    HTCControlKindThumbstick,
    HTCControlKindTouchpad,
    HTCControlKindButton,
};

// TCControlLabel class properties are not compile-time constants, so the
// table stores a stable id and -labelForId: resolves it.
typedef NS_ENUM(NSInteger, HTCLabelId) {
    HTCLabelLeftThumbstick,
    HTCLabelRightThumbstick,
    HTCLabelButtonA,
    HTCLabelButtonB,
    HTCLabelButtonX,
    HTCLabelButtonY,
    HTCLabelButtonMenu,
    HTCLabelButtonOptions,
    HTCLabelButtonLeftShoulder,
    HTCLabelButtonRightShoulder,
    HTCLabelButtonLeftTrigger,
    HTCLabelButtonRightTrigger,
    HTCLabelLeftThumbstickButton,
    HTCLabelRightThumbstickButton,
};

typedef struct {
    HTCControlKind kind;
    HTCLabelId label;
    TCControlLayoutAnchor anchor;
    CGPoint offset;            // screen coords from anchor: +x right, +y down
    CGSize size;
    CGSize stickSize;          // thumbstick only
    NSInteger zIndex;
    TCColliderShape colliderShape;
    BOOL hidesWhenNotPressed;  // thumbstick only
    BOOL reportsRelativeValues; // touchpad only
    const char *symbol;        // SF Symbol name, buttons only
} HTCControlLayout;

static const HTCControlLayout kHTCLayout[] = {
    { HTCControlKindThumbstick, HTCLabelLeftThumbstick,
      TCControlLayoutAnchorBottomLeft, {150, -130}, {140, 140}, {60, 60},
      1, TCColliderShapeLeftSide, YES, NO, NULL },
    // dual sticks, as Halo is played on a pad: the right stick looks, and
    // appears wherever the right thumb lands outside the buttons
    { HTCControlKindThumbstick, HTCLabelRightThumbstick,
      TCControlLayoutAnchorBottomRight, {-300, -130}, {140, 140}, {60, 60},
      1, TCColliderShapeRightSide, YES, NO, NULL },
    { HTCControlKindButton, HTCLabelButtonA,
      TCControlLayoutAnchorBottomRight, {-70, -70}, {64, 64}, {0, 0},
      10, TCColliderShapeCircle, NO, NO, "arrow.up" },
    { HTCControlKindButton, HTCLabelButtonB,
      TCControlLayoutAnchorBottomRight, {-150, -55}, {56, 56}, {0, 0},
      10, TCColliderShapeCircle, NO, NO, "hand.raised.fill" },
    { HTCControlKindButton, HTCLabelButtonX,
      TCControlLayoutAnchorBottomRight, {-225, -50}, {52, 52}, {0, 0},
      10, TCColliderShapeCircle, NO, NO, "arrow.clockwise" },
    { HTCControlKindButton, HTCLabelButtonRightTrigger,
      TCControlLayoutAnchorBottomRight, {-95, -175}, {84, 84}, {0, 0},
      10, TCColliderShapeCircle, NO, NO, "scope" },
    { HTCControlKindButton, HTCLabelButtonLeftTrigger,
      TCControlLayoutAnchorBottomRight, {-195, -150}, {52, 52}, {0, 0},
      10, TCColliderShapeCircle, NO, NO, "flame.fill" },
    { HTCControlKindButton, HTCLabelButtonY,
      TCControlLayoutAnchorBottomRight, {-60, -265}, {50, 50}, {0, 0},
      10, TCColliderShapeCircle, NO, NO, "arrow.left.arrow.right" },
    { HTCControlKindButton, HTCLabelRightThumbstickButton,
      TCControlLayoutAnchorBottomRight, {-280, -130}, {48, 48}, {0, 0},
      10, TCColliderShapeCircle, NO, NO, "plus.magnifyingglass" },
    { HTCControlKindButton, HTCLabelLeftThumbstickButton,
      TCControlLayoutAnchorBottomLeft, {300, -50}, {52, 52}, {0, 0},
      10, TCColliderShapeCircle, NO, NO, "chevron.down" },
    { HTCControlKindButton, HTCLabelButtonMenu,
      TCControlLayoutAnchorTopRight, {-50, 40}, {40, 40}, {0, 0},
      10, TCColliderShapeCircle, NO, NO, "pause.fill" },
    { HTCControlKindButton, HTCLabelButtonRightShoulder,
      TCControlLayoutAnchorTopRight, {-130, 40}, {44, 44}, {0, 0},
      10, TCColliderShapeCircle, NO, NO, "arrow.triangle.2.circlepath" },
    { HTCControlKindButton, HTCLabelButtonOptions,
      TCControlLayoutAnchorTopLeft, {50, 40}, {40, 40}, {0, 0},
      10, TCColliderShapeCircle, NO, NO, "chevron.backward" },
    { HTCControlKindButton, HTCLabelButtonLeftShoulder,
      TCControlLayoutAnchorTopLeft, {130, 40}, {44, 44}, {0, 0},
      10, TCColliderShapeCircle, NO, NO, "flashlight.on.fill" },
};
static const size_t kHTCLayoutCount = sizeof(kHTCLayout) / sizeof(kHTCLayout[0]);

#pragma mark - HaloTouchControls (class extension, before the overlay so the
// overlay can see `tc`)

@class HTCOverlayView;

@interface HaloTouchControls () <MTKViewDelegate>
@property (nonatomic, strong) HTCOverlayView *overlay;
@property (nonatomic, strong) TCTouchController *tc;
@property (nonatomic, strong) id<MTLCommandQueue> commandQueue;
@property (nonatomic, readwrite) BOOL active;
@end

#pragma mark - Overlay view

@interface HTCOverlayView : MTKView
@property (nonatomic, weak) HaloTouchControls *owner;
@property (nonatomic, strong) NSMutableDictionary<NSValue *, NSNumber *> *touchIndex;
@end

@implementation HTCOverlayView

- (instancetype)initWithFrame:(CGRect)frame {
    self = [super initWithFrame:frame];
    if (self) {
        _touchIndex = [NSMutableDictionary dictionary];
    }
    return self;
}

- (NSInteger)lowestFreeIndex {
    for (NSInteger i = 0; i < 10; i++) {
        BOOL taken = NO;
        for (NSNumber *v in self.touchIndex.objectEnumerator) {
            if (v.integerValue == i) { taken = YES; break; }
        }
        if (!taken) return i;
    }
    return -1;
}

- (void)touchesBegan:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    if (!self.owner.tc) return;
    for (UITouch *touch in touches) {
        NSValue *key = [NSValue valueWithNonretainedObject:touch];
        if (self.touchIndex[key]) continue;
        NSInteger index = [self lowestFreeIndex];
        if (index < 0) continue;
        self.touchIndex[key] = @(index);
        CGPoint p = [touch locationInView:self];
        [self.owner.tc handleTouchBeganAtPoint:p index:index];
    }
}

- (void)touchesMoved:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    if (!self.owner.tc) return;
    for (UITouch *touch in touches) {
        NSValue *key = [NSValue valueWithNonretainedObject:touch];
        NSNumber *idx = self.touchIndex[key];
        if (!idx) continue;
        CGPoint p = [touch locationInView:self];
        [self.owner.tc handleTouchMovedAtPoint:p index:idx.integerValue];
    }
}

- (void)touchesEnded:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    if (!self.owner.tc) return;
    for (UITouch *touch in touches) {
        NSValue *key = [NSValue valueWithNonretainedObject:touch];
        NSNumber *idx = self.touchIndex[key];
        if (!idx) continue;
        CGPoint p = [touch locationInView:self];
        [self.owner.tc handleTouchEndedAtPoint:p index:idx.integerValue];
        [self.touchIndex removeObjectForKey:key];
    }
}

- (void)touchesCancelled:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    [self touchesEnded:touches withEvent:event];
}

@end

#pragma mark - HaloTouchControls

@implementation HaloTouchControls

+ (BOOL)isSupported {
    return TCTouchController.isSupported;
}

- (nullable GCController *)controller {
    return self.tc.controller;
}

#pragma mark Attach / detach

- (void)attachToView:(UIView *)hostView {
    if (![HaloTouchControls isSupported]) {
        NSLog(@"HaloTouchControls: TouchController is not supported on this device/OS");
        return;
    }

    HTCOverlayView *overlay = [[HTCOverlayView alloc] initWithFrame:hostView.bounds];
    overlay.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    overlay.multipleTouchEnabled = YES;
    overlay.opaque = NO;
    overlay.backgroundColor = [UIColor clearColor];
    overlay.clearColor = MTLClearColorMake(0, 0, 0, 0);
    overlay.layer.opaque = NO;
    overlay.device = MTLCreateSystemDefaultDevice();
    overlay.colorPixelFormat = MTLPixelFormatBGRA8Unorm;
    overlay.preferredFramesPerSecond = 60;
    overlay.owner = self;
    overlay.delegate = self;
    [hostView addSubview:overlay];
    self.overlay = overlay;

    TCTouchControllerDescriptor *d = [[TCTouchControllerDescriptor alloc] initWithMTKView:overlay];
    self.tc = [[TCTouchController alloc] initWithDescriptor:d];

    [self relayout];
    [self applyPhysicalControllerRule];

    [[NSNotificationCenter defaultCenter] addObserver:self
                                             selector:@selector(controllerConnectNotification:)
                                                 name:GCControllerDidConnectNotification
                                               object:nil];
    [[NSNotificationCenter defaultCenter] addObserver:self
                                             selector:@selector(controllerDisconnectNotification:)
                                                 name:GCControllerDidDisconnectNotification
                                               object:nil];
}

- (void)detach {
    [[NSNotificationCenter defaultCenter] removeObserver:self];
    if (self.tc) [self.tc disconnect];
    [self.overlay removeFromSuperview];
    self.overlay = nil;
    self.tc = nil;
    self.commandQueue = nil;
    self.active = NO;
}

#pragma mark Layout

- (TCControlLabel *)labelForId:(HTCLabelId)id {
    switch (id) {
        case HTCLabelLeftThumbstick:        return TCControlLabel.leftThumbstick;
        case HTCLabelRightThumbstick:       return TCControlLabel.rightThumbstick;
        case HTCLabelButtonA:               return TCControlLabel.buttonA;
        case HTCLabelButtonB:               return TCControlLabel.buttonB;
        case HTCLabelButtonX:               return TCControlLabel.buttonX;
        case HTCLabelButtonY:               return TCControlLabel.buttonY;
        case HTCLabelButtonMenu:            return TCControlLabel.buttonMenu;
        case HTCLabelButtonOptions:         return TCControlLabel.buttonOptions;
        case HTCLabelButtonLeftShoulder:    return TCControlLabel.buttonLeftShoulder;
        case HTCLabelButtonRightShoulder:   return TCControlLabel.buttonRightShoulder;
        case HTCLabelButtonLeftTrigger:     return TCControlLabel.buttonLeftTrigger;
        case HTCLabelButtonRightTrigger:    return TCControlLabel.buttonRightTrigger;
        case HTCLabelLeftThumbstickButton:  return TCControlLabel.leftThumbstickButton;
        case HTCLabelRightThumbstickButton: return TCControlLabel.rightThumbstickButton;
    }
    return nil;
}

- (void)relayout {
    if (!self.tc || !self.overlay) return;
    // the descriptor leaves the drawable size at 0x0 (nothing gets drawn), and
    // MTKView only reports a size change, not the size it starts with
    self.tc.size = self.overlay.bounds.size;
    self.tc.drawableSize = self.overlay.drawableSize;
    [self.tc removeAllControls];

    UIEdgeInsets safe = self.overlay.safeAreaInsets;
    for (size_t i = 0; i < kHTCLayoutCount; i++) {
        const HTCControlLayout *e = &kHTCLayout[i];
        CGPoint offset = e->offset;
        switch (e->anchor) {
            case TCControlLayoutAnchorBottomRight:
                offset.x -= safe.right;
                offset.y -= safe.bottom;
                break;
            case TCControlLayoutAnchorBottomLeft:
                offset.x += safe.left;
                offset.y -= safe.bottom;
                break;
            case TCControlLayoutAnchorTopRight:
                offset.x -= safe.right;
                offset.y += safe.top;
                break;
            case TCControlLayoutAnchorTopLeft:
                offset.x += safe.left;
                offset.y += safe.top;
                break;
            default:
                break; // Center (and any future anchors): no safe-area push
        }

        TCControlLabel *label = [self labelForId:e->label];

        if (e->kind == HTCControlKindThumbstick) {
            TCThumbstickDescriptor *d = [[TCThumbstickDescriptor alloc] init];
            d.label = label;
            d.anchor = e->anchor;
            d.anchorCoordinateSystem = TCControlLayoutAnchorCoordinateSystemRelative;
            d.offset = offset;
            d.size = e->size;
            d.stickSize = e->stickSize;
            d.zIndex = e->zIndex;
            d.colliderShape = e->colliderShape;
            d.hidesWhenNotPressed = e->hidesWhenNotPressed;
            d.backgroundContents = [TCControlContents thumbstickBackgroundContentsOfSize:e->size
                                                                              controller:self.tc];
            d.stickContents = [TCControlContents thumbstickStickContentsOfSize:e->stickSize
                                                                    controller:self.tc];
            [self.tc addThumbstickWithDescriptor:d];
        } else if (e->kind == HTCControlKindTouchpad) {
            TCTouchpadDescriptor *d = [[TCTouchpadDescriptor alloc] init];
            d.label = label;
            d.anchor = e->anchor;
            d.anchorCoordinateSystem = TCControlLayoutAnchorCoordinateSystemRelative;
            d.offset = offset;
            d.size = self.overlay.bounds.size;
            d.zIndex = e->zIndex;
            d.colliderShape = e->colliderShape;
            d.reportsRelativeValues = e->reportsRelativeValues;
            // no contents: invisible, right-half collider only
            [self.tc addTouchpadWithDescriptor:d];
        } else {
            TCButtonDescriptor *d = [[TCButtonDescriptor alloc] init];
            d.label = label;
            d.anchor = e->anchor;
            d.anchorCoordinateSystem = TCControlLayoutAnchorCoordinateSystemRelative;
            d.offset = offset;
            d.size = e->size;
            d.zIndex = e->zIndex;
            d.colliderShape = e->colliderShape;
            NSString *symbol = e->symbol ? [NSString stringWithUTF8String:e->symbol] : nil;
            if (symbol && ![UIImage systemImageNamed:symbol]) {
                symbol = @"circle.fill";
            }
            d.contents = [TCControlContents buttonContentsForSystemImageNamed:symbol
                                                                         size:e->size
                                                                        shape:TCControlContentsButtonShapeCircle
                                                                   controller:self.tc];
            [self.tc addButtonWithDescriptor:d];
        }
    }
    fprintf(stderr, "HTC relayout: overlay %.0fx%.0f drawable %.0fx%.0f tc.size %.0fx%.0f tc.drawable %.0fx%.0f safe l%.0f r%.0f b%.0f controls %lu\n",
            self.overlay.bounds.size.width, self.overlay.bounds.size.height,
            self.overlay.drawableSize.width, self.overlay.drawableSize.height,
            self.tc.size.width, self.tc.size.height, self.tc.drawableSize.width, self.tc.drawableSize.height,
            safe.left, safe.right, safe.bottom, (unsigned long)self.tc.controls.count);
    for (id<TCControl> c in self.tc.controls) {
        if ([(id)c conformsToProtocol:@protocol(TCControlLayout)]) {
            id<TCControlLayout> l = (id<TCControlLayout>)c;
            fprintf(stderr, "HTC   %-22s pos %.0f,%.0f size %.0fx%.0f enabled %d\n", c.label.name.UTF8String,
                    l.position.x, l.position.y, l.size.width, l.size.height, c.enabled);
        }
    }
}

#pragma mark Physical controllers

- (BOOL)hasPhysicalController {
    GCController *virtualPad = self.tc.controller;
    for (GCController *c in GCController.controllers) {
        if (c == virtualPad || c.extendedGamepad == nil) continue;
        // (in case iOS lists the touch pad as a different object than the
        // one TouchController hands back: same name means it is ours)
        if (virtualPad.vendorName && [c.vendorName isEqualToString:virtualPad.vendorName]) continue;
        return YES;
    }
    return NO;
}

- (void)applyPhysicalControllerRule {
    if (!self.tc || !self.overlay) return;
    if ([self hasPhysicalController]) {
        [self.tc disconnect];
        self.overlay.hidden = YES;
        self.overlay.paused = YES;
        self.active = NO;
    } else {
        [self.tc connect];
        self.overlay.hidden = NO;
        self.overlay.paused = NO;
        self.active = YES;
    }
}

- (void)controllerConnectNotification:(NSNotification *)note {
    [self applyPhysicalControllerRule];
}

- (void)controllerDisconnectNotification:(NSNotification *)note {
    [self applyPhysicalControllerRule];
}

#pragma mark MTKViewDelegate

- (void)mtkView:(MTKView *)view drawableSizeWillChange:(CGSize)size {
    self.tc.size = view.bounds.size;
    self.tc.drawableSize = size;
    [self relayout];
}

- (void)drawInMTKView:(MTKView *)view {
    if (!self.tc) return;
    if (!self.commandQueue) {
        self.commandQueue = [view.device newCommandQueue];
    }
    static int frames;
    MTLRenderPassDescriptor *rp = view.currentRenderPassDescriptor;
    if (frames < 3 || frames % 600 == 0)
        fprintf(stderr, "HTC draw %d: pass %s drawable %.0fx%.0f hidden %d paused %d\n", frames, rp ? "yes" : "NO",
                view.drawableSize.width, view.drawableSize.height, view.hidden, view.paused);
    frames++;
    if (!rp) return;
    if (!CGSizeEqualToSize(self.tc.drawableSize, view.drawableSize)) {
        self.tc.size = view.bounds.size;
        self.tc.drawableSize = view.drawableSize;
    }
    id<MTLCommandBuffer> buffer = [self.commandQueue commandBuffer];
    id<MTLRenderCommandEncoder> enc = [buffer renderCommandEncoderWithDescriptor:rp];
    [self.tc renderUsingRenderCommandEncoder:enc];
    [enc endEncoding];
    [buffer presentDrawable:view.currentDrawable];
    [buffer commit];
}

@end