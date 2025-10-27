// ViewControllerMetal
// Used by both Vulkan/MoltenVK and the future Metal backend.

#pragma once

#import "ViewControllerCommon.h"
#import "iCade/iCadeReaderView.h"

@interface PPSSPPViewControllerMetal : PPSSPPBaseViewController<
    iCadeEventDelegate,
    UIGestureRecognizerDelegate, UIKeyInput>
@end

/** The Metal-compatibile view. */
@interface PPSSPPMetalView : UIView
@end
