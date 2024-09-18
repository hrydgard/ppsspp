//
//  PSPNSApplicationDelegate.mm
//  PPSSPP
//
//  Created by Serena on 22/04/2023.
//

#import <Cocoa/Cocoa.h>

#import "PSPNSApplicationDelegate.h"

#include "Common/System/System.h"
#include "Core/SaveState.h"
#include "Core/Config.h"

@implementation PSPNSApplicationDelegate
+ (instancetype)sharedAppDelegate {
	static PSPNSApplicationDelegate *del;
	static dispatch_once_t onceToken;
	dispatch_once(&onceToken, ^{
		del = [PSPNSApplicationDelegate new];
	});
	
	return del;
}

- (void)application:(NSApplication *)application openURLs:(NSArray<NSURL *> *)urls {
	NSURL *firstURL = urls.firstObject;
	if (!firstURL) return; // No URLs, don't do anything
	
	System_PostUIMessage(UIMessage::REQUEST_GAME_BOOT, firstURL.fileSystemRepresentation);
}

- (NSMenu *)applicationDockMenu:(NSApplication *)sender {
	// TODO: Actually implement a dock menu thingy.
	for (std::string iso : g_Config.RecentIsos()) {
		// printf("%s\n", iso.c_str());
	}
	
	return nil;
}
@end
