// ViewController.h boilerplate

#import <UIKit/UIKit.h>
#import <GLKit/GLKit.h>
#if __IPHONE_OS_VERSION_MAX_ALLOWED > __IPHONE_6_1
#import <GameController/GameController.h>
#endif
#import "iCade/iCadeReaderView.h"


#import "http/Reachability/Reachability.h"
#import "ios/http/FileWebServer.h"


@interface ViewController : GLKViewController <iCadeEventDelegate>

@end


void LaunchBrowser(char const* url);
void bindDefaultFBO();