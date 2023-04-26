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
#include "GPU/GPUInterface.h"
#include "Common/File/Path.h"
#include "Common/System/System.h"
#include "Common/System/NativeApp.h"
#include "Core/Config.h"
#include "Common/Data/Text/I18n.h"
#include "Common/StringUtils.h"

void TakeScreenshot();
/* including "Core/Core.h" results in a compilation error soo */
bool Core_IsStepping();
void Core_EnableStepping(bool step, const char *reason = nullptr, u32 relatedAddress = 0);

#ifdef __cplusplus
extern "C" {
#endif

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
@property (assign) std::shared_ptr<I18NCategory> mainSettingsLocalization;
@property (assign) std::shared_ptr<I18NCategory> graphicsLocalization;
@end

void initializeOSXExtras() {
    [NSApplication.sharedApplication setDelegate:[PSPNSApplicationDelegate sharedAppDelegate]];
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
    
    NSMenuItem *fileMenuItem = [[NSMenuItem alloc] init];
    fileMenuItem.submenu = [self makeFileSubmenu];
    fileMenuItem.submenu.delegate = self;

    NSMenuItem *graphicsMenuItem = [[NSMenuItem alloc] init];
    graphicsMenuItem.submenu = [self makeGraphicsMenu];
    graphicsMenuItem.submenu.delegate = self;
    
    NSMenuItem *debugMenuItem = [[NSMenuItem alloc] init];
    debugMenuItem.submenu = [self makeDebugMenu];
    debugMenuItem.submenu.delegate = self;
    
    NSMenuItem *helpMenuItem = [[NSMenuItem alloc] init];
    helpMenuItem.submenu = [self makeHelpMenu];
    
    [NSApplication.sharedApplication.menu addItem:fileMenuItem];
    [NSApplication.sharedApplication.menu addItem:graphicsMenuItem];
    [NSApplication.sharedApplication.menu addItem:debugMenuItem];
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
    } else if ([menu.title isEqualToString: [self localizedString:"Debug" category: self.mainSettingsLocalization]]) {
        for (NSMenuItem *item in menu.itemArray) {
            switch ([item tag]) {
                case 2:
                    item.state = [self controlStateForBool:g_Config.bAutoRun];
                    break;
                case 3:
                    item.state = [self controlStateForBool:g_Config.bIgnoreBadMemAccess];
                    break;
                default:
                    break;
            }
        }
    }
}

-(NSString *)localizedString: (const char *)key category: (std::shared_ptr<I18NCategory>)cat {
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

-(NSMenu *)makeFileSubmenu {
    NSMenu *menu = [[NSMenu alloc] initWithTitle:@"File"];
    NSMenuItem *openWithSystemFolderBrowserItem = [[NSMenuItem alloc] initWithTitle:@"Open..." action:@selector(openSystemFileBrowser) keyEquivalent:@"o"];
    openWithSystemFolderBrowserItem.keyEquivalentModifierMask = NSEventModifierFlagCommand;
    openWithSystemFolderBrowserItem.enabled = YES;
    openWithSystemFolderBrowserItem.target = self;
    [menu addItem:openWithSystemFolderBrowserItem];
    self.fileMenu = menu;
    [self addOpenRecentlyItem];

    [self.fileMenu addItem:[NSMenuItem separatorItem]];

    NSMenuItem *openMemstickFolderItem = [[NSMenuItem alloc] initWithTitle:@"Open Memory Stick" action:@selector(openMemstickFolder) keyEquivalent:@""];
    openMemstickFolderItem.target = self;
    [self.fileMenu addItem:openMemstickFolderItem];

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
    
    MENU_ITEM(softwareRendering, GRAPHICS_LOCALIZED("Software Rendering"), @selector(toggleSoftwareRendering:), g_Config.bSoftwareRendering, 1)
    [parent addItem:softwareRendering];
    
    MENU_ITEM(vsyncItem, GRAPHICS_LOCALIZED("VSync"), @selector(toggleVSync:), g_Config.bVSync, 2)
    [parent addItem:vsyncItem];
    
    MENU_ITEM(fullScreenItem, GRAPHICS_LOCALIZED("Fullscreen"), @selector(toggleFullScreen:), g_Config.bFullScreen, 3)
    [parent addItem:fullScreenItem];
    
    [parent addItem:[NSMenuItem separatorItem]];
    
    MENU_ITEM(autoFrameSkip, GRAPHICS_LOCALIZED("Auto FrameSkip"), @selector(toggleAutoFrameSkip:), g_Config.bAutoFrameSkip, 4)
    [parent addItem:autoFrameSkip];
    
    [parent addItem:[NSMenuItem separatorItem]];
    
    MENU_ITEM(fpsCounterItem, GRAPHICS_LOCALIZED("Show FPS Counter"), @selector(setToggleShowCounterItem:), g_Config.iShowStatusFlags & (int)ShowStatusFlags::FPS_COUNTER, 5)
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

-(NSMenu *)makeDebugMenu {
    std::shared_ptr<I18NCategory> sysInfoLocalization = GetI18NCategory(I18NCat::SYSINFO);
    std::shared_ptr<I18NCategory> desktopUILocalization = GetI18NCategory(I18NCat::DESKTOPUI);
#define DESKTOPUI_LOCALIZED(key) @(UnescapeMenuString(desktopUILocalization->T(key), nil).c_str())
    
    NSMenu *parent = [[NSMenu alloc] initWithTitle:@(sysInfoLocalization->T("Debug"))];
    
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
    
    NSMenuItem *loadSymbolMapAction = [[NSMenuItem alloc] initWithTitle:DESKTOPUI_LOCALIZED("Load Map file") action:@selector(loadMapFile) keyEquivalent:@""];
    loadSymbolMapAction.target = self;
    
    NSMenuItem *saveMapFileAction = [[NSMenuItem alloc] initWithTitle:DESKTOPUI_LOCALIZED("Save Map file") action:@selector(saveMapFile) keyEquivalent:@""];
    saveMapFileAction.target = self;
    
    NSMenuItem *loadSymFileAction = [[NSMenuItem alloc] initWithTitle:DESKTOPUI_LOCALIZED("Load .sym File...") action:@selector(loadSymbolsFile) keyEquivalent:@""];
    loadSymFileAction.target = self;
    
    NSMenuItem *saveSymFileAction = [[NSMenuItem alloc] initWithTitle:DESKTOPUI_LOCALIZED("Save .sym File...") action:@selector(saveSymbolsfile) keyEquivalent:@""];
    saveSymFileAction.target = self;
    
    NSMenuItem *resetSymbolTableAction = [[NSMenuItem alloc] initWithTitle:DESKTOPUI_LOCALIZED("Reset Symbol Table") action:@selector(resetSymbolTable) keyEquivalent:@""];
    resetSymbolTableAction.target = self;
    
    NSMenuItem *takeScreenshotAction = [[NSMenuItem alloc] initWithTitle:DESKTOPUI_LOCALIZED("Take Screenshot") action:@selector(takeScreenshot) keyEquivalent:@""];
    takeScreenshotAction.target = self;
    
    NSMenuItem *dumpNextFrameToLogAction = [[NSMenuItem alloc] initWithTitle:DESKTOPUI_LOCALIZED("Dump next frame to log") action:@selector(dumpNextFrameToLog) keyEquivalent:@""];
    dumpNextFrameToLogAction.target = self;
    
    NSMenuItem *copyBaseAddr = [[NSMenuItem alloc] initWithTitle:DESKTOPUI_LOCALIZED("Copy PSP memory base address") action:@selector(copyAddr) keyEquivalent:@""];
    copyBaseAddr.target = self;
    
    MENU_ITEM(showDebugStatsAction, DESKTOPUI_LOCALIZED("Show Debug Statistics"), @selector(toggleShowDebugStats:), g_Config.bShowDebugStats, 2)
    
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
    
    [parent addItem:[NSMenuItem separatorItem]];
    [parent addItem:copyBaseAddr];
    return parent;
}

-(void)breakAction: (NSMenuItem *)item {
    if (Core_IsStepping()) {
        Core_EnableStepping(false, "ui.break");
        item.title = @"Break";
    } else {
        Core_EnableStepping(true, "ui.break");
        item.title = @"Resume";
    }
}

-(void)copyAddr {
    NSString *stringToCopy = [NSString stringWithFormat: @"%016llx", (uint64_t)(uintptr_t)Memory::base];
    [NSPasteboard.generalPasteboard declareTypes:@[NSPasteboardTypeString] owner:nil];
    [NSPasteboard.generalPasteboard setString:stringToCopy forType:NSPasteboardTypeString];
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
    TakeScreenshot();
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

TOGGLE_METHOD(Sound, g_Config.bEnableSound)
TOGGLE_METHOD(BreakOnLoad, g_Config.bAutoRun)
TOGGLE_METHOD(IgnoreIllegalRWs, g_Config.bIgnoreBadMemAccess)
TOGGLE_METHOD(AutoFrameSkip, g_Config.bAutoFrameSkip, g_Config.UpdateAfterSettingAutoFrameSkip())
TOGGLE_METHOD(SoftwareRendering, g_Config.bSoftwareRendering)
TOGGLE_METHOD(FullScreen, g_Config.bFullScreen, System_MakeRequest(SystemRequestType::TOGGLE_FULLSCREEN_STATE, 0, g_Config.UseFullScreen() ? "1" : "0", "", 3))
TOGGLE_METHOD(VSync, g_Config.bVSync)
TOGGLE_METHOD(ShowDebugStats, g_Config.bShowDebugStats, NativeMessageReceived("clear jit", ""))
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
    [self.fileMenu addItem:openRecent];
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
