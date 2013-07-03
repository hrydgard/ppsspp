// Copyright (c) 2012- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

#pragma once

#include "Core/HLE/sceKernel.h"
#include "Core/HLE/sceRtc.h"
#include "Core/System.h"
#include "Core/Dialog/PSPDialog.h"
#undef st_ctime
#undef st_atime
#undef st_mtime

enum SceUtilitySavedataType
{
	SCE_UTILITY_SAVEDATA_TYPE_AUTOLOAD        = 0,
	SCE_UTILITY_SAVEDATA_TYPE_AUTOSAVE        = 1,
	SCE_UTILITY_SAVEDATA_TYPE_LOAD            = 2,
	SCE_UTILITY_SAVEDATA_TYPE_SAVE            = 3,
	SCE_UTILITY_SAVEDATA_TYPE_LISTLOAD        = 4,
	SCE_UTILITY_SAVEDATA_TYPE_LISTSAVE        = 5,
	SCE_UTILITY_SAVEDATA_TYPE_DELETE          = 6,
	SCE_UTILITY_SAVEDATA_TYPE_LISTDELETE      = 7,
	SCE_UTILITY_SAVEDATA_TYPE_SIZES           = 8,
	SCE_UTILITY_SAVEDATA_TYPE_AUTODELETE      = 9,
	SCE_UTILITY_SAVEDATA_TYPE_SINGLEDELETE    = 10,
	SCE_UTILITY_SAVEDATA_TYPE_LIST            = 11,
	SCE_UTILITY_SAVEDATA_TYPE_FILES           = 12,
	SCE_UTILITY_SAVEDATA_TYPE_MAKEDATASECURE  = 13,
	SCE_UTILITY_SAVEDATA_TYPE_MAKEDATA        = 14,
	SCE_UTILITY_SAVEDATA_TYPE_READDATASECURE  = 15,
	SCE_UTILITY_SAVEDATA_TYPE_READDATA        = 16,
	SCE_UTILITY_SAVEDATA_TYPE_WRITEDATASECURE = 17,
	SCE_UTILITY_SAVEDATA_TYPE_WRITEDATA       = 18,
	SCE_UTILITY_SAVEDATA_TYPE_ERASESECURE     = 19,
	SCE_UTILITY_SAVEDATA_TYPE_ERASE           = 20,
	SCE_UTILITY_SAVEDATA_TYPE_DELETEDATA      = 21,
	SCE_UTILITY_SAVEDATA_TYPE_GETSIZE         = 22,
};

enum SceUtilitySavedataFocus
{
	SCE_UTILITY_SAVEDATA_FOCUS_NAME       = 0, // specified by saveName[]
	SCE_UTILITY_SAVEDATA_FOCUS_FIRSTLIST  = 1, // first listed (on screen or of all?)
	SCE_UTILITY_SAVEDATA_FOCUS_LASTLIST   = 2, // last listed (on screen or of all?)
	SCE_UTILITY_SAVEDATA_FOCUS_LATEST     = 3, // latest by modification date (first if none)
	SCE_UTILITY_SAVEDATA_FOCUS_OLDEST     = 4, // oldest by modification date (first if none)
	SCE_UTILITY_SAVEDATA_FOCUS_FIRSTDATA  = 5, // first non-empty (first if none)
	SCE_UTILITY_SAVEDATA_FOCUS_LASTDATA   = 6, // last non-empty (first if none)
	SCE_UTILITY_SAVEDATA_FOCUS_FIRSTEMPTY = 7, // first empty (what if no empty?)
	SCE_UTILITY_SAVEDATA_FOCUS_LASTEMPTY  = 8, // last empty (what if no empty?)
};

typedef char SceUtilitySavedataSaveName[20];

// title, savedataTitle, detail: parts of the unencrypted SFO
// data, it contains what the VSH and standard load screen shows
struct PspUtilitySavedataSFOParam
{
	char title[0x80];
	char savedataTitle[0x80];
	char detail[0x400];
	unsigned char parentalLevel;
	unsigned char unknown[3];
};

struct PspUtilitySavedataFileData {
	PSPPointer<u8> buf;
	SceSize bufSize;  // Size of the buffer pointed to by buf
	SceSize size;	    // Actual file size to write / was read
	int unknown;
};

struct PspUtilitySavedataSizeEntry {
	u64 size;
	char name[16];
};

struct PspUtilitySavedataSizeInfo {
	int numSecureEntries;
	int numNormalEntries;
	PSPPointer<PspUtilitySavedataSizeEntry> secureEntries;
	PSPPointer<PspUtilitySavedataSizeEntry> normalEntries;
	int sectorSize;
	int freeSectors;
	int freeKB;
	char freeString[8];
	int neededKB;
	char neededString[8];
	int overwriteKB;
	char overwriteString[8];
};

struct SceUtilitySavedataIdListEntry
{
	int st_mode;
	ScePspDateTime st_ctime;
	ScePspDateTime st_atime;
	ScePspDateTime st_mtime;
	SceUtilitySavedataSaveName name;
};

struct SceUtilitySavedataIdListInfo
{
	int maxCount;
	int resultCount;
	PSPPointer<SceUtilitySavedataIdListEntry> entries;
};

struct SceUtilitySavedataFileListEntry
{
	int st_mode;
	u64 st_size;
	ScePspDateTime st_ctime;
	ScePspDateTime st_atime;
	ScePspDateTime st_mtime;
	char name[16];
};

struct SceUtilitySavedataFileListInfo
{
	u32 maxSecureEntries;
	u32 maxNormalEntries;
	u32 maxSystemEntries;
	u32 resultNumSecureEntries;
	u32 resultNumNormalEntries;
	u32 resultNumSystemEntries;
	PSPPointer<SceUtilitySavedataFileListEntry> secureEntries;
	PSPPointer<SceUtilitySavedataFileListEntry> normalEntries;
	PSPPointer<SceUtilitySavedataFileListEntry> systemEntries;
};

struct SceUtilitySavedataMsFreeInfo
{
	int clusterSize;
	int freeClusters;
	int freeSpaceKB;
	char freeSpaceStr[8];
};

struct SceUtilitySavedataUsedDataInfo
{
	int usedClusters;
	int usedSpaceKB;
	char usedSpaceStr[8];
	int usedSpace32KB;
	char usedSpace32Str[8];
};

struct SceUtilitySavedataMsDataInfo
{
	char gameName[13];
	char pad[3];
	SceUtilitySavedataSaveName saveName;
	SceUtilitySavedataUsedDataInfo info;
};

// Structure to hold the parameters for the sceUtilitySavedataInitStart function.
struct SceUtilitySavedataParam
{
	pspUtilityDialogCommon common;

	int mode;  // 0 to load, 1 to save
	int bind;

	int overwriteMode;   // use 0x10  ?

	/** gameName: name used from the game for saves, equal for all saves */
	char gameName[13];
	char unused[3];
	/** saveName: name of the particular save, normally a number */
	SceUtilitySavedataSaveName saveName;
	PSPPointer<SceUtilitySavedataSaveName> saveNameList;
	/** fileName: name of the data file of the game for example DATA.BIN */
	char fileName[13];
	char unused2[3];

	/** pointer to a buffer that will contain data file unencrypted data */
	PSPPointer<u8> dataBuf;
	/** size of allocated space to dataBuf */
	SceSize dataBufSize;
	SceSize dataSize;  // Size of the actual save data

	PspUtilitySavedataSFOParam sfoParam;

	PspUtilitySavedataFileData icon0FileData;
	PspUtilitySavedataFileData icon1FileData;
	PspUtilitySavedataFileData pic1FileData;
	PspUtilitySavedataFileData snd0FileData;

	PSPPointer<PspUtilitySavedataFileData> newData;
	int focus;
	int abortStatus;

	// Function SCE_UTILITY_SAVEDATA_TYPE_SIZES
	PSPPointer<SceUtilitySavedataMsFreeInfo> msFree;
	PSPPointer<SceUtilitySavedataMsDataInfo> msData;
	PSPPointer<SceUtilitySavedataUsedDataInfo> utilityData;

	u8 key[16];

	int secureVersion;
	int multiStatus;

	// Function 11 LIST
	PSPPointer<SceUtilitySavedataIdListInfo> idList;

	// Function 12 FILES
	PSPPointer<SceUtilitySavedataFileListInfo> fileList;

	// Function 22 GETSIZES
	PSPPointer<PspUtilitySavedataSizeInfo> sizeInfo;

};

// Non native, this one we can reorganize as we like
struct SaveFileInfo
{
	s64 size;
	std::string saveName;
	int idx;

	char title[128];
	char saveTitle[128];
	char saveDetail[1024];

	tm modif_time;

	u32 textureData;
	int textureWidth;
	int textureHeight;

	void DoState(PointerWrap &p)
	{
		p.Do(size);
		p.Do(saveName);
		p.Do(idx);

		p.DoArray(title, sizeof(title));
		p.DoArray(saveTitle, sizeof(saveTitle));
		p.DoArray(saveDetail, sizeof(saveDetail));

		p.Do(modif_time);
		p.Do(textureData);
		p.Do(textureWidth);
		p.Do(textureHeight);
	}
};
	
class SavedataParam
{
public:
	SavedataParam();

	static void Init();
	std::string GetSaveFilePath(SceUtilitySavedataParam* param, int saveId = -1);
	std::string GetSaveFilePath(SceUtilitySavedataParam* param, const std::string &saveDir);
	std::string GetSaveDirName(SceUtilitySavedataParam* param, int saveId = -1);
	std::string GetSaveDir(SceUtilitySavedataParam* param, int saveId = -1);
	std::string GetSaveDir(SceUtilitySavedataParam* param, const std::string &saveDirName);
	bool Delete(SceUtilitySavedataParam* param, int saveId = -1);
	bool Save(SceUtilitySavedataParam* param, const std::string &saveDirName, bool secureMode = true);
	bool Load(SceUtilitySavedataParam* param, const std::string &saveDirName, int saveId = -1, bool secureMode = true);
	int GetSizes(SceUtilitySavedataParam* param);
	bool GetList(SceUtilitySavedataParam* param);
	int GetFilesList(SceUtilitySavedataParam* param);
	bool GetSize(SceUtilitySavedataParam* param);
	bool IsSaveEncrypted(SceUtilitySavedataParam* param, const std::string &saveDirName);

	std::string GetGameName(SceUtilitySavedataParam* param);
	std::string GetSaveName(SceUtilitySavedataParam* param);
	std::string GetFileName(SceUtilitySavedataParam* param);

	static std::string GetSpaceText(int size);

	int SetPspParam(SceUtilitySavedataParam* param);
	SceUtilitySavedataParam* GetPspParam();

	int GetFilenameCount();
	const SaveFileInfo& GetFileInfo(int idx);
	std::string GetFilename(int idx);

	int GetSelectedSave();
	void SetSelectedSave(int idx);

	int GetFirstListSave();
	int GetLastListSave();
	int GetLatestSave();
	int GetOldestSave();
	int GetFirstDataSave();
	int GetLastDataSave();
	int GetFirstEmptySave();
	int GetLastEmptySave();

	void DoState(PointerWrap &p);

private:
	void Clear();
	bool CreatePNGIcon(u8* pngData, int pngSize, SaveFileInfo& info);
	void SetFileInfo(int idx, PSPFileInfo &info, std::string saveName);
	void SetFileInfo(SaveFileInfo &saveInfo, PSPFileInfo &info, std::string saveName);
	void ClearFileInfo(SaveFileInfo &saveInfo, std::string saveName);

	int DecryptSave(unsigned int mode, unsigned char *data, int *dataLen, int *alignedLen, unsigned char *cryptkey);
	int EncryptData(unsigned int mode, unsigned char *data, int *dataLen, int *alignedLen, unsigned char *hash, unsigned char *cryptkey);
	int UpdateHash(u8* sfoData, int sfoSize, int sfoDataParamsOffset, int encryptmode);
	int BuildHash(unsigned char *output, unsigned char *data, unsigned int len,  unsigned int alignedLen, int mode, unsigned char *cryptkey);

	SceUtilitySavedataParam* pspParam;
	int selectedSave;
	SaveFileInfo *saveDataList;
	SaveFileInfo *noSaveIcon;
	int saveDataListCount;
	int saveNameListDataCount;
};
