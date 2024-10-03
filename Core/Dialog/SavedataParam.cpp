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

#include <algorithm>
#include <memory>
#include "Common/Log.h"
#include "Common/Data/Text/I18n.h"
#include "Common/Data/Format/ZIMLoad.h"
#include "Common/Serialize/Serializer.h"
#include "Common/Serialize/SerializeFuncs.h"
#include "Common/System/OSD.h"
#include "Common/StringUtils.h"
#include "Core/Config.h"
#include "Core/Reporting.h"
#include "Core/System.h"
#include "Core/Debugger/MemBlockInfo.h"
#include "Core/Dialog/SavedataParam.h"
#include "Core/Dialog/PSPSaveDialog.h"
#include "Core/FileSystems/MetaFileSystem.h"
#include "Core/HLE/sceIo.h"
#include "Core/HLE/sceKernelMemory.h"
#include "Core/HLE/sceChnnlsv.h"
#include "Core/ELF/ParamSFO.h"
#include "Core/HW/MemoryStick.h"
#include "Core/Util/PPGeDraw.h"

static const std::string ICON0_FILENAME = "ICON0.PNG";
static const std::string ICON1_FILENAME = "ICON1.PMF";
static const std::string PIC1_FILENAME = "PIC1.PNG";
static const std::string SND0_FILENAME = "SND0.AT3";
static const std::string SFO_FILENAME = "PARAM.SFO";

static const int FILE_LIST_COUNT_MAX = 99;
static const u32 FILE_LIST_TOTAL_SIZE = sizeof(SaveSFOFileListEntry) * FILE_LIST_COUNT_MAX;

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

	bool ReadPSPFile(const std::string &filename, u8 **data, s64 dataSize, s64 *readSize)
	{
		int handle = pspFileSystem.OpenFile(filename, FILEACCESS_READ);
		if (handle < 0)
			return false;

		if (dataSize == -1) {
			// Determine the size through seeking instead of querying.
			pspFileSystem.SeekFile(handle, 0, FILEMOVE_END);
			dataSize = pspFileSystem.GetSeekPos(handle);
			pspFileSystem.SeekFile(handle, 0, FILEMOVE_BEGIN);

			*data = new u8[(size_t)dataSize];
		}

		size_t result = pspFileSystem.ReadFile(handle, *data, dataSize);
		pspFileSystem.CloseFile(handle);
		if(readSize)
			*readSize = result;

		return result != 0;
	}

	bool WritePSPFile(const std::string &filename, const u8 *data, SceSize dataSize)
	{
		int handle = pspFileSystem.OpenFile(filename, (FileAccess)(FILEACCESS_WRITE | FILEACCESS_CREATE | FILEACCESS_TRUNCATE));
		if (handle < 0)
			return false;

		size_t result = pspFileSystem.WriteFile(handle, data, dataSize);
		pspFileSystem.CloseFile(handle);

		return result == dataSize;
	}

	PSPFileInfo FileFromListing(const std::vector<PSPFileInfo> &listing, const std::string &filename) {
		for (const PSPFileInfo &sub : listing) {
			if (sub.name == filename)
				return sub;
		}

		PSPFileInfo info;
		info.name = filename;
		info.exists = false;
		return info;
	}

	bool PSPMatch(std::string_view text, std::string_view regexp) {
		if (text.empty() && regexp.empty())
			return true;
		else if (regexp == "*")
			return true;
		else if (text.empty())
			return false;
		else if (regexp.empty())
			return false;
		else if (regexp == "?" && text.length() == 1)
			return true;
		else if (text == regexp)
			return true;
		else if (regexp.data()[0] == '*')
		{
			bool res = PSPMatch(text.substr(1),regexp.substr(1));
			if(!res)
				res = PSPMatch(text.substr(1),regexp);
			return res;
		}
		else if (regexp.data()[0] == '?')
		{
			return PSPMatch(text.substr(1),regexp.substr(1));
		}
		else if (regexp.data()[0] == text.data()[0])
		{
			return PSPMatch(text.substr(1),regexp.substr(1));
		}

		return false;
	}

	int align16(int address)
	{
		return (address + 15) & ~15;
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

	Do(p, size);
	Do(p, saveName);
	Do(p, idx);

	DoArray(p, title, sizeof(title));
	DoArray(p, saveTitle, sizeof(saveTitle));
	DoArray(p, saveDetail, sizeof(saveDetail));

	Do(p, modif_time);

	if (s <= 1) {
		u32 textureData;
		int textureWidth;
		int textureHeight;
		Do(p, textureData);
		Do(p, textureWidth);
		Do(p, textureHeight);

		if (textureData != 0) {
			// Must be MODE_READ.
			texture = new PPGeImage("");
			texture->CompatLoad(textureData, textureWidth, textureHeight);
		}
	} else {
		bool hasTexture = texture != NULL;
		Do(p, hasTexture);
		if (hasTexture) {
			if (p.mode == p.MODE_READ) {
				delete texture;
				texture = new PPGeImage("");
			}
			texture->DoState(p);
		}
	}
}

SavedataParam::SavedataParam() { }

void SavedataParam::Init()
{
	if (!pspFileSystem.GetFileInfo(savePath).exists)
	{
		pspFileSystem.MkDir(savePath);
	}
	// Create a nomedia file to hide save icons form Android image viewer
#if PPSSPP_PLATFORM(ANDROID)
	int handle = pspFileSystem.OpenFile(savePath + ".nomedia", (FileAccess)(FILEACCESS_CREATE | FILEACCESS_WRITE), 0);
	if (handle >= 0) {
		pspFileSystem.CloseFile(handle);
	} else {
		INFO_LOG(Log::IO, "Failed to create .nomedia file (might be ok if it already exists)");
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
	if (!str) {
		return std::string();
	} else {
		return std::string(str, strnlen(str, n));
	}
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
	if (!strlen(param->gameName) && param->mode != SCE_UTILITY_SAVEDATA_TYPE_LISTALLDELETE) {
		ERROR_LOG(Log::sceUtility, "Bad param with gameName empty - cannot delete save directory");
		return false;
	}

	std::string dirPath = GetSaveFilePath(param, GetSaveDir(saveId));
	if (dirPath.size() == 0) {
		ERROR_LOG(Log::sceUtility, "GetSaveFilePath (%.*s) returned empty - cannot delete save directory. Might already be deleted?", (int)sizeof(param->gameName), param->gameName);
		return false;
	}

	if (!pspFileSystem.GetFileInfo(dirPath).exists) {
		return false;
	}

	ClearSFOCache();
	pspFileSystem.RmDir(dirPath);
	return true;
}

int SavedataParam::DeleteData(SceUtilitySavedataParam* param) {
	if (!param) {
		return SCE_UTILITY_SAVEDATA_ERROR_RW_FILE_NOT_FOUND;
	}

	std::string subFolder = GetGameName(param) + GetSaveName(param);
	std::string fileName = GetFileName(param);
	std::string dirPath = savePath + subFolder;
	std::string filePath = dirPath + "/" + fileName;
	std::string sfoPath = dirPath + "/" + SFO_FILENAME;

	if (!pspFileSystem.GetFileInfo(dirPath).exists) {
		return SCE_UTILITY_SAVEDATA_ERROR_RW_NO_DATA;
	}

	if (!pspFileSystem.GetFileInfo(sfoPath).exists)
		return SCE_UTILITY_SAVEDATA_ERROR_RW_DATA_BROKEN;

	if (!fileName.empty() && !pspFileSystem.GetFileInfo(filePath).exists) {
		return SCE_UTILITY_SAVEDATA_ERROR_RW_FILE_NOT_FOUND;
	}

	if (fileName.empty()) {
		return 0;
	}

	if (!subFolder.size()) {
		ERROR_LOG(Log::sceUtility, "Bad subfolder, ignoring delete of %s", filePath.c_str());
		return 0;
	}

	ClearSFOCache();
	pspFileSystem.RemoveFile(filePath);

	// Update PARAM.SFO to remove the file, if it was in the list.
	std::shared_ptr<ParamSFOData> sfoFile = LoadCachedSFO(sfoPath);
	if (sfoFile) {
		// Note: do not update values such as TITLE in this operation.
		u32 fileListSize = 0;
		SaveSFOFileListEntry *fileList = (SaveSFOFileListEntry *)sfoFile->GetValueData("SAVEDATA_FILE_LIST", &fileListSize);
		size_t fileListCount = fileListSize / sizeof(SaveSFOFileListEntry);
		bool changed = false;
		for (size_t i = 0; i < fileListCount; ++i) {
			if (strncmp(fileList[i].filename, fileName.c_str(), sizeof(fileList[i].filename)) != 0)
				continue;

			memset(fileList[i].filename, 0, sizeof(fileList[i].filename));
			memset(fileList[i].hash, 0, sizeof(fileList[i].hash));
			changed = true;
			break;
		}

		if (changed) {
			auto updatedList = std::make_unique<u8[]> (fileListSize);
			memcpy(updatedList.get(), fileList, fileListSize);
			sfoFile->SetValue("SAVEDATA_FILE_LIST", updatedList.get(), fileListSize, (int)FILE_LIST_TOTAL_SIZE);

			u8 *sfoData;
			size_t sfoSize;
			sfoFile->WriteSFO(&sfoData, &sfoSize);

			ClearSFOCache();
			WritePSPFile(sfoPath, sfoData, (SceSize)sfoSize);
			delete[] sfoData;
		}
	}

	return 0;
}

int SavedataParam::Save(SceUtilitySavedataParam* param, const std::string &saveDirName, bool secureMode) {
	if (!param) {
		return SCE_UTILITY_SAVEDATA_ERROR_SAVE_MS_NOSPACE;
	}
	if (param->dataSize > param->dataBufSize) {
		ERROR_LOG_REPORT(Log::sceUtility, "Savedata buffer overflow: %d / %d", param->dataSize, param->dataBufSize);
		return SCE_UTILITY_SAVEDATA_ERROR_RW_BAD_PARAMS;
	}
	auto validateSize = [](const PspUtilitySavedataFileData &data) {
		if (data.buf.IsValid() && data.bufSize < data.size) {
			ERROR_LOG_REPORT(Log::sceUtility, "Savedata subdata buffer overflow: %d / %d", data.size, data.bufSize);
			return false;
		}
		return true;
	};
	if (!validateSize(param->icon0FileData) || !validateSize(param->icon1FileData) || !validateSize(param->pic1FileData) || !validateSize(param->snd0FileData)) {
		return SCE_UTILITY_SAVEDATA_ERROR_RW_BAD_PARAMS;
	}

	if (param->secureVersion > 3) {
		ERROR_LOG_REPORT(Log::sceUtility, "Savedata version requested on save: %d", param->secureVersion);
		return SCE_UTILITY_SAVEDATA_ERROR_SAVE_PARAM;
	} else if (param->secureVersion != 0) {
		if (param->secureVersion != 1 && !HasKey(param) && secureMode) {
			ERROR_LOG_REPORT(Log::sceUtility, "Savedata version with missing key on save: %d", param->secureVersion);
			return SCE_UTILITY_SAVEDATA_ERROR_SAVE_PARAM;
		}
		WARN_LOG(Log::sceUtility, "Savedata version requested on save: %d", param->secureVersion);
	}

	std::string dirPath = GetSaveFilePath(param, GetSaveDir(param, saveDirName));

	if (!pspFileSystem.GetFileInfo(dirPath).exists) {
		if (!pspFileSystem.MkDir(dirPath)) {
			auto err = GetI18NCategory(I18NCat::ERRORS);
			g_OSD.Show(OSDType::MESSAGE_ERROR, err->T("Unable to write savedata, disk may be full"));
		}
	}

	u8* cryptedData = 0;
	int cryptedSize = 0;
	u8 cryptedHash[0x10]{};
	// Encrypt save.
	// TODO: Is this the correct difference between MAKEDATA and MAKEDATASECURE?
	if (param->dataBuf.IsValid() && g_Config.bEncryptSave && secureMode)
	{
		cryptedSize = param->dataSize;
		if (cryptedSize == 0 || (SceSize)cryptedSize > param->dataBufSize) {
			ERROR_LOG(Log::sceUtility, "Bad cryptedSize %d", cryptedSize);
			cryptedSize = param->dataBufSize; // fallback, should never use this
		}
		u8 *data_ = param->dataBuf;

		int aligned_len = align16(cryptedSize);
		if (aligned_len != cryptedSize) {
			WARN_LOG(Log::sceUtility, "cryptedSize unaligned: %d (%d)", cryptedSize, cryptedSize & 15);
		}

		cryptedData = new u8[aligned_len + 0x10]();
		memcpy(cryptedData, data_, cryptedSize);
		// EncryptData will do a memmove to make room for the key in front.
		// Technically we could just copy it into place here to avoid that.

		int decryptMode = DetermineCryptMode(param);
		bool hasKey = decryptMode > 1;
		if (hasKey && !HasKey(param)) {
			delete[] cryptedData;
			return SCE_UTILITY_SAVEDATA_ERROR_SAVE_PARAM;
		}

		if (EncryptData(decryptMode, cryptedData, &cryptedSize, &aligned_len, cryptedHash, (hasKey ? param->key : 0)) != 0) {
			auto err = GetI18NCategory(I18NCat::ERRORS);
			g_OSD.Show(OSDType::MESSAGE_WARNING, err->T("Save encryption failed. This save won't work on real PSP"), 6.0f);
			ERROR_LOG(Log::sceUtility,"Save encryption failed. This save won't work on real PSP");
			delete[] cryptedData;
			cryptedData = 0;
		}
	}

	// SAVE PARAM.SFO
	std::string sfopath = dirPath + "/" + SFO_FILENAME;
	std::shared_ptr<ParamSFOData> sfoFile = LoadCachedSFO(sfopath, true);

	// This was added in #18430, see below.
	bool subWrite = param->mode == SCE_UTILITY_SAVEDATA_TYPE_WRITEDATASECURE || param->mode == SCE_UTILITY_SAVEDATA_TYPE_WRITEDATA;
	bool wasCrypted = GetSaveCryptMode(param, saveDirName) != 0;

	// Update values. NOTE! #18430 made this conditional on !subWrite, but this is not correct, as it causes #18687.
	// So now we do a hacky trick and just check for a valid title before we proceed with updating the sfoFile.
	if (strnlen(param->sfoParam.title, sizeof(param->sfoParam.title)) > 0) {
		sfoFile->SetValue("TITLE", param->sfoParam.title, 128);
		sfoFile->SetValue("SAVEDATA_TITLE", param->sfoParam.savedataTitle, 128);
		sfoFile->SetValue("SAVEDATA_DETAIL", param->sfoParam.detail, 1024);
		sfoFile->SetValue("PARENTAL_LEVEL", param->sfoParam.parentalLevel, 4);
		sfoFile->SetValue("CATEGORY", "MS", 4);
		sfoFile->SetValue("SAVEDATA_DIRECTORY", GetSaveDir(param, saveDirName), 64);
	}

	// Always write and update the file list.
	// For each file, 13 bytes for filename, 16 bytes for file hash (0 in PPSSPP), 3 byte for padding
	u32 tmpDataSize = 0;
	SaveSFOFileListEntry *tmpDataOrig = (SaveSFOFileListEntry *)sfoFile->GetValueData("SAVEDATA_FILE_LIST", &tmpDataSize);
	SaveSFOFileListEntry *updatedList = new SaveSFOFileListEntry[FILE_LIST_COUNT_MAX];
	if (tmpDataSize != 0)
		memcpy(updatedList, tmpDataOrig, std::min(tmpDataSize, FILE_LIST_TOTAL_SIZE));
	if (tmpDataSize < FILE_LIST_TOTAL_SIZE)
		memset(updatedList + tmpDataSize, 0, FILE_LIST_TOTAL_SIZE - tmpDataSize);
	// Leave a hash there and unchanged if it was already there.
	if (secureMode && param->dataBuf.IsValid()) {
		const std::string saveFilename = GetFileName(param);
		for (auto entry = updatedList; entry < updatedList + FILE_LIST_COUNT_MAX; ++entry) {
			if (entry->filename[0] != '\0') {
				if (strncmp(entry->filename, saveFilename.c_str(), sizeof(entry->filename)) != 0)
					continue;
			}

			snprintf(entry->filename, sizeof(entry->filename), "%s", saveFilename.c_str());
			memcpy(entry->hash, cryptedHash, 16);
			break;
		}
	}

	sfoFile->SetValue("SAVEDATA_FILE_LIST", (u8 *)updatedList, FILE_LIST_TOTAL_SIZE, (int)FILE_LIST_TOTAL_SIZE);
	delete[] updatedList;

	// Init param with 0. This will be used to detect crypted save or not on loading
	u8 zeroes[128]{};
	sfoFile->SetValue("SAVEDATA_PARAMS", zeroes, 128, 128);

	u8 *sfoData;
	size_t sfoSize;
	sfoFile->WriteSFO(&sfoData, &sfoSize);

	// Calc SFO hash for PSP.
	if (cryptedData != 0 || (subWrite && wasCrypted)) {
		int offset = sfoFile->GetDataOffset(sfoData, "SAVEDATA_PARAMS");
		if (offset >= 0)
			UpdateHash(sfoData, (int)sfoSize, offset, DetermineCryptMode(param));
	}

	ClearSFOCache();
	WritePSPFile(sfopath, sfoData, (SceSize)sfoSize);
	delete[] sfoData;
	sfoData = nullptr;

	if(param->dataBuf.IsValid())	// Can launch save without save data in mode 13
	{
		std::string fileName = GetFileName(param);
		std::string filePath = dirPath + "/" + fileName;
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

		INFO_LOG(Log::sceUtility,"Saving file with size %u in %s",saveSize,filePath.c_str());

		// copy back save name in request
		strncpy(param->saveName, saveDirName.c_str(), 20);

		if (!fileName.empty()) {
			if (!WritePSPFile(filePath, data_, saveSize)) {
				ERROR_LOG(Log::sceUtility, "Error writing file %s", filePath.c_str());
				delete[] cryptedData;
				return SCE_UTILITY_SAVEDATA_ERROR_SAVE_MS_NOSPACE;
			}
		}	
		delete[] cryptedData;
	}

	// SAVE ICON0
	if (param->icon0FileData.buf.IsValid())
	{
		std::string icon0path = dirPath + "/" + ICON0_FILENAME;
		WritePSPFile(icon0path, param->icon0FileData.buf, param->icon0FileData.size);
	}
	// SAVE ICON1
	if (param->icon1FileData.buf.IsValid())
	{
		std::string icon1path = dirPath + "/" + ICON1_FILENAME;
		WritePSPFile(icon1path, param->icon1FileData.buf, param->icon1FileData.size);
	}
	// SAVE PIC1
	if (param->pic1FileData.buf.IsValid())
	{
		std::string pic1path = dirPath + "/" + PIC1_FILENAME;
		WritePSPFile(pic1path, param->pic1FileData.buf, param->pic1FileData.size);
	}
	// Save SND
	if (param->snd0FileData.buf.IsValid())
	{
		std::string snd0path = dirPath + "/" + SND0_FILENAME;
		WritePSPFile(snd0path, param->snd0FileData.buf, param->snd0FileData.size);
	}
	return 0;
}

int SavedataParam::Load(SceUtilitySavedataParam *param, const std::string &saveDirName, int saveId, bool secureMode) {
	if (!param) {
		return SCE_UTILITY_SAVEDATA_ERROR_LOAD_NO_DATA;
	}

	bool isRWMode = param->mode == SCE_UTILITY_SAVEDATA_TYPE_READDATA || param->mode == SCE_UTILITY_SAVEDATA_TYPE_READDATASECURE;

	std::string dirPath = GetSaveFilePath(param, GetSaveDir(param, saveDirName));
	std::string fileName = GetFileName(param);
	std::string filePath = dirPath + "/" + fileName;

	if (!pspFileSystem.GetFileInfo(dirPath).exists) {
		return isRWMode ? SCE_UTILITY_SAVEDATA_ERROR_RW_NO_DATA : SCE_UTILITY_SAVEDATA_ERROR_LOAD_NO_DATA;
	}

	if (!fileName.empty() && !pspFileSystem.GetFileInfo(filePath).exists) {
		return isRWMode ? SCE_UTILITY_SAVEDATA_ERROR_RW_FILE_NOT_FOUND : SCE_UTILITY_SAVEDATA_ERROR_LOAD_FILE_NOT_FOUND;
	}

	// If it wasn't zero, force to zero before loading and especially in case of error.
	// This isn't reset if the path doesn't even exist.
	param->dataSize = 0;
	int result = LoadSaveData(param, saveDirName, dirPath, secureMode);
	if (result != 0)
		return result;

	// Load sfo
	if (!LoadSFO(param, dirPath)) {
		WARN_LOG(Log::sceUtility, "Load: Failed to load SFO from %s", dirPath.c_str());
		return isRWMode ? SCE_UTILITY_SAVEDATA_ERROR_RW_DATA_BROKEN : SCE_UTILITY_SAVEDATA_ERROR_LOAD_DATA_BROKEN;
	}

	// Don't know what it is, but PSP always respond this and this unlock some game
	param->bind = 1021;

	// Load other files, seems these are required by some games, e.g. Fushigi no Dungeon Fuurai no Shiren 4 Plus.

	// Load ICON0.PNG
	LoadFile(dirPath, ICON0_FILENAME, &param->icon0FileData);
	// Load ICON1.PNG
	LoadFile(dirPath, ICON1_FILENAME, &param->icon1FileData);
	// Load PIC1.PNG
	LoadFile(dirPath, PIC1_FILENAME, &param->pic1FileData);
	// Load SND0.AT3
	LoadFile(dirPath, SND0_FILENAME, &param->snd0FileData);

	return 0;
}

int SavedataParam::LoadSaveData(SceUtilitySavedataParam *param, const std::string &saveDirName, const std::string &dirPath, bool secureMode) {
	if (param->secureVersion > 3) {
		ERROR_LOG_REPORT(Log::sceUtility, "Savedata version requested: %d", param->secureVersion);
		return SCE_UTILITY_SAVEDATA_ERROR_LOAD_PARAM;
	} else if (param->secureVersion != 0) {
		if (param->secureVersion != 1 && !HasKey(param) && secureMode) {
			ERROR_LOG_REPORT(Log::sceUtility, "Savedata version with missing key: %d", param->secureVersion);
			return SCE_UTILITY_SAVEDATA_ERROR_LOAD_PARAM;
		}
		WARN_LOG_REPORT(Log::sceUtility, "Savedata version requested: %d", param->secureVersion);
	}

	std::string filename = GetFileName(param);
	std::string filePath = dirPath + "/" + filename;
	// Blank filename always means success, if secureVersion was correct.
	if (filename.empty())
		return 0;

	s64 readSize;
	INFO_LOG(Log::sceUtility, "Loading file with size %u in %s", param->dataBufSize, filePath.c_str());
	u8 *saveData = nullptr;
	int saveSize = -1;
	if (!ReadPSPFile(filePath, &saveData, saveSize, &readSize)) {
		ERROR_LOG(Log::sceUtility,"Error reading file %s",filePath.c_str());
		return SCE_UTILITY_SAVEDATA_ERROR_LOAD_NO_DATA;
	}
	saveSize = (int)readSize;

	// copy back save name in request
	strncpy(param->saveName, saveDirName.c_str(), 20);

	int prevCryptMode = GetSaveCryptMode(param, saveDirName);
	bool isCrypted = prevCryptMode != 0 && secureMode;
	bool saveDone = false;
	u32 loadedSize = 0;
	if (isCrypted) {
		if (DetermineCryptMode(param) > 1 && !HasKey(param)) {
			return SCE_UTILITY_SAVEDATA_ERROR_LOAD_PARAM;
		}
		u8 hash[16];
		bool hasExpectedHash = GetExpectedHash(dirPath, filename, hash);
		loadedSize = LoadCryptedSave(param, param->dataBuf, saveData, saveSize, prevCryptMode, hasExpectedHash ? hash : nullptr, saveDone);
		// TODO: Should return SCE_UTILITY_SAVEDATA_ERROR_LOAD_DATA_BROKEN here if !saveDone.
	}
	if (!saveDone) {
		loadedSize = LoadNotCryptedSave(param, param->dataBuf, saveData, saveSize);
	}
	delete[] saveData;

	// Ignore error codes.
	if (loadedSize != 0 && (loadedSize & 0x80000000) == 0) {
		std::string tag = "LoadSaveData/" + filePath;
		NotifyMemInfo(MemBlockFlags::WRITE, param->dataBuf.ptr, loadedSize, tag.c_str(), tag.size());
	}

	if ((loadedSize & 0x80000000) != 0)
		return loadedSize;

	param->dataSize = (SceSize)saveSize;
	return 0;
}

int SavedataParam::DetermineCryptMode(const SceUtilitySavedataParam *param) const {
	int decryptMode = 1;
	if (param->secureVersion == 1) {
		decryptMode = 1;
	} else if (param->secureVersion == 2) {
		decryptMode = 3;
	} else if (param->secureVersion == 3) {
		decryptMode = GetSDKMainVersion(sceKernelGetCompiledSdkVersion()) >= 4 ? 5 : 1;
	} else if (HasKey(param)) {
		// TODO: This should ignore HasKey(), which would trigger errors.  Not doing that yet to play it safe.
		decryptMode = GetSDKMainVersion(sceKernelGetCompiledSdkVersion()) >= 4 ? 5 : 3;
	}
	return decryptMode;
}

u32 SavedataParam::LoadCryptedSave(SceUtilitySavedataParam *param, u8 *data, const u8 *saveData, int &saveSize, int prevCryptMode, const u8 *expectedHash, bool &saveDone) {
	int orig_size = saveSize;
	int align_len = align16(saveSize);
	u8 *data_base = new u8[align_len];
	u8 *cryptKey = new u8[0x10];

	int decryptMode = DetermineCryptMode(param);
	const int detectedMode = decryptMode;
	bool hasKey;

	auto resetData = [&](int mode) {
		saveSize = orig_size;
		align_len = align16(saveSize);
		hasKey = mode > 1;

		if (hasKey) {
			memcpy(cryptKey, param->key, 0x10);
		}
		memcpy(data_base, saveData, saveSize);
		memset(data_base + saveSize, 0, align_len - saveSize);
	};
	resetData(decryptMode);

	if (decryptMode != prevCryptMode) {
		if (prevCryptMode == 1 && param->key[0] == 0) {
			// Backwards compat for a bug we used to have.
			WARN_LOG(Log::sceUtility, "Savedata loading with hashmode %d instead of detected %d", prevCryptMode, decryptMode);
			decryptMode = prevCryptMode;

			// Don't notify the user if we're not going to upgrade the save.
			if (!g_Config.bEncryptSave) {
				auto di = GetI18NCategory(I18NCat::DIALOG);
				g_OSD.Show(OSDType::MESSAGE_WARNING, di->T("When you save, it will load on a PSP, but not an older PPSSPP"), 6.0f);
				g_OSD.Show(OSDType::MESSAGE_WARNING, di->T("Old savedata detected"), 6.0f);
			}
		} else {
			if (decryptMode == 5 && prevCryptMode == 3) {
				WARN_LOG(Log::sceUtility, "Savedata loading with detected hashmode %d instead of file's %d", decryptMode, prevCryptMode);
			} else {
				WARN_LOG_REPORT(Log::sceUtility, "Savedata loading with detected hashmode %d instead of file's %d", decryptMode, prevCryptMode);
			}
			if (g_Config.bSavedataUpgrade) {
				decryptMode = prevCryptMode;
				auto di = GetI18NCategory(I18NCat::DIALOG);
				g_OSD.Show(OSDType::MESSAGE_WARNING, di->T("When you save, it will not work on outdated PSP Firmware anymore"), 6.0f);
				g_OSD.Show(OSDType::MESSAGE_WARNING, di->T("Old savedata detected"), 6.0f);
			}
		}
		hasKey = decryptMode > 1;
	}

	int err = DecryptData(decryptMode, data_base, &saveSize, &align_len, hasKey ? cryptKey : nullptr, expectedHash);
	// Perhaps the file had the wrong mode....
	if (err != 0 && detectedMode != decryptMode) {
		resetData(detectedMode);
		err = DecryptData(detectedMode, data_base, &saveSize, &align_len, hasKey ? cryptKey : nullptr, expectedHash);
	}
	// TODO: Should return an error, but let's just try with a bad hash.
	if (err != 0 && expectedHash != nullptr) {
		WARN_LOG(Log::sceUtility, "Incorrect hash on save data, likely corrupt");
		resetData(decryptMode);
		err = DecryptData(decryptMode, data_base, &saveSize, &align_len, hasKey ? cryptKey : nullptr, nullptr);
	}

	u32 sz = 0;
	if (err == 0) {
		if (param->dataBuf.IsValid()) {
			if ((u32)saveSize > param->dataBufSize || !Memory::IsValidRange(param->dataBuf.ptr, saveSize)) {
				sz = SCE_UTILITY_SAVEDATA_ERROR_LOAD_DATA_BROKEN;
			} else {
				sz = (u32)saveSize;
				memcpy(data, data_base, sz);
			}
		}
		saveDone = true;
	}
	delete[] data_base;
	delete[] cryptKey;

	return sz;
}

u32 SavedataParam::LoadNotCryptedSave(SceUtilitySavedataParam *param, u8 *data, u8 *saveData, int &saveSize) {
	if (param->dataBuf.IsValid()) {
		if ((u32)saveSize > param->dataBufSize || !Memory::IsValidRange(param->dataBuf.ptr, saveSize)) {
			return SCE_UTILITY_SAVEDATA_ERROR_LOAD_DATA_BROKEN;
		}
		memcpy(data, saveData, saveSize);
		return saveSize;
	}
	return 0;
}

bool SavedataParam::LoadSFO(SceUtilitySavedataParam *param, const std::string& dirPath) {
	std::string sfopath = dirPath + "/" + SFO_FILENAME;
	std::shared_ptr<ParamSFOData> sfoFile = LoadCachedSFO(sfopath);
	if (sfoFile) {
		// copy back info in request
		strncpy(param->sfoParam.title, sfoFile->GetValueString("TITLE").c_str(), 128);
		strncpy(param->sfoParam.savedataTitle, sfoFile->GetValueString("SAVEDATA_TITLE").c_str(), 128);
		strncpy(param->sfoParam.detail, sfoFile->GetValueString("SAVEDATA_DETAIL").c_str(), 1024);
		param->sfoParam.parentalLevel = sfoFile->GetValueInt("PARENTAL_LEVEL");
		return true;
	}
	return false;
}

std::vector<SaveSFOFileListEntry> SavedataParam::GetSFOEntries(const std::string &dirPath) {
	std::vector<SaveSFOFileListEntry> result;
	const std::string sfoPath = dirPath + "/" + SFO_FILENAME;

	std::shared_ptr<ParamSFOData> sfoFile = LoadCachedSFO(sfoPath);
	if (!sfoFile) {
		return result;
	}

	u32 sfoFileListSize = 0;
	SaveSFOFileListEntry *sfoFileList = (SaveSFOFileListEntry *)sfoFile->GetValueData("SAVEDATA_FILE_LIST", &sfoFileListSize);
	const u32 count = std::min((u32)FILE_LIST_COUNT_MAX, sfoFileListSize / (u32)sizeof(SaveSFOFileListEntry));

	for (u32 i = 0; i < count; ++i) {
		if (sfoFileList[i].filename[0] != '\0')
			result.push_back(sfoFileList[i]);
	}

	return result;
}

std::set<std::string> SavedataParam::GetSecureFileNames(const std::string &dirPath) {
	auto entries = GetSFOEntries(dirPath);

	std::set<std::string> secureFileNames;
	for (const auto &entry : entries) {
		char temp[14];
		truncate_cpy(temp, entry.filename);
		secureFileNames.insert(temp);
	}
	return secureFileNames;
}

bool SavedataParam::GetExpectedHash(const std::string &dirPath, const std::string &filename, u8 hash[16]) {
	auto entries = GetSFOEntries(dirPath);

	for (auto entry : entries) {
		if (strncmp(entry.filename, filename.c_str(), sizeof(entry.filename)) == 0) {
			memcpy(hash, entry.hash, sizeof(entry.hash));
			return true;
		}
	}
	return false;
}

void SavedataParam::LoadFile(const std::string& dirPath, const std::string& filename, PspUtilitySavedataFileData *fileData) {
	std::string filePath = dirPath + "/" + filename;
	if (!fileData->buf.IsValid())
		return;

	u8 *buf = fileData->buf;
	u32 size = Memory::ValidSize(fileData->buf.ptr, fileData->bufSize);
	s64 readSize = -1;
	if (ReadPSPFile(filePath, &buf, size, &readSize)) {
		fileData->size = readSize;
		const std::string tag = "SavedataLoad/" + filePath;
		NotifyMemInfo(MemBlockFlags::WRITE, fileData->buf.ptr, fileData->size, tag.c_str(), tag.size());
		INFO_LOG(Log::sceUtility, "Loaded subfile %s (size: %d bytes) into %08x", filePath.c_str(), fileData->size, fileData->buf.ptr);
	} else {
		WARN_LOG(Log::sceUtility, "Failed to load subfile %s into %08x", filePath.c_str(), fileData->buf.ptr);
	}
}

// Note: The work is done in-place, hence the memmove etc.
int SavedataParam::EncryptData(unsigned int mode,
		 unsigned char *data,
		 int *dataLen,
		 int *alignedLen,
		 unsigned char *hash,
		 unsigned char *cryptkey)
{
	pspChnnlsvContext1 ctx1{};
	pspChnnlsvContext2 ctx2{};

	INFO_LOG(Log::sceUtility, "EncryptData(mode=%d, *dataLen=%d, *alignedLen=%d)", mode, *dataLen, *alignedLen);

	/* Make room for the IV in front of the data. */
	memmove(data + 0x10, data, *alignedLen);

	/* Set up buffers */
	memset(hash, 0, 0x10);

	// Zero out the IV before we begin.
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
	if (sceSdCleanList_(ctx2) < 0)
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

// Note: The work is done in-place, hence the memmove etc.
int SavedataParam::DecryptData(unsigned int mode, unsigned char *data, int *dataLen, int *alignedLen, unsigned char *cryptkey, const u8 *expectedHash) {
	pspChnnlsvContext1 ctx1{};
	pspChnnlsvContext2 ctx2{};

	/* Need a 16-byte IV plus some data */
	if (*alignedLen <= 0x10)
		return -1;
	*dataLen -= 0x10;
	*alignedLen -= 0x10;

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
	if (sceSdCleanList_(ctx2) < 0)
		return -7;

	if (expectedHash) {
		u8 hash[16];
		if (sceSdGetLastIndex_(ctx1, hash, cryptkey) < 0)
			return -7;
		if (memcmp(hash, expectedHash, sizeof(hash)) != 0)
			return -8;
	}

	/* The decrypted data starts at data + 0x10, so shift it back. */
	memmove(data, data + 0x10, *dataLen);
	return 0;
}

// Requires sfoData to be padded with zeroes to the next 16-byte boundary (due to BuildHash)
int SavedataParam::UpdateHash(u8* sfoData, int sfoSize, int sfoDataParamsOffset, int encryptmode)
{
	int alignedLen = align16(sfoSize);
	memset(sfoData + sfoDataParamsOffset, 0, 128);
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
	memcpy(sfoData + sfoDataParamsOffset + 0x20, filehash, 0x10);
	*(sfoData + sfoDataParamsOffset) |= 0x01;

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

// Requires sfoData to be padded with zeroes to the next 16-byte boundary.
int SavedataParam::BuildHash(uint8_t *output,
		const uint8_t *data,
		unsigned int len,
		unsigned int alignedLen,
		int mode,
		const uint8_t *cryptkey) {
	pspChnnlsvContext1 ctx1;

	/* Set up buffers */
	memset(&ctx1, 0, sizeof(pspChnnlsvContext1));
	memset(output, 0, 0x10);

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

// TODO: Merge with NiceSizeFormat? That one has a decimal though.
std::string SavedataParam::GetSpaceText(u64 size, bool roundUp)
{
	char text[50];
	static const char * const suffixes[] = {"B", "KB", "MB", "GB"};
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

inline std::string FmtPspTime(const ScePspDateTime &dt) {
	return StringFromFormat("%04d-%02d-%02d %02d:%02d:%02d.%06d", dt.year, dt.month, dt.day, dt.hour, dt.minute, dt.second, dt.microsecond);
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
		NotifyMemInfo(MemBlockFlags::WRITE, param->msFree.ptr, sizeof(SceUtilitySavedataMsFreeInfo), "SavedataGetSizes");
	}
	if (param->msData.IsValid())
	{
		const SceUtilitySavedataMsDataInfo *msData = param->msData;
		const std::string gameName(msData->gameName, strnlen(msData->gameName, sizeof(msData->gameName)));
		const std::string saveName(msData->saveName, strnlen(msData->saveName, sizeof(msData->saveName)));
		// TODO: How should <> be handled?
		std::string path = GetSaveFilePath(param, gameName + (saveName == "<>" ? "" : saveName));
		bool listingExists = false;
		auto listing = pspFileSystem.GetDirListing(path, &listingExists);
		if (listingExists) {
			param->msData->info.usedClusters = 0;
			for (auto &item : listing) {
				param->msData->info.usedClusters += (item.size + (u32)MemoryStick_SectorSize() - 1) / (u32)MemoryStick_SectorSize();
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
		NotifyMemInfo(MemBlockFlags::WRITE, param->msData.ptr, sizeof(SceUtilitySavedataMsDataInfo), "SavedataGetSizes");
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
		std::string spaceTxt32 = SavedataParam::GetSpaceText(total_size, true);
		memset(param->utilityData->usedSpace32Str, 0, sizeof(param->utilityData->usedSpace32Str));
		strncpy(param->utilityData->usedSpace32Str, spaceTxt32.c_str(), sizeof(param->utilityData->usedSpace32Str));

		INFO_LOG(Log::sceUtility, "GetSize: usedSpaceKB: %d (str: %s) (clusters: %d)", param->utilityData->usedSpaceKB, spaceTxt.c_str(), param->utilityData->usedClusters);
		INFO_LOG(Log::sceUtility, "GetSize: usedSpace32KB: %d (str32: %s)", param->utilityData->usedSpace32KB, spaceTxt32.c_str());

		NotifyMemInfo(MemBlockFlags::WRITE, param->utilityData.ptr, sizeof(SceUtilitySavedataUsedDataInfo), "SavedataGetSizes");
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
		u32 maxFileCount = param->idList->maxCount;

		std::vector<PSPFileInfo> validDir;
		std::vector<PSPFileInfo> sfoFiles;
		std::vector<PSPFileInfo> allDir = pspFileSystem.GetDirListing(savePath);

		std::string searchString = GetGameName(param) + GetSaveName(param);
		for (size_t i = 0; i < allDir.size() && validDir.size() < maxFileCount; i++) {
			std::string dirName = allDir[i].name;
			if (PSPMatch(dirName, searchString)) {
				validDir.push_back(allDir[i]);
			}
		}

		PSPFileInfo sfoFile;
		for (size_t i = 0; i < validDir.size(); ++i) {
			// GetFileName(param) == null here
			// so use sfo files to set the date.
			sfoFile = pspFileSystem.GetFileInfo(savePath + validDir[i].name + "/" + SFO_FILENAME);
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
		// Save num of folder found
		param->idList->resultCount = (u32)validDir.size();
		// Log out the listing.
		if (GenericLogEnabled(LogLevel::LINFO, Log::sceUtility)) {
			INFO_LOG(Log::sceUtility, "LIST (searchstring=%s): %d files (max: %d)", searchString.c_str(), param->idList->resultCount, maxFileCount);
			for (int i = 0; i < validDir.size(); i++) {
				INFO_LOG(Log::sceUtility, "%s: mode %08x, ctime: %s, atime: %s, mtime: %s",
					entries[i].name, entries[i].st_mode, FmtPspTime(entries[i].st_ctime).c_str(), FmtPspTime(entries[i].st_atime).c_str(), FmtPspTime(entries[i].st_mtime).c_str());
			}
		}
		NotifyMemInfo(MemBlockFlags::WRITE, param->idList.ptr, sizeof(SceUtilitySavedataIdListInfo), "SavedataGetList");
		NotifyMemInfo(MemBlockFlags::WRITE, param->idList->entries.ptr, (uint32_t)validDir.size() * sizeof(SceUtilitySavedataIdListEntry), "SavedataGetList");
	}
	return true;
}

int SavedataParam::GetFilesList(SceUtilitySavedataParam *param, u32 requestAddr) {
	if (!param)	{
		return SCE_UTILITY_SAVEDATA_ERROR_RW_BAD_STATUS;
	}

	if (!param->fileList.IsValid()) {
		ERROR_LOG_REPORT(Log::sceUtility, "SavedataParam::GetFilesList(): bad fileList address %08x", param->fileList.ptr);
		// Should crash.
		return -1;
	}

	auto &fileList = param->fileList;
	if (fileList->secureEntries.IsValid() && fileList->maxSecureEntries > 99) {
		ERROR_LOG_REPORT(Log::sceUtility, "SavedataParam::GetFilesList(): too many secure entries, %d", fileList->maxSecureEntries);
		return SCE_UTILITY_SAVEDATA_ERROR_RW_BAD_PARAMS;
	}
	if (fileList->normalEntries.IsValid() && fileList->maxNormalEntries > 8192) {
		ERROR_LOG_REPORT(Log::sceUtility, "SavedataParam::GetFilesList(): too many normal entries, %d", fileList->maxNormalEntries);
		return SCE_UTILITY_SAVEDATA_ERROR_RW_BAD_PARAMS;
	}
	if (sceKernelGetCompiledSdkVersion() >= 0x02060000) {
		if (fileList->systemEntries.IsValid() && fileList->maxSystemEntries > 5) {
			ERROR_LOG_REPORT(Log::sceUtility, "SavedataParam::GetFilesList(): too many system entries, %d", fileList->maxSystemEntries);
			return SCE_UTILITY_SAVEDATA_ERROR_RW_BAD_PARAMS;
		}
	}

	std::string dirPath = savePath + GetGameName(param) + GetSaveName(param);
	bool dirPathExists = false;
	auto files = pspFileSystem.GetDirListing(dirPath, &dirPathExists);
	if (!dirPathExists) {
		DEBUG_LOG(Log::sceUtility, "SavedataParam::GetFilesList(): directory %s does not exist", dirPath.c_str());
		return SCE_UTILITY_SAVEDATA_ERROR_RW_NO_DATA;
	}

	// Even if there are no files, initialize to 0.
	fileList->resultNumSecureEntries = 0;
	fileList->resultNumNormalEntries = 0;
	fileList->resultNumSystemEntries = 0;

	// We need PARAM.SFO's SAVEDATA_FILE_LIST to determine which entries are secure.
	PSPFileInfo sfoFileInfo = FileFromListing(files, SFO_FILENAME);
	std::set<std::string> secureFilenames;

	if (sfoFileInfo.exists) {
		secureFilenames = GetSecureFileNames(dirPath);
	} else {
		return SCE_UTILITY_SAVEDATA_ERROR_RW_DATA_BROKEN;
	}

	// TODO: Does this always happen?
	// Don't know what it is, but PSP always respond this.
	param->bind = 1021;
	// This should be set around the same time as the file data.  This runs on a thread, so set immediately.
	auto requestPtr = PSPPointer<SceUtilitySavedataParam>::Create(requestAddr);
	requestPtr->bind = 1021;

	// Does not list directories, nor recurse into them, and ignores files not ALL UPPERCASE.
	bool isCrypted = GetSaveCryptMode(param, GetSaveDirName(param, 0)) != 0;
	for (auto file = files.begin(), end = files.end(); file != end; ++file) {
		if (file->type == FILETYPE_DIRECTORY) {
			continue;
		}
		// TODO: What are the exact rules?  It definitely skips lowercase, and allows FILE or FILE.EXT.
		if (file->name.find_first_of("abcdefghijklmnopqrstuvwxyz") != file->name.npos) {
			DEBUG_LOG(Log::sceUtility, "SavedataParam::GetFilesList(): skipping file %s with lowercase", file->name.c_str());
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

	if (GenericLogEnabled(LogLevel::LINFO, Log::sceUtility)) {
		INFO_LOG(Log::sceUtility, "FILES: %d files listed", fileList->resultNumNormalEntries);
		for (int i = 0; i < (int)fileList->resultNumNormalEntries; i++) {
			const SceUtilitySavedataFileListEntry &info = fileList->systemEntries[i];
			INFO_LOG(Log::sceUtility, "%s: mode %08x, ctime: %s, atime: %s, mtime: %s",
				info.name, info.st_mode, FmtPspTime(info.st_ctime).c_str(), FmtPspTime(info.st_atime).c_str(), FmtPspTime(info.st_mtime).c_str());
		}
	}

	NotifyMemInfo(MemBlockFlags::WRITE, fileList.ptr, sizeof(SceUtilitySavedataFileListInfo), "SavedataGetFilesList");
	if (fileList->resultNumSystemEntries != 0)
		NotifyMemInfo(MemBlockFlags::WRITE, fileList->systemEntries.ptr, fileList->resultNumSystemEntries * sizeof(SceUtilitySavedataFileListEntry), "SavedataGetFilesList");
	if (fileList->resultNumSecureEntries != 0)
		NotifyMemInfo(MemBlockFlags::WRITE, fileList->secureEntries.ptr, fileList->resultNumSecureEntries * sizeof(SceUtilitySavedataFileListEntry), "SavedataGetFilesList");
	if (fileList->resultNumNormalEntries != 0)
		NotifyMemInfo(MemBlockFlags::WRITE, fileList->normalEntries.ptr, fileList->resultNumNormalEntries * sizeof(SceUtilitySavedataFileListEntry), "SavedataGetFilesList");

	return 0;
}

bool SavedataParam::GetSize(SceUtilitySavedataParam *param) {
	if (!param) {
		return false;
	}

	const std::string saveDir = savePath + GetGameName(param) + GetSaveName(param);
	bool exists = false;

	if (param->sizeInfo.IsValid()) {
		auto listing = pspFileSystem.GetDirListing(saveDir, &exists);
		const u64 freeBytes = MemoryStick_FreeSpace();

		s64 overwriteBytes = 0;
		s64 writeBytes = 0;
		for (int i = 0; i < param->sizeInfo->numNormalEntries; ++i) {
			const auto &entry = param->sizeInfo->normalEntries[i];
			overwriteBytes += FileFromListing(listing, entry.name).size;
			writeBytes += entry.size;
		}
		for (int i = 0; i < param->sizeInfo->numSecureEntries; ++i) {
			const auto &entry = param->sizeInfo->secureEntries[i];
			overwriteBytes += FileFromListing(listing, entry.name).size;
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
			truncate_cpy(param->sizeInfo->neededString, spaceTxt);
			truncate_cpy(param->sizeInfo->overwriteString, spaceTxt);
		} else {
			// Bytes needed to save additional data.
			s64 neededBytes = writeBytes - freeBytes;
			param->sizeInfo->neededKB = (neededBytes + 1023) / 1024;
			spaceTxt = GetSpaceText(neededBytes, true);
			truncate_cpy(param->sizeInfo->neededString, spaceTxt);

			if (writeBytes - overwriteBytes < (s64)freeBytes) {
				param->sizeInfo->overwriteKB = 0;
				spaceTxt = GetSpaceText(0, true);
				truncate_cpy(param->sizeInfo->overwriteString, spaceTxt);
			} else {
				s64 neededOverwriteBytes = writeBytes - freeBytes - overwriteBytes;
				param->sizeInfo->overwriteKB = (neededOverwriteBytes + 1023) / 1024;
				spaceTxt = GetSpaceText(neededOverwriteBytes, true);
				truncate_cpy(param->sizeInfo->overwriteString, spaceTxt);
			}
		}

		INFO_LOG(Log::sceUtility, "SectorSize: %d FreeSectors: %d FreeKB: %d neededKb: %d overwriteKb: %d",
			param->sizeInfo->sectorSize, param->sizeInfo->freeSectors, param->sizeInfo->freeKB, param->sizeInfo->neededKB, param->sizeInfo->overwriteKB);

		NotifyMemInfo(MemBlockFlags::WRITE, param->sizeInfo.ptr, sizeof(PspUtilitySavedataSizeInfo), "SavedataGetSize");
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
		saveDataList = NULL;
		saveDataListCount = 0;
	}
	if (noSaveIcon)
	{
		delete noSaveIcon->texture;
		noSaveIcon->texture = NULL;
		delete noSaveIcon;
		noSaveIcon = NULL;
	}
}

int SavedataParam::SetPspParam(SceUtilitySavedataParam *param)
{
	pspParam = param;
	if (!pspParam) {
		Clear();
		return 0;
	}

	if (param->mode == SCE_UTILITY_SAVEDATA_TYPE_LISTALLDELETE) {
		Clear();
		int realCount = 0;
		auto allSaves = pspFileSystem.GetDirListing(savePath);
		saveDataListCount = (int)allSaves.size();
		saveDataList = new SaveFileInfo[saveDataListCount];
		for (auto save : allSaves) {
			if (save.type != FILETYPE_DIRECTORY || save.name == "." || save.name == "..")
				continue;
			std::string fileDataDir = savePath + save.name;
			PSPFileInfo info = GetSaveInfo(fileDataDir);
			SetFileInfo(realCount, info, "", save.name);
			realCount++;
		}
		saveNameListDataCount = realCount;
		return 0;
	}

	bool listEmptyFile = true;
	if (param->mode == SCE_UTILITY_SAVEDATA_TYPE_LISTLOAD || param->mode == SCE_UTILITY_SAVEDATA_TYPE_LISTDELETE) {
		listEmptyFile = false;
	}

	SceUtilitySavedataSaveName *saveNameListData;
	bool hasMultipleFileName = false;
	if (param->saveNameList.IsValid()) {
		Clear();

		saveNameListData = param->saveNameList;

		// Get number of fileName in array
		saveDataListCount = 0;
		while (saveNameListData[saveDataListCount][0] != 0) {
			saveDataListCount++;
		}

		if (saveDataListCount > 0 && WouldHaveMultiSaveName(param)) {
			hasMultipleFileName = true;
			saveDataList = new SaveFileInfo[saveDataListCount];
			
			// get and stock file info for each file
			int realCount = 0;
			for (int i = 0; i < saveDataListCount; i++) {
				// "<>" means saveName can be anything...
				if (strncmp(saveNameListData[i], "<>", ARRAY_SIZE(saveNameListData[i])) == 0) {
					// TODO:Maybe we need a way to reorder the files?
					auto allSaves = pspFileSystem.GetDirListing(savePath);
					std::string gameName = GetGameName(param);
					for (auto it = allSaves.begin(); it != allSaves.end(); ++it) {
						if (it->name.compare(0, gameName.length(), gameName) == 0) {
							std::string saveName = it->name.substr(gameName.length());
							
							if (IsInSaveDataList(saveName, realCount)) // Already in SaveDataList, skip...
								continue;

							std::string fileDataPath = savePath + it->name;
							if (it->exists) {
								SetFileInfo(realCount, *it, saveName);
								DEBUG_LOG(Log::sceUtility, "%s Exist", fileDataPath.c_str());
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

				std::string fileDataDir = savePath + GetGameName(param) + thisSaveName;
				PSPFileInfo info = GetSaveInfo(fileDataDir);
				if (info.exists) {
					SetFileInfo(realCount, info, thisSaveName);
					INFO_LOG(Log::sceUtility, "Save data exists: %s = %s", thisSaveName.c_str(), fileDataDir.c_str());
					realCount++;
				} else {
					if (listEmptyFile) {
						ClearFileInfo(saveDataList[realCount], thisSaveName);
						INFO_LOG(Log::sceUtility, "Listing missing save data: %s = %s", thisSaveName.c_str(), fileDataDir.c_str());
						realCount++;
					} else {
						INFO_LOG(Log::sceUtility, "Save data not found: %s = %s", thisSaveName.c_str(), fileDataDir.c_str());
					}
				}
			}
			saveNameListDataCount = realCount;
		}
	}
	// Load info on only save 
	if (!hasMultipleFileName) {
		saveNameListData = 0;

		Clear();
		saveDataList = new SaveFileInfo[1];
		saveDataListCount = 1;

		// get and stock file info for each file
		std::string fileDataDir = savePath + GetGameName(param) + GetSaveName(param);
		PSPFileInfo info = GetSaveInfo(fileDataDir);
		if (info.exists) {
			SetFileInfo(0, info, GetSaveName(param));
			INFO_LOG(Log::sceUtility, "Save data exists: %s = %s", GetSaveName(param).c_str(), fileDataDir.c_str());
			saveNameListDataCount = 1;
		} else {
			if (listEmptyFile) {
				ClearFileInfo(saveDataList[0], GetSaveName(param));
				INFO_LOG(Log::sceUtility, "Listing missing save data: %s = %s", GetSaveName(param).c_str(), fileDataDir.c_str());
			} else {
				INFO_LOG(Log::sceUtility, "Save data not found: %s = %s", GetSaveName(param).c_str(), fileDataDir.c_str());
			}
			saveNameListDataCount = 0;
			return 0;
		}
	}
	return 0;
}

void SavedataParam::SetFileInfo(SaveFileInfo &saveInfo, PSPFileInfo &info, const std::string &saveName, const std::string &savrDir)
{
	saveInfo.size = info.size;
	saveInfo.saveName = saveName;
	saveInfo.idx = 0;
	saveInfo.modif_time = info.mtime;

	std::string saveDir = savrDir.empty() ? GetGameName(pspParam) + saveName : savrDir;
	saveInfo.saveDir = saveDir;

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
	if (!ignoreTextures_) {
		saveInfo.texture = new PPGeImage(savePath + saveDir + "/" + ICON0_FILENAME);
	}

	// Load info in PARAM.SFO
	std::string sfoFilename = savePath + saveDir + "/" + SFO_FILENAME;
	std::shared_ptr<ParamSFOData> sfoFile = LoadCachedSFO(sfoFilename);
	if (sfoFile) {
		SetStringFromSFO(*sfoFile, "TITLE", saveInfo.title, sizeof(saveInfo.title));
		SetStringFromSFO(*sfoFile, "SAVEDATA_TITLE", saveInfo.saveTitle, sizeof(saveInfo.saveTitle));
		SetStringFromSFO(*sfoFile, "SAVEDATA_DETAIL", saveInfo.saveDetail, sizeof(saveInfo.saveDetail));
	} else {
		saveInfo.broken = true;
		truncate_cpy(saveInfo.title, saveDir);
	}
}

void SavedataParam::SetFileInfo(int idx, PSPFileInfo &info, const std::string &saveName, const std::string &saveDir)
{
	SetFileInfo(saveDataList[idx], info, saveName, saveDir);
	saveDataList[idx].idx = idx;
}

void SavedataParam::ClearFileInfo(SaveFileInfo &saveInfo, const std::string &saveName) {
	saveInfo.size = 0;
	saveInfo.saveName = saveName;
	saveInfo.idx = 0;
	saveInfo.broken = false;
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

PSPFileInfo SavedataParam::GetSaveInfo(const std::string &saveDir) {
	PSPFileInfo info = pspFileSystem.GetFileInfo(saveDir);
	if (info.exists) {
		info.access = 0777;
		auto allFiles = pspFileSystem.GetDirListing(saveDir);
		bool firstFile = true;
		for (auto file : allFiles) {
			if (file.type == FILETYPE_DIRECTORY || file.name == "." || file.name == "..")
				continue;
			// Use a file to determine save date.
			if (firstFile) {
				info.ctime = file.ctime;
				info.mtime = file.mtime;
				info.atime = file.atime;
				info.size += file.size;
				firstFile = false;
			} else {
				info.size += file.size;
			}
		}
	}
	return info;
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

std::string SavedataParam::GetSaveDir(int idx) const {
	return saveDataList[idx].saveDir;
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

int SavedataParam::GetSaveNameIndex(const SceUtilitySavedataParam *param) {
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

bool SavedataParam::WouldHaveMultiSaveName(const SceUtilitySavedataParam *param) {
	switch ((SceUtilitySavedataType)(u32)param->mode) {
	case SCE_UTILITY_SAVEDATA_TYPE_LOAD:
	case SCE_UTILITY_SAVEDATA_TYPE_AUTOLOAD:
	case SCE_UTILITY_SAVEDATA_TYPE_SAVE:
	case SCE_UTILITY_SAVEDATA_TYPE_AUTOSAVE:
	case SCE_UTILITY_SAVEDATA_TYPE_MAKEDATASECURE:
	case SCE_UTILITY_SAVEDATA_TYPE_MAKEDATA:
	case SCE_UTILITY_SAVEDATA_TYPE_READDATASECURE:
	case SCE_UTILITY_SAVEDATA_TYPE_READDATA:
	case SCE_UTILITY_SAVEDATA_TYPE_WRITEDATASECURE:
	case SCE_UTILITY_SAVEDATA_TYPE_WRITEDATA:
	case SCE_UTILITY_SAVEDATA_TYPE_AUTODELETE:
	case SCE_UTILITY_SAVEDATA_TYPE_DELETE:
	case SCE_UTILITY_SAVEDATA_TYPE_ERASESECURE:
	case SCE_UTILITY_SAVEDATA_TYPE_ERASE:
	case SCE_UTILITY_SAVEDATA_TYPE_DELETEDATA:
		return false;
	default:
		return true;
	}
}

void SavedataParam::DoState(PointerWrap &p) {
	auto s = p.Section("SavedataParam", 1, 2);
	if (!s)
		return;

	// pspParam is handled in PSPSaveDialog.
	Do(p, selectedSave);
	Do(p, saveDataListCount);
	Do(p, saveNameListDataCount);
	if (p.mode == p.MODE_READ) {
		delete [] saveDataList;
		if (saveDataListCount != 0) {
			saveDataList = new SaveFileInfo[saveDataListCount];
			DoArray(p, saveDataList, saveDataListCount);
		} else {
			saveDataList = nullptr;
		}
	}
	else
		DoArray(p, saveDataList, saveDataListCount);

	if (s >= 2) {
		Do(p, ignoreTextures_);
	} else {
		ignoreTextures_ = false;
	}
}

void SavedataParam::ClearSFOCache() {
	std::lock_guard<std::mutex> guard(cacheLock_);
	sfoCache_.clear();
}

std::shared_ptr<ParamSFOData> SavedataParam::LoadCachedSFO(const std::string &path, bool orCreate) {
	std::lock_guard<std::mutex> guard(cacheLock_);
	if (sfoCache_.find(path) == sfoCache_.end()) {
		std::vector<u8> data;
		if (pspFileSystem.ReadEntireFile(path, data, true) < 0) {
			// Mark as not existing for later.
			sfoCache_[path].reset();
		} else {
			sfoCache_.emplace(path, new ParamSFOData());
			// If it fails to load, also keep it to indicate failed.
			if (!sfoCache_.at(path)->ReadSFO(data))
				sfoCache_.at(path).reset();
		}
	}

	if (!sfoCache_.at(path)) {
		if (!orCreate)
			return nullptr;
		sfoCache_.at(path).reset(new ParamSFOData());
	}
	return sfoCache_.at(path);
}

int SavedataParam::GetSaveCryptMode(const SceUtilitySavedataParam *param, const std::string &saveDirName) {
	std::string dirPath = GetSaveFilePath(param, GetSaveDir(param, saveDirName));
	std::string sfopath = dirPath + "/" + SFO_FILENAME;
	std::shared_ptr<ParamSFOData> sfoFile = LoadCachedSFO(sfopath);
	if (sfoFile) {
		// save created in PPSSPP and not encrypted has '0' in SAVEDATA_PARAMS
		u32 tmpDataSize = 0;
		const u8 *tmpDataOrig = sfoFile->GetValueData("SAVEDATA_PARAMS", &tmpDataSize);
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
			ERROR_LOG_REPORT(Log::sceUtility, "Unexpected SAVEDATA_PARAMS hash flag: %02x", tmpDataOrig[0]);
			return 1;
		}
	}
	return 0;
}

bool SavedataParam::IsInSaveDataList(const std::string &saveName, int count) {
	for(int i = 0; i < count; ++i) {
		if(strcmp(saveDataList[i].saveName.c_str(),saveName.c_str()) == 0)
			return true;
	}
	return false;
}
