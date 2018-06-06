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

#include "i18n/i18n.h"
#include "base/logging.h"
#include "Common/ChunkFile.h"
#include "Common/StringUtils.h"
#include "Core/Config.h"
#include "Core/Host.h"
#include "Core/Reporting.h"
#include "Core/System.h"
#include "Core/Dialog/SavedataParam.h"
#include "Core/Dialog/PSPSaveDialog.h"
#include "Core/FileSystems/MetaFileSystem.h"
#include "Core/HLE/sceIo.h"
#include "Core/HLE/sceKernelMemory.h"
#include "Core/HLE/sceChnnlsv.h"
#include "Core/ELF/ParamSFO.h"
#include "Core/HW/MemoryStick.h"
#include "Core/Util/PPGeDraw.h"

#include "image/png_load.h"

#include <algorithm>

static const std::string ICON0_FILENAME = "ICON0.PNG";
static const std::string ICON1_FILENAME = "ICON1.PMF";
static const std::string PIC1_FILENAME = "PIC1.PNG";
static const std::string SND0_FILENAME = "SND0.AT3";
static const std::string SFO_FILENAME = "PARAM.SFO";

static const std::string savePath = "ms0:/PSP/SAVEDATA/";

namespace
{
	int getSizeNormalized(int size)
	{
		int sizeCluster = (int)MemoryStick_SectorSize();
		return ((int)((size + sizeCluster - 1) / sizeCluster)) * sizeCluster;
	}

	void SetStringFromSFO(ParamSFOData &sfoFile, const char *name, char *str, int strLength)
	{
		std::string value = sfoFile.GetValueString(name);
		truncate_cpy(str, strLength, value.c_str());
	}

	bool ReadPSPFile(std::string filename, u8 **data, s64 dataSize, s64 *readSize)
	{
		u32 handle = pspFileSystem.OpenFile(filename, FILEACCESS_READ);
		if (handle == 0)
			return false;

		if(dataSize == -1)
		{
			dataSize = pspFileSystem.GetFileInfo(filename).size;
			*data = new u8[(size_t)dataSize];
		}

		size_t result = pspFileSystem.ReadFile(handle, *data, dataSize);
		pspFileSystem.CloseFile(handle);
		if(readSize)
			*readSize = result;

		return result != 0;
	}

	bool WritePSPFile(std::string filename, u8 *data, SceSize dataSize)
	{
		u32 handle = pspFileSystem.OpenFile(filename, (FileAccess)(FILEACCESS_WRITE | FILEACCESS_CREATE | FILEACCESS_TRUNCATE));
		if (handle == 0)
			return false;

		size_t result = pspFileSystem.WriteFile(handle, data, dataSize);
		pspFileSystem.CloseFile(handle);

		return result == dataSize;
	}

	bool PSPMatch(std::string text, std::string regexp)
	{
		if(text.empty() && regexp.empty())
			return true;
		else if(regexp == "*")
			return true;
		else if(text.empty())
			return false;
		else if(regexp.empty())
			return false;
		else if(regexp == "?" && text.length() == 1)
			return true;
		else if(text == regexp)
			return true;
		else if(regexp.data()[0] == '*')
		{
			bool res = PSPMatch(text.substr(1),regexp.substr(1));
			if(!res)
				res = PSPMatch(text.substr(1),regexp);
			return res;
		}
		else if(regexp.data()[0] == '?')
		{
			return PSPMatch(text.substr(1),regexp.substr(1));
		}
		else if(regexp.data()[0] == text.data()[0])
		{
			return PSPMatch(text.substr(1),regexp.substr(1));
		}

		return false;
	}

	int align16(int address)
	{
		return ((address + 0xF) >> 4) << 4;
	}

	int GetSDKMainVersion(int sdkVersion)
	{
		if(sdkVersion > 0x0307FFFF)
			return 6;
		if(sdkVersion > 0x0300FFFF)
			return 5;
		if(sdkVersion > 0x0206FFFF)
			return 4;
		if(sdkVersion > 0x0205FFFF)
			return 3;
		if(sdkVersion >= 0x02000000)
			return 2;
		if(sdkVersion >= 0x01000000)
			return 1;
		return 0;
	};
}

void SaveFileInfo::DoState(PointerWrap &p)
{
	auto s = p.Section("SaveFileInfo", 1, 2);
	if (!s)
		return;

	p.Do(size);
	p.Do(saveName);
	p.Do(idx);

	p.DoArray(title, sizeof(title));
	p.DoArray(saveTitle, sizeof(saveTitle));
	p.DoArray(saveDetail, sizeof(saveDetail));

	p.Do(modif_time);

	if (s <= 1) {
		u32 textureData;
		int textureWidth;
		int textureHeight;
		p.Do(textureData);
		p.Do(textureWidth);
		p.Do(textureHeight);

		if (textureData != 0) {
			// Must be MODE_READ.
			texture = new PPGeImage("");
			texture->CompatLoad(textureData, textureWidth, textureHeight);
		}
	} else {
		bool hasTexture = texture != NULL;
		p.Do(hasTexture);
		if (hasTexture) {
			if (p.mode == p.MODE_READ) {
				delete texture;
				texture = new PPGeImage("");
			}
			texture->DoState(p);
		}
	}
}

SavedataParam::SavedataParam()
	: pspParam(0)
	, selectedSave(0)
	, saveDataList(0)
	, noSaveIcon(0)
	, saveDataListCount(0)
	, saveNameListDataCount(0)
{

}

void SavedataParam::Init()
{
	if (!pspFileSystem.GetFileInfo(savePath).exists)
	{
		pspFileSystem.MkDir(savePath);
	}
	// Create a nomedia file to hide save icons form Android image viewer
#ifdef __ANDROID__
	int handle = pspFileSystem.OpenFile(savePath + ".nomedia", (FileAccess)(FILEACCESS_CREATE | FILEACCESS_WRITE), 0);
	if (handle) {
		pspFileSystem.CloseFile(handle);
	} else {
		ELOG("Failed to create .nomedia file");
	}
#endif
}

std::string SavedataParam::GetSaveDirName(const SceUtilitySavedataParam *param, int saveId) const
{
	if (!param) {
		return "";
	}

	if (saveId >= 0 && saveNameListDataCount > 0) // if user selection, use it
		return GetFilename(saveId);
	else
		return GetSaveName(param);
}

std::string SavedataParam::GetSaveDir(const SceUtilitySavedataParam *param, const std::string &saveDirName) const
{
	if (!param) {
		return "";
	}

	return GetGameName(param) + saveDirName;
}

std::string SavedataParam::GetSaveDir(const SceUtilitySavedataParam *param, int saveId) const
{
	return GetSaveDir(param, GetSaveDirName(param, saveId));
}

std::string SavedataParam::GetSaveFilePath(const SceUtilitySavedataParam *param, const std::string &saveDir) const
{
	if (!param) {
		return "";
	}

	if (!saveDir.size())
		return "";

	return savePath + saveDir;
}

std::string SavedataParam::GetSaveFilePath(const SceUtilitySavedataParam *param, int saveId) const
{
	return GetSaveFilePath(param, GetSaveDir(param, saveId));
}

inline static std::string FixedToString(const char *str, size_t n)
{
	return std::string(str, strnlen(str, n));
}

std::string SavedataParam::GetGameName(const SceUtilitySavedataParam *param) const
{
	return FixedToString(param->gameName, ARRAY_SIZE(param->gameName));
}

std::string SavedataParam::GetSaveName(const SceUtilitySavedataParam *param) const
{
	const std::string saveName = FixedToString(param->saveName, ARRAY_SIZE(param->saveName));
	if (saveName == "<>")
		return "";
	return saveName;
}

std::string SavedataParam::GetFileName(const SceUtilitySavedataParam *param) const
{
	return FixedToString(param->fileName, ARRAY_SIZE(param->fileName));
}

std::string SavedataParam::GetKey(const SceUtilitySavedataParam *param) const
{
	static const char* const lut = "0123456789ABCDEF";

	std::string output;
	if (HasKey(param))
	{
		output.reserve(2 * sizeof(param->key));
		for (size_t i = 0; i < sizeof(param->key); ++i)
		{
			const unsigned char c = param->key[i];
			output.push_back(lut[c >> 4]);
			output.push_back(lut[c & 15]);
		}
	}
	return output;
}

bool SavedataParam::HasKey(const SceUtilitySavedataParam *param) const
{
	for (size_t i = 0; i < ARRAY_SIZE(param->key); ++i)
	{
		if (param->key[i] != 0)
			return true;
	}
	return false;
}

bool SavedataParam::Delete(SceUtilitySavedataParam* param, int saveId) {
	if (!param) {
		return false;
	}

	// Sanity check, preventing full delete of savedata/ in MGS PW demo (!)
	if (!strlen(param->gameName)) {
		ERROR_LOG(SCEUTILITY, "Bad param with gameName empty - cannot delete save directory");
		return false;
	}

	std::string dirPath = GetSaveFilePath(param,saveId);
	if (dirPath.size() == 0) {
		ERROR_LOG(SCEUTILITY, "GetSaveFilePath returned empty - cannot delete save directory");
		return false;
	}

	if (!pspFileSystem.GetFileInfo(dirPath).exists) {
		return false;
	}

	pspFileSystem.RmDir(dirPath);
	return true;
}

int  SavedataParam::DeleteData(SceUtilitySavedataParam* param) {
	if (!param) {
		return SCE_UTILITY_SAVEDATA_ERROR_DELETE_NO_DATA;
	}
	if (param->fileName[0] == '\0') {
		return SCE_UTILITY_SAVEDATA_ERROR_DELETE_NO_DATA;
	}

	std::string subFolder = GetGameName(param) + GetSaveName(param);
	std::string filename = savePath + subFolder + "/" + GetFileName(param);
	if (!subFolder.size()) {
		ERROR_LOG(SCEUTILITY, "Bad subfolder, ignoring delete of %s", filename.c_str());
		return 0;
	}

	PSPFileInfo info = pspFileSystem.GetFileInfo(filename);
	if (info.exists) {
		pspFileSystem.RemoveFile(filename);
	}
	return 0;
}

bool SavedataParam::Save(SceUtilitySavedataParam* param, const std::string &saveDirName, bool secureMode) {
	if (!param) {
		return false;
	}

	std::string dirPath = GetSaveFilePath(param, GetSaveDir(param, saveDirName));

	if (!pspFileSystem.GetFileInfo(dirPath).exists) {
		if (!pspFileSystem.MkDir(dirPath)) {
			I18NCategory *err = GetI18NCategory("Error");
			host->NotifyUserMessage(err->T("Unable to write savedata, disk may be full"));
		}
	}

	u8* cryptedData = 0;
	int cryptedSize = 0;
	u8 cryptedHash[0x10];
	memset(cryptedHash,0,0x10);
	// Encrypt save.
	// TODO: Is this the correct difference between MAKEDATA and MAKEDATASECURE?
	if (param->dataBuf.IsValid() && g_Config.bEncryptSave && secureMode)
	{
		cryptedSize = param->dataSize;
		if(cryptedSize == 0 || (SceSize)cryptedSize > param->dataBufSize)
			cryptedSize = param->dataBufSize; // fallback, should never use this
		u8 *data_ = param->dataBuf;

		int aligned_len = align16(cryptedSize);
		cryptedData = new u8[aligned_len + 0x10];
		memcpy(cryptedData, data_, cryptedSize);

		int decryptMode = DetermineCryptMode(param);
		if (EncryptData(decryptMode, cryptedData, &cryptedSize, &aligned_len, cryptedHash, (HasKey(param) ? param->key : 0)) != 0)
		{
			I18NCategory *err = GetI18NCategory("Error");
			host->NotifyUserMessage(err->T("Save encryption failed. This save won't work on real PSP"), 6.0f);
			ERROR_LOG(SCEUTILITY,"Save encryption failed. This save won't work on real PSP");
			delete[] cryptedData;
			cryptedData = 0;
		}
	}

	// SAVE PARAM.SFO
	ParamSFOData sfoFile;
	std::string sfopath = dirPath+"/" + SFO_FILENAME;
	PSPFileInfo sfoInfo = pspFileSystem.GetFileInfo(sfopath);
	if (sfoInfo.exists) // Read old sfo if exist
	{
		std::vector<u8> sfoData;
		if (pspFileSystem.ReadEntireFile(sfopath, sfoData) >= 0)
			sfoFile.ReadSFO(sfoData);
	}

	// Update values
	sfoFile.SetValue("TITLE",param->sfoParam.title,128);
	sfoFile.SetValue("SAVEDATA_TITLE",param->sfoParam.savedataTitle,128);
	sfoFile.SetValue("SAVEDATA_DETAIL",param->sfoParam.detail,1024);
	sfoFile.SetValue("PARENTAL_LEVEL",param->sfoParam.parentalLevel,4);
	sfoFile.SetValue("CATEGORY","MS",4);
	sfoFile.SetValue("SAVEDATA_DIRECTORY", GetSaveDir(param, saveDirName), 64);

	// For each file, 13 bytes for filename, 16 bytes for file hash (0 in PPSSPP), 3 byte for padding
	if (secureMode)
	{
		const int FILE_LIST_ITEM_SIZE = 13 + 16 + 3;
		const int FILE_LIST_COUNT_MAX = 99;
		const u32 FILE_LIST_TOTAL_SIZE = FILE_LIST_ITEM_SIZE * FILE_LIST_COUNT_MAX;
		u32 tmpDataSize = 0;
		u8 *tmpDataOrig = sfoFile.GetValueData("SAVEDATA_FILE_LIST", &tmpDataSize);
		u8 *tmpData = new u8[FILE_LIST_TOTAL_SIZE];

		if (tmpDataOrig != NULL)
			memcpy(tmpData, tmpDataOrig, tmpDataSize > FILE_LIST_TOTAL_SIZE ? FILE_LIST_TOTAL_SIZE : tmpDataSize);
		else
			memset(tmpData, 0, FILE_LIST_TOTAL_SIZE);

		if (param->dataBuf.IsValid())
		{
			char *fName = (char*)tmpData;
			for(int i = 0; i < FILE_LIST_COUNT_MAX; i++)
			{
				if(fName[0] == 0)
					break; // End of list
				if(strncmp(fName,GetFileName(param).c_str(),20) == 0)
					break;
				fName += FILE_LIST_ITEM_SIZE;
			}

			if (fName + 13 <= (char*)tmpData + FILE_LIST_TOTAL_SIZE)
				snprintf(fName, 13, "%s",GetFileName(param).c_str());
			if (fName + 13 + 16 <= (char*)tmpData + FILE_LIST_TOTAL_SIZE)
				memcpy(fName+13, cryptedHash, 16);
		}
		sfoFile.SetValue("SAVEDATA_FILE_LIST", tmpData, FILE_LIST_TOTAL_SIZE, (int)FILE_LIST_TOTAL_SIZE);
		delete[] tmpData;
	}

	// Init param with 0. This will be used to detect crypted save or not on loading
	u8 *tmpData = new u8[128];
	memset(tmpData, 0, 128);
	sfoFile.SetValue("SAVEDATA_PARAMS", tmpData, 128, 128);
	delete[] tmpData;

	u8 *sfoData;
	size_t sfoSize;
	sfoFile.WriteSFO(&sfoData,&sfoSize);

	// Calc SFO hash for PSP.
	if(cryptedData != 0)
	{
		int offset = sfoFile.GetDataOffset(sfoData,"SAVEDATA_PARAMS");
		if(offset >= 0)
			UpdateHash(sfoData, (int)sfoSize, offset, DetermineCryptMode(param));
	}
	WritePSPFile(sfopath, sfoData, (SceSize)sfoSize);
	delete[] sfoData;

	if(param->dataBuf.IsValid())	// Can launch save without save data in mode 13
	{
		std::string filePath = dirPath+"/"+GetFileName(param);
		u8 *data_ = 0;
		SceSize saveSize = 0;
		if(cryptedData == 0) // Save decrypted data
		{
			saveSize = param->dataSize;
			if(saveSize == 0 || saveSize > param->dataBufSize)
				saveSize = param->dataBufSize; // fallback, should never use this

			data_ = param->dataBuf;
		}
		else
		{
			data_ = cryptedData;
			saveSize = cryptedSize;
		}

		INFO_LOG(SCEUTILITY,"Saving file with size %u in %s",saveSize,filePath.c_str());

		// copy back save name in request
		strncpy(param->saveName, saveDirName.c_str(), 20);

		if (!WritePSPFile(filePath, data_, saveSize))
		{
			ERROR_LOG(SCEUTILITY,"Error writing file %s",filePath.c_str());
			if(cryptedData != 0)
			{
				delete[] cryptedData;
			}
			return false;
		}
		delete[] cryptedData;
	}


	// SAVE ICON0
	if (param->icon0FileData.buf.IsValid())
	{
		std::string icon0path = dirPath + "/" + ICON0_FILENAME;
		WritePSPFile(icon0path, param->icon0FileData.buf, param->icon0FileData.bufSize);
	}
	// SAVE ICON1
	if (param->icon1FileData.buf.IsValid())
	{
		std::string icon1path = dirPath + "/" + ICON1_FILENAME;
		WritePSPFile(icon1path, param->icon1FileData.buf, param->icon1FileData.bufSize);
	}
	// SAVE PIC1
	if (param->pic1FileData.buf.IsValid())
	{
		std::string pic1path = dirPath + "/" + PIC1_FILENAME;
		WritePSPFile(pic1path, param->pic1FileData.buf, param->pic1FileData.bufSize);
	}

	// Save SND
	if (param->snd0FileData.buf.IsValid())
	{
		std::string snd0path = dirPath + "/" + SND0_FILENAME;
		WritePSPFile(snd0path, param->snd0FileData.buf, param->snd0FileData.bufSize);
	}

	return true;
}

bool SavedataParam::Load(SceUtilitySavedataParam *param, const std::string &saveDirName, int saveId, bool secureMode)
{
	if (!param) {
		return false;
	}

	std::string dirPath = GetSaveFilePath(param, GetSaveDir(param, saveDirName));
	std::string filePath = dirPath + "/" + GetFileName(param);
	if (!pspFileSystem.GetFileInfo(filePath).exists) {
		return false;
	}

	if(!LoadSaveData(param, saveDirName, dirPath, secureMode)) // Load main savedata
		return false;

	LoadSFO(param, dirPath);  // Load sfo

	// Don't know what it is, but PSP always respond this and this unlock some game
	param->bind = 1021;

	// Load another files,seems these are required by some games, e.g. Fushigi no Dungeon Fuurai no Shiren 4 Plus.

	// Load ICON0.PNG
	LoadFile(dirPath, ICON0_FILENAME, &param->icon0FileData);
	// Load ICON1.PNG
	LoadFile(dirPath, ICON1_FILENAME, &param->icon1FileData);
	// Load PIC1.PNG
	LoadFile(dirPath, PIC1_FILENAME, &param->pic1FileData);
	// Load SND0.AT3
	LoadFile(dirPath, SND0_FILENAME, &param->snd0FileData);

	return true;
}

bool SavedataParam::LoadSaveData(SceUtilitySavedataParam *param, const std::string &saveDirName, const std::string& dirPath, bool secureMode) {
	if (param->secureVersion != 0) {
		WARN_LOG_REPORT(SCEUTILITY, "Savedata version requested: %d", param->secureVersion);
	}
	u8 *data_ = param->dataBuf;
	std::string filePath = dirPath+"/"+GetFileName(param);
	s64 readSize;
	INFO_LOG(SCEUTILITY,"Loading file with size %u in %s",param->dataBufSize,filePath.c_str());
	u8* saveData = 0;
	int saveSize = -1;
	if (!ReadPSPFile(filePath, &saveData, saveSize, &readSize)) {
		ERROR_LOG(SCEUTILITY,"Error reading file %s",filePath.c_str());
		return false;
	}
	saveSize = (int)readSize;

	// copy back save name in request
	strncpy(param->saveName, saveDirName.c_str(), 20);

	int prevCryptMode = GetSaveCryptMode(param, saveDirName);
	bool isCrypted = prevCryptMode != 0 && secureMode;
	bool saveDone = false;
	if (isCrypted) {
		LoadCryptedSave(param, data_, saveData, saveSize, prevCryptMode, saveDone);
	}
	if (!saveDone) {
		LoadNotCryptedSave(param, data_, saveData, saveSize);
	}
	param->dataSize = (SceSize)saveSize;
	delete[] saveData;

	return true;
}

int SavedataParam::DetermineCryptMode(const SceUtilitySavedataParam *param) const {
	int decryptMode = 1;
	if (param->secureVersion != 0) {
		decryptMode = param->secureVersion;
	} else if (HasKey(param)) {
		decryptMode = GetSDKMainVersion(sceKernelGetCompiledSdkVersion()) >= 4 ? 5 : 3;
	}
	return decryptMode;
}

void SavedataParam::LoadCryptedSave(SceUtilitySavedataParam *param, u8 *data, u8 *saveData, int &saveSize, int prevCryptMode, bool &saveDone) {
	int align_len = align16(saveSize);
	u8 *data_base = new u8[align_len];
	u8 *cryptKey = new u8[0x10];
	memset(cryptKey, 0, 0x10);

	int decryptMode = DetermineCryptMode(param);
	bool hasKey = decryptMode > 1;
	if (hasKey) {
		memcpy(cryptKey, param->key, 0x10);
	}
	memset(data_base + saveSize, 0, align_len - saveSize);
	memcpy(data_base, saveData, saveSize);

	if (decryptMode != prevCryptMode) {
		if (prevCryptMode == 1 && param->key[0] == 0) {
			// Backwards compat for a bug we used to have.
			WARN_LOG(SCEUTILITY, "Savedata loading with hashmode %d instead of detected %d", prevCryptMode, decryptMode);
			decryptMode = prevCryptMode;

			// Don't notify the user if we're not going to upgrade the save.
			if (!g_Config.bEncryptSave) {
				I18NCategory *di = GetI18NCategory("Dialog");
				host->NotifyUserMessage(di->T("When you save, it will load on a PSP, but not an older PPSSPP"), 6.0f);
				host->NotifyUserMessage(di->T("Old savedata detected"), 6.0f);
			}
		} else {
			if (decryptMode == 5 && prevCryptMode == 3) {
				WARN_LOG(SCEUTILITY, "Savedata loading with detected hashmode %d instead of file's %d", decryptMode, prevCryptMode);
			} else {
				WARN_LOG_REPORT(SCEUTILITY, "Savedata loading with detected hashmode %d instead of file's %d", decryptMode, prevCryptMode);
			}
			if (g_Config.bSavedataUpgrade) {
				decryptMode = prevCryptMode;
				I18NCategory *di = GetI18NCategory("Dialog");
				host->NotifyUserMessage(di->T("When you save, it will not work on outdated PSP Firmware anymore"), 6.0f);
				host->NotifyUserMessage(di->T("Old savedata detected"), 6.0f);
			}
		}
		hasKey = decryptMode > 1;
	}

	if (DecryptSave(decryptMode, data_base, &saveSize, &align_len, (hasKey?cryptKey:0)) == 0) {
		if (param->dataBuf.IsValid())
			memcpy(data, data_base, std::min((u32)saveSize, (u32)param->dataBufSize));
		saveDone = true;
	}
	delete[] data_base;
	delete[] cryptKey;
}

void SavedataParam::LoadNotCryptedSave(SceUtilitySavedataParam *param, u8 *data, u8 *saveData, int &saveSize) {
	if (param->dataBuf.IsValid())
		memcpy(data, saveData, std::min((u32)saveSize, (u32)param->dataBufSize));
}

void SavedataParam::LoadSFO(SceUtilitySavedataParam *param, const std::string& dirPath) {
	ParamSFOData sfoFile;
	std::string sfopath = dirPath+"/" + SFO_FILENAME;
	PSPFileInfo sfoInfo = pspFileSystem.GetFileInfo(sfopath);
	if (sfoInfo.exists) {
		// Read sfo
		std::vector<u8> sfoData;
		if (pspFileSystem.ReadEntireFile(sfopath, sfoData) >= 0) {
			sfoFile.ReadSFO(sfoData);

			// copy back info in request
			strncpy(param->sfoParam.title,sfoFile.GetValueString("TITLE").c_str(),128);
			strncpy(param->sfoParam.savedataTitle,sfoFile.GetValueString("SAVEDATA_TITLE").c_str(),128);
			strncpy(param->sfoParam.detail,sfoFile.GetValueString("SAVEDATA_DETAIL").c_str(),1024);
			param->sfoParam.parentalLevel = sfoFile.GetValueInt("PARENTAL_LEVEL");
		}
	}
}

std::set<std::string> SavedataParam::getSecureFileNames(std::string dirPath) {
	PSPFileInfo sfoFileInfo = pspFileSystem.GetFileInfo(dirPath + "/" + SFO_FILENAME);
	std::set<std::string> secureFileNames;
	if (!sfoFileInfo.exists)
		return secureFileNames;

	ParamSFOData sfoFile;
	std::vector<u8> sfoData;
	if (pspFileSystem.ReadEntireFile(dirPath + "/" + SFO_FILENAME, sfoData) >= 0) {
		sfoFile.ReadSFO(sfoData);
	}

	u32 sfoFileListSize = 0;
	char *sfoFileList = (char *)sfoFile.GetValueData("SAVEDATA_FILE_LIST", &sfoFileListSize);
	const int FILE_LIST_ITEM_SIZE = 13 + 16 + 3;
	const u32 FILE_LIST_COUNT_MAX = 99;

	// Filenames are 13 bytes long at most.  Add a NULL so there's no surprises.
	char temp[14];
	temp[13] = '\0';

	for (u32 i = 0; i < FILE_LIST_COUNT_MAX; ++i) {
		// Ends at a NULL filename.
		if (i * FILE_LIST_ITEM_SIZE >= sfoFileListSize || sfoFileList[i * FILE_LIST_ITEM_SIZE] == '\0') {
			break;
		}

		strncpy(temp, &sfoFileList[i * FILE_LIST_ITEM_SIZE], 13);
		secureFileNames.insert(temp);
	}
	return secureFileNames;
}

void SavedataParam::LoadFile(const std::string& dirPath, const std::string& filename, PspUtilitySavedataFileData *fileData) {
	std::string filePath = dirPath + "/" + filename;
	s64 readSize = -1;
	if(!fileData->buf.IsValid())
		return;
	u8 *buf = fileData->buf;
	if(ReadPSPFile(filePath, &buf, fileData->bufSize, &readSize))
		fileData->size = readSize;
}

int SavedataParam::EncryptData(unsigned int mode,
		 unsigned char *data,
		 int *dataLen,
		 int *alignedLen,
		 unsigned char *hash,
		 unsigned char *cryptkey)
{
	pspChnnlsvContext1 ctx1;
	pspChnnlsvContext2 ctx2;

	/* Make room for the IV in front of the data. */
	memmove(data + 0x10, data, *alignedLen);

	/* Set up buffers */
	memset(&ctx1, 0, sizeof(pspChnnlsvContext1));
	memset(&ctx2, 0, sizeof(pspChnnlsvContext2));
	memset(hash, 0, 0x10);
	memset(data, 0, 0x10);

	/* Build the 0x10-byte IV and setup encryption */
	if (sceSdCreateList_(ctx2, mode, 1, data, cryptkey) < 0)
		return -1;
	if (sceSdSetIndex_(ctx1, mode) < 0)
		return -2;
	if (sceSdRemoveValue_(ctx1, data, 0x10) < 0)
		return -3;
	if (sceSdSetMember_(ctx2, data + 0x10, *alignedLen) < 0)
		return -4;

	/* Clear any extra bytes left from the previous steps */
	memset(data + 0x10 + *dataLen, 0, *alignedLen - *dataLen);

	/* Encrypt the data */
	if (sceSdRemoveValue_(ctx1, data + 0x10, *alignedLen) < 0)
		return -5;

	/* Verify encryption */
	if (sceChnnlsv_21BE78B4_(ctx2) < 0)
		return -6;

	/* Build the file hash from this PSP */
	if (sceSdGetLastIndex_(ctx1, hash, cryptkey) < 0)
		return -7;

	/* Adjust sizes to account for IV */
	*alignedLen += 0x10;
	*dataLen += 0x10;

	/* All done */
	return 0;
}

int SavedataParam::DecryptSave(unsigned int mode,
		 unsigned char *data,
		 int *dataLen,
		 int *alignedLen,
		 unsigned char *cryptkey)
{

	pspChnnlsvContext1 ctx1;
	pspChnnlsvContext2 ctx2;

	/* Need a 16-byte IV plus some data */
	if (*alignedLen <= 0x10)
		return -1;
	*dataLen -= 0x10;
	*alignedLen -= 0x10;

	/* Set up buffers */
	memset(&ctx1, 0, sizeof(pspChnnlsvContext1));
	memset(&ctx2, 0, sizeof(pspChnnlsvContext2));

	/* Perform the magic */
	if (sceSdSetIndex_(ctx1, mode) < 0)
		return -2;
	if (sceSdCreateList_(ctx2, mode, 2, data, cryptkey) < 0)
		return -3;
	if (sceSdRemoveValue_(ctx1, data, 0x10) < 0)
		return -4;
	if (sceSdRemoveValue_(ctx1, data + 0x10, *alignedLen) < 0)
		return -5;
	if (sceSdSetMember_(ctx2, data + 0x10, *alignedLen) < 0)
		return -6;

	/* Verify that it decrypted correctly */
	if (sceChnnlsv_21BE78B4_(ctx2) < 0)
		return -7;

	/* The decrypted data starts at data + 0x10, so shift it back. */
	memmove(data, data + 0x10, *dataLen);
	return 0;
}

int SavedataParam::UpdateHash(u8* sfoData, int sfoSize, int sfoDataParamsOffset, int encryptmode)
{
	int alignedLen = align16(sfoSize);
	memset(sfoData+sfoDataParamsOffset, 0, 128);
	u8 filehash[16];
	int ret = 0;

	int firstHashMode = encryptmode & 2 ? 4 : 2;
	int secondHashMode = encryptmode & 2 ? 3 : 0;
	if (encryptmode & 4) {
		firstHashMode = 6;
		secondHashMode = 5;
	}

	// Compute 11D0 hash over entire file
	if ((ret = BuildHash(filehash, sfoData, sfoSize, alignedLen, firstHashMode, 0)) < 0)
	{
		// Not sure about "2"
		return ret - 400;
	}

	// Copy 11D0 hash to param.sfo and set flag indicating it's there
	memcpy(sfoData+sfoDataParamsOffset + 0x20, filehash, 0x10);
	*(sfoData+sfoDataParamsOffset) |= 0x01;

	// If new encryption mode, compute and insert the 1220 hash.
	if (encryptmode & 6)
	{
		/* Enable the hash bit first */
		*(sfoData+sfoDataParamsOffset) |= (encryptmode & 6) << 4;

		if ((ret = BuildHash(filehash, sfoData, sfoSize, alignedLen, secondHashMode, 0)) < 0)
		{
			return ret - 500;
		}
		memcpy(sfoData+sfoDataParamsOffset + 0x70, filehash, 0x10);
	}

	/* Compute and insert the 11C0 hash. */
	if ((ret = BuildHash(filehash, sfoData, sfoSize, alignedLen, 1, 0)) < 0)
	{
		return ret - 600;
	}
	memcpy(sfoData+sfoDataParamsOffset + 0x10, filehash, 0x10);

	/* All done. */
	return 0;
}

int SavedataParam::BuildHash(unsigned char *output,
		unsigned char *data,
		unsigned int len,
		unsigned int alignedLen,
		int mode,
		unsigned char *cryptkey)
{
	pspChnnlsvContext1 ctx1;

	/* Set up buffers */
	memset(&ctx1, 0, sizeof(pspChnnlsvContext1));
	memset(output, 0, 0x10);
	memset(data + len, 0, alignedLen - len);

	/* Perform the magic */
	if (sceSdSetIndex_(ctx1, mode & 0xFF) < 0)
		return -1;
	if (sceSdRemoveValue_(ctx1, data, alignedLen) < 0)
		return -2;
	if (sceSdGetLastIndex_(ctx1, output, cryptkey) < 0)
	{
		// Got here since Kirk CMD5 missing, return random value;
		memset(output,0x1,0x10);
		return 0;
	}
	/* All done. */
	return 0;
}

std::string SavedataParam::GetSpaceText(u64 size, bool roundUp)
{
	static const char *suffixes[] = {"B", "KB", "MB", "GB"};
	char text[50];

	for (size_t i = 0; i < ARRAY_SIZE(suffixes); ++i)
	{
		if (size < 1024)
		{
			snprintf(text, sizeof(text), "%lld %s", size, suffixes[i]);
			return std::string(text);
		}
		if (roundUp) {
			size = (size + 1023) / 1024;
		} else {
			size /= 1024;
		}
	}

	snprintf(text, sizeof(text), "%llu TB", size);
	return std::string(text);
}

int SavedataParam::GetSizes(SceUtilitySavedataParam *param)
{
	if (!param) {
		return SCE_UTILITY_SAVEDATA_ERROR_SIZES_NO_DATA;
	}

	int ret = 0;

	if (param->msFree.IsValid())
	{
		const u64 freeBytes = MemoryStick_FreeSpace();
		param->msFree->clusterSize = (u32)MemoryStick_SectorSize();
		param->msFree->freeClusters = (u32)(freeBytes / MemoryStick_SectorSize());
		param->msFree->freeSpaceKB = (u32)(freeBytes / 0x400);
		const std::string spaceTxt = SavedataParam::GetSpaceText(freeBytes, false);
		memset(param->msFree->freeSpaceStr, 0, sizeof(param->msFree->freeSpaceStr));
		strncpy(param->msFree->freeSpaceStr, spaceTxt.c_str(), sizeof(param->msFree->freeSpaceStr));
	}
	if (param->msData.IsValid())
	{
		const std::string gameName(param->msData->gameName, strnlen(param->msData->gameName, sizeof(param->msData->gameName)));
		const std::string saveName(param->msData->saveName, strnlen(param->msData->saveName, sizeof(param->msData->saveName)));
		// TODO: How should <> be handled?
		std::string path = GetSaveFilePath(param, gameName + (saveName == "<>" ? "" : saveName));
		PSPFileInfo finfo = pspFileSystem.GetFileInfo(path);
		if (finfo.exists)
		{
			param->msData->info.usedClusters = 0;
			auto listing = pspFileSystem.GetDirListing(path);
			for (auto it = listing.begin(), end = listing.end(); it != end; ++it) {
				param->msData->info.usedClusters += (it->size + (u32)MemoryStick_SectorSize() - 1) / (u32)MemoryStick_SectorSize();
			}

			// The usedSpaceKB value is definitely based on clusters, not bytes or even KB.
			// Fieldrunners expects 736 KB, even though the files add up to ~600 KB.
			int total_size = param->msData->info.usedClusters * (u32)MemoryStick_SectorSize();
			param->msData->info.usedSpaceKB = total_size / 0x400;
			std::string spaceTxt = SavedataParam::GetSpaceText(total_size, true);
			strncpy(param->msData->info.usedSpaceStr, spaceTxt.c_str(), sizeof(param->msData->info.usedSpaceStr));

			// TODO: What does this mean, then?  Seems to be the same.
			param->msData->info.usedSpace32KB = param->msData->info.usedSpaceKB;
			strncpy(param->msData->info.usedSpace32Str, spaceTxt.c_str(), sizeof(param->msData->info.usedSpace32Str));
		}
		else
		{
			param->msData->info.usedClusters = 0;
			param->msData->info.usedSpaceKB = 0;
			strncpy(param->msData->info.usedSpaceStr, "", sizeof(param->msData->info.usedSpaceStr));
			param->msData->info.usedSpace32KB = 0;
			strncpy(param->msData->info.usedSpace32Str, "", sizeof(param->msData->info.usedSpace32Str));
			ret = SCE_UTILITY_SAVEDATA_ERROR_SIZES_NO_DATA;
		}
	}
	if (param->utilityData.IsValid())
	{
		int total_size = 0;

		// The directory record itself.
		// TODO: Account for number of files / actual record size?
		total_size += getSizeNormalized(1);
		// Account for the SFO (is this always 1 sector?)
		total_size += getSizeNormalized(1);
		// Add the size of the data itself (don't forget encryption overhead.)
		// This is only added if a filename is specified.
		if (param->fileName[0] != 0) {
			if (g_Config.bEncryptSave) {
				total_size += getSizeNormalized((u32)param->dataSize + 16);
			} else {
				total_size += getSizeNormalized((u32)param->dataSize);
			}
		}
		total_size += getSizeNormalized(param->icon0FileData.size);
		total_size += getSizeNormalized(param->icon1FileData.size);
		total_size += getSizeNormalized(param->pic1FileData.size);
		total_size += getSizeNormalized(param->snd0FileData.size);

		param->utilityData->usedClusters = total_size / (u32)MemoryStick_SectorSize();
		param->utilityData->usedSpaceKB = total_size / 0x400;
		std::string spaceTxt = SavedataParam::GetSpaceText(total_size, true);
		memset(param->utilityData->usedSpaceStr, 0, sizeof(param->utilityData->usedSpaceStr));
		strncpy(param->utilityData->usedSpaceStr, spaceTxt.c_str(), sizeof(param->utilityData->usedSpaceStr));

		// TODO: Maybe these are rounded to the nearest 32KB?  Or something?
		param->utilityData->usedSpace32KB = total_size / 0x400;
		spaceTxt = SavedataParam::GetSpaceText(total_size, true);
		memset(param->utilityData->usedSpace32Str, 0, sizeof(param->utilityData->usedSpace32Str));
		strncpy(param->utilityData->usedSpace32Str, spaceTxt.c_str(), sizeof(param->utilityData->usedSpace32Str));
	}
	return ret;

}

bool SavedataParam::GetList(SceUtilitySavedataParam *param)
{
	if (!param) {
		return false;
	}

	if (param->idList.IsValid())
	{
		u32 maxFile = param->idList->maxCount;

		std::vector<PSPFileInfo> validDir;
		std::vector<PSPFileInfo> sfoFiles;
		std::vector<PSPFileInfo> allDir = pspFileSystem.GetDirListing(savePath);

		if (param->idList.IsValid())
		{
			std::string searchString = GetGameName(param)+GetSaveName(param);
			for (size_t i = 0; i < allDir.size() && validDir.size() < maxFile; i++)
			{
				std::string dirName = allDir[i].name;
				if(PSPMatch(dirName, searchString))
				{
					validDir.push_back(allDir[i]);
				}
			}

			PSPFileInfo sfoFile;
			for (size_t i = 0; i < validDir.size(); ++i) {
				// GetFileName(param) == NUll here
				// so use sfo files to set the date. 
				sfoFile = pspFileSystem.GetFileInfo(savePath + validDir[i].name + "/" + "PARAM.SFO");
				sfoFiles.push_back(sfoFile);
			}

			SceUtilitySavedataIdListEntry *entries = param->idList->entries;
			for (u32 i = 0; i < (u32)validDir.size(); i++)
			{
				entries[i].st_mode = 0x11FF;
				if (sfoFiles[i].exists) {
					__IoCopyDate(entries[i].st_ctime, sfoFiles[i].ctime);
					__IoCopyDate(entries[i].st_atime, sfoFiles[i].atime);
					__IoCopyDate(entries[i].st_mtime, sfoFiles[i].mtime);
				} else {
					__IoCopyDate(entries[i].st_ctime, validDir[i].ctime);
					__IoCopyDate(entries[i].st_atime, validDir[i].atime);
					__IoCopyDate(entries[i].st_mtime, validDir[i].mtime);
				}
				// folder name without gamename (max 20 u8)
				std::string outName = validDir[i].name.substr(GetGameName(param).size());
				memset(entries[i].name, 0, sizeof(entries[i].name));
				strncpy(entries[i].name, outName.c_str(), sizeof(entries[i].name));
			}
		}
		// Save num of folder found
		param->idList->resultCount = (u32)validDir.size();
	}
	return true;
}

int SavedataParam::GetFilesList(SceUtilitySavedataParam *param)
{
	if (!param)	{
		return SCE_UTILITY_SAVEDATA_ERROR_RW_BAD_STATUS;
	}

	if (!param->fileList.IsValid()) {
		ERROR_LOG_REPORT(SCEUTILITY, "SavedataParam::GetFilesList(): bad fileList address %08x", param->fileList.ptr);
		// Should crash.
		return -1;
	}

	auto &fileList = param->fileList;
	if (fileList->secureEntries.IsValid() && fileList->maxSecureEntries > 99) {
		ERROR_LOG_REPORT(SCEUTILITY, "SavedataParam::GetFilesList(): too many secure entries, %d", fileList->maxSecureEntries);
		return SCE_UTILITY_SAVEDATA_ERROR_RW_BAD_PARAMS;
	}
	if (fileList->normalEntries.IsValid() && fileList->maxNormalEntries > 8192) {
		ERROR_LOG_REPORT(SCEUTILITY, "SavedataParam::GetFilesList(): too many normal entries, %d", fileList->maxNormalEntries);
		return SCE_UTILITY_SAVEDATA_ERROR_RW_BAD_PARAMS;
	}
	if (sceKernelGetCompiledSdkVersion() >= 0x02060000) {
		if (fileList->systemEntries.IsValid() && fileList->maxSystemEntries > 5) {
			ERROR_LOG_REPORT(SCEUTILITY, "SavedataParam::GetFilesList(): too many system entries, %d", fileList->maxSystemEntries);
			return SCE_UTILITY_SAVEDATA_ERROR_RW_BAD_PARAMS;
		}
	}

	std::string dirPath = savePath + GetGameName(param) + GetSaveName(param);
	if (!pspFileSystem.GetFileInfo(dirPath).exists) {
		DEBUG_LOG(SCEUTILITY, "SavedataParam::GetFilesList(): directory %s does not exist", dirPath.c_str());
		return SCE_UTILITY_SAVEDATA_ERROR_RW_NO_DATA;
	}

	// Even if there are no files, initialize to 0.
	fileList->resultNumSecureEntries = 0;
	fileList->resultNumNormalEntries = 0;
	fileList->resultNumSystemEntries = 0;

	// We need PARAMS.SFO's SAVEDATA_FILE_LIST to determine which entries are secure.
	PSPFileInfo sfoFileInfo = pspFileSystem.GetFileInfo(dirPath + "/" + SFO_FILENAME);
	std::set<std::string> secureFilenames;

	if (sfoFileInfo.exists) {
		secureFilenames = getSecureFileNames(dirPath);
	} else {
		return SCE_UTILITY_SAVEDATA_ERROR_RW_DATA_BROKEN;
	}

	// Does not list directories, nor recurse into them, and ignores files not ALL UPPERCASE.
	auto files = pspFileSystem.GetDirListing(dirPath);
	for (auto file = files.begin(), end = files.end(); file != end; ++file) {
		if (file->type == FILETYPE_DIRECTORY) {
			continue;
		}
		// TODO: What are the exact rules?  It definitely skips lowercase, and allows FILE or FILE.EXT.
		if (file->name.find_first_of("abcdefghijklmnopqrstuvwxyz") != file->name.npos) {
			DEBUG_LOG(SCEUTILITY, "SavedataParam::GetFilesList(): skipping file %s with lowercase", file->name.c_str());
			continue;
		}

		bool isSystemFile = file->name == ICON0_FILENAME || file->name == ICON1_FILENAME || file->name == PIC1_FILENAME;
		isSystemFile = isSystemFile || file->name == SND0_FILENAME || file->name == SFO_FILENAME;

		SceUtilitySavedataFileListEntry *entry = NULL;
		int sizeOffset = 0;
		if (isSystemFile) {
			if (fileList->systemEntries.IsValid() && fileList->resultNumSystemEntries < fileList->maxSystemEntries) {
				entry = &fileList->systemEntries[fileList->resultNumSystemEntries++];
			}
		} else if (secureFilenames.find(file->name) != secureFilenames.end()) {
			if (fileList->secureEntries.IsValid() && fileList->resultNumSecureEntries < fileList->maxSecureEntries) {
				entry = &fileList->secureEntries[fileList->resultNumSecureEntries++];
			}
			// Secure files are slightly bigger.
			bool isCrypted = GetSaveCryptMode(param, GetSaveDirName(param, 0)) != 0;
			if (isCrypted) {
				sizeOffset = -0x10;
			}
		} else {
			if (fileList->normalEntries.IsValid() && fileList->resultNumNormalEntries < fileList->maxNormalEntries) {
				entry = &fileList->normalEntries[fileList->resultNumNormalEntries++];
			}
		}

		// Out of space for this file in the list.
		if (entry == NULL) {
			continue;
		}

		entry->st_mode = 0x21FF;
		entry->st_size = file->size + sizeOffset;
		__IoCopyDate(entry->st_ctime, file->ctime);
		__IoCopyDate(entry->st_atime, file->atime);
		__IoCopyDate(entry->st_mtime, file->mtime);
		// TODO: Probably actually 13 + 3 pad...
		strncpy(entry->name, file->name.c_str(), 16);
		entry->name[15] = '\0';
	}

	// TODO: Does this always happen?
	// Don't know what it is, but PSP always respond this
	param->bind = 1021;

	return 0;
}

bool SavedataParam::GetSize(SceUtilitySavedataParam *param)
{
	if (!param)
	{
		return false;
	}

	const std::string saveDir = savePath + GetGameName(param) + GetSaveName(param);
	PSPFileInfo info = pspFileSystem.GetFileInfo(saveDir);
	bool exists = info.exists;

	if (param->sizeInfo.IsValid())
	{
		const u64 freeBytes = MemoryStick_FreeSpace();

		s64 overwriteBytes = 0;
		s64 writeBytes = 0;
		for (int i = 0; i < param->sizeInfo->numNormalEntries; ++i) {
			const auto &entry = param->sizeInfo->normalEntries[i];
			overwriteBytes += pspFileSystem.GetFileInfo(saveDir + "/" + entry.name).size;
			writeBytes += entry.size;
		}
		for (int i = 0; i < param->sizeInfo->numSecureEntries; ++i) {
			const auto &entry = param->sizeInfo->secureEntries[i];
			overwriteBytes += pspFileSystem.GetFileInfo(saveDir + "/" + entry.name).size;
			writeBytes += entry.size + 0x10;
		}

		param->sizeInfo->sectorSize = (int)MemoryStick_SectorSize();
		param->sizeInfo->freeSectors = (int)(freeBytes / MemoryStick_SectorSize());

		// TODO: Is this after the specified files?  Probably before?
		param->sizeInfo->freeKB = (int)(freeBytes / 1024);
		std::string spaceTxt = SavedataParam::GetSpaceText(freeBytes, false);
		truncate_cpy(param->sizeInfo->freeString, spaceTxt.c_str());

		if (writeBytes - overwriteBytes < (s64)freeBytes) {
			param->sizeInfo->neededKB = 0;

			// Note: this is "needed to overwrite".
			param->sizeInfo->overwriteKB = 0;

			spaceTxt = GetSpaceText(0, true);
			truncate_cpy(param->sizeInfo->neededString, spaceTxt.c_str());
			truncate_cpy(param->sizeInfo->overwriteString, spaceTxt.c_str());
		} else {
			// Bytes needed to save additional data.
			s64 neededBytes = writeBytes - freeBytes;
			param->sizeInfo->neededKB = (neededBytes + 1023) / 1024;
			spaceTxt = GetSpaceText(neededBytes, true);
			truncate_cpy(param->sizeInfo->neededString, spaceTxt.c_str());

			if (writeBytes - overwriteBytes < (s64)freeBytes) {
				param->sizeInfo->overwriteKB = 0;
				spaceTxt = GetSpaceText(0, true);
				truncate_cpy(param->sizeInfo->overwriteString, spaceTxt.c_str());
			} else {
				s64 neededOverwriteBytes = writeBytes - freeBytes - overwriteBytes;
				param->sizeInfo->overwriteKB = (neededOverwriteBytes + 1023) / 1024;
				spaceTxt = GetSpaceText(neededOverwriteBytes, true);
				truncate_cpy(param->sizeInfo->overwriteString, spaceTxt.c_str());
			}
		}
	}

	return exists;
}

void SavedataParam::Clear()
{
	if (saveDataList)
	{
		for (int i = 0; i < saveNameListDataCount; i++)
		{
			if (saveDataList[i].texture != NULL && (!noSaveIcon || saveDataList[i].texture != noSaveIcon->texture))
				delete saveDataList[i].texture;
			saveDataList[i].texture = NULL;
		}

		delete [] saveDataList;
		saveDataList = 0;
		saveDataListCount = 0;
	}
	if (noSaveIcon)
	{
		if (noSaveIcon->texture != NULL)
			delete noSaveIcon->texture;
		noSaveIcon->texture = NULL;
		delete noSaveIcon;
		noSaveIcon = 0;
	}
}

int SavedataParam::SetPspParam(SceUtilitySavedataParam *param)
{
	pspParam = param;
	if (!pspParam)
	{
		Clear();
		return 0;
	}

	bool listEmptyFile = true;
	if (param->mode == SCE_UTILITY_SAVEDATA_TYPE_LISTLOAD ||
			param->mode == SCE_UTILITY_SAVEDATA_TYPE_LISTDELETE)
	{
		listEmptyFile = false;
	}

	SceUtilitySavedataSaveName *saveNameListData;
	bool hasMultipleFileName = false;
	if (param->saveNameList.IsValid())
	{
		Clear();

		saveNameListData = param->saveNameList;

		// Get number of fileName in array
		saveDataListCount = 0;
		while (saveNameListData[saveDataListCount][0] != 0)
		{
			saveDataListCount++;
		}

		if (saveDataListCount > 0)
		{
			hasMultipleFileName = true;
			saveDataList = new SaveFileInfo[saveDataListCount];
			
			// get and stock file info for each file
			int realCount = 0;
			for (int i = 0; i < saveDataListCount; i++) {
				// "<>" means saveName can be anything...
				if (strncmp(saveNameListData[i], "<>", ARRAY_SIZE(saveNameListData[i])) == 0) {
					std::string fileDataPath = "";				
					// TODO:Maybe we need a way to reorder the files?
					auto allSaves = pspFileSystem.GetDirListing(savePath);
					std::string gameName = GetGameName(param);
					std::string saveName = "";
					for(auto it = allSaves.begin(); it != allSaves.end(); ++it) {
						if(it->name.compare(0, gameName.length(), gameName) == 0) {
							saveName = it->name.substr(gameName.length());
							
							if(IsInSaveDataList(saveName, realCount)) // Already in SaveDataList, skip...
								continue;

							fileDataPath = savePath + it->name +  "/" + GetFileName(param);
							PSPFileInfo info = pspFileSystem.GetFileInfo(fileDataPath);
							if (info.exists) {
								SetFileInfo(realCount, info, saveName);
								DEBUG_LOG(SCEUTILITY,"%s Exist",fileDataPath.c_str());
								++realCount;
							} else {
								if (listEmptyFile) {
									// If file doesn't exist,we only skip...
									continue;
								}
							}
							break;
						}
					}
					continue;
				}

				const std::string thisSaveName = FixedToString(saveNameListData[i], ARRAY_SIZE(saveNameListData[i]));
				DEBUG_LOG(SCEUTILITY, "Name : %s", thisSaveName.c_str());

				std::string fileDataPath = savePath + GetGameName(param) + thisSaveName + "/" + GetFileName(param);
				PSPFileInfo info = pspFileSystem.GetFileInfo(fileDataPath);
				if (info.exists)
				{
					SetFileInfo(realCount, info, thisSaveName);

					DEBUG_LOG(SCEUTILITY,"%s Exist",fileDataPath.c_str());
					realCount++;
				}
				else
				{
					if (listEmptyFile)
					{
						ClearFileInfo(saveDataList[realCount], thisSaveName);
						DEBUG_LOG(SCEUTILITY,"Don't Exist");
						realCount++;
					}
				}
			}
			saveNameListDataCount = realCount;
		}
	}
	if (!hasMultipleFileName) // Load info on only save
	{
		saveNameListData = 0;

		Clear();
		saveDataList = new SaveFileInfo[1];
		saveDataListCount = 1;

		// get and stock file info for each file
		DEBUG_LOG(SCEUTILITY,"Name : %s",GetSaveName(param).c_str());

		std::string fileDataPath = savePath + GetGameName(param) + GetSaveName(param) + "/" + GetFileName(param);
		PSPFileInfo info = pspFileSystem.GetFileInfo(fileDataPath);
		if (info.exists)
		{
			SetFileInfo(0, info, GetSaveName(param));

			DEBUG_LOG(SCEUTILITY,"%s Exist",fileDataPath.c_str());
			saveNameListDataCount = 1;
		}
		else
		{
			if (listEmptyFile)
			{
				ClearFileInfo(saveDataList[0], GetSaveName(param));
				DEBUG_LOG(SCEUTILITY,"Don't Exist");
			}
			saveNameListDataCount = 0;
			return 0;
		}
	}
	return 0;
}

void SavedataParam::SetFileInfo(SaveFileInfo &saveInfo, PSPFileInfo &info, std::string saveName)
{
	saveInfo.size = info.size;
	saveInfo.saveName = saveName;
	saveInfo.idx = 0;
	saveInfo.modif_time = info.mtime;

	// Start with a blank slate.
	if (saveInfo.texture != NULL) {
		if (!noSaveIcon || saveInfo.texture != noSaveIcon->texture) {
			delete saveInfo.texture;
		}
		saveInfo.texture = NULL;
	}
	saveInfo.title[0] = 0;
	saveInfo.saveTitle[0] = 0;
	saveInfo.saveDetail[0] = 0;

	// Search save image icon0
	// TODO : If icon0 don't exist, need to use icon1 which is a moving icon. Also play sound
	std::string fileDataPath2 = savePath + GetGameName(pspParam) + saveName + "/" + ICON0_FILENAME;
	PSPFileInfo info2 = pspFileSystem.GetFileInfo(fileDataPath2);
	if (info2.exists)
		saveInfo.texture = new PPGeImage(fileDataPath2);

	// Load info in PARAM.SFO
	fileDataPath2 = savePath + GetGameName(pspParam) + saveName + "/" + SFO_FILENAME;
	info2 = pspFileSystem.GetFileInfo(fileDataPath2);
	if (info2.exists)
	{
		std::vector<u8> sfoData;
		pspFileSystem.ReadEntireFile(fileDataPath2, sfoData);
		ParamSFOData sfoFile;
		if (sfoFile.ReadSFO(sfoData))
		{
			SetStringFromSFO(sfoFile, "TITLE", saveInfo.title, sizeof(saveInfo.title));
			SetStringFromSFO(sfoFile, "SAVEDATA_TITLE", saveInfo.saveTitle, sizeof(saveInfo.saveTitle));
			SetStringFromSFO(sfoFile, "SAVEDATA_DETAIL", saveInfo.saveDetail, sizeof(saveInfo.saveDetail));
		}
	}
}

void SavedataParam::SetFileInfo(int idx, PSPFileInfo &info, std::string saveName)
{
	SetFileInfo(saveDataList[idx], info, saveName);
	saveDataList[idx].idx = idx;
}

void SavedataParam::ClearFileInfo(SaveFileInfo &saveInfo, const std::string &saveName) {
	saveInfo.size = 0;
	saveInfo.saveName = saveName;
	saveInfo.idx = 0;
	if (saveInfo.texture != NULL) {
		if (!noSaveIcon || saveInfo.texture != noSaveIcon->texture) {
			delete saveInfo.texture;
		}
		saveInfo.texture = NULL;
	}

	if (GetPspParam()->newData.IsValid() && GetPspParam()->newData->buf.IsValid()) {
		// We have a png to show
		if (!noSaveIcon) {
			noSaveIcon = new SaveFileInfo();
			PspUtilitySavedataFileData *newData = GetPspParam()->newData;
			noSaveIcon->texture = new PPGeImage(newData->buf.ptr, (SceSize)newData->size);
		}
		saveInfo.texture = noSaveIcon->texture;
	} else if ((u32)GetPspParam()->mode == SCE_UTILITY_SAVEDATA_TYPE_SAVE && GetPspParam()->icon0FileData.buf.IsValid()) {
		const PspUtilitySavedataFileData &icon0FileData = GetPspParam()->icon0FileData;
		saveInfo.texture = new PPGeImage(icon0FileData.buf.ptr, (SceSize)icon0FileData.size);
	}
}

SceUtilitySavedataParam *SavedataParam::GetPspParam()
{
	return pspParam;
}

const SceUtilitySavedataParam *SavedataParam::GetPspParam() const
{
	return pspParam;
}

int SavedataParam::GetFilenameCount()
{
	return saveNameListDataCount;
}

const SaveFileInfo& SavedataParam::GetFileInfo(int idx)
{
	return saveDataList[idx];
}
std::string SavedataParam::GetFilename(int idx) const
{
	return saveDataList[idx].saveName;
}

int SavedataParam::GetSelectedSave()
{
	// The slot # of the same save on LOAD/SAVE lists can dismatch so this isn't right anyhow
	return selectedSave < saveNameListDataCount ? selectedSave : 0;
}

void SavedataParam::SetSelectedSave(int idx)
{
	selectedSave = idx;
}

int SavedataParam::GetFirstListSave()
{
	return 0;
}

int SavedataParam::GetLastListSave()
{
	return saveNameListDataCount - 1;
}

int SavedataParam::GetLatestSave()
{
	int idx = 0;
	time_t idxTime = 0;
	for (int i = 0; i < saveNameListDataCount; ++i)
	{
		if (saveDataList[i].size == 0)
			continue;
		time_t thisTime = mktime(&saveDataList[i].modif_time);
		if ((s64)idxTime < (s64)thisTime)
		{
			idx = i;
			idxTime = thisTime;
		}
	}
	return idx;
}

int SavedataParam::GetOldestSave()
{
	int idx = 0;
	time_t idxTime = 0;
	for (int i = 0; i < saveNameListDataCount; ++i)
	{
		if (saveDataList[i].size == 0)
			continue;
		time_t thisTime = mktime(&saveDataList[i].modif_time);
		if ((s64)idxTime > (s64)thisTime)
		{
			idx = i;
			idxTime = thisTime;
		}
	}
	return idx;
}

int SavedataParam::GetFirstDataSave()
{
	int idx = 0;
	for (int i = 0; i < saveNameListDataCount; ++i)
	{
		if (saveDataList[i].size != 0)
		{
			idx = i;
			break;
		}
	}
	return idx;
}

int SavedataParam::GetLastDataSave()
{
	int idx = 0;
	for (int i = saveNameListDataCount; i > 0; )
	{
		--i;
		if (saveDataList[i].size != 0)
		{
			idx = i;
			break;
		}
	}
	return idx;
}

int SavedataParam::GetFirstEmptySave()
{
	int idx = 0;
	for (int i = 0; i < saveNameListDataCount; ++i)
	{
		if (saveDataList[i].size == 0)
		{
			idx = i;
			break;
		}
	}
	return idx;
}

int SavedataParam::GetLastEmptySave()
{
	int idx = 0;
	for (int i = saveNameListDataCount; i > 0; )
	{
		--i;
		if (saveDataList[i].size == 0)
		{
			idx = i;
			break;
		}
	}
	return idx;
}

int SavedataParam::GetSaveNameIndex(SceUtilitySavedataParam* param)
{
	std::string saveName = GetSaveName(param);
	for (int i = 0; i < saveNameListDataCount; i++)
	{
		// TODO: saveName may contain wildcards
		if (saveDataList[i].saveName == saveName)
		{
			return i;
		}
	}

	return 0;
}

void SavedataParam::DoState(PointerWrap &p)
{
	auto s = p.Section("SavedataParam", 1);
	if (!s)
		return;

	// pspParam is handled in PSPSaveDialog.
	p.Do(selectedSave);
	p.Do(saveDataListCount);
	p.Do(saveNameListDataCount);
	if (p.mode == p.MODE_READ)
	{
		if (saveDataList != NULL)
			delete [] saveDataList;
		if (saveDataListCount != 0)
		{
			saveDataList = new SaveFileInfo[saveDataListCount];
			p.DoArray(saveDataList, saveDataListCount);
		}
		else
			saveDataList = NULL;
	}
	else
		p.DoArray(saveDataList, saveDataListCount);
}

int SavedataParam::GetSaveCryptMode(SceUtilitySavedataParam* param, const std::string &saveDirName)
{
	ParamSFOData sfoFile;
	std::string dirPath = GetSaveFilePath(param, GetSaveDir(param, saveDirName));
	std::string sfopath = dirPath + "/" + SFO_FILENAME;
	PSPFileInfo sfoInfo = pspFileSystem.GetFileInfo(sfopath);
	if(sfoInfo.exists) // Read sfo
	{
		std::vector<u8> sfoData;
		if (pspFileSystem.ReadEntireFile(sfopath, sfoData) >= 0)
		{
			sfoFile.ReadSFO(sfoData);

			// save created in PPSSPP and not encrypted has '0' in SAVEDATA_PARAMS
			u32 tmpDataSize = 0;
			const u8 *tmpDataOrig = sfoFile.GetValueData("SAVEDATA_PARAMS", &tmpDataSize);
			if (tmpDataSize == 0 || !tmpDataOrig) {
				return 0;
			}
			switch (tmpDataOrig[0]) {
			case 0:
				return 0;
			case 0x01:
				return 1;
			case 0x21:
				return 3;
			case 0x41:
				return 5;
			default:
				// Well, it's not zero, so yes.
				ERROR_LOG_REPORT(SCEUTILITY, "Unexpected SAVEDATA_PARAMS hash flag: %02x", tmpDataOrig[0]);
				return 1;
			}
		}
	}
	return 0;
}

bool SavedataParam::IsInSaveDataList(std::string saveName, int count) {
	for(int i = 0; i < count; ++i) {
		if(strcmp(saveDataList[i].saveName.c_str(),saveName.c_str()) == 0)
			return true;
	}
	return false;
}

bool SavedataParam::IsSaveDirectoryExist(SceUtilitySavedataParam* param) {
	std::string dirPath = savePath + GetGameName(param) + GetSaveName(param);
	PSPFileInfo directoryInfo = pspFileSystem.GetFileInfo(dirPath);
	return directoryInfo.exists;
}

bool SavedataParam::IsSfoFileExist(SceUtilitySavedataParam* param) {
	std::string dirPath = savePath + GetGameName(param) + GetSaveName(param);
	std::string sfoPath = dirPath + "/" + SFO_FILENAME;
	PSPFileInfo sfoInfo = pspFileSystem.GetFileInfo(sfoPath);
	return sfoInfo.exists;
}
