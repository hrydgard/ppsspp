// Copyright (c) 2012- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

// PPSSPPHeadless as an iOS app. iOS only runs app bundles, and kills one that doesn't bring up
// UIKit, so this starts UIKit and runs the headless main on a thread of its own, with the
// arguments devicectl launched it with:
//
//   xcrun devicectl device process launch --console --device <udid> org.ppsspp.headless <args>
//
// Relative paths are resolved against the app's Documents directory, which
// `devicectl device copy to/from --domain-type appDataContainer` reaches. See frametests.py --ios.

#import <UIKit/UIKit.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>
#include <unistd.h>

int HeadlessMain(int argc, const char *argv[]);

// DarwinFileSystemServices presents its pickers on the app's view controller. Headless has none.
UIViewController *sharedViewController = nil;

// iOS 27 stops an app that doesn't adopt the scene lifecycle, even one that draws nothing.
@interface HeadlessSceneDelegate : UIResponder <UIWindowSceneDelegate>
@property (strong, nonatomic) UIWindow *window;
@end

@implementation HeadlessSceneDelegate
@end

@interface HeadlessAppDelegate : UIResponder <UIApplicationDelegate>
@end

@implementation HeadlessAppDelegate

- (BOOL)application:(UIApplication *)application didFinishLaunchingWithOptions:(NSDictionary *)launchOptions {
	NSString *documents = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES).firstObject;
	chdir(documents.UTF8String);

	NSArray<NSString *> *arguments = [NSProcessInfo processInfo].arguments;
	std::vector<std::string> args;
	for (NSString *arg in arguments) {
		// --ios-clear=DIR deletes a folder under Documents first, since devicectl can't delete
		// anything (its --remove-existing-content empties all of Documents).
		if ([arg hasPrefix:@"--ios-clear="]) {
			NSString *dir = [arg substringFromIndex:12];
			if (dir.length == 0 || [dir hasPrefix:@"/"] || [dir containsString:@".."]) {
				printf("--ios-clear needs a folder inside Documents: %s\n", dir.UTF8String);
				exit(1);
			}
			[[NSFileManager defaultManager] removeItemAtPath:[documents stringByAppendingPathComponent:dir] error:nil];
			continue;
		}
		args.push_back(arg.UTF8String);
	}

	std::thread([args]() {
		std::vector<const char *> argv;
		for (const std::string &arg : args) {
			argv.push_back(arg.c_str());
		}
		argv.push_back(nullptr);
		int ret = HeadlessMain((int)args.size(), argv.data());
		fflush(stdout);
		fflush(stderr);
		exit(ret);
	}).detach();
	return YES;
}

@end

int main(int argc, char *argv[]) {
	// devicectl --console only forwards stdout, from a pipe, which would otherwise be block buffered.
	dup2(STDOUT_FILENO, STDERR_FILENO);
	setvbuf(stdout, nullptr, _IOLBF, 0);
	setvbuf(stderr, nullptr, _IONBF, 0);
	@autoreleasepool {
		return UIApplicationMain(argc, argv, nil, NSStringFromClass([HeadlessAppDelegate class]));
	}
}
