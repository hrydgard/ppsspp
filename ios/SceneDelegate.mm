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
#include "Core/Util/PathUtil.h"

#import <AVFoundation/AVFoundation.h>
#include <string>

static std::string gStartupArgStorage;

static NSString *ExtractDeepLinkPath(NSURL *url) {
	if (![[url scheme] isEqualToString:@"ppsspp"]) {
		return nil;
	}

	NSURLComponents *components = [NSURLComponents componentsWithURL:url resolvingAgainstBaseURL:NO];
	for (NSURLQueryItem *item in components.queryItems) {
		if ([item.name isEqualToString:@"path"] && item.value.length > 0) {
			return item.value;
		}
	}

	return nil;
}

@interface SceneDelegate ()
@property (nonatomic, strong) NSString *pendingLaunchPath;
@end

@implementation SceneDelegate

+ (void)load {
	NSLog(@"✅ SceneDelegate class was loaded!");
}

- (void)capturePendingPathFromURLContexts:(NSSet<UIOpenURLContext *> *)URLContexts {
	for (UIOpenURLContext *context in URLContexts) {
		NSString *path = ExtractDeepLinkPath(context.URL);
		if (path.length > 0) {
			self.pendingLaunchPath = path;
			NSLog(@"SceneDelegate: captured cold-start deep link path: %@", self.pendingLaunchPath);
			break;
		}
	}
}

- (void)handleURLContextsWhileRunning:(NSSet<UIOpenURLContext *> *)URLContexts {
	AppDelegate *appDelegate = (AppDelegate *)[UIApplication sharedApplication].delegate;
	for (UIOpenURLContext *context in URLContexts) {
		NSLog(@"SceneDelegate: openURLContexts called with URL: %@", context.URL);
		NSString *path = ExtractDeepLinkPath(context.URL);
		if (path.length > 0) {
			[appDelegate processFilePath:path];
		} else {
			NSLog(@"SceneDelegate: ignoring deep link URL (invalid scheme or missing path): %@", context.URL);
		}
	}
}

- (void)scene:(UIScene *)scene willConnectToSession:(UISceneSession *)session options:(UISceneConnectionOptions *)connectionOptions {
	if (![scene isKindOfClass:[UIWindowScene class]]) {
		return;
	}

	[self capturePendingPathFromURLContexts:connectionOptions.URLContexts];

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
	NSString *startupPath = nil;
	NSURL *nsUrl = [launchOptions objectForKey:UIApplicationLaunchOptionsURLKey];

	if (self.pendingLaunchPath.length > 0) {
		startupPath = self.pendingLaunchPath;
		self.pendingLaunchPath = nil;
	}

	if (startupPath == nil && nsUrl != nullptr && nsUrl.isFileURL) {
		NSString *nsString = nsUrl.path;
		startupPath = nsString;
	}

	if (startupPath.length > 0) {
		// Keep cold-start argv behavior aligned with processFilePath().
		std::string startupPathUtf8(startupPath.UTF8String);
		Path gamePath(startupPathUtf8);
		gStartupArgStorage = gamePath.ToString();
		argv[argc++] = (char *)gStartupArgStorage.c_str();
		NSLog(@"SceneDelegate: startup path passed to argv: %s", gStartupArgStorage.c_str());
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

- (void)scene:(UIScene *)scene openURLContexts:(NSSet<UIOpenURLContext *> *)URLContexts {
	[self handleURLContextsWhileRunning:URLContexts];
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
