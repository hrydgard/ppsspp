//
//  DarwinFileSystemServices.h
//  PPSSPP
//
//  Created by Serena on 20/01/2023.
//

#pragma once

#include "ppsspp_config.h"
#include "Common/File/Path.h"

#define PreferredMemoryStickUserDefaultsKey "UserPreferredMemoryStickDirectoryPath"

typedef std::function<void (bool, Path)> DarwinDirectoryPanelCallback;

/// A Class providing help functions to work with the FileSystem
/// on Darwin platforms.
class DarwinFileSystemServices {
public:
    /// Present a pannel to choose the directory as the memory stick manager.
	void presentDirectoryPanel(DarwinDirectoryPanelCallback,
							   bool allowFiles = false,
							   bool allowDirectories = true);
    
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
