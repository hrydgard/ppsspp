//
//  MacOSBarItems.mm
//  PPSSPP
//
//  Created by Serena on 06/02/2023.
//

#include "Common/System/System.h"
#include "Common/System/NativeApp.h"
#include "Core/Config.h"
#include "Common/Data/Text/I18n.h"
#include "Common/StringUtils.h"
#import <AppKit/AppKit.h>

#ifdef __cplusplus
extern "C" {
#endif

// NSMenuItem requires the use of an objective-c selector (aka the devil's greatest trick)
// So we have to make this class
@interface BarItemsManager : NSObject
+(instancetype)sharedInstance;
-(void)setupAppBarItems;
@property (assign) NSMenu *openMenu;
@property (assign) std::shared_ptr<I18NCategory> mainSettingsLocalization;
@end

void initBarItemsForApp() {
    [[BarItemsManager sharedInstance] setupAppBarItems];
}

// im soooooo sorry for whoever had to read this impl
@implementation BarItemsManager
+ (instancetype)sharedInstance {
    static BarItemsManager *stub;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        stub = [BarItemsManager new];
        stub.mainSettingsLocalization = GetI18NCategory("MainSettings");
    });
    
    return stub;
}

-(void)setupAppBarItems {
    NSMenuItem *openMenuItem = [[NSMenuItem alloc] init];
    openMenuItem.submenu = [self makeOpenSubmenu];
    
    NSMenuItem *graphicsMenuItem = [[NSMenuItem alloc] init];
    graphicsMenuItem.submenu = [self makeGraphicsMenu];
    
    NSMenuItem *audioMenuItem = [[NSMenuItem alloc] init];
    audioMenuItem.submenu = [self makeAudioMenu];
    
    [NSApplication.sharedApplication.menu addItem:openMenuItem];
    [NSApplication.sharedApplication.menu addItem:graphicsMenuItem];
    [NSApplication.sharedApplication.menu addItem:audioMenuItem];
}

-(NSMenu *)makeOpenSubmenu {
    NSMenu *menu = [[NSMenu alloc] initWithTitle:@"File"];
    NSMenuItem *openWithSystemFolderBrowserItem = [[NSMenuItem alloc] initWithTitle:@"Open..." action:@selector(openSystemFileBrowser) keyEquivalent:@"o"];
    openWithSystemFolderBrowserItem.keyEquivalentModifierMask = NSEventModifierFlagCommand;
    openWithSystemFolderBrowserItem.enabled = YES;
    openWithSystemFolderBrowserItem.target = self;
    [menu addItem:openWithSystemFolderBrowserItem];
    self.openMenu = menu;
    
    [self addOpenRecentlyItem];
    return menu;
}

-(NSMenu *)makeGraphicsMenu {
    NSMenu *parent = [[NSMenu alloc] initWithTitle:@(self.mainSettingsLocalization->T("Graphics"))];
    NSMenu *backendsMenu = [[NSMenu alloc] init];
    
    auto graphicsLocalization = GetI18NCategory("Graphics");
#define GRAPHICS_LOCALIZED(key) @(graphicsLocalization->T(key))
    
    NSMenuItem *gpuBackendItem = [[NSMenuItem alloc] initWithTitle:GRAPHICS_LOCALIZED("Backend") action:nil keyEquivalent:@""];
    
    std::vector<GPUBackend> allowed = [self allowedGPUBackends];
    for (int i = 0; i < allowed.size(); i++) {
        NSMenuItem *backendMenuItem = [[NSMenuItem alloc] initWithTitle:@(GPUBackendToString(allowed[i]).c_str()) action: @selector(setCurrentGPUBackend:) keyEquivalent: @""];
        backendMenuItem.tag = i;
        backendMenuItem.target = self;
        backendMenuItem.state = [self controlStateForBool: g_Config.iGPUBackend == (int)allowed[i]];
        [backendsMenu addItem:backendMenuItem];
    }
    
    gpuBackendItem.submenu = backendsMenu;
    [parent addItem:gpuBackendItem];
    
    [parent addItem:[NSMenuItem separatorItem]];
    
#define MENU_ITEM(variableName, localizedTitleName, SEL, ConfigurationValueName) \
NSMenuItem *variableName = [[NSMenuItem alloc] initWithTitle:GRAPHICS_LOCALIZED(localizedTitleName) action:SEL keyEquivalent:@""]; \
variableName.target = self; \
variableName.state = [self controlStateForBool: ConfigurationValueName];
    
    MENU_ITEM(softwareRendering, "Software Rendering", @selector(toggleSoftwareRendering:), g_Config.bSoftwareRendering)
    [parent addItem:softwareRendering];
    
    MENU_ITEM(vsyncItem, "VSync", @selector(toggleVSync:), g_Config.bVSync)
    [parent addItem:vsyncItem];
    
    MENU_ITEM(fullScreenItem, "Fullscreen", @selector(toggleFullScreen:), g_Config.bFullScreen)
    [parent addItem:fullScreenItem];
    
    [parent addItem:[NSMenuItem separatorItem]];
    
    MENU_ITEM(autoFrameSkip, "Auto FrameSkip", @selector(toggleAutoFrameSkip:), g_Config.bAutoFrameSkip)
    [parent addItem:autoFrameSkip];
    
    [parent addItem:[NSMenuItem separatorItem]];
    
    MENU_ITEM(fpsCounterItem, "Show FPS Counter", @selector(setToggleShowCounterItem:), g_Config.iShowStatusFlags & (int)ShowStatusFlags::FPS_COUNTER)
    fpsCounterItem.tag = (int)ShowStatusFlags::FPS_COUNTER;
    
    MENU_ITEM(speedCounterItem, "Show Speed", @selector(setToggleShowCounterItem:), g_Config.iShowStatusFlags & (int)ShowStatusFlags::SPEED_COUNTER)
    speedCounterItem.tag = (int)ShowStatusFlags::SPEED_COUNTER;
    
    MENU_ITEM(batteryPercentItem, "Show battery %", @selector(setToggleShowCounterItem:), g_Config.iShowStatusFlags & (int)ShowStatusFlags::BATTERY_PERCENT)
    batteryPercentItem.tag = (int)ShowStatusFlags::BATTERY_PERCENT;
    
    [parent addItem:[NSMenuItem separatorItem]];
    [parent addItem:fpsCounterItem];
    [parent addItem:speedCounterItem];
    [parent addItem:batteryPercentItem];
    
    [[NSNotificationCenter defaultCenter] addObserverForName:@"ConfigDidChange" object:nil queue:nil usingBlock:^(NSNotification * _Nonnull note) {
        NSString *value = [note object];
        // NOTE: Though it may seem like it,
        // the next few lines were not written by yandere dev
        if ([value isEqualToString:@"VSync"]) {
            vsyncItem.state = [self controlStateForBool: g_Config.bVSync];
        } else if ([value isEqualToString:@"FullScreen"]) {
            fullScreenItem.state = [self controlStateForBool: g_Config.bFullScreen];
        } else if ([value isEqualToString:@"SoftwareRendering"]) {
            softwareRendering.state = [self controlStateForBool: g_Config.bSoftwareRendering];
        } else if ([value isEqualToString:@"AutoFrameSkip"]) {
            autoFrameSkip.state = [self controlStateForBool: g_Config.bAutoFrameSkip];
        } else if ([value isEqualToString:@"ShowFPSCounter"]) {
            fpsCounterItem.state = [self controlStateForBool:g_Config.iShowStatusFlags & (int)ShowStatusFlags::FPS_COUNTER];
        } else if ([value isEqualToString:@"ShowSpeed"]) {
            speedCounterItem.state = [self controlStateForBool:g_Config.iShowStatusFlags & (int)ShowStatusFlags::SPEED_COUNTER];
        } else if ([value isEqualToString:@"BatteryPercent"]) {
            batteryPercentItem.state = [self controlStateForBool:g_Config.iShowStatusFlags & (int)ShowStatusFlags::BATTERY_PERCENT];
        }
    }];
#undef MENU_ITEM
#undef GRAPHICS_LOCALIZED
    return parent;
}

-(NSMenu *)makeAudioMenu {
    NSMenu *parent = [[NSMenu alloc] initWithTitle:@(self.mainSettingsLocalization->T("Audio"))];
    auto audioLocalization = GetI18NCategory("Audio");
    
    NSMenuItem *enableSoundItem = [[NSMenuItem alloc] initWithTitle:@(audioLocalization->T("Enable Sound")) action:@selector(toggleSound:) keyEquivalent:@""];
    enableSoundItem.target = self;
    enableSoundItem.state = [self controlStateForBool: g_Config.bEnableSound];
    [parent addItem:enableSoundItem];
    
    NSMenuItem *deviceListItem = [[NSMenuItem alloc] initWithTitle:@(audioLocalization->T("Device")) action:nil keyEquivalent:@""];
    deviceListItem.submenu = [self makeAudioListMenuWithItem:deviceListItem];
    [parent addItem:deviceListItem];
    
    [[NSNotificationCenter defaultCenter] addObserverForName:@"AudioConfChanged" object:nil queue:nil usingBlock:^(NSNotification * _Nonnull note) {
        NSString *value = [note object];
        if ([value isEqualToString:@"EnableSound"]) {
            enableSoundItem.state = [self controlStateForBool:g_Config.bEnableSound];
        }
    }];
    
    return parent;
}

-(NSMenu *)makeAudioListMenuWithItem: (NSMenuItem *)callerItem {
    __block NSMenu *theMenu = [[NSMenu alloc] init];
    std::vector<std::string> audioDeviceList;
    SplitString(System_GetProperty(SYSPROP_AUDIO_DEVICE_LIST), '\0', audioDeviceList);
    
    for (int i = 0; i < audioDeviceList.size(); i++) {
        std::string itemName = audioDeviceList[i];
        NSMenuItem *item = [[NSMenuItem alloc] initWithTitle:@(itemName.c_str()) action:@selector(setAudioItem:) keyEquivalent:@""];
        item.tag = i;
        item.target = self;
        item.state = [self controlStateForBool:g_Config.sAudioDevice == itemName];
        [theMenu addItem:item];
    }
    
    [[NSNotificationCenter defaultCenter] addObserverForName:@"AudioConfigurationHasChanged" object:nil queue:nil usingBlock:^(NSNotification * _Nonnull note) {
        NSString *value = [note object];
        if ([value isEqualToString: @"DeviceAddedOrChanged"]) {
            callerItem.submenu = [self makeAudioListMenuWithItem:callerItem];
        } else if ([value isEqualToString:@"CurrentDeviceWasChanged"]) {
            // set the new item to be the selected one
            dispatch_async(dispatch_get_main_queue(), ^{
                for (NSMenuItem *item in theMenu.itemArray)
                    item.state = [self controlStateForBool:g_Config.sAudioDevice == audioDeviceList[item.tag]];
            });
        }
    }];
    
    return theMenu;
}

-(void) setAudioItem: (NSMenuItem *)sender {
    std::vector<std::string> audioDeviceList;
    SplitString(System_GetProperty(SYSPROP_AUDIO_DEVICE_LIST), '\0', audioDeviceList);
    
    std::string theItemSelected = audioDeviceList[sender.tag];
    if (theItemSelected == g_Config.sAudioDevice)
        return; // device already selected
    
    g_Config.sAudioDevice = theItemSelected;
    for (NSMenuItem *item in sender.menu.itemArray) {
        item.state = [self controlStateForBool:g_Config.sAudioDevice == theItemSelected];
    }
    
    System_SendMessage("audio_resetDevice", "");
}

#define TOGGLE_METHOD(name, ConfigValueName, ...) \
-(void)toggle##name: (NSMenuItem *)item { \
ConfigValueName = !ConfigValueName; \
__VA_ARGS__; /* for any additional updates */ \
item.state = [self controlStateForBool: ConfigValueName]; \
}

TOGGLE_METHOD(Sound, g_Config.bEnableSound)
TOGGLE_METHOD(AutoFrameSkip, g_Config.bAutoFrameSkip, g_Config.updateAfterSettingAutoFrameSkip())
TOGGLE_METHOD(SoftwareRendering, g_Config.bSoftwareRendering)
TOGGLE_METHOD(FullScreen, g_Config.bFullScreen, System_SendMessage("toggle_fullscreen", g_Config.UseFullScreen() ? "1" : "0"))
TOGGLE_METHOD(VSync, g_Config.bVSync)
#undef TOGGLE_METHOD

-(void)setToggleShowCounterItem: (NSMenuItem *)item {
    [self addOrRemoveInteger:(int)item.tag to:&g_Config.iShowStatusFlags];
    item.state = [self controlStateForBool:g_Config.iShowStatusFlags & item.tag];
}

-(void)addOrRemoveInteger: (int)integer to: (int *)r {
    if (integer & *r) {
        *r -= integer;
    } else {
        *r |= integer;
    }
}

-(void)setCurrentGPUBackend: (NSMenuItem *)sender {
    std::vector<GPUBackend> allowed = [self allowedGPUBackends];
    if (allowed.size() == 1) {
        printf("only one item, bailing");
        return;
    }
    
    g_Config.iGPUBackend = (int)(allowed[sender.tag]);
    sender.state = NSControlStateValueOn;
    
    for (NSMenuItem *item in sender.menu.itemArray) {
        // deselect the previously selected item
        if (item.state == NSControlStateValueOn && item.tag != sender.tag) {
            item.state = NSControlStateValueOff;
            break;
        }
    }
}

-(NSControlStateValue) controlStateForBool: (BOOL)boolValue {
    return boolValue ? NSControlStateValueOn : NSControlStateValueOff;
}

-(std::vector<GPUBackend>)allowedGPUBackends {
    std::vector<GPUBackend> allBackends = {
        GPUBackend::OPENGL, GPUBackend::VULKAN,
        GPUBackend::DIRECT3D11, GPUBackend::DIRECT3D9
    };
    
    std::vector<GPUBackend> allowed;
    
    for (GPUBackend backend : allBackends) {
        if (g_Config.IsBackendEnabled(backend)) {
            allowed.push_back(backend);
        }
    }
    
    return allowed;
}

-(void)addOpenRecentlyItem {
    std::vector<std::string> recentIsos = g_Config.RecentIsos();
    NSMenuItem *openRecent = [[NSMenuItem alloc] initWithTitle:@"Open Recent" action:nil keyEquivalent:@""];
    NSMenu *recentsMenu = [[NSMenu alloc] init];
    if (recentIsos.empty())
        openRecent.enabled = NO;
    
    for (int i = 0; i < recentIsos.size(); i++) {
        std::string filename = Path(recentIsos[i]).GetFilename();
        NSMenuItem *item = [[NSMenuItem alloc] initWithTitle:@(filename.c_str()) action:@selector(openRecentItem:) keyEquivalent:@""];
        item.target = self;
        [recentsMenu addItem:item];
    }
    
    openRecent.submenu = recentsMenu;
    [self.openMenu addItem:openRecent];
}

-(void)openRecentItem: (NSMenuItem *)item {
    NativeMessageReceived("browse_fileSelect", g_Config.RecentIsos()[item.tag].c_str());
}

-(void)openSystemFileBrowser {
    System_SendMessage("browse_folder", "");
}

- (void)dealloc {
    [NSNotificationCenter.defaultCenter removeObserver:self];
}

@end

#ifdef __cplusplus
}
#endif
