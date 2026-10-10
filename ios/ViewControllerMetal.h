// ViewControllerMetal
// Used by both Vulkan/MoltenVK and the future Metal backend.

#pragma once

#import "ViewControllerCommon.h"

@interface PPSSPPViewControllerMetal : PPSSPPBaseViewController
@end

/** The Metal-compatibile view. */
@interface PPSSPPMetalView : UIView
@end
