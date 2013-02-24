// AppDelegate.m boilerplate

#import "AppDelegate.h"

#import "ViewController.h"

@implementation AppDelegate

- (void)dealloc
{
	[_window release];
	[_viewController release];
	[super dealloc];
}

- (BOOL)application:(UIApplication *)application didFinishLaunchingWithOptions:(NSDictionary *)launchOptions
{
	self.window = [[[UIWindow alloc] initWithFrame:[[UIScreen mainScreen] bounds]] autorelease];
	self.viewController = [[[ViewController alloc] init] autorelease];
	self.window.rootViewController = self.viewController;
	[self.window makeKeyAndVisible];
	[self setFrameRate:60];
	return YES;
}

@end
