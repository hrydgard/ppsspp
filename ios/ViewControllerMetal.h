// ViewControllerMetal
// Used by both Vulkan/MoltenVK and the future Metal backend.

#pragma once

#import "ViewControllerCommon.h"
#import "iCade/iCadeReaderView.h"
#import "CameraHelper.h"
#import "LocationHelper.h"

@interface PPSSPPViewControllerMetal : PPSSPPBaseViewController<
    iCadeEventDelegate, LocationHandlerDelegate, CameraFrameDelegate,
    UIGestureRecognizerDelegate, UIKeyInput,
	UIImagePickerControllerDelegate, UINavigationControllerDelegate>
@end

/** The Metal-compatibile view. */
@interface PPSSPPMetalView : UIView
@end
