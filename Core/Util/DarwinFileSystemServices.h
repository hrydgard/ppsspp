//
//  DarwinFileSystemServices.h
//  PPSSPP
//
//  Created by Serena on 20/01/2023.
//

#pragma once

#include "ppsspp_config.h"
#include "Common/File/Path.h"
#include "Common/System/Request.h"

#if PPSSPP_PLATFORM(IOS) || PPSSPP_PLATFORM(MAC)

#define PreferredMemoryStickUserDefaultsKey "UserPreferredMemoryStickDirectoryPath"

typedef std::function<void (bool, Path)> DarwinDirectoryPanelCallback;

/// A Class providing help functions to work with the FileSystem
/// on Darwin platforms.
class DarwinFileSystemServices {
public:
	/// Present a panel to choose a file or directory.
	static void presentDirectoryPanel(
		DarwinDirectoryPanelCallback panelCallback,
		bool allowFiles = false,
		bool allowDirectories = true,
		BrowseFileType fileType = BrowseFileType::ANY);

	static Path appropriateMemoryStickDirectoryToUse();
	static void setUserPreferredMemoryStickDirectory(Path);
	static Path defaultMemoryStickPath();
	static void ClearDelegate();

	static Path reauthorizeBookmarkByPath(const Path &pathStr);
	static void stopAccessingPath(const Path &pathStr);
	static void terminate();
private:
#if PPSSPP_PLATFORM(IOS)
	// iOS only, needed for UIDocumentPickerViewController
	static void *__pickerDelegate;
#endif // PPSSPP_PLATFORM(IOS)
};

void RestartMacApp();

#if PPSSPP_PLATFORM(MAC)
// True while a panel from DarwinPresentPanel is up. SDL delivers input to its window regardless,
// so the caller has to drop it.
bool DarwinIsPanelOpen();
#endif

#if PPSSPP_PLATFORM(MAC) && defined(__OBJC__)
#import <AppKit/AppKit.h>
// Shows the panel as a sheet on the key (or main) window, so that focus returns to it afterwards.
void DarwinPresentPanel(NSSavePanel *panel, void (^handler)(NSModalResponse response));
#endif

#endif
