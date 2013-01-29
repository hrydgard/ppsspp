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

#include "../HLE/sceKernel.h"
#include "../System.h"

enum SceUtilitySavedataType
{
	SCE_UTILITY_SAVEDATA_TYPE_AUTOLOAD		= 0,
	SCE_UTILITY_SAVEDATA_TYPE_AUTOSAVE		= 1,
	SCE_UTILITY_SAVEDATA_TYPE_LOAD			= 2,
	SCE_UTILITY_SAVEDATA_TYPE_SAVE			= 3,
	SCE_UTILITY_SAVEDATA_TYPE_LISTLOAD		= 4,
	SCE_UTILITY_SAVEDATA_TYPE_LISTSAVE		= 5,
	SCE_UTILITY_SAVEDATA_TYPE_DELETE		= 6,
	SCE_UTILITY_SAVEDATA_TYPE_LISTDELETE		= 7,
	SCE_UTILITY_SAVEDATA_TYPE_SIZES			= 8,
	SCE_UTILITY_SAVEDATA_TYPE_SINGLEDELETE	= 10,
	SCE_UTILITY_SAVEDATA_TYPE_LIST			= 11,
	SCE_UTILITY_SAVEDATA_TYPE_FILES			= 12,
	SCE_UTILITY_SAVEDATA_TYPE_MAKEDATASECURE	= 13,
	SCE_UTILITY_SAVEDATA_TYPE_READDATASECURE	= 15,
	SCE_UTILITY_SAVEDATA_TYPE_WRITEDATASECURE	= 17,
	SCE_UTILITY_SAVEDATA_TYPE_GETSIZE		= 22
} ;

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
	int buf;
	SceSize bufSize;  // Size of the buffer pointed to by buf
	SceSize size;	    // Actual file size to write / was read
	int unknown;
};

// Structure to hold the parameters for the sceUtilitySavedataInitStart function.
struct SceUtilitySavedataParam
{
	SceSize size; // Size of the structure

	int language;

	int buttonSwap;

	int unknown[4];
	int result;
	int unknown2[4];

	int mode;  // 0 to load, 1 to save
	int bind;

	int overwriteMode;   // use 0x10  ?

	/** gameName: name used from the game for saves, equal for all saves */
	char gameName[13];
	char unused[3];
	/** saveName: name of the particular save, normally a number */
	char saveName[20];
	int saveNameList;
	/** fileName: name of the data file of the game for example DATA.BIN */
	char fileName[13];
	char unused2[3];

	/** pointer to a buffer that will contain data file unencrypted data */
	u32 dataBuf; // Initially void*, but void* in 64bit system take 8 bytes.
	/** size of allocated space to dataBuf */
	SceSize dataBufSize;
	SceSize dataSize;  // Size of the actual save data

	PspUtilitySavedataSFOParam sfoParam;

	PspUtilitySavedataFileData icon0FileData;
	PspUtilitySavedataFileData icon1FileData;
	PspUtilitySavedataFileData pic1FileData;
	PspUtilitySavedataFileData snd0FileData;

	int newData;
	int focus;
	int abortStatus;

	// Function SCE_UTILITY_SAVEDATA_TYPE_SIZES
	u32 msFree;
	u32 msData;
	u32 utilityData;

	u8 key[16];

	int secureVersion;
	int multiStatus;

	// Function 11 LIST
	u32 idListAddr;

	// Function 12 FILES
	u32 fileListAddr;

	// Function 22 GETSIZES
	u32 sizeAddr;

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
};
	
class SavedataParam
{
public:
	SavedataParam();

	static void Init();
	std::string GetSaveFilePath(SceUtilitySavedataParam* param, int saveId = -1);
	std::string GetSaveDirName(SceUtilitySavedataParam* param, int saveId = -1);
	std::string GetSaveDir(SceUtilitySavedataParam* param, int saveId = -1);
	bool Delete(SceUtilitySavedataParam* param, int saveId = -1);
	bool Save(SceUtilitySavedataParam* param, int saveId = -1);
	bool Load(SceUtilitySavedataParam* param, int saveId = -1);
	bool GetSizes(SceUtilitySavedataParam* param);
	bool GetList(SceUtilitySavedataParam* param);
	bool GetFilesList(SceUtilitySavedataParam* param);
	bool GetSize(SceUtilitySavedataParam* param);
	bool IsSaveEncrypted(SceUtilitySavedataParam* param, int saveId);

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

	void DoState(PointerWrap &p);

private:
	void Clear();
	bool CreatePNGIcon(u8* pngData, int pngSize, SaveFileInfo& info);
	void SetFileInfo(int idx, PSPFileInfo &info, std::string saveName);

	int DecryptSave(unsigned int mode, unsigned char *data, int *dataLen, int *alignedLen, unsigned char *cryptkey);
	int EncryptData(unsigned int mode, unsigned char *data, int *dataLen, int *alignedLen, unsigned char *hash, unsigned char *cryptkey);
	int UpdateHash(u8* sfoData, int sfoSize, int sfoDataParamsOffset, int encryptmode);
	int BuildHash(unsigned char *output, unsigned char *data, unsigned int len,  unsigned int alignedLen, int mode, unsigned char *cryptkey);

	SceUtilitySavedataParam* pspParam;
	int selectedSave;
	SaveFileInfo* saveDataList;
	SaveFileInfo* noSaveIcon;
	int saveDataListCount;
	int saveNameListDataCount;
};
