// main.mm boilerplate

#import <UIKit/UIKit.h>
#import <string>
#import <stdio.h>
#import <stdlib.h>

#import "AppDelegate.h"

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

void Vibrate(int length_ms) {
	// TODO: Haptic feedback?
}

int main(int argc, char *argv[])
{
	@autoreleasepool {
		return UIApplicationMain(argc, argv, nil, NSStringFromClass([AppDelegate class]));
	}
}
