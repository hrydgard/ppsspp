// SceneDelegate.mm

#import "SceneDelegate.h"
#import "AppDelegate.h"

#import "iOSCoreAudio.h"
#import "ViewControllerCommon.h"
#import "ViewController.h"
#import "ViewControllerMetal.h"
#import "Common/System/NativeApp.h"
#import "Common/System/System.h"
#import "Core/System.h"
#import "Core/Config.h"
#import "Common/Log.h"
#import "IAPManager.h"

#import <AVFoundation/AVFoundation.h>

@implementation SceneDelegate

+ (void)load {
	NSLog(@"✅ SceneDelegate class was loaded!");
}

- (void)scene:(UIScene *)scene willConnectToSession:(UISceneSession *)session options:(UISceneConnectionOptions *)connectionOptions {
	if (![scene isKindOfClass:[UIWindowScene class]]) {
		return;
	}

	UIWindowScene *windowScene = (UIWindowScene *)scene;
	if (self.window) {
		NSLog(@"✅ Window already exists, not creating again!");
		return;
	}
	self.windowScene = windowScene;
	[self launchPPSSPP];
}

-(void) launchPPSSPP {
	INFO_LOG(Log::G3D, "SceneDelegate: Launching PPSSPP");
	AppDelegate *appDelegate = (AppDelegate *)[UIApplication sharedApplication].delegate;
	NSDictionary *launchOptions = appDelegate.launchOptions;

	int argc = 1;
	char *argv[5]{};
	NSURL *nsUrl = [launchOptions objectForKey:UIApplicationLaunchOptionsURLKey];

	if (nsUrl != nullptr && nsUrl.isFileURL) {
		NSString *nsString = nsUrl.path;
		const char *string = nsString.UTF8String;
		argv[argc++] = (char*)string;
	}

	NSString *documentsPath = [NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES) objectAtIndex:0];
	NSString *bundlePath = [[[NSBundle mainBundle] resourcePath] stringByAppendingString:@"/assets/"];
	NativeInit(argc, (const char**)argv, documentsPath.UTF8String, bundlePath.UTF8String, NULL);

	self.window = [[UIWindow alloc] initWithWindowScene:self.windowScene];
//	self.window = [[UIWindow alloc] initWithFrame:[[UIScreen mainScreen] bounds]];

	// Choose viewcontroller depending on backend.
	if (g_Config.iGPUBackend == (int)GPUBackend::VULKAN) {
		PPSSPPViewControllerMetal *vc = [[PPSSPPViewControllerMetal alloc] init];
		// sharedViewController gets initialized in the constructor.
		self.window.rootViewController = vc;

	} else {
		PPSSPPViewControllerGL *vc = [[PPSSPPViewControllerGL alloc] init];
		// Here we can switch viewcontroller depending on backend.
		self.window.rootViewController = vc;
	}

	[self.window makeKeyAndVisible];
}

- (void)restart:(const char*)restartArgs {
	INFO_LOG(Log::G3D, "SceneDelegate: Restart requested: %s", restartArgs);

	// Notify current view controller
	[sharedViewController willResignActive];
	[sharedViewController shutdown];

	// Remove the current root view controller
	self.window.rootViewController = nil;

	INFO_LOG(Log::G3D, "SceneDelegate: viewController nilled");

	// Shut down native systems
	NativeShutdown();

	// Launch a fresh instance
	[self launchPPSSPP];

	// Notify new view controller
	[sharedViewController didBecomeActive];
}

- (void)sceneWillResignActive:(UIScene *)scene {
	INFO_LOG(Log::G3D, "sceneWillResignActive");

	[sharedViewController willResignActive];

	if (g_Config.bEnableSound) {
		iOSCoreAudioShutdown();
	}

	System_PostUIMessage(UIMessage::LOST_FOCUS);
}

- (void)sceneDidBecomeActive:(UIScene *)scene {
	INFO_LOG(Log::G3D, "sceneDidBecomeActive");

	if (g_Config.bEnableSound) {
		iOSCoreAudioInit();
	}

	System_PostUIMessage(UIMessage::GOT_FOCUS);

	[sharedViewController didBecomeActive];
}
@end
