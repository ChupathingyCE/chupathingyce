#import "HaloTouchControls.h"
#import <MetalKit/MetalKit.h>
#import <TouchController/TouchController.h>

// STUB. The implementation is the blade's task: see docs/SPEC.md, "Part A".

@implementation HaloTouchControls

+ (BOOL)isSupported {
    return NO;
}

- (nullable GCController *)controller {
    return nil;
}

- (BOOL)isActive {
    return NO;
}

- (void)attachToView:(UIView *)hostView {
}

- (void)detach {
}

- (void)relayout {
}

@end
