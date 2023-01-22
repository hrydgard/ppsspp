//
//  DarwinMemoryStickManager.h
//  PPSSPP
//
//  Created by Serena on 20/01/2023.
//

#ifndef DarwinMemoryStickManager_h
#define DarwinMemoryStickManager_h

#include "ppsspp_config.h"
#include "Common/File/Path.h"

#define PreferredMemoryStickUserDefaultsKey "UserPreferredMemoryStickDirectoryPath"

typedef std::function<void (Path)> DarwinDirectoryPanelCallback;

/// A Class to manage the memory stick on Darwin (macOS, iOS) platforms,
/// consisting of meth(od)s to present the directory panel
/// to choose the user preferred memory stick directory,
/// to determine the appropriate memory stick directory,
/// and to *set* the preferred memory stick directory.
class DarwinMemoryStickManager {
public:
    /// Present a pannel to choose the directory as the memory stick manager.
    void presentDirectoryPanel(DarwinDirectoryPanelCallback);
    
    static Path appropriateMemoryStickDirectoryToUse();
    static void setUserPreferredMemoryStickDirectory(Path);
private:
    static Path __defaultMemoryStickPath();
#if PPSSPP_PLATFORM(IOS)
    // iOS only, needed for UIDocumentPickerViewController
    void *__pickerDelegate = NULL;
#endif // PPSSPP_PLATFORM(IOS)
};

#endif /* DarwinMemoryStickManager_h */
