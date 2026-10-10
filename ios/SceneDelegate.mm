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
#include "Core/CmdLine.h"

#import <AVFoundation/AVFoundation.h>
#include <string>
#include <vector>

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

// Detects a library-export request of the form "ppsspp://gameInfo?scheme=<callerScheme>"
// and returns the caller's URL scheme to respond to (or nil if it's not one).
static NSString *ExtractGameInfoScheme(NSURL *url) {
	if (![[url scheme] isEqualToString:@"ppsspp"]) {
		return nil;
	}
	if (![[url host] isEqualToString:@"gameInfo"]) {
		return nil;
	}

	NSURLComponents *components = [NSURLComponents componentsWithURL:url resolvingAgainstBaseURL:NO];
	for (NSURLQueryItem *item in components.queryItems) {
		if ([item.name isEqualToString:@"scheme"] && item.value.length > 0) {
			return item.value;
		}
	}

	return nil;
}

@interface SceneDelegate ()
@property (nonatomic, strong) NSString *pendingLaunchPath;
@property (nonatomic, strong) NSString *pendingExportScheme;
@end

@implementation SceneDelegate

- (void)capturePendingPathFromURLContexts:(NSSet<UIOpenURLContext *> *)URLContexts {
	for (UIOpenURLContext *context in URLContexts) {
		NSString *exportScheme = ExtractGameInfoScheme(context.URL);
		if (exportScheme.length > 0) {
			// The library export is performed after PPSSPP has initialized (and loaded its config)
			self.pendingExportScheme = exportScheme;
			NSLog(@"SceneDelegate: captured cold-start gameInfo export scheme: %@", exportScheme);
			continue;
		}

		NSString *path = ExtractDeepLinkPath(context.URL);
		if (path.length > 0) {
			self.pendingLaunchPath = path;
			NSLog(@"SceneDelegate: captured cold-start deep link path: %@", self.pendingLaunchPath);
		}
	}
}

- (void)handleURLContextsWhileRunning:(NSSet<UIOpenURLContext *> *)URLContexts {
	AppDelegate *appDelegate = (AppDelegate *)[UIApplication sharedApplication].delegate;
	for (UIOpenURLContext *context in URLContexts) {
		NSLog(@"SceneDelegate: openURLContexts called with URL: %@", context.URL);

		NSString *exportScheme = ExtractGameInfoScheme(context.URL);
		if (exportScheme.length > 0) {
			[appDelegate exportLibraryToScheme:exportScheme];
			continue;
		}

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
	[self launchPPSSPP:""];
}

-(void) launchPPSSPP:(const std::string &)extraArgs {
	INFO_LOG(Log::G3D, "SceneDelegate: Launching PPSSPP");
	AppDelegate *appDelegate = (AppDelegate *)[UIApplication sharedApplication].delegate;
	NSDictionary *launchOptions = appDelegate.launchOptions;

	// argv[0] is never looked at.
	std::vector<std::string> args{"ppsspp"};
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
		args.push_back(gamePath.ToString());
		NSLog(@"SceneDelegate: startup path passed to argv: %s", args.back().c_str());
	}

	// From a restart, like --start-screen=touchscreentest.
	for (const std::string &arg : SplitCommandLine(extraArgs)) {
		args.push_back(arg);
	}

	std::vector<const char *> argv;
	for (const std::string &arg : args) {
		argv.push_back(arg.c_str());
	}
	const int argc = (int)argv.size();

	CommandLineOptions cmdLineOptions;
	CommandLineParseResult parseResult = cmdLineOptions.Parse(argc, argv.data());
	switch (parseResult) {
	case CommandLineParseResult::Exit:
		INFO_LOG(Log::System, "Command line parse said to exit - mobile, so ignoring.");
		break;
	case CommandLineParseResult::Error:
		INFO_LOG(Log::System, "Command line parse reported error - mobile, so ignoring.");
		break;
	default:
		// Continue with launch.
		break;
	}

	NSString *documentsPath = [NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES) objectAtIndex:0];
	NSString *bundlePath = [[[NSBundle mainBundle] resourcePath] stringByAppendingString:@"/assets/"];
	// Caches the system may clear when space runs low, and the zips ShareFilesAsZip hands to other apps.
	NSString *cachesPath = [NSSearchPathForDirectoriesInDomains(NSCachesDirectory, NSUserDomainMask, YES) objectAtIndex:0];
	NativeInit(argc, argv.data(), cmdLineOptions, documentsPath.UTF8String, bundlePath.UTF8String, cachesPath.UTF8String);

	// If we were cold-started by a library export request, serve it now that
	// the config (and recent games list) has been loaded by NativeInit.
	if (self.pendingExportScheme.length > 0) {
		NSString *exportScheme = self.pendingExportScheme;
		self.pendingExportScheme = nil;
		[appDelegate exportLibraryToScheme:exportScheme];
	}

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
	[self launchPPSSPP:restartArgs];

	// Notify new view controller
	[sharedViewController didBecomeActive];
}

- (void)scene:(UIScene *)scene openURLContexts:(NSSet<UIOpenURLContext *> *)URLContexts {
	[self handleURLContextsWhileRunning:URLContexts];
}

- (void)sceneWillResignActive:(UIScene *)scene {
	INFO_LOG(Log::G3D, "sceneWillResignActive");

	[sharedViewController willResignActive];

	iOSCoreAudioShutdown();

	System_PostUIMessage(UIMessage::LOST_FOCUS);
}

- (void)sceneDidBecomeActive:(UIScene *)scene {
	INFO_LOG(Log::G3D, "sceneDidBecomeActive");

	iOSCoreAudioInit();

	System_PostUIMessage(UIMessage::GOT_FOCUS);

	[sharedViewController didBecomeActive];
}
@end
