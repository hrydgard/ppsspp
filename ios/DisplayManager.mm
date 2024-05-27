//
//  DisplayManager.m
//  native
//
//  Created by xieyi on 2019/6/9.
//

#import "DisplayManager.h"
#import "iOSCoreAudio.h"
#import "ViewController.h"
#import "AppDelegate.h"
#include "Common/System/Display.h"
#include "Common/System/System.h"
#include "Common/System/NativeApp.h"
#include "Core/System.h"
#import <AVFoundation/AVFoundation.h>

#define IS_IPAD() ([UIDevice currentDevice].userInterfaceIdiom == UIUserInterfaceIdiomPad)

@interface DisplayManager ()

@property BOOL listenerActive;
@property (atomic, retain) NSMutableArray<UIScreen *> *extDisplays;
@property CGRect originalFrame;
@property CGRect originalBounds;
@property CGAffineTransform originalTransform;

- (void)updateScreen:(UIScreen *)screen;

@end

@implementation DisplayManager

- (instancetype)init
{
	self = [super init];
	if (self) {
		[self setListenerActive:NO];
		[self setExtDisplays:[[NSMutableArray<UIScreen *> alloc] init]];
	}
	return self;
}

+ (DisplayManager *)shared {
	static DisplayManager *sharedInstance = nil;
	static dispatch_once_t onceToken;
	dispatch_once(&onceToken, ^{
		sharedInstance = [[DisplayManager alloc] init];
	});
	return sharedInstance;
}

- (void)setupDisplayListener {
	// Disable external display by default
	if ([[NSUserDefaults standardUserDefaults] boolForKey:@"enable_external_display"] == NO) {
		return;
	}
	if ([self listenerActive]) {
		NSLog(@"setupDisplayListener already called");
		return;
	}
	NSLog(@"Setting up display manager");
	[self setMainScreen:[UIScreen mainScreen]];
	UIWindow *gameWindow = [(AppDelegate *)[[UIApplication sharedApplication] delegate] window];
	[self setOriginalFrame: [gameWindow frame]];
	[self setOriginalBounds:[gameWindow bounds]];
	[self setOriginalTransform:[gameWindow transform]];

	// TODO: From iOS 13, should use UIScreenDidConnectNotification instead of the below.

	// Display connected
	[[NSNotificationCenter defaultCenter] addObserverForName:UIScreenDidConnectNotification object:nil queue:nil usingBlock:^(NSNotification * _Nonnull notification) {
		UIScreen *screen = (UIScreen *) notification.object;
		NSLog(@"New display connected: %@", [screen debugDescription]);
		[[self extDisplays] addObject:screen];
		// Do not switch to second connected display
		if ([self mainScreen] != [UIScreen mainScreen]) {
			return;
		}
		// Ignore mute switch when connected to external display
		iOSCoreAudioSetDisplayConnected(true);
		[self updateScreen:screen];
	}];
	// Display disconnected
	[[NSNotificationCenter defaultCenter] addObserverForName:UIScreenDidDisconnectNotification object:nil queue:nil usingBlock:^(NSNotification * _Nonnull notification) {
		UIScreen *screen = (UIScreen *) notification.object;
		NSLog(@"Display disconnected: %@", [screen debugDescription]);
		if ([[self extDisplays] containsObject:screen]) {
			[[self extDisplays] removeObject:screen];
		}
		if ([[self extDisplays] count] > 0) {
			UIScreen *newScreen = [[self extDisplays] lastObject];
			[self updateScreen:newScreen];
		} else {
			iOSCoreAudioSetDisplayConnected(false);
			[self updateScreen:[UIScreen mainScreen]];
		}
	}];
	[self setListenerActive:YES];
}

- (void)updateScreen:(UIScreen *)screen {
	[self setMainScreen:screen];
	UIWindow *gameWindow = [(AppDelegate *)[[UIApplication sharedApplication] delegate] window];
	// Hide before moving window to external display, otherwise iPhone won't switch to it
	[gameWindow setHidden:YES];
	[gameWindow setScreen:screen];
	// Set optimal resolution
	// Dispatch later to prevent "no window is preset" error
	dispatch_async(dispatch_get_main_queue(), ^{
		if (screen != [UIScreen mainScreen]) {
			NSUInteger count = [[screen availableModes] count];
			UIScreenMode* mode = [screen availableModes][count - 1];
			[screen setCurrentMode:mode];
			mode = [screen currentMode];
			// Fix overscan
			// TODO: Hacky solution. Screen is still scaled even if UIScreenOverscanCompensationNone is set.
			[screen setOverscanCompensation:UIScreenOverscanCompensationNone];
			CGSize fullSize = mode.size;
			UIEdgeInsets insets = [screen overscanCompensationInsets];
			fullSize.width -= insets.left + insets.right;
			fullSize.height -= insets.top + insets.bottom;
			[gameWindow setFrame:CGRectMake(insets.left, insets.top, fullSize.width, fullSize.height)];
			[gameWindow setBounds:CGRectMake(0, 0, fullSize.width, fullSize.height)];
			[self updateResolution:screen];
			[gameWindow setTransform:CGAffineTransformMakeScale(mode.size.width / fullSize.width, mode.size.height / fullSize.height)];
		} else {
			[gameWindow setTransform:[self originalTransform]];
			[gameWindow setFrame:[self originalFrame]];
			[gameWindow setBounds:[self originalBounds]];
			[self updateResolution:screen];
		}
		[gameWindow setHidden:NO];
	});
}

- (void)updateResolution:(UIScreen *)screen {
	float scale = screen.nativeScale;
	CGSize size = screen.bounds.size;
	
	if (size.height > size.width) {
		std::swap(size.height, size.width);
	}

	if (screen == [UIScreen mainScreen]) {
		g_display.dpi = (IS_IPAD() ? 200.0f : 150.0f) * scale;
	} else {
		float diagonal = sqrt(size.height * size.height + size.width * size.width);
		g_display.dpi = diagonal * scale * 0.1f;
	}
	g_display.dpi_scale_x = 240.0f / g_display.dpi;
	g_display.dpi_scale_y = 240.0f / g_display.dpi;
	g_display.dpi_scale_real_x = g_display.dpi_scale_x;
	g_display.dpi_scale_real_y = g_display.dpi_scale_y;
	g_display.pixel_xres = size.width * scale;
	g_display.pixel_yres = size.height * scale;

	g_display.dp_xres = g_display.pixel_xres * g_display.dpi_scale_x;
	g_display.dp_yres = g_display.pixel_yres * g_display.dpi_scale_y;

	g_display.pixel_in_dps_x = (float)g_display.pixel_xres / (float)g_display.dp_xres;
	g_display.pixel_in_dps_y = (float)g_display.pixel_yres / (float)g_display.dp_yres;
	
	[[sharedViewController getView] setContentScaleFactor:scale];
	
	// PSP native resize
	PSP_CoreParameter().pixelWidth = g_display.pixel_xres;
	PSP_CoreParameter().pixelHeight = g_display.pixel_yres;

	NativeResized();
	
	NSLog(@"Updated display resolution: (%d, %d) @%.1fx", g_display.pixel_xres, g_display.pixel_yres, scale);
}

@end
