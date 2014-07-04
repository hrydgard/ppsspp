// main.mm boilerplate

#import <UIKit/UIKit.h>
#import <string>
#import <stdio.h>
#import <stdlib.h>
#import <AudioToolbox/AudioToolbox.h>

#import "AppDelegate.h"
#import <AudioToolbox/AudioToolbox.h>

#include "base/NativeApp.h"

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

void System_SendMessage(const char *command, const char *parameter) {
	if (!strcmp(command, "finish")) {
		exit(0);
	}
}

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
	@autoreleasepool {
		return UIApplicationMain(argc, argv, nil, NSStringFromClass([AppDelegate class]));
	}
}
