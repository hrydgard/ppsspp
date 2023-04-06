//
//  CocoaBarItems.mm
//  PPSSPP
//
//  Created by Serena on 06/02/2023.
//

#import <Cocoa/Cocoa.h>
#include "UI/DarwinFileSystemServices.h"
#include "Common/File/Path.h"
#include "Common/System/System.h"
#include "Common/System/NativeApp.h"
#include "Core/Config.h"
#include "Common/Data/Text/I18n.h"
#include "Common/StringUtils.h"

#ifdef __cplusplus
extern "C" {
#endif

// NSMenuItem requires the use of an objective-c selector (aka the devil's greatest trick)
// So we have to make this class
@interface BarItemsManager : NSObject <NSMenuDelegate>
+(instancetype)sharedInstance;
-(void)setupAppBarItems;
@property (assign) NSMenu *openMenu;
@property (assign) I18NCategory *mainSettingsLocalization;
@property (assign) I18NCategory *graphicsLocalization;
@end

void initBarItemsForApp() {
    [[BarItemsManager sharedInstance] setupAppBarItems];
}

@implementation BarItemsManager
+ (instancetype)sharedInstance {
    static BarItemsManager *stub;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        stub = [BarItemsManager new];
        stub.mainSettingsLocalization = GetI18NCategory(I18NCat::MAINSETTINGS);
    });
    
    return stub;
}

-(void)setupAppBarItems {
    NSMenuItem *openMenuItem = [[NSMenuItem alloc] init];
    openMenuItem.submenu = [self makeOpenSubmenu];
    openMenuItem.submenu.delegate = self;
    
    NSMenuItem *graphicsMenuItem = [[NSMenuItem alloc] init];
    graphicsMenuItem.submenu = [self makeGraphicsMenu];
    graphicsMenuItem.submenu.delegate = self;
    
	NSMenuItem *helpMenuItem = [[NSMenuItem alloc] init];
	helpMenuItem.submenu = [self makeHelpMenu];
	
    [NSApplication.sharedApplication.menu addItem:openMenuItem];
    [NSApplication.sharedApplication.menu addItem:graphicsMenuItem];
	[NSApplication.sharedApplication.menu addItem:helpMenuItem];
	
	NSString *windowMenuItemTitle = @"Window";
	// Rearrange 'Window' to be behind 'Help'
	for (NSMenuItem *item in NSApplication.sharedApplication.menu.itemArray) {
		if ([item.title isEqualToString:windowMenuItemTitle]) {
			[NSApplication.sharedApplication.menu removeItem:item];
			// 'Help' is the last item in the bar
			// so we can just use `NSApplication.sharedApplication.menu.numberOfItems - 1`
			// as it's index
			[NSApplication.sharedApplication.menu insertItem:item atIndex:NSApplication.sharedApplication.menu.numberOfItems - 1];
			break;
		}
	}
}

- (void)menuNeedsUpdate:(NSMenu *)menu {
    if ([menu.title isEqualToString: [self localizedString:"Graphics" category: self.mainSettingsLocalization]]) {
        for (NSMenuItem *item in menu.itemArray) {
            switch (item.tag) {
                case 1:
                    item.state = [self controlStateForBool:g_Config.bSoftwareRendering];
                    break;
                case 2:
                    item.state = [self controlStateForBool:g_Config.bVSync];
                    break;
                case 3:
                    item.state = [self controlStateForBool:g_Config.bFullScreen];
                    break;
                case 4:
                    item.state = [self controlStateForBool:g_Config.bAutoFrameSkip];
                    break;
				case (int)ShowStatusFlags::FPS_COUNTER + 100:
                    item.state = [self controlStateForBool:g_Config.iShowStatusFlags & (int)ShowStatusFlags::FPS_COUNTER];
                    break;
				case (int)ShowStatusFlags::SPEED_COUNTER + 100:
                    item.state = [self controlStateForBool:g_Config.iShowStatusFlags & (int)ShowStatusFlags::SPEED_COUNTER];
                    break;
				case (int)ShowStatusFlags::BATTERY_PERCENT + 100:
                    item.state = [self controlStateForBool:g_Config.iShowStatusFlags & (int)ShowStatusFlags::BATTERY_PERCENT];
                    break;
                default:
                    break;
            }
        }
    }
}

-(NSString *)localizedString: (const char *)key category: (I18NCategory *)cat {
    return @(self.mainSettingsLocalization->T(key));
}

-(NSMenu *)makeHelpMenu {
	NSMenu *menu = [[NSMenu alloc] initWithTitle:@"Help"];
	NSMenuItem *githubItem = [[NSMenuItem alloc] initWithTitle:@"Report an issue" action:@selector(reportAnIssue) keyEquivalent:@""];
	githubItem.target = self;
	[menu addItem:githubItem];
	
	NSMenuItem *discordItem = [[NSMenuItem alloc] initWithTitle:@"Join the Discord" action:@selector(joinTheDiscord) keyEquivalent:@""];
	discordItem.target = self;
	[menu addItem:discordItem];
	return menu;
}

-(void)reportAnIssue {
	[[NSWorkspace sharedWorkspace] openURL:[NSURL URLWithString:@"https://github.com/hrydgard/ppsspp/issues/new/choose"]];
}

-(void)joinTheDiscord {
	[[NSWorkspace sharedWorkspace] openURL:[NSURL URLWithString:@"https://discord.gg/5NJB6dD"]];
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
    
    self.graphicsLocalization = GetI18NCategory(I18NCat::GRAPHICS);
#define GRAPHICS_LOCALIZED(key) @(self.graphicsLocalization->T(key))
    
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
    
#define MENU_ITEM(variableName, localizedTitleName, SEL, ConfigurationValueName, Tag) \
NSMenuItem *variableName = [[NSMenuItem alloc] initWithTitle:GRAPHICS_LOCALIZED(localizedTitleName) action:SEL keyEquivalent:@""]; \
variableName.target = self; \
variableName.tag = Tag; \
variableName.state = [self controlStateForBool: ConfigurationValueName];
    
    MENU_ITEM(softwareRendering, "Software Rendering", @selector(toggleSoftwareRendering:), g_Config.bSoftwareRendering, 1)
    [parent addItem:softwareRendering];
    
    MENU_ITEM(vsyncItem, "VSync", @selector(toggleVSync:), g_Config.bVSync, 2)
    [parent addItem:vsyncItem];
    
    MENU_ITEM(fullScreenItem, "Fullscreen", @selector(toggleFullScreen:), g_Config.bFullScreen, 3)
    [parent addItem:fullScreenItem];
    
    [parent addItem:[NSMenuItem separatorItem]];
    
    MENU_ITEM(autoFrameSkip, "Auto FrameSkip", @selector(toggleAutoFrameSkip:), g_Config.bAutoFrameSkip, 4)
    [parent addItem:autoFrameSkip];
    
    [parent addItem:[NSMenuItem separatorItem]];
    
    MENU_ITEM(fpsCounterItem, "Show FPS Counter", @selector(setToggleShowCounterItem:), g_Config.iShowStatusFlags & (int)ShowStatusFlags::FPS_COUNTER, 5)
    fpsCounterItem.tag = (int)ShowStatusFlags::FPS_COUNTER + 100;
    
    MENU_ITEM(speedCounterItem, "Show Speed", @selector(setToggleShowCounterItem:), g_Config.iShowStatusFlags & (int)ShowStatusFlags::SPEED_COUNTER, 6)
    speedCounterItem.tag = (int)ShowStatusFlags::SPEED_COUNTER + 100; // because of menuNeedsUpdate:
    
    MENU_ITEM(batteryPercentItem, "Show battery %", @selector(setToggleShowCounterItem:), g_Config.iShowStatusFlags & (int)ShowStatusFlags::BATTERY_PERCENT, 7)
    batteryPercentItem.tag = (int)ShowStatusFlags::BATTERY_PERCENT + 100;
    
    [parent addItem:[NSMenuItem separatorItem]];
    [parent addItem:fpsCounterItem];
    [parent addItem:speedCounterItem];
    [parent addItem:batteryPercentItem];
	
#undef MENU_ITEM
#undef GRAPHICS_LOCALIZED
    return parent;
}

/*
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
 */

/*
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
 */

#define TOGGLE_METHOD(name, ConfigValueName, ...) \
-(void)toggle##name: (NSMenuItem *)item { \
ConfigValueName = !ConfigValueName; \
__VA_ARGS__; /* for any additional updates */ \
item.state = [self controlStateForBool: ConfigValueName]; \
}

TOGGLE_METHOD(Sound, g_Config.bEnableSound)
TOGGLE_METHOD(AutoFrameSkip, g_Config.bAutoFrameSkip, g_Config.UpdateAfterSettingAutoFrameSkip())
TOGGLE_METHOD(SoftwareRendering, g_Config.bSoftwareRendering)
TOGGLE_METHOD(FullScreen, g_Config.bFullScreen, System_MakeRequest(SystemRequestType::TOGGLE_FULLSCREEN_STATE, 0, g_Config.UseFullScreen() ? "1" : "0", "", 3))
TOGGLE_METHOD(VSync, g_Config.bVSync)
#undef TOGGLE_METHOD

-(void)setToggleShowCounterItem: (NSMenuItem *)item {
    [self addOrRemoveInteger:(int)(item.tag - 100) to:&g_Config.iShowStatusFlags];
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
    NativeMessageReceived("boot", g_Config.RecentIsos()[item.tag].c_str());
}

-(void)openSystemFileBrowser {
	int g = 0;
	DarwinDirectoryPanelCallback callback = [g] (bool succ, Path thePathChosen) {
		if (succ)
			NativeMessageReceived("boot", thePathChosen.c_str());
	};

	DarwinFileSystemServices services;
	services.presentDirectoryPanel(callback, /* allowFiles = */ true, /* allowDirectorites = */ true);
}

- (void)dealloc {
    [NSNotificationCenter.defaultCenter removeObserver:self];
}

@end

#ifdef __cplusplus
}
#endif
