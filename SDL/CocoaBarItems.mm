//
//  CocoaBarItems.mm
//  PPSSPP
//
//  Created by Serena on 06/02/2023.
//

#import <Cocoa/Cocoa.h>
#import "PPSSPPAboutViewController.h"

#include "Core/Util/DarwinFileSystemServices.h"
#include "UI/PSPNSApplicationDelegate.h"
#include "UI/PauseScreen.h"

#include "Core/Debugger/SymbolMap.h"
#include "Core/MemMap.h"
#include "Core/System.h"
#include "Core/Core.h"
#include "Core/SaveState.h"
#include "Core/RetroAchievements.h"
#include "Core/ELF/ParamSFO.h"
#include "Core/HLE/sceNet.h"
#include "Core/HLE/sceUmd.h"
#include "GPU/GPUCommon.h"
#include "Common/File/Path.h"
#include "Common/System/System.h"
#include "Common/System/Request.h"
#include "Common/System/NativeApp.h"
#include "Common/System/OSD.h"
#include "Common/Data/Text/I18n.h"
#include "Common/StringUtils.h"
#include "Core/Config.h"
#include "Core/ConfigValues.h"
#include "Core/Util/RecentFiles.h"

#ifdef __cplusplus
extern "C" {
#endif

extern bool g_TakeScreenshot;

typedef BOOL (^MenuCondition)(void);

// A menu item that carries its own action, and optionally how to tell whether it's checked,
// enabled or what it's called right now. These are checked each time the menu is shown.
@interface PPSSPPMenuItem : NSMenuItem
@property (copy) dispatch_block_t onSelect;
@property (copy) MenuCondition checkedWhen;
@property (copy) MenuCondition enabledWhen;
@property (copy) NSString *(^currentTitle)(void);
@end

@implementation PPSSPPMenuItem
@end

// NSMenuItem requires the use of an objective-c selector (aka the devil's greatest trick)
// So we have to make this class
@interface BarItemsManager : NSObject <NSMenuDelegate, NSMenuItemValidation>
+(instancetype)sharedInstance;
-(void)setupAppBarItems;
@property (strong) NSMenu *recentsMenu;
@property (strong) NSMenu *saveStateSlotMenu;
@end

void initializeOSXExtras() {
    [NSApplication.sharedApplication setDelegate:[PSPNSApplicationDelegate sharedAppDelegate]];
    [[BarItemsManager sharedInstance] setupAppBarItems];
}

void OSXShowInFinder(const char *path) {
    NSURL *url = [NSURL fileURLWithPath:@(path)];
    [NSWorkspace.sharedWorkspace activateFileViewerSelectingURLs:@[url]];
}

void OSXOpenURL(const char *url) {
    NSURL *nsURL = [NSURL URLWithString:@(url)];
    [NSWorkspace.sharedWorkspace openURL:nsURL];
}

static NSString *MenuString(I18NCat cat, const char *key) {
    // Falls back to the key itself: not every menu string is in the language files.
    return @(UnescapeMenuString(T_cstr(cat, key, key), nullptr).c_str());
}

static NSString *DesktopUI(const char *key) {
    return MenuString(I18NCat::DESKTOPUI, key);
}

static BOOL InGame() {
    return GetUIState() == UISTATE_INGAME;
}

static void SaveStateActionFinished(SaveState::Status status, std::string_view message, std::string_view metadata) {
    ShowMessageAfterSaveStateAction(status, message, metadata);
}

static bool CanUseSaveStates(bool isSaveAction) {
    return !Achievements::WarnUserIfHardcoreModeActive(isSaveAction) && !NetworkWarnUserIfOnlineAndCantSavestate();
}

@implementation BarItemsManager
+ (instancetype)sharedInstance {
    static BarItemsManager *stub;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        stub = [BarItemsManager new];
    });
    return stub;
}

// Menu construction helpers.

-(NSMenu *)newMenu: (NSString *)title {
    NSMenu *menu = [[NSMenu alloc] initWithTitle:title];
    menu.delegate = self;
    return menu;
}

-(NSMenu *)addSubmenu: (NSString *)title to: (NSMenu *)menu {
    NSMenuItem *item = [[NSMenuItem alloc] initWithTitle:title action:nil keyEquivalent:@""];
    item.submenu = [self newMenu:title];
    [menu addItem:item];
    return item.submenu;
}

-(PPSSPPMenuItem *)addItem: (NSString *)title to: (NSMenu *)menu action: (dispatch_block_t)action {
    PPSSPPMenuItem *item = [[PPSSPPMenuItem alloc] initWithTitle:title action:@selector(runItem:) keyEquivalent:@""];
    item.target = self;
    item.onSelect = action;
    [menu addItem:item];
    return item;
}

-(PPSSPPMenuItem *)addToggle: (NSString *)title to: (NSMenu *)menu value: (bool *)value then: (dispatch_block_t)after {
    PPSSPPMenuItem *item = [self addItem:title to:menu action:^{
        *value = !*value;
        if (after) {
            after();
        }
    }];
    item.checkedWhen = ^BOOL { return *value; };
    return item;
}

-(PPSSPPMenuItem *)addChoice: (NSString *)title to: (NSMenu *)menu value: (int *)value is: (int)choice then: (dispatch_block_t)after {
    PPSSPPMenuItem *item = [self addItem:title to:menu action:^{
        *value = choice;
        if (after) {
            after();
        }
    }];
    item.checkedWhen = ^BOOL { return *value == choice; };
    return item;
}

-(void)addSeparatorTo: (NSMenu *)menu {
    [menu addItem:[NSMenuItem separatorItem]];
}

-(void)runItem: (PPSSPPMenuItem *)item {
    if (item.onSelect) {
        item.onSelect();
    }
}

- (BOOL)validateMenuItem:(NSMenuItem *)menuItem {
    if (![menuItem isKindOfClass:[PPSSPPMenuItem class]]) {
        return YES;
    }
    PPSSPPMenuItem *item = (PPSSPPMenuItem *)menuItem;
    if (item.checkedWhen) {
        item.state = item.checkedWhen() ? NSControlStateValueOn : NSControlStateValueOff;
    }
    if (item.currentTitle) {
        item.title = item.currentTitle();
    }
    return item.enabledWhen ? item.enabledWhen() : YES;
}

- (void)menuNeedsUpdate:(NSMenu *)menu {
    if (menu == self.recentsMenu) {
        [self rebuildRecentsMenu];
    } else if (menu == self.saveStateSlotMenu) {
        [self rebuildSaveStateSlotMenu];
    }
}

-(void)setupAppBarItems {
    NSMenu *mainMenu = NSApplication.sharedApplication.menu;
    for (NSMenu *menu in @[[self makeFileMenu], [self makeEmulationMenu], [self makeDebugMenu], [self makeGameSettingsMenu], [self makeHelpMenu]]) {
        NSMenuItem *item = [[NSMenuItem alloc] init];
        item.submenu = menu;
        [mainMenu addItem:item];
    }

    NSString *windowMenuItemTitle = @"Window";  // Don't translate, we lookup this.
    // Rearrange 'Window' to be behind 'Help'
    for (NSMenuItem *item in mainMenu.itemArray) {
        if ([item.title isEqualToString:windowMenuItemTitle]) {
            [mainMenu removeItem:item];
            // 'Help' is the last item in the bar
            // so we can just use `mainMenu.numberOfItems - 1`
            // as it's index
            [mainMenu insertItem:item atIndex:mainMenu.numberOfItems - 1];
            break;
        }
    }

    NSMenu *appMenu = mainMenu.itemArray.firstObject.submenu;
    for (NSMenuItem *item in appMenu.itemArray) {
        // about item, set action
        if ([item.title hasPrefix:@"About "]) {
            item.target = self;
            item.action = @selector(presentAboutMenu);

            // Settings go right after About, as in every other Mac app.
            NSInteger index = [appMenu indexOfItem:item] + 1;
            [appMenu insertItem:[NSMenuItem separatorItem] atIndex:index];
            PPSSPPMenuItem *settings = [self addItem:[MenuString(I18NCat::DIALOG, "Settings") stringByAppendingString:@"…"] to:appMenu action:^{
                System_PostUIMessage(UIMessage::SHOW_SETTINGS);
            }];
            settings.keyEquivalent = @",";
            [appMenu removeItem:settings];
            [appMenu insertItem:settings atIndex:index + 1];
            break;
        }
    }
}

-(void)presentAboutMenu {
    NSWindow *window = [NSWindow windowWithContentViewController:[PPSSPPAboutViewController new]];
    window.title = @"PPSSPP";
    window.titleVisibility = NSWindowTitleHidden;
    window.titlebarAppearsTransparent = YES;
    window.styleMask &= ~NSWindowStyleMaskResizable;
    [[window standardWindowButton:NSWindowMiniaturizeButton] setEnabled:NO];

    window.backgroundColor = [NSColor colorWithName:nil dynamicProvider:^NSColor * _Nonnull(NSAppearance * _Nonnull appearance) {
        /* no I can't use switch statements here it's an NSString pointer */
        if (appearance.name == NSAppearanceNameDarkAqua ||
            appearance.name == NSAppearanceNameAccessibilityHighContrastVibrantDark ||
            appearance.name == NSAppearanceNameAccessibilityHighContrastDarkAqua ||
            appearance.name == NSAppearanceNameVibrantDark)
            return [NSColor colorWithRed:0.19 green:0.19 blue:0.19 alpha:1];
        return [NSColor whiteColor];
    }];

    [[[NSWindowController alloc] initWithWindow:window] showWindow:nil];
}

-(NSMenu *)makeFileMenu {
    NSMenu *menu = [self newMenu:DesktopUI("File")];

    PPSSPPMenuItem *load = [self addItem:DesktopUI("Load") to:menu action:^{
        DarwinFileSystemServices::presentDirectoryPanel([](bool succ, Path thePathChosen) {
            if (succ)
                System_PostUIMessage(UIMessage::REQUEST_GAME_BOOT, thePathChosen.c_str());
        }, /* allowFiles = */ true, /* allowDirectories = */ true);
    }];
    load.keyEquivalent = @"o";

    NSMenuItem *openRecent = [[NSMenuItem alloc] initWithTitle:MenuString(I18NCat::MAINMENU, "Recent") action:nil keyEquivalent:@""];
    self.recentsMenu = [self newMenu:openRecent.title];
    openRecent.submenu = self.recentsMenu;
    [menu addItem:openRecent];
    [self rebuildRecentsMenu];

    [self addSeparatorTo:menu];
    [self addItem:DesktopUI("Open Memory Stick") to:menu action:^{
        [NSWorkspace.sharedWorkspace openURL:[NSURL fileURLWithPath:@(g_Config.memStickDirectory.c_str())]];
    }];

    [self addSeparatorTo:menu];
    [self addItem:DesktopUI("Load XMB (VSH)") to:menu action:^{
        System_PostUIMessage(UIMessage::REQUEST_GAME_BOOT, (g_Config.nandRootDirectory / "flash0/vsh/module/vshmain.prx").ToString());
    }];

    [self addSeparatorTo:menu];
    MenuCondition inGame = ^BOOL { return InGame(); };

    NSMenuItem *slotItem = [[NSMenuItem alloc] initWithTitle:DesktopUI("Savestate Slot") action:nil keyEquivalent:@""];
    self.saveStateSlotMenu = [self newMenu:slotItem.title];
    slotItem.submenu = self.saveStateSlotMenu;
    [menu addItem:slotItem];
    [self rebuildSaveStateSlotMenu];

    PPSSPPMenuItem *loadState = [self addItem:DesktopUI("Load State") to:menu action:^{
        if (CanUseSaveStates(false)) {
            SaveState::LoadSlot(SaveState::GetGamePrefix(g_paramSFO), g_Config.iCurrentStateSlot, &SaveStateActionFinished);
        }
    }];
    loadState.keyEquivalent = @"l";
    loadState.enabledWhen = inGame;

    PPSSPPMenuItem *saveState = [self addItem:DesktopUI("Save State") to:menu action:^{
        if (CanUseSaveStates(true)) {
            SaveState::SaveSlot(SaveState::GetGamePrefix(g_paramSFO), g_Config.iCurrentStateSlot, &SaveStateActionFinished);
        }
    }];
    saveState.keyEquivalent = @"s";
    saveState.enabledWhen = inGame;

    [self addItem:DesktopUI("Load State File...") to:menu action:^{
        if (!CanUseSaveStates(false)) {
            return;
        }
        NSURL *url = [BarItemsManager presentOpenPanelWithAllowedFileTypes:@[@"ppst"]];
        if (url) {
            SaveState::Load(Path(url.fileSystemRepresentation), -1, &SaveStateActionFinished);
        }
    }].enabledWhen = inGame;

    [self addItem:DesktopUI("Save State File...") to:menu action:^{
        if (!CanUseSaveStates(true)) {
            return;
        }
        NSURL *url = [BarItemsManager presentSavePanelWithDefaultFilename:@"state.ppst"];
        if (url) {
            SaveState::Save(Path(url.fileSystemRepresentation), -1, &SaveStateActionFinished);
        }
    }].enabledWhen = inGame;

    return menu;
}

-(void)rebuildRecentsMenu {
    [self.recentsMenu removeAllItems];
    for (const auto &file : g_recentFiles.GetRecentFiles()) {
        std::string path = file;
        [self addItem:@(Path(file).GetFilename().c_str()) to:self.recentsMenu action:^{
            System_PostUIMessage(UIMessage::REQUEST_GAME_BOOT, path);
        }];
    }
}

-(void)rebuildSaveStateSlotMenu {
    // The slot count is a setting, so this can change.
    [self.saveStateSlotMenu removeAllItems];
    auto di = GetI18NCategory(I18NCat::DIALOG);
    for (int i = 0; i < g_Config.iSaveStateSlotCount; ++i) {
        std::string label = ApplySafeSubstitutions(di->T("Slot %1"), i + 1);
        PPSSPPMenuItem *item = [self addItem:@(label.c_str()) to:self.saveStateSlotMenu action:^{
            if (CanUseSaveStates(true)) {
                g_Config.iCurrentStateSlot = i;
            }
        }];
        item.checkedWhen = ^BOOL { return g_Config.iCurrentStateSlot == i; };
    }
}

-(NSMenu *)makeEmulationMenu {
    NSMenu *menu = [self newMenu:DesktopUI("Emulation")];
    MenuCondition inGame = ^BOOL { return InGame(); };

    [self addItem:DesktopUI("Pause") to:menu action:^{
        System_PostUIMessage(UIMessage::REQUEST_GAME_PAUSE);
    }].enabledWhen = inGame;
    [self addItem:DesktopUI("Stop") to:menu action:^{
        System_PostUIMessage(UIMessage::REQUEST_GAME_STOP);
    }].enabledWhen = inGame;
    [self addItem:DesktopUI("Reset") to:menu action:^{
        System_PostUIMessage(UIMessage::REQUEST_GAME_RESET);
        Core_Resume();
    }].enabledWhen = inGame;
    [self addItem:DesktopUI("Switch UMD") to:menu action:^{
        System_BrowseForFile(NON_EPHEMERAL_TOKEN, T(I18NCat::MAINMENU, "Switch UMD"), BrowseFileType::BOOTABLE, [](std::string_view value, int) {
            // The callback runs on the emu thread.
            __UmdReplace(Path(value));
        });
    }].enabledWhen = inGame;

    return menu;
}

-(NSMenu *)makeDebugMenu {
    NSMenu *menu = [self newMenu:DesktopUI("Debugging")];
    MenuCondition inGame = ^BOOL { return InGame(); };

    PPSSPPMenuItem *breakItem = [self addItem:DesktopUI("Break") to:menu action:^{
        if (Core_IsStepping()) {
            Core_Resume();
        } else {
            Core_Break(BreakReason::DebugBreak, 0);
        }
    }];
    breakItem.enabledWhen = inGame;
    breakItem.currentTitle = ^NSString * {
        return Core_IsStepping() ? MenuString(I18NCat::DEVELOPER, "Resume") : DesktopUI("Break");
    };
    [self addSeparatorTo:menu];

    [self addItem:DesktopUI("Break on Load") to:menu action:^{
        g_Config.bAutoRun = !g_Config.bAutoRun;
    }].checkedWhen = ^BOOL { return !g_Config.bAutoRun; };
    [self addToggle:DesktopUI("Ignore Illegal Reads/Writes") to:menu value:&g_Config.bIgnoreBadMemAccess then:nil];
    [self addSeparatorTo:menu];

    [self addItem:DesktopUI("Load Map File...") to:menu action:^{
        NSURL *url = [BarItemsManager presentOpenPanelWithAllowedFileTypes:@[@"map"]];
        if (url)
            g_symbolMap->LoadSymbolMap(Path(url.fileSystemRepresentation));
    }].enabledWhen = inGame;
    [self addItem:DesktopUI("Save Map file...") to:menu action:^{
        NSURL *url = [BarItemsManager presentSavePanelWithDefaultFilename:@"Symbols.map"];
        if (url)
            g_symbolMap->SaveSymbolMap(Path(url.fileSystemRepresentation));
    }].enabledWhen = inGame;
    [self addSeparatorTo:menu];

    [self addItem:DesktopUI("Load .sym File...") to:menu action:^{
        NSURL *url = [BarItemsManager presentOpenPanelWithAllowedFileTypes:@[@"sym"]];
        if (url)
            g_symbolMap->LoadNocashSym(Path(url.fileSystemRepresentation));
    }].enabledWhen = inGame;
    [self addItem:DesktopUI("Save .sym File...") to:menu action:^{
        NSURL *url = [BarItemsManager presentSavePanelWithDefaultFilename:@"Symbols.sym"];
        if (url)
            g_symbolMap->SaveNocashSym(Path(url.fileSystemRepresentation));
    }].enabledWhen = inGame;
    [self addSeparatorTo:menu];

    [self addItem:DesktopUI("Reset Symbol Table") to:menu action:^{
        g_symbolMap->Clear();
    }].enabledWhen = inGame;
    [self addSeparatorTo:menu];

    [self addItem:DesktopUI("Take Screenshot") to:menu action:^{
        g_TakeScreenshot = true;
    }].enabledWhen = inGame;
    [self addItem:DesktopUI("Save frame dump") to:menu action:^{
        System_PostUIMessage(UIMessage::SAVE_FRAME_DUMP);
    }].enabledWhen = inGame;
    [self addItem:DesktopUI("Show Debug Statistics") to:menu action:^{
        const bool on = (DebugOverlay)g_Config.iDebugOverlay == DebugOverlay::DEBUG_STATS;
        g_Config.iDebugOverlay = (int)(on ? DebugOverlay::OFF : DebugOverlay::DEBUG_STATS);
        System_PostUIMessage(UIMessage::REQUEST_CLEAR_JIT);
    }].checkedWhen = ^BOOL { return (DebugOverlay)g_Config.iDebugOverlay == DebugOverlay::DEBUG_STATS; };
    [self addSeparatorTo:menu];

    [self addItem:DesktopUI("Copy PSP memory base address") to:menu action:^{
        NSString *stringToCopy = [NSString stringWithFormat: @"%016llx", (uint64_t)(uintptr_t)Memory::base];
        [NSPasteboard.generalPasteboard declareTypes:@[NSPasteboardTypeString] owner:nil];
        [NSPasteboard.generalPasteboard setString:stringToCopy forType:NSPasteboardTypeString];
    }].enabledWhen = inGame;

    return menu;
}

-(NSMenu *)makeGameSettingsMenu {
    NSMenu *menu = [self newMenu:DesktopUI("Game Settings")];
    auto graphics = [](const char *key) { return MenuString(I18NCat::GRAPHICS, key); };

    [self addItem:DesktopUI("More Settings...") to:menu action:^{
        System_PostUIMessage(UIMessage::SHOW_SETTINGS);
    }];
    [self addItem:DesktopUI("Control Mapping...") to:menu action:^{
        System_PostUIMessage(UIMessage::SHOW_CONTROL_MAPPING);
    }];
    [self addItem:DesktopUI("Display Layout && Effects") to:menu action:^{
        System_PostUIMessage(UIMessage::SHOW_DISPLAY_LAYOUT_EDITOR);
    }];
    [self addItem:DesktopUI("Language...") to:menu action:^{
        System_PostUIMessage(UIMessage::SHOW_LANGUAGE_SCREEN);
    }];
    [self addSeparatorTo:menu];

    NSMenu *backends = [self addSubmenu:DesktopUI("Backend") to:menu];
    for (GPUBackend backend : { GPUBackend::OPENGL, GPUBackend::VULKAN }) {
        if (!g_Config.IsBackendEnabled(backend)) {
            continue;
        }
        [self addItem:@(GPUBackendToString(backend).c_str()) to:backends action:^{
            if (g_Config.iGPUBackend != (int)backend) {
                g_Config.iGPUBackend = (int)backend;
                // This does a clean shutdown, so the config will be saved automatically.
                System_RestartApp("");
            }
        }].checkedWhen = ^BOOL { return g_Config.iGPUBackend == (int)backend; };
    }
    [self addToggle:graphics("Software Rendering") to:menu value:&g_Config.bSoftwareRendering then:nil];
    [self addToggle:DesktopUI("Fullscreen") to:menu value:&g_Config.bFullScreen then:^{
        System_ApplyFullscreenState();
    }];
    [self addSeparatorTo:menu];

    NSMenu *resolution = [self addSubmenu:DesktopUI("Rendering Resolution") to:menu];
    dispatch_block_t resized = ^{
        System_PostUIMessage(UIMessage::GPU_RENDER_RESIZED);
    };
    [self addChoice:DesktopUI("Auto") to:resolution value:&g_Config.iInternalResolution is:0 then:resized];
    for (int i = 1; i <= 10; i++) {
        [self addChoice:[NSString stringWithFormat:@"%dx", i] to:resolution value:&g_Config.iInternalResolution is:i then:resized];
    }

    NSMenu *filtering = [self addSubmenu:DesktopUI("Texture Filtering") to:menu];
    [self addChoice:DesktopUI("Auto") to:filtering value:&g_Config.iTexFiltering is:TEX_FILTER_AUTO then:nil];
    [self addChoice:DesktopUI("Nearest") to:filtering value:&g_Config.iTexFiltering is:TEX_FILTER_FORCE_NEAREST then:nil];
    [self addChoice:DesktopUI("Linear") to:filtering value:&g_Config.iTexFiltering is:TEX_FILTER_FORCE_LINEAR then:nil];
    [self addChoice:DesktopUI("Auto Max Quality") to:filtering value:&g_Config.iTexFiltering is:TEX_FILTER_AUTO_MAX_QUALITY then:nil];
    [self addSeparatorTo:filtering];
    [self addToggle:DesktopUI("Smart 2D texture filtering") to:filtering value:&g_Config.bSmart2DTexFiltering then:nil];

    [self addToggle:graphics("Auto FrameSkip") to:menu value:&g_Config.bAutoFrameSkip then:^{
        g_Config.UpdateAfterSettingAutoFrameSkip();
    }];
    [self addSeparatorTo:menu];

    for (auto [flag, title] : { std::pair{ ShowStatusFlags::FPS_COUNTER, DesktopUI("Show FPS Counter") },
                                std::pair{ ShowStatusFlags::SPEED_COUNTER, graphics("Show Speed") },
                                std::pair{ ShowStatusFlags::BATTERY_PERCENT, graphics("Show Battery %") } }) {
        const int mask = (int)flag;
        [self addItem:title to:menu action:^{
            g_Config.iShowStatusFlags ^= mask;
        }].checkedWhen = ^BOOL { return (g_Config.iShowStatusFlags & mask) != 0; };
    }
    [self addSeparatorTo:menu];

    [self addToggle:DesktopUI("Enable Sound") to:menu value:&g_Config.bEnableSound then:nil];
    [self addToggle:DesktopUI("Enable Cheats") to:menu value:&g_Config.bEnableCheats then:^{
        g_OSD.ShowOnOff(T(I18NCat::GRAPHICS, "Cheats"), g_Config.bEnableCheats);
    }];

    return menu;
}

-(NSMenu *)makeHelpMenu {
    NSMenu *menu = [self newMenu:DesktopUI("Help")];
    auto addLink = ^(NSString *title, const char *url) {
        std::string link = url;
        [self addItem:title to:menu action:^{
            System_LaunchUrl(LaunchUrlType::BROWSER_URL, link);
        }];
    };

    addLink(DesktopUI("www.ppsspp.org"), "https://www.ppsspp.org/");
    addLink(DesktopUI("PPSSPP Forums"), "https://forums.ppsspp.org/");
    if (!System_GetPropertyBool(SYSPROP_APP_GOLD)) {
        addLink(DesktopUI("Buy PPSSPP Gold"), "https://www.ppsspp.org/buygold");
    }
    [self addSeparatorTo:menu];
    addLink(DesktopUI("GitHub"), "https://github.com/hrydgard/ppsspp/");
    addLink(@"Report an issue", "https://github.com/hrydgard/ppsspp/issues/new/choose");
    addLink(DesktopUI("Discord"), "https://discord.gg/5NJB6dD");
    return menu;
}

+(NSURL *)presentOpenPanelWithAllowedFileTypes: (NSArray<NSString *> *)allowedFileTypes {
    NSOpenPanel *openPanel = [[NSOpenPanel alloc] init];
    openPanel.allowedFileTypes = allowedFileTypes;
    if ([openPanel runModal] == NSModalResponseOK) {
        NSURL *urlWeWant = openPanel.URLs.firstObject;
        if (urlWeWant) {
            return urlWeWant;
        }
    }
    return nil;
}

+(NSURL *)presentSavePanelWithDefaultFilename: (NSString *)filename {
    NSSavePanel *savePanel = [[NSSavePanel alloc] init];
    savePanel.nameFieldStringValue = filename;
    if ([savePanel runModal] == NSModalResponseOK && savePanel.URL) {
        return savePanel.URL;
    }
    return nil;
}

@end

#ifdef __cplusplus
}
#endif
