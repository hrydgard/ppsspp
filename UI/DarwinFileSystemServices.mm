//
//  DarwinFileSystemServices.mm
//  PPSSPP
//
//  Created by Serena on 20/01/2023.
//

#include "ppsspp_config.h"
#include "Core/Config.h"
#include "Common/Log.h"
#include "DarwinFileSystemServices.h"
#include <dispatch/dispatch.h>
#include <CoreServices/CoreServices.h>

#if !__has_feature(objc_arc)
#error Must be built with ARC, please revise the flags for DarwinFileSystemServices.mm to include -fobjc-arc.
#endif

#if __has_include(<UIKit/UIKit.h>)
#include "../ios/ViewControllerCommon.h"
#include <UIKit/UIKit.h>

@interface DocumentPickerDelegate : NSObject <UIDocumentPickerDelegate>
@property DarwinDirectoryPanelCallback panelCallback;
@end

void *DarwinFileSystemServices::__pickerDelegate = nullptr;

void DarwinFileSystemServices::ClearDelegate() {
	// TODO: Figure out how to free the delegate.
	// CFRelease((__bridge DocumentPickerDelegate *)__pickerDelegate);
	__pickerDelegate = NULL;
}

@implementation DocumentPickerDelegate
-(instancetype)initWithCallback: (DarwinDirectoryPanelCallback)panelCallback {
	if (self = [super init]) {
		self.panelCallback = panelCallback;
	}
	return self;
}

- (void)documentPicker:(UIDocumentPickerViewController *)controller didPickDocumentsAtURLs:(NSArray<NSURL *> *)urls {
	if (urls.count >= 1)
		self.panelCallback(true, Path(urls[0].path.UTF8String));
	else
		self.panelCallback(false, Path());

	INFO_LOG(Log::System, "Callback processed, pre-emptively hide keyboard");
	[sharedViewController hideKeyboard];
	DarwinFileSystemServices::ClearDelegate();
}

- (void)documentPickerWasCancelled:(UIDocumentPickerViewController *)controller {
	self.panelCallback(false, Path());

	INFO_LOG(Log::System, "Picker cancelled, pre-emptively hide keyboard");
	[sharedViewController hideKeyboard];
}

@end

#else
#include <AppKit/AppKit.h>
#endif // __has_include(<UIKit/UIKit.h>)

void DarwinFileSystemServices::presentDirectoryPanel(
	DarwinDirectoryPanelCallback panelCallback,
	bool allowFiles, bool allowDirectories,
	BrowseFileType fileType) {
	dispatch_async(dispatch_get_main_queue(), ^{
#if PPSSPP_PLATFORM(MAC)
		NSOpenPanel *panel = [[NSOpenPanel alloc] init];
		panel.allowsMultipleSelection = NO;
		panel.canChooseFiles = allowFiles;
		panel.canChooseDirectories = allowDirectories;
		switch (fileType) {
		case BrowseFileType::BOOTABLE:
			[panel setAllowedFileTypes:[NSArray arrayWithObjects:@"iso", @"cso", @"pbp", @"elf", @"zip", @"ppdmp", nil]];
			break;
		case BrowseFileType::IMAGE:
			[panel setAllowedFileTypes:[NSArray arrayWithObjects:@"jpg", @"png", nil]];
			break;
		case BrowseFileType::INI:
			[panel setAllowedFileTypes:[NSArray arrayWithObject:@"ini"]];
			break;
		case BrowseFileType::DB:
			[panel setAllowedFileTypes:[NSArray arrayWithObject:@"db"]];
			break;
		case BrowseFileType::SOUND_EFFECT:
			[panel setAllowedFileTypes:[NSArray arrayWithObject:@"wav"]];
			break;
		default:
			break;
		}
//		if (!allowFiles && allowDirectories)
//			panel.allowedFileTypes = @[(__bridge NSString *)kUTTypeFolder];
		NSModalResponse modalResponse = [panel runModal];
		if (modalResponse == NSModalResponseOK && panel.URLs.firstObject) {
			INFO_LOG(Log::System, "Mac: Received OK response from modal");
			panelCallback(true, Path(panel.URLs.firstObject.path.UTF8String));
		} else if (modalResponse == NSModalResponseCancel) {
			INFO_LOG(Log::System, "Mac: Received Cancel response from modal");
			panelCallback(false, Path());
		} else {
			WARN_LOG(Log::System, "Mac: Received unknown responsde from modal");
			panelCallback(false, Path());
		}
#elif PPSSPP_PLATFORM(IOS)
		UIViewController *rootViewController = UIApplication.sharedApplication
			.keyWindow
			.rootViewController;
		
		// get current window view controller
		if (!rootViewController)
			return;
		
		NSMutableArray<NSString *> *types = [NSMutableArray array];
		UIDocumentPickerMode pickerMode = UIDocumentPickerModeOpen;
		
		if (allowDirectories)
			[types addObject: (__bridge NSString *)kUTTypeFolder];
		if (allowFiles) {
			[types addObject: (__bridge NSString *)kUTTypeItem];
			pickerMode = UIDocumentPickerModeImport;
		}
		
		UIDocumentPickerViewController *pickerVC = [[UIDocumentPickerViewController alloc] initWithDocumentTypes: types inMode: pickerMode];
		// What if you wanted to go to heaven, but then God showed you the next few lines?
		// serious note: have to do this, because __pickerDelegate has to stay retained as a class property
		__pickerDelegate = (void *)CFBridgingRetain([[DocumentPickerDelegate alloc] initWithCallback:panelCallback]);
		pickerVC.delegate = (__bridge DocumentPickerDelegate *)__pickerDelegate;
		[rootViewController presentViewController:pickerVC animated:true completion:nil];
#endif
    });
}

Path DarwinFileSystemServices::appropriateMemoryStickDirectoryToUse() {
    NSString *userPreferred = [[NSUserDefaults standardUserDefaults] stringForKey:@(PreferredMemoryStickUserDefaultsKey)];
    if (userPreferred)
        return Path(userPreferred.UTF8String);
    
    return defaultMemoryStickPath();
}

Path DarwinFileSystemServices::defaultMemoryStickPath() {
#if PPSSPP_PLATFORM(IOS)
    NSString *documentsPath = [NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES)
                               objectAtIndex:0];
    return Path(documentsPath.UTF8String);
#elif PPSSPP_PLATFORM(MAC)
    return g_Config.defaultCurrentDirectory / ".config/ppsspp";
#endif
}

void DarwinFileSystemServices::setUserPreferredMemoryStickDirectory(Path path) {
    [[NSUserDefaults standardUserDefaults] setObject:@(path.c_str())
                                              forKey:@(PreferredMemoryStickUserDefaultsKey)];
    g_Config.memStickDirectory = path;
}

void RestartMacApp() {
#if PPSSPP_PLATFORM(MAC)
    NSURL *bundleURL = NSBundle.mainBundle.bundleURL;
    NSTask *task = [[NSTask alloc] init];
    task.executableURL = [NSURL fileURLWithPath:@"/usr/bin/open"];
    task.arguments = @[@"-n", bundleURL.path];
    [task launch];
    exit(0);
#endif
}
