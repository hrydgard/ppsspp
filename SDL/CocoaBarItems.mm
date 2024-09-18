//
//  CocoaBarItems.mm
//  PPSSPP
//
//  Created by Serena on 06/02/2023.
//

#import <Cocoa/Cocoa.h>
#import "PPSSPPAboutViewController.h"

#include "UI/DarwinFileSystemServices.h"
#include "UI/PSPNSApplicationDelegate.h"

#include "Core/Debugger/SymbolMap.h"
#include "Core/MemMap.h"
#include "Core/System.h"
#include "Core/Core.h"
#include "GPU/GPUInterface.h"
#include "Common/File/Path.h"
#include "Common/System/System.h"
#include "Common/System/Request.h"
#include "Common/System/NativeApp.h"
#include "Core/Config.h"
#include "Common/Data/Text/I18n.h"
#include "Common/StringUtils.h"

#ifdef __cplusplus
extern "C" {
#endif

extern bool g_TakeScreenshot;

#define MENU_ITEM(variableName, localizedTitleName, SEL, ConfigurationValueName, Tag) \
NSMenuItem *variableName = [[NSMenuItem alloc] initWithTitle:localizedTitleName action:SEL keyEquivalent:@""]; \
variableName.target = self; \
variableName.tag = Tag; \
variableName.state = [self controlStateForBool: ConfigurationValueName];

// NSMenuItem requires the use of an objective-c selector (aka the devil's greatest trick)
// So we have to make this class
@interface BarItemsManager : NSObject <NSMenuDelegate>
+(instancetype)sharedInstance;
-(void)setupAppBarItems;
@property (assign) NSMenu *fileMenu;
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

@implementation BarItemsManager
+ (instancetype)sharedInstance {
    static BarItemsManager *stub;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        stub = [BarItemsManager new];
    });
    return stub;
}

-(NSString *)localizedString: (const char *)key category: (I18NCat)cat {
    return @(T_cstr(cat, key));
}

-(NSString *)localizedMenuString: (const char *)key {
    std::string processed = UnescapeMenuString(T_cstr(I18NCat::DESKTOPUI, key), nullptr);
    return @(processed.c_str());
}

-(void)setupAppBarItems {
    
    NSMenuItem *fileMenuItem = [[NSMenuItem alloc] init];
    fileMenuItem.submenu = [self makeFileSubmenu];
    fileMenuItem.submenu.delegate = self;

    NSMenuItem *emulationMenuItem = [[NSMenuItem alloc] init];
    emulationMenuItem.submenu = [self makeEmulationMenu];
    emulationMenuItem.submenu.delegate = self;

    NSMenuItem *debugMenuItem = [[NSMenuItem alloc] init];
    debugMenuItem.submenu = [self makeDebugMenu];
    debugMenuItem.submenu.delegate = self;

    NSMenuItem *graphicsMenuItem = [[NSMenuItem alloc] init];
    graphicsMenuItem.submenu = [self makeGraphicsMenu];
    graphicsMenuItem.submenu.delegate = self;
    
    NSMenuItem *helpMenuItem = [[NSMenuItem alloc] init];
    helpMenuItem.submenu = [self makeHelpMenu];
    
    [NSApplication.sharedApplication.menu addItem:fileMenuItem];
    [NSApplication.sharedApplication.menu addItem:emulationMenuItem];
    [NSApplication.sharedApplication.menu addItem:debugMenuItem];
    [NSApplication.sharedApplication.menu addItem:graphicsMenuItem];
    [NSApplication.sharedApplication.menu addItem:helpMenuItem];
    
    NSString *windowMenuItemTitle = @"Window";  // Don't translate, we lookup this.
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

    NSArray <NSMenuItem *> *firstSubmenu = NSApp.menu.itemArray.firstObject.submenu.itemArray;
    for (NSMenuItem *item in firstSubmenu) {
        // about item, set action
        if ([item.title hasPrefix:@"About "]) {
            item.target = self;
            item.action = @selector(presentAboutMenu);
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
    
    if (@available(macOS 10.15, *)) {
        window.backgroundColor = [NSColor colorWithName:nil dynamicProvider:^NSColor * _Nonnull(NSAppearance * _Nonnull appearance) {
            // check for dark/light mode (dark mode is OS X 10.14+ only)
            /* no I can't use switch statements here it's an NSString pointer */
            if (appearance.name == NSAppearanceNameDarkAqua ||
                appearance.name == NSAppearanceNameAccessibilityHighContrastVibrantDark ||
                appearance.name == NSAppearanceNameAccessibilityHighContrastDarkAqua ||
                appearance.name == NSAppearanceNameVibrantDark)
                return [NSColor colorWithRed:0.19 green:0.19 blue:0.19 alpha:1];
            
            // macOS pre 10.14 is always light mode
            return [NSColor whiteColor];
        }];
    } else {
        window.backgroundColor = [NSColor whiteColor];
    }
    
    [[[NSWindowController alloc] initWithWindow:window] showWindow:nil];
}

- (void)menuNeedsUpdate:(NSMenu *)menu {
    if ([menu.title isEqualToString: [self localizedMenuString:"Emulation"]]) {
        menu.autoenablesItems = NO;
        // Enable/disable the various items.
        for (NSMenuItem *item in menu.itemArray) {
            switch (item.tag) {
            case 1:
            case 2:
            case 3:
            {
                GlobalUIState state = GetUIState();
                item.enabled = state == UISTATE_INGAME ? YES : NO;
                printf("Setting enabled state to %d\n", (int)item.enabled);
                break;
            }
            default:
                printf("Unknown tag %d\n", (int)(long)item.tag);
                break;
            }
        }
    } else if ([menu.title isEqualToString: [self localizedMenuString:"Graphics"]]) {
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
    } else if ([menu.title isEqualToString: [self localizedMenuString:"Debug"]]) {
        menu.autoenablesItems = NO;
        GlobalUIState state = GetUIState();
        for (NSMenuItem *item in menu.itemArray) {
            switch ([item tag]) {
                case 2:
                    item.state = [self controlStateForBool:!g_Config.bAutoRun];
                    break;
                case 3:
                    item.state = [self controlStateForBool:g_Config.bIgnoreBadMemAccess];
                    break;
                case 12:
                    item.state = [self controlStateForBool:(DebugOverlay)g_Config.iDebugOverlay == DebugOverlay::DEBUG_STATS];
                    break;
                default:
                    item.enabled = state == UISTATE_INGAME ? YES : NO;
                    break;
            }
        }
    }
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

-(NSMenu *)makeFileSubmenu {
    std::shared_ptr<I18NCategory> desktopUILocalization = GetI18NCategory(I18NCat::DESKTOPUI);
#define DESKTOPUI_LOCALIZED(key) @(UnescapeMenuString(desktopUILocalization->T_cstr(key), nil).c_str())

    NSMenu *menu = [[NSMenu alloc] initWithTitle:DESKTOPUI_LOCALIZED("File")];
    NSMenuItem *openWithSystemFolderBrowserItem = [[NSMenuItem alloc] initWithTitle:DESKTOPUI_LOCALIZED("Load") action:@selector(openSystemFileBrowser) keyEquivalent:@"o"];
    openWithSystemFolderBrowserItem.keyEquivalentModifierMask = NSEventModifierFlagCommand;
    openWithSystemFolderBrowserItem.enabled = YES;
    openWithSystemFolderBrowserItem.target = self;
    [menu addItem:openWithSystemFolderBrowserItem];
    self.fileMenu = menu;
    [self addOpenRecentlyItem];

    [self.fileMenu addItem:[NSMenuItem separatorItem]];

    NSMenuItem *openMemstickFolderItem = [[NSMenuItem alloc] initWithTitle:DESKTOPUI_LOCALIZED("Open Memory Stick") action:@selector(openMemstickFolder) keyEquivalent:@""];
    openMemstickFolderItem.target = self;
    [self.fileMenu addItem:openMemstickFolderItem];

    return menu;
}

-(NSMenu *)makeGraphicsMenu {    
    std::shared_ptr<I18NCategory> mainSettingsLocalization = GetI18NCategory(I18NCat::MAINSETTINGS);
    std::shared_ptr<I18NCategory> graphicsLocalization = GetI18NCategory(I18NCat::GRAPHICS);
    std::shared_ptr<I18NCategory> desktopUILocalization = GetI18NCategory(I18NCat::DESKTOPUI);

    NSMenu *parent = [[NSMenu alloc] initWithTitle:@(mainSettingsLocalization->T_cstr("Graphics"))];
    NSMenu *backendsMenu = [[NSMenu alloc] init];
#define GRAPHICS_LOCALIZED(key) @(graphicsLocalization->T_cstr(key))
#define DESKTOPUI_LOCALIZED(key) @(UnescapeMenuString(desktopUILocalization->T_cstr(key), nil).c_str())

    NSMenuItem *gpuBackendItem = [[NSMenuItem alloc] initWithTitle:DESKTOPUI_LOCALIZED("Backend") action:nil keyEquivalent:@""];

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

    MENU_ITEM(softwareRendering, GRAPHICS_LOCALIZED("Software Rendering"), @selector(toggleSoftwareRendering:), g_Config.bSoftwareRendering, 1)
    [parent addItem:softwareRendering];

    /*
    MENU_ITEM(vsyncItem, GRAPHICS_LOCALIZED("VSync"), @selector(toggleVSync:), g_Config.bVSync, 2)
    [parent addItem:vsyncItem];
    */

    MENU_ITEM(fullScreenItem, DESKTOPUI_LOCALIZED("Fullscreen"), @selector(toggleFullScreen:), g_Config.bFullScreen, 3)
    [parent addItem:fullScreenItem];

    [parent addItem:[NSMenuItem separatorItem]];

    MENU_ITEM(autoFrameSkip, GRAPHICS_LOCALIZED("Auto FrameSkip"), @selector(toggleAutoFrameSkip:), g_Config.bAutoFrameSkip, 4)
    [parent addItem:autoFrameSkip];

    [parent addItem:[NSMenuItem separatorItem]];

    MENU_ITEM(fpsCounterItem, DESKTOPUI_LOCALIZED("Show FPS Counter"), @selector(setToggleShowCounterItem:), g_Config.iShowStatusFlags & (int)ShowStatusFlags::FPS_COUNTER, 5)
    fpsCounterItem.tag = (int)ShowStatusFlags::FPS_COUNTER + 100;

    MENU_ITEM(speedCounterItem, GRAPHICS_LOCALIZED("Show Speed"), @selector(setToggleShowCounterItem:), g_Config.iShowStatusFlags & (int)ShowStatusFlags::SPEED_COUNTER, 6)
    speedCounterItem.tag = (int)ShowStatusFlags::SPEED_COUNTER + 100; // because of menuNeedsUpdate:

    MENU_ITEM(batteryPercentItem, GRAPHICS_LOCALIZED("Show battery %"), @selector(setToggleShowCounterItem:), g_Config.iShowStatusFlags & (int)ShowStatusFlags::BATTERY_PERCENT, 7)
    batteryPercentItem.tag = (int)ShowStatusFlags::BATTERY_PERCENT + 100;

    [parent addItem:[NSMenuItem separatorItem]];
    [parent addItem:fpsCounterItem];
    [parent addItem:speedCounterItem];
    [parent addItem:batteryPercentItem];
    
#undef GRAPHICS_LOCALIZED
    return parent;
}

-(NSMenu *)makeEmulationMenu {
    std::shared_ptr<I18NCategory> desktopUILocalization = GetI18NCategory(I18NCat::DESKTOPUI);
#define DESKTOPUI_LOCALIZED(key) @(UnescapeMenuString(desktopUILocalization->T_cstr(key), nil).c_str())

    NSMenu *parent = [[NSMenu alloc] initWithTitle:DESKTOPUI_LOCALIZED("Emulation")];

    NSMenuItem *pauseAction = [[NSMenuItem alloc] initWithTitle:DESKTOPUI_LOCALIZED("Pause") action:@selector(pauseAction:) keyEquivalent:@""];
    pauseAction.target = self;
    pauseAction.tag = 1;
    NSMenuItem *resetAction = [[NSMenuItem alloc] initWithTitle:DESKTOPUI_LOCALIZED("Reset") action:@selector(resetAction:) keyEquivalent:@""];
    resetAction.target = self;
    resetAction.tag = 2;

    [parent addItem:pauseAction];
    [parent addItem:resetAction];

    return parent;
}

-(NSMenu *)makeDebugMenu {
    std::shared_ptr<I18NCategory> sysInfoLocalization = GetI18NCategory(I18NCat::SYSINFO);
    std::shared_ptr<I18NCategory> desktopUILocalization = GetI18NCategory(I18NCat::DESKTOPUI);
#define DESKTOPUI_LOCALIZED(key) @(UnescapeMenuString(desktopUILocalization->T_cstr(key), nil).c_str())

    NSMenu *parent = [[NSMenu alloc] initWithTitle:DESKTOPUI_LOCALIZED("Debugging")];

    NSMenuItem *breakAction = [[NSMenuItem alloc] initWithTitle:DESKTOPUI_LOCALIZED("Break") action:@selector(breakAction:) keyEquivalent:@""];
    breakAction.tag = 1;
    breakAction.target = self;

    MENU_ITEM(breakOnLoadAction, DESKTOPUI_LOCALIZED("Break on Load"), @selector(toggleBreakOnLoad:), g_Config.bAutoRun, 2)
    MENU_ITEM(ignoreIllegalRWAction, DESKTOPUI_LOCALIZED("Ignore Illegal Reads/Writes"), @selector(toggleIgnoreIllegalRWs:), g_Config.bIgnoreBadMemAccess, 3)

    [parent addItem:breakAction];
    [parent addItem:[NSMenuItem separatorItem]];

    [parent addItem:breakOnLoadAction];
    [parent addItem:ignoreIllegalRWAction];
    [parent addItem:[NSMenuItem separatorItem]];

    // Tags 1, 2, and 3 are taken above.

    NSMenuItem *loadSymbolMapAction = [[NSMenuItem alloc] initWithTitle:DESKTOPUI_LOCALIZED("Load Map File...") action:@selector(loadMapFile) keyEquivalent:@""];
    loadSymbolMapAction.target = self;
    loadSymbolMapAction.tag = 4;

    NSMenuItem *saveMapFileAction = [[NSMenuItem alloc] initWithTitle:DESKTOPUI_LOCALIZED("Save Map file...") action:@selector(saveMapFile) keyEquivalent:@""];
    saveMapFileAction.target = self;
    saveMapFileAction.tag = 5;

    NSMenuItem *loadSymFileAction = [[NSMenuItem alloc] initWithTitle:DESKTOPUI_LOCALIZED("Load .sym File...") action:@selector(loadSymbolsFile) keyEquivalent:@""];
    loadSymFileAction.target = self;
    loadSymFileAction.tag = 6;

    NSMenuItem *saveSymFileAction = [[NSMenuItem alloc] initWithTitle:DESKTOPUI_LOCALIZED("Save .sym File...") action:@selector(saveSymbolsfile) keyEquivalent:@""];
    saveSymFileAction.target = self;
    saveSymFileAction.tag = 7;

    NSMenuItem *resetSymbolTableAction = [[NSMenuItem alloc] initWithTitle:DESKTOPUI_LOCALIZED("Reset Symbol Table") action:@selector(resetSymbolTable) keyEquivalent:@""];
    resetSymbolTableAction.target = self;
    resetSymbolTableAction.tag = 8;

    NSMenuItem *takeScreenshotAction = [[NSMenuItem alloc] initWithTitle:DESKTOPUI_LOCALIZED("Take Screenshot") action:@selector(takeScreenshot) keyEquivalent:@""];
    takeScreenshotAction.target = self;
    takeScreenshotAction.tag = 9;

    NSMenuItem *dumpNextFrameToLogAction = [[NSMenuItem alloc] initWithTitle:DESKTOPUI_LOCALIZED("Dump Next Frame to Log") action:@selector(dumpNextFrameToLog) keyEquivalent:@""];
    dumpNextFrameToLogAction.target = self;
    dumpNextFrameToLogAction.tag = 10;

    NSMenuItem *copyBaseAddr = [[NSMenuItem alloc] initWithTitle:DESKTOPUI_LOCALIZED("Copy PSP memory base address") action:@selector(copyAddr) keyEquivalent:@""];
    copyBaseAddr.target = self;
    copyBaseAddr.tag = 11;

    NSMenuItem *restartGraphicsAction = [[NSMenuItem alloc] initWithTitle:DESKTOPUI_LOCALIZED("Restart Graphics") action:@selector(restartGraphics) keyEquivalent:@""];
    restartGraphicsAction.target = self;
    restartGraphicsAction.tag = 12;

    MENU_ITEM(showDebugStatsAction, DESKTOPUI_LOCALIZED("Show Debug Statistics"), @selector(toggleShowDebugStats:), ((DebugOverlay)g_Config.iDebugOverlay == DebugOverlay::DEBUG_STATS), 12)

    [parent addItem:loadSymbolMapAction];
    [parent addItem:saveMapFileAction];
    [parent addItem:[NSMenuItem separatorItem]];

    [parent addItem:loadSymFileAction];
    [parent addItem:saveSymFileAction];
    [parent addItem:[NSMenuItem separatorItem]];

    [parent addItem:resetSymbolTableAction];
    [parent addItem:[NSMenuItem separatorItem]];

    [parent addItem:takeScreenshotAction];
    [parent addItem:dumpNextFrameToLogAction];
    [parent addItem:showDebugStatsAction];
    [parent addItem:restartGraphicsAction];

    [parent addItem:[NSMenuItem separatorItem]];
    [parent addItem:copyBaseAddr];
    return parent;
}

-(void)breakAction: (NSMenuItem *)item {
   std::shared_ptr<I18NCategory> desktopUILocalization = GetI18NCategory(I18NCat::DESKTOPUI);
#define DESKTOPUI_LOCALIZED(key) @(UnescapeMenuString(desktopUILocalization->T_cstr(key), nil).c_str())
   std::shared_ptr<I18NCategory> developerUILocalization = GetI18NCategory(I18NCat::DEVELOPER);
#define DEVELOPERUI_LOCALIZED(key) @(developerUILocalization->T_cstr(key))
    if (Core_IsStepping()) {
        Core_EnableStepping(false, "ui.break");
        item.title = DESKTOPUI_LOCALIZED("Break");
    } else {
        Core_EnableStepping(true, "ui.break");
        item.title = DEVELOPERUI_LOCALIZED("Resume");
    }
}

-(void)pauseAction: (NSMenuItem *)item {
	System_PostUIMessage(UIMessage::REQUEST_GAME_PAUSE);
}

-(void)resetAction: (NSMenuItem *)item {
    System_PostUIMessage(UIMessage::REQUEST_GAME_RESET);
	Core_EnableStepping(false);
}

-(void)chatAction: (NSMenuItem *)item {
    if (GetUIState() == UISTATE_INGAME) {
        System_PostUIMessage(UIMessage::SHOW_CHAT_SCREEN);
    }
}

-(void)copyAddr {
    NSString *stringToCopy = [NSString stringWithFormat: @"%016llx", (uint64_t)(uintptr_t)Memory::base];
    [NSPasteboard.generalPasteboard declareTypes:@[NSPasteboardTypeString] owner:nil];
    [NSPasteboard.generalPasteboard setString:stringToCopy forType:NSPasteboardTypeString];
}

-(void)restartGraphics {
    System_PostUIMessage(UIMessage::RESTART_GRAPHICS);
}

-(NSURL *)presentOpenPanelWithAllowedFileTypes: (NSArray<NSString *> *)allowedFileTypes {
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

-(NSURL *)presentSavePanelWithDefaultFilename: (NSString *)filename {
    NSSavePanel *savePanel = [[NSSavePanel alloc] init];
    savePanel.nameFieldStringValue = filename;
    if ([savePanel runModal] == NSModalResponseOK && savePanel.URL) {
        return savePanel.URL;
    }
    return nil;
}

-(void)dumpNextFrameToLog {
    gpu->DumpNextFrame();
}

-(void)takeScreenshot {
    g_TakeScreenshot = true;
}

-(void)resetSymbolTable {
    g_symbolMap->Clear();
}

-(void)saveSymbolsfile {
    NSURL *url = [self presentSavePanelWithDefaultFilename:@"Symbols.sym"];
    if (url)
        g_symbolMap->SaveNocashSym(Path(url.fileSystemRepresentation));
}

-(void)loadSymbolsFile {
    NSURL *url = [self presentOpenPanelWithAllowedFileTypes:@[@"sym"]];
    if (url)
        g_symbolMap->LoadNocashSym(Path(url.fileSystemRepresentation));
}

-(void)loadMapFile {
    /* Using NSOpenPanel to filter by `allowedFileTypes` */
    NSURL *url = [self presentOpenPanelWithAllowedFileTypes:@[@"map"]];
    if (url)
        g_symbolMap->LoadSymbolMap(Path(url.fileSystemRepresentation));
}

-(void)saveMapFile {
    NSURL *symbolsMapFileURL = [self presentSavePanelWithDefaultFilename:@"Symbols.map"];
    if (symbolsMapFileURL)
        g_symbolMap->SaveSymbolMap(Path(symbolsMapFileURL.fileSystemRepresentation));
}

#define TOGGLE_METHOD(name, ConfigValueName, ...) \
-(void)toggle##name: (NSMenuItem *)item { \
ConfigValueName = !ConfigValueName; \
__VA_ARGS__; /* for any additional updates */ \
item.state = [self controlStateForBool: ConfigValueName]; \
}

#define TOGGLE_METHOD_INVERSE(name, ConfigValueName, ...) \
-(void)toggle##name: (NSMenuItem *)item { \
ConfigValueName = !ConfigValueName; \
__VA_ARGS__; /* for any additional updates */ \
item.state = [self controlStateForBool: !ConfigValueName]; \
}

TOGGLE_METHOD(Sound, g_Config.bEnableSound)
TOGGLE_METHOD_INVERSE(BreakOnLoad, g_Config.bAutoRun)
TOGGLE_METHOD(IgnoreIllegalRWs, g_Config.bIgnoreBadMemAccess)
TOGGLE_METHOD(AutoFrameSkip, g_Config.bAutoFrameSkip, g_Config.UpdateAfterSettingAutoFrameSkip())
TOGGLE_METHOD(SoftwareRendering, g_Config.bSoftwareRendering)
TOGGLE_METHOD(FullScreen, g_Config.bFullScreen, System_MakeRequest(SystemRequestType::TOGGLE_FULLSCREEN_STATE, 0, g_Config.UseFullScreen() ? "1" : "0", "", 3, 0))
// TOGGLE_METHOD(VSync, g_Config.bVSync)
#undef TOGGLE_METHOD

-(void)toggleShowDebugStats: (NSMenuItem *)item { \
    if ((DebugOverlay)g_Config.iDebugOverlay == DebugOverlay::DEBUG_STATS) {
        g_Config.iDebugOverlay = (int)DebugOverlay::OFF;
    } else {
        g_Config.iDebugOverlay = (int)DebugOverlay::DEBUG_STATS;
    }
    System_PostUIMessage(UIMessage::REQUEST_CLEAR_JIT);
    item.state = [self controlStateForBool: (DebugOverlay)g_Config.iDebugOverlay == DebugOverlay::DEBUG_STATS]; \
}

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

    // TODO: Use same command line params as the previous startup?
    // Note that this does a clean shutdown, so the config will be saved automatically.
    System_RestartApp("");
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
    std::shared_ptr<I18NCategory> mainmenuLocalization = GetI18NCategory(I18NCat::MAINMENU);
#define MAINMENU_LOCALIZED(key) @(mainmenuLocalization->T_cstr(key))

    std::vector<std::string> recentIsos = g_Config.RecentIsos();
    NSMenuItem *openRecent = [[NSMenuItem alloc] initWithTitle:MAINMENU_LOCALIZED("Recent") action:nil keyEquivalent:@""];
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
    [self.fileMenu addItem:openRecent];
}

-(void)openRecentItem: (NSMenuItem *)item {
    System_PostUIMessage(UIMessage::REQUEST_GAME_BOOT, g_Config.RecentIsos()[item.tag].c_str());
}

-(void)openSystemFileBrowser {
    int g = 0;
    DarwinDirectoryPanelCallback panelCallback = [g] (bool succ, Path thePathChosen) {
        if (succ)
            System_PostUIMessage(UIMessage::REQUEST_GAME_BOOT, thePathChosen.c_str());
    };
    DarwinFileSystemServices::presentDirectoryPanel(panelCallback, /* allowFiles = */ true, /* allowDirectorites = */ true);
}

-(void)openMemstickFolder {
    NSURL *memstickURL = [NSURL fileURLWithPath:@(g_Config.memStickDirectory.c_str())];
    [NSWorkspace.sharedWorkspace openURL:memstickURL];
}

- (void)dealloc {
    [NSNotificationCenter.defaultCenter removeObserver:self];
}

@end

#ifdef __cplusplus
}
#endif
