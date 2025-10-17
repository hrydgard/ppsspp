#import "AppDelegate.h"
#import "SceneDelegate.h"
#import "iOSCoreAudio.h"
#import "Common/System/System.h"
#import "Common/System/NativeApp.h"
#import "Core/System.h"
#import "Core/Config.h"
#import "Common/Log.h"
#import "IAPManager.h"

#import <AVFoundation/AVFoundation.h>
#import <objc/runtime.h>

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

@end
