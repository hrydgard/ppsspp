#import "AppDelegate.h"
#import "ViewController.h"
#import "base/NativeApp.h"
#import "Core/System.h"
#import "Core/Config.h"
#import "Common/Log.h"

#import <AVFoundation/AVFoundation.h>

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
			INFO_LOG(SYSTEM, "ios audio session interruption beginning");
			if (g_Config.bEnableSound) {
				Audio_Shutdown();
			}
			break;
			
		case AVAudioSessionInterruptionTypeEnded:
			INFO_LOG(SYSTEM, "ios audio session interruption ending");
			if (g_Config.bEnableSound) {
				/* 
				 * Only try to reinit audio if in the foreground, otherwise
				 * it may fail. Instead, trust that applicationDidBecomeActive
				 * will do it later.
				 */
				if ([UIApplication sharedApplication].applicationState == UIApplicationStateActive) { 
					Audio_Init();
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
	INFO_LOG(SYSTEM, "ios media services were reset - reinitializing audio");
	
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
		Audio_Shutdown();
		Audio_Init();
	}
}

-(BOOL) application:(UIApplication *)application didFinishLaunchingWithOptions:(NSDictionary *)launchOptions {
	self.window = [[UIWindow alloc] initWithFrame:[[UIScreen mainScreen] bounds]];
	self.viewController = [[ViewController alloc] init];
	
	[[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(handleAudioSessionInterruption:) name:AVAudioSessionInterruptionNotification object:[AVAudioSession sharedInstance]];
	
	[[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(handleMediaServicesWereReset:) name:AVAudioSessionMediaServicesWereResetNotification object:nil];
	
	self.window.rootViewController = self.viewController;
	[self.window makeKeyAndVisible];
	
	return YES;
}

-(void) dealloc {
	[[NSNotificationCenter defaultCenter] removeObserver:self];
}

-(void) applicationWillResignActive:(UIApplication *)application {
	if (g_Config.bEnableSound) {
		Audio_Shutdown();
	}
	
	NativeMessageReceived("lost_focus", "");	
}

-(void) applicationDidBecomeActive:(UIApplication *)application {
	if (g_Config.bEnableSound) {
		Audio_Init();
	}
	
	NativeMessageReceived("got_focus", "");	
}

@end
