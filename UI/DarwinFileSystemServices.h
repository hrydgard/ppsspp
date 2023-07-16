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

#define PreferredMemoryStickUserDefaultsKey "UserPreferredMemoryStickDirectoryPath"

typedef std::function<void (bool, Path)> DarwinDirectoryPanelCallback;

/// A Class providing help functions to work with the FileSystem
/// on Darwin platforms.
class DarwinFileSystemServices {
public:
	/// Present a panel to choose a file or directory.
	void presentDirectoryPanel(
		DarwinDirectoryPanelCallback,
		bool allowFiles = false,
		bool allowDirectories = true,
		BrowseFileType fileType = BrowseFileType::ANY);

	static Path appropriateMemoryStickDirectoryToUse();
	static void setUserPreferredMemoryStickDirectory(Path);
private:
	static Path __defaultMemoryStickPath();
#if PPSSPP_PLATFORM(IOS)
	// iOS only, needed for UIDocumentPickerViewController
	void *__pickerDelegate = NULL;
#endif // PPSSPP_PLATFORM(IOS)
};

void RestartMacApp();
