//
//  DarwinMemoryStickManager.mm
//  PPSSPP
//
//  Created by Serena on 20/01/2023.
//

#include "ppsspp_config.h"
#include "Core/Config.h"
#include "DarwinMemoryStickManager.h"
#include <dispatch/dispatch.h>
#include <CoreServices/CoreServices.h>

#if !__has_feature(objc_arc)
#error Must be built with ARC, please revise the flags for DarwinMemoryStickManager.mm to include -fobjc-arc.
#endif

#if __has_include(<UIKit/UIKit.h>)
#include <UIKit/UIKit.h>

@interface DocumentPickerDelegate : NSObject <UIDocumentPickerDelegate>
@property DarwinDirectoryPanelCallback callback;
@end

@implementation DocumentPickerDelegate
-(instancetype)initWithCallback: (DarwinDirectoryPanelCallback)callback {
    if (self = [super init]) {
        self.callback = callback;
    }
    
    return self;
}

- (void)documentPicker:(UIDocumentPickerViewController *)controller didPickDocumentsAtURLs:(NSArray<NSURL *> *)urls {
    // Notice: we could've hardcoded behaviour here to call
    // `DarwinMemoryStickManager::setUserPreferredMemoryStickDirectory`
    // But for the sake of flexibility in the future, i'll just call a provided callback.
    if (urls.count >= 1) self.callback(Path(urls[0].path.UTF8String));
}

@end

#else
#include <AppKit/AppKit.h>
#endif // __has_include(<UIKit/UIKit.h>)

void DarwinMemoryStickManager::presentDirectoryPanel(DarwinDirectoryPanelCallback callback) {
    dispatch_async(dispatch_get_main_queue(), ^{
#if PPSSPP_PLATFORM(MAC)
        NSOpenPanel *panel = [[NSOpenPanel alloc] init];
        panel.allowsMultipleSelection = NO;
        panel.canChooseFiles = NO;
        panel.canChooseDirectories = YES;
        panel.allowedFileTypes = @[(__bridge NSString *)kUTTypeFolder];
        
        NSModalResponse modalResponse = [panel runModal];
        if (modalResponse == NSModalResponseOK && panel.URLs && panel.URLs.firstObject)
            callback(Path(panel.URLs.firstObject.path.UTF8String));
#elif PPSSPP_PLATFORM(IOS)
        if (UIWindow *window = [UIApplication.sharedApplication keyWindow]) {
            if (UIViewController *viewController = window.rootViewController) {
                NSString *folderUTType = (__bridge NSString *)kUTTypeFolder;
                UIDocumentPickerViewController *pickerVC = [[UIDocumentPickerViewController alloc]
                                                            initWithDocumentTypes:@[folderUTType] inMode:UIDocumentPickerModeOpen];
                // What if you wanted to go to heaven, but then God showed you the next few lines?
                // serious note: have to do this, because __pickerDelegate has to stay retained as a class property
                __pickerDelegate = (void *)CFBridgingRetain([[DocumentPickerDelegate alloc] initWithCallback:callback]);
                pickerVC.delegate = (__bridge DocumentPickerDelegate *)__pickerDelegate;
                [viewController presentViewController:pickerVC animated:true completion:nil];
            }
        }
#endif
    });
}

Path DarwinMemoryStickManager::appropriateMemoryStickDirectoryToUse() {
    NSString *userPreferred = [[NSUserDefaults standardUserDefaults] stringForKey:@(PreferredMemoryStickUserDefaultsKey)];
    if (userPreferred)
        return Path(userPreferred.UTF8String);
    
    return __defaultMemoryStickPath();
}

Path DarwinMemoryStickManager::__defaultMemoryStickPath() {
#if PPSSPP_PLATFORM(IOS)
    NSString *documentsPath = [NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES)
                               objectAtIndex:0];
    return Path(documentsPath.UTF8String);
#elif PPSSPP_PLATFORM(MAC)
    return g_Config.defaultCurrentDirectory / ".config/ppsspp";
#endif
}

void DarwinMemoryStickManager::setUserPreferredMemoryStickDirectory(Path path) {
    [[NSUserDefaults standardUserDefaults] setObject:@(path.c_str())
                                              forKey:@(PreferredMemoryStickUserDefaultsKey)];
    g_Config.memStickDirectory = path;
}
