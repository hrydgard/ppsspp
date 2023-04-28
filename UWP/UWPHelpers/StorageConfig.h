// UWP STORAGE MANAGER
// Copyright (c) 2023 Bashar Astifan.
// Email: bashar@astifan.online
// Telegram: @basharastifan
// GitHub: https://github.com/basharast/UWP2Win32

#pragma once

#include <string>

// Known locations
// ACTIVATE BELOW ONLY IF YOU ADDED THE CAPABILITY
#define APPEND_APP_LOCALDATA_LOCATION 1 // recommended to be always 1
#define APPEND_APP_INSTALLATION_LOCATION 1 // recommended to be always 1
#define APPEND_DOCUMENTS_LOCATION 0 // (requires 'documentsLibrary' capability)
#define APPEND_VIDEOS_LOCATION 0 // (requires 'videosLibrary' capability)
#define APPEND_MUSIC_LOCATION 0 // (requires musicLibrary' capability)
#define APPEND_PICTURES_LOCATION 0 // (requires 'picturesLibrary' capability)


// Working folder
// set this value by calling `SetWorkingFolder` from `StorageManager.h`
static std::string AppWorkingFolder;
