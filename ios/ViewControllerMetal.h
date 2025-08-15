// ViewControllerMetal
// Used by both Vulkan/MoltenVK and the future Metal backend.

#pragma once
#include "ViewControllerCommon.h"

#import "iCade/iCadeReaderView.h"
#import "CameraHelper.h"
#import "LocationHelper.h"

@interface PPSSPPViewControllerMetal : UIViewController<
    iCadeEventDelegate, LocationHandlerDelegate, CameraFrameDelegate,
    UIGestureRecognizerDelegate, UIKeyInput, PPSSPPViewController,
	UIImagePickerControllerDelegate, UINavigationControllerDelegate>
@end

/** The Metal-compatibile view. */
@interface PPSSPPMetalView : UIView
@end
