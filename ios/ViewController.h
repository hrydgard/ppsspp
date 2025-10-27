// ViewController.h boilerplate

#import <UIKit/UIKit.h>
#import <GLKit/GLKit.h>
#import <GameController/GameController.h>

#import "iCade/iCadeReaderView.h"
#import "CameraHelper.h"
#import "LocationHelper.h"

#include "ViewControllerCommon.h"

@interface PPSSPPViewControllerGL : UIViewController <GLKViewDelegate,
    iCadeEventDelegate, LocationHandlerDelegate, CameraFrameDelegate,
    UIGestureRecognizerDelegate, UIKeyInput, PPSSPPViewController,
	UIImagePickerControllerDelegate, UINavigationControllerDelegate>


// Public-ish control similar to GLKViewController
@property (nonatomic, assign) NSInteger preferredFramesPerSecond; // default 60

@end
