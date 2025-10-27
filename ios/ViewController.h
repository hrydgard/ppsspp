// ViewController.h boilerplate

#import <UIKit/UIKit.h>
#import <GLKit/GLKit.h>

#import "iCade/iCadeReaderView.h"
#import "LocationHelper.h"

#import "ViewControllerCommon.h"

@interface PPSSPPViewControllerGL : PPSSPPBaseViewController<GLKViewDelegate>

// Public-ish control similar to GLKViewController
@property (nonatomic, assign) NSInteger preferredFramesPerSecond; // default 60

@end
