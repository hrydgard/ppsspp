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

#include "Common/CommonTypes.h"
#include "Core/MemMap.h"
#include "Core/HLE/sceRtc.h"
#include "Core/Dialog/PSPDialog.h"

#undef st_ctime
#undef st_atime
#undef st_mtime

class PPGeImage;
struct PSPFileInfo;
typedef u32_le SceSize_le;

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

static const char *const utilitySavedataTypeNames[] = {
	"AUTOLOAD",
	"AUTOSAVE",
	"LOAD",
	"SAVE",
	"LISTLOAD",
	"LISTSAVE",
	"DELETE",
	"LISTDELETE",
	"SIZES",
	"AUTODELETE",
	"SINGLEDELETE",
	"LIST",
	"FILES",
	"MAKEDATASECURE",
	"MAKEDATA",
	"READDATASECURE",
	"READDATA",
	"WRITEDATASECURE",
	"WRITEDATA",
	"ERASESECURE",
	"ERASE",
	"DELETEDATA",
	"GETSIZE",
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

#if COMMON_LITTLE_ENDIAN
typedef SceUtilitySavedataType SceUtilitySavedataType_le;
typedef SceUtilitySavedataFocus SceUtilitySavedataFocus_le;
#else
typedef swap_struct_t<SceUtilitySavedataType, swap_32_t<SceUtilitySavedataType> > SceUtilitySavedataType_le;
typedef swap_struct_t<SceUtilitySavedataFocus, swap_32_t<SceUtilitySavedataFocus> > SceUtilitySavedataFocus_le;
#endif

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
	SceSize_le bufSize;     // Size of the buffer pointed to by buf
	SceSize_le size;        // Actual file size to write / was read
	s32_le unknown;
};

struct PspUtilitySavedataSizeEntry {
	u64_le size;
	char name[16];
};

struct PspUtilitySavedataSizeInfo {
	s32_le numSecureEntries;
	s32_le numNormalEntries;
	PSPPointer<PspUtilitySavedataSizeEntry> secureEntries;
	PSPPointer<PspUtilitySavedataSizeEntry> normalEntries;
	s32_le sectorSize;
	s32_le freeSectors;
	s32_le freeKB;
	char freeString[8];
	s32_le neededKB;
	char neededString[8];
	s32_le overwriteKB;
	char overwriteString[8];
};

struct SceUtilitySavedataIdListEntry
{
	s32_le st_mode;
	ScePspDateTime st_ctime;
	ScePspDateTime st_atime;
	ScePspDateTime st_mtime;
	SceUtilitySavedataSaveName name;
};

struct SceUtilitySavedataIdListInfo
{
	s32_le maxCount;
	s32_le resultCount;
	PSPPointer<SceUtilitySavedataIdListEntry> entries;
};

struct SceUtilitySavedataFileListEntry
{
	s32_le st_mode;
	u32_le st_unk0;
	u64_le st_size;
	ScePspDateTime st_ctime;
	ScePspDateTime st_atime;
	ScePspDateTime st_mtime;
	char name[16];
};

struct SceUtilitySavedataFileListInfo
{
	u32_le maxSecureEntries;
	u32_le maxNormalEntries;
	u32_le maxSystemEntries;
	u32_le resultNumSecureEntries;
	u32_le resultNumNormalEntries;
	u32_le resultNumSystemEntries;
	PSPPointer<SceUtilitySavedataFileListEntry> secureEntries;
	PSPPointer<SceUtilitySavedataFileListEntry> normalEntries;
	PSPPointer<SceUtilitySavedataFileListEntry> systemEntries;
};

struct SceUtilitySavedataMsFreeInfo
{
	s32_le clusterSize;
	s32_le freeClusters;
	s32_le freeSpaceKB;
	char freeSpaceStr[8];
};

struct SceUtilitySavedataUsedDataInfo
{
	s32_le usedClusters;
	s32_le usedSpaceKB;
	char usedSpaceStr[8];
	s32_le usedSpace32KB;
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

	SceUtilitySavedataType_le mode;  // 0 to load, 1 to save
	s32_le bind;

	s32_le overwriteMode;   // use 0x10  ?

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
	SceSize_le dataBufSize;
	SceSize_le dataSize;  // Size of the actual save data

	PspUtilitySavedataSFOParam sfoParam;

	PspUtilitySavedataFileData icon0FileData;
	PspUtilitySavedataFileData icon1FileData;
	PspUtilitySavedataFileData pic1FileData;
	PspUtilitySavedataFileData snd0FileData;

	PSPPointer<PspUtilitySavedataFileData> newData;
	SceUtilitySavedataFocus_le focus;
	s32_le abortStatus;

	// Function SCE_UTILITY_SAVEDATA_TYPE_SIZES
	PSPPointer<SceUtilitySavedataMsFreeInfo> msFree;
	PSPPointer<SceUtilitySavedataMsDataInfo> msData;
	PSPPointer<SceUtilitySavedataUsedDataInfo> utilityData;

	u8 key[16];

	s32_le secureVersion;
	s32_le multiStatus;

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

	PPGeImage *texture;

	SaveFileInfo() : size(0), saveName(""), idx(0), texture(NULL)
	{
		memset(title, 0, 128);
		memset(saveTitle, 0, 128);
		memset(saveDetail, 0, 1024);
		memset(&modif_time, 0, sizeof(modif_time));
	}

	void DoState(PointerWrap &p);
};

class SavedataParam
{
public:
	SavedataParam();

	static void Init();
	std::string GetSaveFilePath(const SceUtilitySavedataParam *param, int saveId = -1) const;
	std::string GetSaveFilePath(const SceUtilitySavedataParam *param, const std::string &saveDir) const;
	std::string GetSaveDirName(const SceUtilitySavedataParam *param, int saveId = -1) const;
	std::string GetSaveDir(const SceUtilitySavedataParam *param, int saveId = -1) const;
	std::string GetSaveDir(const SceUtilitySavedataParam *param, const std::string &saveDirName) const;
	bool Delete(SceUtilitySavedataParam* param, int saveId = -1);
	int DeleteData(SceUtilitySavedataParam* param);
	bool Save(SceUtilitySavedataParam* param, const std::string &saveDirName, bool secureMode = true);
	bool Load(SceUtilitySavedataParam* param, const std::string &saveDirName, int saveId = -1, bool secureMode = true);
	int GetSizes(SceUtilitySavedataParam* param);
	bool GetList(SceUtilitySavedataParam* param);
	int GetFilesList(SceUtilitySavedataParam* param);
	bool GetSize(SceUtilitySavedataParam* param);
	int GetSaveCryptMode(SceUtilitySavedataParam* param, const std::string &saveDirName);
	bool IsInSaveDataList(std::string saveName, int count);
	bool IsSaveDirectoryExist(SceUtilitySavedataParam* param);
	bool IsSfoFileExist(SceUtilitySavedataParam* param);

	std::string GetGameName(const SceUtilitySavedataParam *param) const;
	std::string GetSaveName(const SceUtilitySavedataParam *param) const;
	std::string GetFileName(const SceUtilitySavedataParam *param) const;
	std::string GetKey(const SceUtilitySavedataParam *param) const;
	bool HasKey(const SceUtilitySavedataParam *param) const;

	static std::string GetSpaceText(u64 size);

	int SetPspParam(SceUtilitySavedataParam* param);
	SceUtilitySavedataParam *GetPspParam();
	const SceUtilitySavedataParam *GetPspParam() const;

	int GetFilenameCount();
	const SaveFileInfo& GetFileInfo(int idx);
	std::string GetFilename(int idx) const;

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
	int GetSaveNameIndex(SceUtilitySavedataParam* param);

	void DoState(PointerWrap &p);

private:
	void Clear();
	void SetFileInfo(int idx, PSPFileInfo &info, std::string saveName);
	void SetFileInfo(SaveFileInfo &saveInfo, PSPFileInfo &info, std::string saveName);
	void ClearFileInfo(SaveFileInfo &saveInfo, std::string saveName);

	bool LoadSaveData(SceUtilitySavedataParam *param, const std::string &saveDirName, const std::string& dirPath, bool secureMode);
	void LoadCryptedSave(SceUtilitySavedataParam *param, u8 *data, u8 *saveData, int &saveSize, int prevCryptMode, bool &saveDone);
	void LoadNotCryptedSave(SceUtilitySavedataParam *param, u8 *data, u8 *saveData, int &saveSize);
	void LoadSFO(SceUtilitySavedataParam *param, const std::string& dirPath);
	void LoadFile(const std::string& dirPath, const std::string& filename, PspUtilitySavedataFileData *fileData);

	int DecryptSave(unsigned int mode, unsigned char *data, int *dataLen, int *alignedLen, unsigned char *cryptkey);
	int EncryptData(unsigned int mode, unsigned char *data, int *dataLen, int *alignedLen, unsigned char *hash, unsigned char *cryptkey);
	int UpdateHash(u8* sfoData, int sfoSize, int sfoDataParamsOffset, int encryptmode);
	int BuildHash(unsigned char *output, unsigned char *data, unsigned int len,  unsigned int alignedLen, int mode, unsigned char *cryptkey);
	int DetermineCryptMode(const SceUtilitySavedataParam *param) const;

	std::set<std::string> getSecureFileNames(std::string dirPath);

	SceUtilitySavedataParam* pspParam;
	int selectedSave;
	SaveFileInfo *saveDataList;
	SaveFileInfo *noSaveIcon;
	int saveDataListCount;
	int saveNameListDataCount;
};
