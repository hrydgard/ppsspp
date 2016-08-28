// main.mm boilerplate

#import <UIKit/UIKit.h>
#import <string>
#import <stdio.h>
#import <stdlib.h>
#import <sys/syscall.h>
#import <AudioToolbox/AudioToolbox.h>

#import "AppDelegate.h"
#import <AudioToolbox/AudioToolbox.h>

#include "base/NativeApp.h"

@interface UIApplication (Private)
-(void) suspend;
-(void) terminateWithSuccess;
@end

@interface UIApplication (SpringBoardAnimatedExit)
-(void) animatedExit;
@end

@implementation UIApplication (SpringBoardAnimatedExit)
-(void) animatedExit {
	BOOL multitaskingSupported = NO;
	if ([[UIDevice currentDevice] respondsToSelector:@selector(isMultitaskingSupported)]) {
		multitaskingSupported = [UIDevice currentDevice].multitaskingSupported;
	}
	if ([self respondsToSelector:@selector(suspend)]) {
		if (multitaskingSupported) {
			[self beginBackgroundTaskWithExpirationHandler:^{}];
			[self performSelector:@selector(exit) withObject:nil afterDelay:0.4];
		}
		[self suspend];
	} else {
		[self exit];
	}
}

-(void) exit {
	if ([self respondsToSelector:@selector(terminateWithSuccess)]) {
		[self terminateWithSuccess];
	} else {
		exit(0);
	}
}
@end

std::string System_GetProperty(SystemProperty prop) {
	switch (prop) {
	case SYSPROP_NAME:
		return "iOS:";
	case SYSPROP_LANGREGION:
		return "en_US";
	default:
		return "";
	}
}

int System_GetPropertyInt(SystemProperty prop) {
	switch (prop) {
	case SYSPROP_AUDIO_SAMPLE_RATE:
		return 44100;
	case SYSPROP_DISPLAY_REFRESH_RATE:
		return 60000;
	case SYSPROP_DEVICE_TYPE:
		return DEVICE_TYPE_MOBILE;
	default:
		return -1;
	}
}

void System_SendMessage(const char *command, const char *parameter) {
	if (!strcmp(command, "finish")) {
		[[UIApplication sharedApplication] animatedExit];
	}
}

void System_AskForPermission(SystemPermission permission) {}
PermissionStatus System_GetPermissionStatus(SystemPermission permission) { return PERMISSION_STATUS_GRANTED; }

FOUNDATION_EXTERN void AudioServicesPlaySystemSoundWithVibration(unsigned long, objc_object*, NSDictionary*);

void Vibrate(int length_ms) {
	NSMutableDictionary *dictionary = [NSMutableDictionary dictionary];
	NSArray *pattern = @[@YES, @30, @NO, @2];
	
	dictionary[@"VibePattern"] = pattern;
	dictionary[@"Intensity"] = @2;
	
	AudioServicesPlaySystemSoundWithVibration(kSystemSoundID_Vibrate, nil, dictionary);
	// TODO: Actually make use of length_ms if PPSSPP ever adds that in the config
}

int main(int argc, char *argv[])
{
	// Simulates a debugger. Makes it possible to use JIT (though only W^X)
	syscall(SYS_ptrace, 0 /*PTRACE_TRACEME*/, 0, 0, 0);
	@autoreleasepool {
		return UIApplicationMain(argc, argv, nil, NSStringFromClass([AppDelegate class]));
	}
}
