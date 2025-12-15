// ViewControllerMetal
// Used by both Vulkan/MoltenVK and the future Metal backend.

#pragma once

#import "ViewControllerCommon.h"
#import "iCade/iCadeReaderView.h"

@interface PPSSPPViewControllerMetal : PPSSPPBaseViewController
@end

/** The Metal-compatibile view. */
@interface PPSSPPMetalView : UIView
@property (nonatomic, strong) CADisplayLink *displayLink;
- (void)startDisplayLink;
- (void)stopDisplayLink;
@end
