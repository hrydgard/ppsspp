// main.mm boilerplate

#import <UIKit/UIKit.h>
#import <string>

#import "AppDelegate.h"

std::string System_GetName()
{
	// TODO: iPad/etc.?
	return "iOS:";
}

int main(int argc, char *argv[])
{
	@autoreleasepool {
		return UIApplicationMain(argc, argv, nil, NSStringFromClass([AppDelegate class]));
	}
}
