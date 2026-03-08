//
//  DarwinFileSystemServices.mm
//  PPSSPP
//
//  Created by Serena on 20/01/2023.
//

// NOTES:
// Files inside the app folder opened in iOS via the document picker end up with this path:
// /private/var/mobile/Containers/Data/Application/<UUID>/Documents
// However, when we query the home directory we get:
// /var/mobile/Containers/Data/Application/<UUID>/Documents
// The /private prefix seems to be optional, so we strip it off before returning it.

#include <dispatch/dispatch.h>
#include <CoreServices/CoreServices.h>
#include <map>

#include "ppsspp_config.h"

#include "Common/Log.h"
#include "Common/StringUtils.h"

#include "Core/Config.h"

#include "DarwinFileSystemServices.h"

#if !__has_feature(objc_arc)
#error Must be built with ARC, please revise the flags for DarwinFileSystemServices.mm to include -fobjc-arc.
#endif

#if __has_include(<UIKit/UIKit.h>)
#include "../ios/ViewControllerCommon.h"
#include <UIKit/UIKit.h>

@interface DocumentPickerDelegate : NSObject <UIDocumentPickerDelegate>
@property DarwinDirectoryPanelCallback panelCallback;
@end

static std::map<std::string, void*> activeAccessTokens;

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

static void addBookmark(NSURL *url);

- (void)documentPicker:(UIDocumentPickerViewController *)controller didPickDocumentsAtURLs:(NSArray<NSURL *> *)urls {
	NSURL *url = urls.firstObject;
	if (!url) {
		self.panelCallback(false, Path());
		return;
	}

	addBookmark(url);

	// 1. Start accessing the resource
	BOOL success = [url startAccessingSecurityScopedResource];
	if (success) {
		// 2. Pass the path to PPSSPP
		// You should call stopAccessingSecurityScopedResource
		// when the file is closed, otherwise you "leak" kernel permissions.
		self.panelCallback(true, Path(url.path.UTF8String));
	} else {
		ERROR_LOG(Log::System, "Failed to start accessing security scoped resource for: %s", url.path.UTF8String);
		self.panelCallback(false, Path());
	}

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

// Internal helper to generate and store a bookmark for a given URL.
// Also activates the security scope for the current session.
static void addBookmark(NSURL *url) {
	if (!url) return;

	// IMPORTANT: You must start accessing BEFORE creating the bookmark,
	// otherwise the OS denies the 'read' required to seal the bookmark.
	BOOL accessStarted = [url startAccessingSecurityScopedResource];

	INFO_LOG(Log::System, "Darwin: Creating bookmark (Access: %s) for: %s",
			 accessStarted ? "YES" : "NO", url.path.UTF8String);

	NSError *error = nil;
	#if PPSSPP_PLATFORM(IOS)
		NSURLBookmarkCreationOptions options = 0;
	#else
		NSURLBookmarkCreationOptions options = NSURLBookmarkCreationWithSecurityScope;
	#endif

	NSData *bookmark = [url bookmarkDataWithOptions:options
					  includingResourceValuesForKeys:nil
									   relativeToURL:nil
											   error:&error];

	if (bookmark) {
		NSUserDefaults *defaults = [NSUserDefaults standardUserDefaults];
		NSMutableDictionary *dict = [[defaults dictionaryForKey:@"SecureBookmarks"] mutableCopy] ?: [NSMutableDictionary dictionary];

		[dict setObject:bookmark forKey:url.path];
		[defaults setObject:dict forKey:@"SecureBookmarks"];
		[defaults synchronize];

		// If we started access specifically for this function, we keep the token.
		// If we didn't have it in our map yet, store this URL.
		std::string pathKey = url.path.UTF8String;
		if (accessStarted && activeAccessTokens.find(pathKey) == activeAccessTokens.end()) {
			activeAccessTokens[pathKey] = (void *)CFBridgingRetain(url);
		} else if (accessStarted) {
			// If it's already in the map, we don't need a second retain.
			[url stopAccessingSecurityScopedResource];
		}
	} else {
		ERROR_LOG(Log::System, "Darwin: Bookmark failed. Error 260 often means the file isn't 'coordinated' yet. Detail: %s",
				  error.localizedDescription.UTF8String);

		if (accessStarted) [url stopAccessingSecurityScopedResource];
	}
}

Path DarwinFileSystemServices::reauthorizeBookmarkByPath(const Path &pathStr) {
    INFO_LOG(Log::System, "Darwin: Reauthorizing [%s]", pathStr.c_str());

    // 1. Return immediately if already active
    if (activeAccessTokens.count(pathStr.ToString())) {
        return pathStr;
    }

    // 2. Retrieve bookmark from disk
    NSUserDefaults *defaults = [NSUserDefaults standardUserDefaults];
    NSDictionary *dict = [defaults dictionaryForKey:@"SecureBookmarks"];
    NSData *bookmarkData = dict[@(pathStr.c_str())];

    if (!bookmarkData) {
        WARN_LOG(Log::System, "Darwin: No bookmark data found for path (%s)", pathStr.c_str());
        return Path();
    }

    // 3. Resolve
    BOOL isStale = NO;
    NSError *error = nil;
#if PPSSPP_PLATFORM(IOS)
    NSURLBookmarkResolutionOptions resOptions = 0;
#else
    NSURLBookmarkResolutionOptions resOptions = NSURLBookmarkResolutionWithSecurityScope;
#endif

    NSURL *url = [NSURL URLByResolvingBookmarkData:bookmarkData
                                           options:resOptions
                                     relativeToURL:nil
                               bookmarkDataIsStale:&isStale
                                             error:&error];

    if (url && [url startAccessingSecurityScopedResource]) {
        Path resolvedPath(url.path.UTF8String);

        // 4. Handle stale/moved files
        if (isStale || resolvedPath != pathStr) {
            INFO_LOG(Log::System, "Darwin: File moved! Updating [%s] -> [%s]", pathStr.c_str(), resolvedPath.c_str());

            // Clean up the old key from defaults
            NSMutableDictionary *mutableDict = [dict mutableCopy];
            [mutableDict removeObjectForKey:@(pathStr.c_str())];
            [defaults setObject:mutableDict forKey:@"SecureBookmarks"];

            // Re-save under the new path
            addBookmark(url);
        } else {
            // Path is the same, just cache the token
            activeAccessTokens[resolvedPath.ToString()] = (void *)CFBridgingRetain(url);
        }

        return resolvedPath;
    }

    ERROR_LOG(Log::System, "Darwin: Resolution failed for %s", pathStr.c_str());
    return Path();
}

void DarwinFileSystemServices::stopAccessingPath(const Path &pathStr) {
    auto it = activeAccessTokens.find(pathStr.ToString());
    if (it != activeAccessTokens.end()) {
        INFO_LOG(Log::System, "Darwin: Stopping access for: %s", pathStr.c_str());

        NSURL *url = (__bridge_transfer NSURL *)it->second;
        [url stopAccessingSecurityScopedResource];

        activeAccessTokens.erase(it);
	}
}

void DarwinFileSystemServices::terminate() {
    INFO_LOG(Log::System, "Darwin: Terminating all security-scoped access.");
    for (auto const& [path, token] : activeAccessTokens) {
        NSURL *url = (__bridge_transfer NSURL *)token;
        [url stopAccessingSecurityScopedResource];
    }
    activeAccessTokens.clear();
}

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
			[panel setAllowedFileTypes:[NSArray arrayWithObjects:@"iso", @"cso", @"chd", @"pbp", @"elf", @"zip", @"ppdmp", @"prx", nil]];
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
			[panel setAllowedFileTypes:[NSArray arrayWithObjects:@"wav", @"mp3", nil]];
			break;
		case BrowseFileType::SYMBOL_MAP:
			[panel setAllowedFileTypes:[NSArray arrayWithObject:@"ppsym"]];
			break;
		case BrowseFileType::SYMBOL_MAP_NOCASH:
			[panel setAllowedFileTypes:[NSArray arrayWithObject:@"sym"]];
			break;
		case BrowseFileType::ATRAC3:
			[panel setAllowedFileTypes:[NSArray arrayWithObject:@"at3"]];
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
			// NOTE: We do not want to copy files here - we handle it ourselves if needed.
			// Previously this was Import mode.
			pickerMode = UIDocumentPickerModeOpen;
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
