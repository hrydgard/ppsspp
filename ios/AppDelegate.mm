#import "AppDelegate.h"
#import "SceneDelegate.h"
#import "iOSCoreAudio.h"
#import "Common/System/System.h"
#import "Common/System/NativeApp.h"
#import "Core/System.h"
#import "Core/Config.h"
#import "Core/Util/PathUtil.h"
#import "Core/Util/RecentFiles.h"
#import "Common/Log.h"
#import "Common/File/Path.h"
#import "Common/File/DirListing.h"
#import "Common/File/FileUtil.h"
#import "IAPManager.h"

#import <AVFoundation/AVFoundation.h>
#import <objc/runtime.h>

#include <set>
#include <string>
#include <vector>

// TODO: Unfortunate hack to force link SceneDelegate class.
// This is necessary because otherwise classes from static libraries aren't loaded.
// We should move all the iOS code into the main binary directly to avoid this problem.
__attribute__((used)) static Class _forceLinkSceneDelegate = [SceneDelegate class];

@implementation AppDelegate

// This will be called when the user receives and dismisses a phone call
// or other interruption to the audio session
// Registered in application:didFinishLaunchingWithOptions:
// for AVAudioSessionInterruptionNotification
-(void) handleAudioSessionInterruption:(NSNotification *)notification {
	NSNumber *interruptionType = notification.userInfo[AVAudioSessionInterruptionTypeKey];

	// Sanity check in case it's somehow not an NSNumber
	if (![interruptionType respondsToSelector:@selector(unsignedIntegerValue)]) {
		return;  // Lets not crash
	}

	switch ([interruptionType unsignedIntegerValue]) {
		case AVAudioSessionInterruptionTypeBegan:
			INFO_LOG(Log::System, "ios audio session interruption beginning");
			if (g_Config.bEnableSound) {
				iOSCoreAudioShutdown();
			}
			break;

		case AVAudioSessionInterruptionTypeEnded:
			INFO_LOG(Log::System, "ios audio session interruption ending");
			if (g_Config.bEnableSound) {
				/*
				 * Only try to reinit audio if in the foreground, otherwise
				 * it may fail. Instead, trust that applicationDidBecomeActive
				 * will do it later.
				 */
				if ([UIApplication sharedApplication].applicationState == UIApplicationStateActive) {
					iOSCoreAudioInit();
				}
			}
			break;

		default:
			break;
	};
}

// This will be called when the iOS's shared media process was reset
// Registered in application:didFinishLaunchingWithOptions:
// for AVAudioSessionMediaServicesWereResetNotification
-(void) handleMediaServicesWereReset:(NSNotification *)notification {
	INFO_LOG(Log::System, "ios media services were reset - reinitializing audio");

	/*
	 When media services were reset, Apple recommends:
	 1) Dispose of orphaned audio objects (such as players, recorders,
	    converters, or audio queues) and create new ones
	 2) Reset any internal audio states being tracked, including all
	    properties of AVAudioSession
	 3) When appropriate, reactivate the AVAudioSession instance using the
	    setActive:error: method
	 We accomplish this by shutting down and reinitializing audio
	 */

	if (g_Config.bEnableSound) {
		iOSCoreAudioShutdown();
		iOSCoreAudioInit();
	}
}

-(BOOL) application:(UIApplication *)application didFinishLaunchingWithOptions:(NSDictionary *)launchOptions {
	self.launchOptions = launchOptions;
	// Make sure SceneDelegate class is loaded
	Class cls = objc_getClass("SceneDelegate");
	if (!cls) {
		NSLog(@"⚠️ SceneDelegate not found via objc_getClass");
	} else {
		NSLog(@"✅ SceneDelegate loaded via objc_getClass");
	}

#if PPSSPP_PLATFORM(IOS_APP_STORE)
	[IAPManager sharedIAPManager];  // Kick off the IAPManager early.
#endif  // IOS_APP_STORE

	[[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(handleAudioSessionInterruption:) name:AVAudioSessionInterruptionNotification object:[AVAudioSession sharedInstance]];
	[[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(handleMediaServicesWereReset:) name:AVAudioSessionMediaServicesWereResetNotification object:nil];

	return YES;
}

- (void)dealloc {
	[[NSNotificationCenter defaultCenter] removeObserver:self];
}

- (void)applicationWillTerminate:(UIApplication *)application {
}

// In AppDelegate.mm
- (void)processFilePath:(NSString *)path {
	// Convert NSString to std::string
	std::string cppPath([path UTF8String]);

	// Try to fixup the path, the app directory changes on every bootup.
	Path gamePath(cppPath);
	TryUpdateSavedPath(&gamePath);

	// Call the C++ function to handle the file
	System_PostUIMessage(UIMessage::REQUEST_GAME_BOOT, gamePath.ToString());
}

// Gathers the game library as a flat list of file paths. This mirrors what the
// user sees in the "Games" tab. Duplicates are
// removed while preserving order.
static std::vector<std::string> GatherGameLibrary() {
	std::vector<std::string> paths;
	std::set<std::string> seen;

	auto addPath = [&](const std::string &p) {
		if (p.empty()) {
			return;
		}
		if (seen.insert(p).second) {
			paths.push_back(p);
		}
	};

	const Path &gamesDir = g_Config.currentDirectory;
	if (!gamesDir.empty()) {
		std::vector<File::FileInfo> fileInfo;
		if (File::GetFilesInDir(gamesDir, &fileInfo, "iso:cso:chd:pbp:elf:prx:ppdmp:")) {
			for (const File::FileInfo &info : fileInfo) {
				if (info.isDirectory) {
					// Detect installed/extracted PSP game folders.
					if (File::Exists(info.fullName / "EBOOT.PBP") ||
						File::Exists(info.fullName / "PSP_GAME/SYSDIR")) {
						addPath(info.fullName.ToString());
					}
				} else {
					addPath(info.fullName.ToString());
				}
			}
		}
	}

	// Also include recently played games.
	for (const std::string &recent : g_recentFiles.GetRecentFiles()) {
		addPath(recent);
	}

	return paths;
}

- (void)exportLibraryToScheme:(NSString *)callerScheme {
	if (callerScheme.length == 0) {
		NSLog(@"exportLibraryToScheme: empty caller scheme, ignoring");
		return;
	}

	// Build a list of games. Each game's "titleId" is set to the file path so
	// the receiving app can build a "ppsspp://open?path=<path>" deep link from it.
	std::vector<std::string> library = GatherGameLibrary();

	NSMutableArray<NSDictionary *> *games = [NSMutableArray arrayWithCapacity:library.size()];
	for (const std::string &file : library) {
		NSString *path = [[NSString alloc] initWithBytes:file.data()
												  length:file.length()
												encoding:NSUTF8StringEncoding];
		if (path.length == 0) {
			continue;
		}
		// Create a human-readable title from the filename (without extension).
		NSString *titleName = [[path lastPathComponent] stringByDeletingPathExtension];
		if (titleName.length == 0) {
			titleName = [path lastPathComponent];
		}
		[games addObject:@{
			@"titleName": titleName ?: @"",
			@"titleId": path,
			@"developer": @"",
			@"version": @"",
		}];
	}

	NSError *error = nil;
	NSData *jsonData = [NSJSONSerialization dataWithJSONObject:games options:0 error:&error];
	if (!jsonData || error) {
		NSLog(@"exportLibraryToScheme: failed to serialize library: %@", error);
		return;
	}

	// base64url-encode (URL-safe alphabet, no padding) to keep it URL-safe.
	NSString *encoded = [jsonData base64EncodedStringWithOptions:0];
	encoded = [encoded stringByReplacingOccurrencesOfString:@"+" withString:@"-"];
	encoded = [encoded stringByReplacingOccurrencesOfString:@"/" withString:@"_"];
	encoded = [encoded stringByReplacingOccurrencesOfString:@"=" withString:@""];

	NSString *urlString = [NSString stringWithFormat:@"%@://ppsspp?games=%@", callerScheme, encoded];
	NSURL *returnURL = [NSURL URLWithString:urlString];
	if (!returnURL) {
		NSLog(@"exportLibraryToScheme: failed to build return URL for scheme '%@'", callerScheme);
		return;
	}

	NSLog(@"exportLibraryToScheme: returning %lu games to scheme '%@'", (unsigned long)games.count, callerScheme);
	dispatch_async(dispatch_get_main_queue(), ^{
		[[UIApplication sharedApplication] openURL:returnURL options:@{} completionHandler:nil];
	});
}

- (UIInterfaceOrientationMask)application:(UIApplication *)application supportedInterfaceOrientationsForWindow:(UIWindow *)window {
	switch (g_Config.iScreenRotation) {
	case ROTATION_LOCKED_HORIZONTAL:
		return UIInterfaceOrientationMaskLandscapeRight;
	case ROTATION_LOCKED_VERTICAL:
	case ROTATION_LOCKED_VERTICAL180:
		if (UI_USER_INTERFACE_IDIOM() == UIUserInterfaceIdiomPad) {
			// iPad supports both portrait orientations, so allow them.
			return UIInterfaceOrientationMaskPortrait | UIInterfaceOrientationMaskPortraitUpsideDown;
		}
		// We do not support reverse portrait on iOS.
		return UIInterfaceOrientationMaskPortrait;
	case ROTATION_LOCKED_HORIZONTAL180:
		return UIInterfaceOrientationMaskLandscapeLeft;
	case ROTATION_AUTO_HORIZONTAL:
		return UIInterfaceOrientationMaskLandscape;
	case ROTATION_AUTO:
	default:
		return UIInterfaceOrientationMaskAll;
	}

	return UIInterfaceOrientationMaskAll; // or at least include Portrait
}

@end

void copyDeepLinkForPath(std::string_view filePath) {
	// 1. Convert std::string_view to NSString
	// We use the length and data to avoid issues with null terminators
	NSString *pathString = [[NSString alloc] initWithBytes:filePath.data()
							length:filePath.length()
							encoding:NSUTF8StringEncoding];

	// 2. Percent-encode the path
	// This handles spaces, slashes, and special characters for the URL query
	NSCharacterSet *allowedChars = [NSCharacterSet URLQueryAllowedCharacterSet];
	NSString *encodedPath = [pathString stringByAddingPercentEncodingWithAllowedCharacters:allowedChars];

	// 3. Construct the full URL
	// Format: scheme://host?query
	NSString *deepLink = [NSString stringWithFormat:@"ppsspp://open?path=%@", encodedPath];

	// 4. Copy to the iOS System Clipboard (UIPasteboard)
	[UIPasteboard generalPasteboard].string = deepLink;

	// Optional: Log it for debugging
	NSLog(@"Deep link copied: %@", deepLink);
}
