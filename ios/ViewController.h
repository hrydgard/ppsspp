// ViewController.h boilerplate

#import <UIKit/UIKit.h>
#import <GLKit/GLKit.h>
#if __IPHONE_OS_VERSION_MAX_ALLOWED > __IPHONE_6_1
#import <GameController/GameController.h>
#endif

#import <string>
#import "iCade/iCadeReaderView.h"

#import "ios/http/FileWebServer.h"


FileWebServer *fileWebServer;

@interface ViewController : GLKViewController <iCadeEventDelegate>

@end


void bindDefaultFBO();