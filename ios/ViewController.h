// ViewController.h boilerplate

#import <UIKit/UIKit.h>
#import <GLKit/GLKit.h>
#if __IPHONE_OS_VERSION_MAX_ALLOWED > __IPHONE_6_1
#import <GameController/GameController.h>
#endif
#import "iCade/iCadeReaderView.h"
#import "CameraHelper.h"
#import "LocationHelper.h"

@protocol PPSSPPViewController<NSObject>
@optional
- (void)hideKeyboard;
- (void)showKeyboard;
- (void)shareText:(NSString *)text;
- (void)shutdown;
- (void)bindDefaultFBO;
@end

@interface PPSSPPViewControllerGL : GLKViewController <
    iCadeEventDelegate, LocationHandlerDelegate, CameraFrameDelegate,
    UIGestureRecognizerDelegate, UIKeyInput, PPSSPPViewController>
@end

extern id <PPSSPPViewController> sharedViewController;

void setCameraSize(int width, int height);
void startVideo();
void stopVideo();
void startLocation();
void stopLocation();
