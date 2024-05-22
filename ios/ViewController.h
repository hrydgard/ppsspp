// ViewController.h boilerplate

#import <UIKit/UIKit.h>
#import <GLKit/GLKit.h>
#import <GameController/GameController.h>

#import "iCade/iCadeReaderView.h"
#import "CameraHelper.h"
#import "LocationHelper.h"

#include "ViewControllerCommon.h"

@interface PPSSPPViewControllerGL : GLKViewController <
    iCadeEventDelegate, LocationHandlerDelegate, CameraFrameDelegate,
    UIGestureRecognizerDelegate, UIKeyInput, PPSSPPViewController>
@end
