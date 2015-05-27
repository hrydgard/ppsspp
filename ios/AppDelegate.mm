#import "AppDelegate.h"
#import "ViewController.h"
#import "base/NativeApp.h"

@implementation AppDelegate
-(BOOL) application:(UIApplication *)application didFinishLaunchingWithOptions:(NSDictionary *)launchOptions {
	self.window = [[UIWindow alloc] initWithFrame:[[UIScreen mainScreen] bounds]];
	self.viewController = [[ViewController alloc] init];
	self.window.rootViewController = self.viewController;
	[self.window makeKeyAndVisible];
	return YES;
}

-(void) applicationWillResignActive:(UIApplication *)application {
	NativeMessageReceived("lost_focus", "");	
}

-(void) applicationDidBecomeActive:(UIApplication *)application {
	NativeMessageReceived("got_focus", "");	
}
@end