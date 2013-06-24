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

#include "Core/Reporting.h"
#include "Core/Dialog/SavedataParam.h"
#include "Core/Dialog/PSPSaveDialog.h"
#include "Core/HLE/sceKernelMemory.h"
#include "Core/HLE/sceChnnlsv.h"
#include "Core/ELF/ParamSFO.h"
#include "Core/HW/MemoryStick.h"

#include "image/png_load.h"

#ifdef BLACKBERRY
using std::strnlen;
#endif

static const std::string ICON0_FILENAME = "ICON0.PNG";
static const std::string ICON1_FILENAME = "ICON1.PMF";
static const std::string PIC1_FILENAME = "PIC1.PNG";
static const std::string SND0_FILENAME = "SND0.AT3";
static const std::string SFO_FILENAME = "PARAM.SFO";

std::string savePath = "ms0:/PSP/SAVEDATA/";

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
		strncpy(str, value.c_str(), strLength - 1);
		str[strLength - 1] = 0;
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
		u32 handle = pspFileSystem.OpenFile(filename, (FileAccess)(FILEACCESS_WRITE | FILEACCESS_CREATE));
		if (handle == 0)
			return false;

		size_t result = pspFileSystem.WriteFile(handle, data, dataSize);
		pspFileSystem.CloseFile(handle);

		return result != 0;
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
		if(sdkVersion > 0x307FFFF)
			return 6;
		if(sdkVersion > 0x300FFFF)
			return 5;
		if(sdkVersion > 0x206FFFF)
			return 4;
		if(sdkVersion > 0x205FFFF)
			return 3;
		if(sdkVersion >= 0x2000000)
			return 2;
		if(sdkVersion >= 0x1000000)
			return 1;
		return 0;
	};
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
}

std::string SavedataParam::GetSaveDirName(SceUtilitySavedataParam* param, int saveId)
{
	if (!param) {
		return "";
	}

	std::string dirName = GetSaveName(param);
	if (saveId >= 0 && saveNameListDataCount > 0) // if user selection, use it
		dirName = GetFilename(saveId);

	return dirName;
}

std::string SavedataParam::GetSaveDir(SceUtilitySavedataParam* param, const std::string &saveDirName)
{
	if (!param) {
		return "";
	}

	return GetGameName(param) + saveDirName;
}

std::string SavedataParam::GetSaveDir(SceUtilitySavedataParam* param, int saveId)
{
	return GetSaveDir(param, GetSaveDirName(param, saveId));
}

std::string SavedataParam::GetSaveFilePath(SceUtilitySavedataParam* param, const std::string &saveDir)
{
	if (!param) {
		return "";
	}

	return savePath + saveDir;
}

std::string SavedataParam::GetSaveFilePath(SceUtilitySavedataParam* param, int saveId)
{
	return GetSaveFilePath(param, GetSaveDir(param, saveId));
}

std::string SavedataParam::GetGameName(SceUtilitySavedataParam* param)
{
	char gameName[14];
	memcpy(gameName,param->gameName,13);
	gameName[13] = 0;
	return gameName;
}

std::string SavedataParam::GetSaveName(SceUtilitySavedataParam* param)
{
	char saveName[21];
	memcpy(saveName,param->saveName,20);
	saveName[20] = 0;
	if(strcmp(saveName,"<>") == 0)
		return "";
	return saveName;
}

std::string SavedataParam::GetFileName(SceUtilitySavedataParam* param)
{
	char fileName[14];
	memcpy(fileName,param->fileName,13);
	fileName[13] = 0;
	return fileName;
}

bool SavedataParam::Delete(SceUtilitySavedataParam* param, int saveId)
{
	if (!param)
	{
		return false;
	}

	std::string dirPath = GetSaveFilePath(param,saveId);
	if (saveId >= 0 && saveNameListDataCount > 0) // if user selection, use it
	{
		if (saveDataList[saveId].size == 0) // don't delete no existing file
		{
			return false;
		}
	}

	pspFileSystem.RmDir(dirPath);
	return true;
}

bool SavedataParam::Save(SceUtilitySavedataParam* param, const std::string &saveDirName, bool secureMode)
{
	if (!param) {
		return false;
	}

	std::string dirPath = GetSaveFilePath(param, GetSaveDir(param, saveDirName));

	if (!pspFileSystem.GetFileInfo(dirPath).exists)
		pspFileSystem.MkDir(dirPath);

	u8* cryptedData = 0;
	int cryptedSize = 0;
	u8 cryptedHash[0x10];
	memset(cryptedHash,0,0x10);
	// Encrypt save.
	// TODO: Is this the correct difference between MAKEDATA and MAKEDATASECURE?
	if (param->dataBuf != 0 && g_Config.bEncryptSave && secureMode)
	{
		cryptedSize = param->dataSize;
		if(cryptedSize == 0 || (SceSize)cryptedSize > param->dataBufSize)
			cryptedSize = param->dataBufSize; // fallback, should never use this
		u8 *data_ = param->dataBuf;

		int aligned_len = align16(cryptedSize);
		cryptedData = new u8[aligned_len + 0x10];
		memcpy(cryptedData, data_, cryptedSize);

		int decryptMode = 1;
		if(param->key[0] != 0)
		{
			decryptMode = (GetSDKMainVersion(sceKernelGetCompiledSdkVersion()) >= 4 ? 5 : 3);
		}

		if(EncryptData(decryptMode, cryptedData, &cryptedSize, &aligned_len, cryptedHash, ((param->key[0] != 0)?param->key:0)) == 0)
		{
		}
		else
		{
			ERROR_LOG(HLE,"Save encryption failed. This save won't work on real PSP");
			delete[] cryptedData;
			cryptedData = 0;
		}
	}

	// SAVE PARAM.SFO
	ParamSFOData sfoFile;
	std::string sfopath = dirPath+"/" + SFO_FILENAME;
	PSPFileInfo sfoInfo = pspFileSystem.GetFileInfo(sfopath);
	if(sfoInfo.exists) // Read old sfo if exist
	{
		u8 *sfoData = new u8[(size_t)sfoInfo.size];
		size_t sfoSize = (size_t)sfoInfo.size;
		if(ReadPSPFile(sfopath,&sfoData,sfoSize, NULL))
		{
			sfoFile.ReadSFO(sfoData,sfoSize);
			delete[] sfoData;
		}
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
		const int FILE_LIST_TOTAL_SIZE = FILE_LIST_ITEM_SIZE * FILE_LIST_COUNT_MAX;
		u32 tmpDataSize = 0;
		u8 *tmpDataOrig = sfoFile.GetValueData("SAVEDATA_FILE_LIST", &tmpDataSize);
		u8 *tmpData = new u8[FILE_LIST_TOTAL_SIZE];

		if (tmpDataOrig != NULL)
			memcpy(tmpData, tmpDataOrig, tmpDataSize > FILE_LIST_TOTAL_SIZE ? FILE_LIST_TOTAL_SIZE : tmpDataSize);
		else
			memset(tmpData, 0, FILE_LIST_TOTAL_SIZE);

		if (param->dataBuf != 0)
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
		sfoFile.SetValue("SAVEDATA_FILE_LIST", tmpData, FILE_LIST_TOTAL_SIZE, FILE_LIST_TOTAL_SIZE);
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
			UpdateHash(sfoData, (int)sfoSize, offset, (param->key[0] ? 3 : 1));
	}
	WritePSPFile(sfopath, sfoData, (SceSize)sfoSize);
	delete[] sfoData;

	if(param->dataBuf != 0)	// Can launch save without save data in mode 13
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

		INFO_LOG(HLE,"Saving file with size %u in %s",saveSize,filePath.c_str());

		// copy back save name in request
		strncpy(param->saveName, saveDirName.c_str(), 20);

		if (!WritePSPFile(filePath, data_, saveSize))
		{
			ERROR_LOG(HLE,"Error writing file %s",filePath.c_str());
			if(cryptedData != 0)
			{
				delete[] cryptedData;
			}
			return false;
		}
		delete[] cryptedData;
	}


	// SAVE ICON0
	if (param->icon0FileData.buf.Valid())
	{
		std::string icon0path = dirPath + "/" + ICON0_FILENAME;
		WritePSPFile(icon0path, param->icon0FileData.buf, param->icon0FileData.bufSize);
	}
	// SAVE ICON1
	if (param->icon1FileData.buf.Valid())
	{
		std::string icon1path = dirPath + "/" + ICON1_FILENAME;
		WritePSPFile(icon1path, param->icon1FileData.buf, param->icon1FileData.bufSize);
	}
	// SAVE PIC1
	if (param->pic1FileData.buf.Valid())
	{
		std::string pic1path = dirPath + "/" + PIC1_FILENAME;
		WritePSPFile(pic1path, param->pic1FileData.buf, param->pic1FileData.bufSize);
	}

	// Save SND
	if (param->snd0FileData.buf.Valid())
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

	u8 *data_ = param->dataBuf;

	std::string dirPath = GetSaveFilePath(param, GetSaveDir(param, saveDirName));
	if (saveId >= 0 && saveNameListDataCount > 0) // if user selection, use it
	{
		if (saveDataList[saveId].size == 0) // don't read no existing file
		{
			return false;
		}
	}

	std::string filePath = dirPath+"/"+GetFileName(param);
	s64 readSize;
	INFO_LOG(HLE,"Loading file with size %u in %s",param->dataBufSize,filePath.c_str());
	u8* saveData = 0;
	int saveSize = -1;
	if (!ReadPSPFile(filePath, &saveData, saveSize, &readSize))
	{
		ERROR_LOG(HLE,"Error reading file %s",filePath.c_str());
		return false;
	}
	saveSize = (int)readSize;

	// copy back save name in request
	strncpy(param->saveName, saveDirName.c_str(), 20);

	ParamSFOData sfoFile;
	std::string sfopath = dirPath+"/" + SFO_FILENAME;
	PSPFileInfo sfoInfo = pspFileSystem.GetFileInfo(sfopath);
	if(sfoInfo.exists) // Read sfo
	{
		u8 *sfoData = new u8[(size_t)sfoInfo.size];
		size_t sfoSize = (size_t)sfoInfo.size;
		if(ReadPSPFile(sfopath,&sfoData,sfoSize, NULL))
		{
			sfoFile.ReadSFO(sfoData,sfoSize);

			// copy back info in request
			strncpy(param->sfoParam.title,sfoFile.GetValueString("TITLE").c_str(),128);
			strncpy(param->sfoParam.savedataTitle,sfoFile.GetValueString("SAVEDATA_TITLE").c_str(),128);
			strncpy(param->sfoParam.detail,sfoFile.GetValueString("SAVEDATA_DETAIL").c_str(),1024);
			param->sfoParam.parentalLevel = sfoFile.GetValueInt("PARENTAL_LEVEL");
		}
		delete[] sfoData;
	}
	// Don't know what it is, but PSP always respond this and this unlock some game
	param->bind = 1021;

	bool isCrypted = IsSaveEncrypted(param, saveDirName) && secureMode;
	bool saveDone = false;
	if(isCrypted)// Try to decrypt
	{
		int align_len = align16(saveSize);
		u8* data_base = new u8[align_len];
		u8* cryptKey = new u8[0x10];
		memset(cryptKey,0,0x10);

		if(param->key[0] != 0)
		{
			memcpy(cryptKey, param->key, 0x10);
		}
		memset(data_base + saveSize, 0, align_len - saveSize);
		memcpy(data_base, saveData, saveSize);

		int decryptMode = 1;
		if(param->key[0] != 0)
		{
			decryptMode = (GetSDKMainVersion(sceKernelGetCompiledSdkVersion()) >= 4 ? 5 : 3);
		}

		if(DecryptSave(decryptMode, data_base, &saveSize, &align_len, ((param->key[0] != 0)?cryptKey:0)) == 0)
		{
			memcpy(data_, data_base, saveSize);
			saveDone = true;
		}
		delete[] data_base;
		delete[] cryptKey;
	}
	if(!saveDone) // not crypted or decrypt fail
	{
		memcpy(data_, saveData, saveSize);
	}
	param->dataSize = (SceSize)saveSize;
	delete[] saveData;

	return true;
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

	/* Compute 11D0 hash over entire file */
	if ((ret = BuildHash(filehash, sfoData, sfoSize, alignedLen, (encryptmode & 2) ? 4 : 2, NULL)) < 0)
	{	// Not sure about "2"
		return ret - 400;
	}

	/* Copy 11D0 hash to param.sfo and set flag indicating it's there */
	memcpy(sfoData+sfoDataParamsOffset + 0x20, filehash, 0x10);
	*(sfoData+sfoDataParamsOffset) |= 0x01;

	/* If new encryption mode, compute and insert the 1220 hash. */
	if (encryptmode & 2)
	{

		/* Enable the hash bit first */
		*(sfoData+sfoDataParamsOffset) |= 0x20;

		if ((ret = BuildHash(filehash, sfoData, sfoSize, alignedLen, 3, 0)) < 0)
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

std::string SavedataParam::GetSpaceText(int size)
{
	char text[50];

	if(size < 1024)
	{
		sprintf(text,"%d B",size);
		return std::string(text);
	}

	size /= 1024;

	if(size < 1024)
	{
		sprintf(text,"%d KB",size);
		return std::string(text);
	}

	size /= 1024;

	if(size < 1024)
	{
		sprintf(text,"%d MB",size);
		return std::string(text);
	}

	size /= 1024;
	sprintf(text,"%d GB",size);
	return std::string(text);
}

int SavedataParam::GetSizes(SceUtilitySavedataParam *param)
{
	if (!param) {
		return SCE_UTILITY_SAVEDATA_ERROR_SIZES_NO_DATA;
	}

	int ret = 0;

	if (param->msFree.Valid())
	{
		param->msFree->clusterSize = (u32)MemoryStick_SectorSize();
		param->msFree->freeClusters = (u32)(MemoryStick_FreeSpace() / MemoryStick_SectorSize());
		param->msFree->freeSpaceKB = (u32)(MemoryStick_FreeSpace() / 0x400);
		const std::string spaceTxt = SavedataParam::GetSpaceText((int)MemoryStick_FreeSpace());
		memset(param->msFree->freeSpaceStr, 0, sizeof(param->msFree->freeSpaceStr));
		strncpy(param->msFree->freeSpaceStr, spaceTxt.c_str(), sizeof(param->msFree->freeSpaceStr));
	}
	if (param->msData.Valid())
	{
		const std::string gameName(param->msData->gameName, strnlen(param->msData->gameName, sizeof(param->msData->gameName)));
		const std::string saveName(param->msData->saveName, strnlen(param->msData->saveName, sizeof(param->msData->saveName)));
		std::string path = GetSaveFilePath(param, gameName + saveName);
		PSPFileInfo finfo = pspFileSystem.GetFileInfo(path);
		if(finfo.exists)
		{
			// TODO : fill correctly with the total save size, be aware of crypted file size
			param->msData->info.usedClusters = 1;
			param->msData->info.usedSpaceKB = 0x20;
			strncpy(param->msData->info.usedSpaceStr, "", sizeof(param->msData->info.usedSpaceStr));	// "32 KB" // 8 u8
			param->msData->info.usedSpace32KB = 0x20;
			strncpy(param->msData->info.usedSpace32Str, "", sizeof(param->msData->info.usedSpace32Str));	// "32 KB" // 8 u8
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
	if (param->utilityData.Valid())
	{
		int total_size = 0;
		total_size += getSizeNormalized(1); // SFO;
		total_size += getSizeNormalized(param->dataSize); // Save Data
		total_size += getSizeNormalized(param->icon0FileData.size);
		total_size += getSizeNormalized(param->icon1FileData.size);
		total_size += getSizeNormalized(param->pic1FileData.size);
		total_size += getSizeNormalized(param->snd0FileData.size);

		param->utilityData->usedClusters = total_size / (u32)MemoryStick_SectorSize();
		param->utilityData->usedSpaceKB = total_size / 0x400;
		std::string spaceTxt = SavedataParam::GetSpaceText(total_size);
		memset(param->utilityData->usedSpaceStr, 0, sizeof(param->utilityData->usedSpaceStr));
		strncpy(param->utilityData->usedSpaceStr, spaceTxt.c_str(), sizeof(param->utilityData->usedSpaceStr));

		// TODO: Maybe these are rounded to the nearest 32KB?  Or something?
		param->utilityData->usedSpace32KB = total_size / 0x400;
		spaceTxt = SavedataParam::GetSpaceText(total_size);
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

	if (param->idList.Valid())
	{
		u32 maxFile = param->idList->maxCount;

		std::vector<PSPFileInfo> validDir;
		std::vector<PSPFileInfo> allDir = pspFileSystem.GetDirListing(savePath);

		if (param->idList.Valid())
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

			SceUtilitySavedataIdListEntry *entries = param->idList->entries;
			for (u32 i = 0; i < (u32)validDir.size(); i++)
			{
				entries[i].st_mode = 0x11FF;
				// TODO
				memset(&entries[i].st_ctime, 0, sizeof(entries[i].st_ctime));
				memset(&entries[i].st_atime, 0, sizeof(entries[i].st_atime));
				memset(&entries[i].st_mtime, 0, sizeof(entries[i].st_mtime));
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

	if (!param->fileList.Valid()) {
		ERROR_LOG_REPORT(HLE, "SavedataParam::GetFilesList(): bad fileList address %08x", param->fileList.ptr);
		// Should crash.
		return -1;
	}

	auto &fileList = param->fileList;
	if (fileList->secureEntries.Valid() && fileList->maxSecureEntries > 99) {
		ERROR_LOG_REPORT(HLE, "SavedataParam::GetFilesList(): too many secure entries, %d", fileList->maxSecureEntries);
		return SCE_UTILITY_SAVEDATA_ERROR_RW_BAD_PARAMS;
	}
	if (fileList->normalEntries.Valid() && fileList->maxNormalEntries > 8192) {
		ERROR_LOG_REPORT(HLE, "SavedataParam::GetFilesList(): too many normal entries, %d", fileList->maxNormalEntries);
		return SCE_UTILITY_SAVEDATA_ERROR_RW_BAD_PARAMS;
	}
	// TODO: This may depend on sdk version or something?  Not returned by default.
	if (false && fileList->systemEntries.Valid() && fileList->maxSystemEntries > 5) {
		ERROR_LOG_REPORT(HLE, "SavedataParam::GetFilesList(): too many system entries, %d", fileList->maxSystemEntries);
		return SCE_UTILITY_SAVEDATA_ERROR_RW_BAD_PARAMS;
	}

	std::string dirPath = savePath + GetGameName(param) + GetSaveName(param);
	if (!pspFileSystem.GetFileInfo(dirPath).exists) {
		DEBUG_LOG(HLE, "SavedataParam::GetFilesList(): directory %s does not exist", dirPath.c_str());
		return SCE_UTILITY_SAVEDATA_ERROR_RW_NO_DATA;
	}

	// Even if there are no files, initialize to 0.
	fileList->resultNumSecureEntries = 0;
	fileList->resultNumNormalEntries = 0;
	fileList->resultNumSystemEntries = 0;

	// We need PARAMS.SFO's SAVEDATA_FILE_LIST to determine which entries are secure.
	PSPFileInfo sfoFileInfo = pspFileSystem.GetFileInfo(dirPath + "/" + SFO_FILENAME);
	std::set<std::string> secureFilenames;
	// TODO: Error code if not?
	if (sfoFileInfo.exists) {
		ParamSFOData sfoFile;
		size_t sfoSize = (size_t)sfoFileInfo.size;
		u8 *sfoData = new u8[sfoSize];
		if (ReadPSPFile(dirPath + "/" + SFO_FILENAME, &sfoData, sfoSize, NULL)){
			sfoFile.ReadSFO(sfoData, sfoSize);
		}
		delete[] sfoData;

		u32 sfoFileListSize = 0;
		char *sfoFileList = (char *)sfoFile.GetValueData("SAVEDATA_FILE_LIST", &sfoFileListSize);
		const int FILE_LIST_ITEM_SIZE = 13 + 16 + 3;
		const int FILE_LIST_COUNT_MAX = 99;

		// Filenames are 13 bytes long at most.  Add a NULL so there's no surprises.
		char temp[14];
		temp[13] = '\0';

		for (u32 i = 0; i < FILE_LIST_COUNT_MAX; ++i) {
			// Ends at a NULL filename.
			if (i * FILE_LIST_ITEM_SIZE >= sfoFileListSize || sfoFileList[i * FILE_LIST_ITEM_SIZE] == '\0') {
				break;
			}

			strncpy(temp, &sfoFileList[i * FILE_LIST_ITEM_SIZE], 13);
			secureFilenames.insert(temp);
		}
	}

	// Does not list directories, nor recurse into them, and ignores files not ALL UPPERCASE.
	auto files = pspFileSystem.GetDirListing(dirPath);
	for (auto file = files.begin(), end = files.end(); file != end; ++file) {
		if (file->type == FILETYPE_DIRECTORY) {
			continue;
		}
		// TODO: What are the exact rules?  It definitely skips lowercase, and allows FILE or FILE.EXT.
		if (file->name.find_first_of("abcdefghijklmnopqrstuvwxyz") != file->name.npos) {
			DEBUG_LOG(HLE, "SavedataParam::GetFilesList(): skipping file %s with lowercase", file->name.c_str());
			continue;
		}

		bool isSystemFile = file->name == ICON0_FILENAME || file->name == ICON1_FILENAME || file->name == PIC1_FILENAME;
		isSystemFile = isSystemFile || file->name == SND0_FILENAME || file->name == SFO_FILENAME;

		SceUtilitySavedataFileListEntry *entry = NULL;
		int sizeOffset = 0;
		if (isSystemFile) {
			if (fileList->systemEntries.Valid() && fileList->resultNumSystemEntries < fileList->maxSystemEntries) {
				entry = &fileList->systemEntries[fileList->resultNumSystemEntries++];
			}
		} else if (secureFilenames.find(file->name) != secureFilenames.end()) {
			if (fileList->secureEntries.Valid() && fileList->resultNumSecureEntries < fileList->maxSecureEntries) {
				entry = &fileList->secureEntries[fileList->resultNumSecureEntries++];
			}
			// Secure files are slightly bigger.
			bool isCrypted = IsSaveEncrypted(param, GetSaveDirName(param, 0));
			if (isCrypted) {
				sizeOffset = -0x10;
			}
		} else {
			if (fileList->normalEntries.Valid() && fileList->resultNumNormalEntries < fileList->maxNormalEntries) {
				entry = &fileList->normalEntries[fileList->resultNumNormalEntries++];
			}
		}

		// Out of space for this file in the list.
		if (entry == NULL) {
			continue;
		}

		entry->st_mode = 0x21FF;
		entry->st_size = file->size + sizeOffset;
		// TODO: ctime, atime, mtime
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

	std::string saveDir = savePath + GetGameName(param) + GetSaveName(param);
	PSPFileInfo info = pspFileSystem.GetFileInfo(saveDir);
	bool exists = info.exists;

	if (param->sizeInfo.Valid())
	{
		// TODO: Read the entries and count up the size vs. existing size?

		param->sizeInfo->sectorSize = (int)MemoryStick_SectorSize();
		param->sizeInfo->freeSectors = (int)(MemoryStick_FreeSpace() / MemoryStick_SectorSize());

		// TODO: Is this after the specified files?  Before?
		param->sizeInfo->freeKB = (int)(MemoryStick_FreeSpace() / 1024);
		std::string spaceTxt = SavedataParam::GetSpaceText((int)MemoryStick_FreeSpace());
		strncpy(param->sizeInfo->freeString, spaceTxt.c_str(), 8);
		param->sizeInfo->freeString[7] = '\0';

		// TODO.
		param->sizeInfo->neededKB = 0;
		strcpy(param->sizeInfo->neededString, "0 KB");
		param->sizeInfo->overwriteKB = 0;
		strcpy(param->sizeInfo->overwriteString, "0 KB");
	}

	return exists;
}

void SavedataParam::Clear()
{
	if (saveDataList)
	{
		for (int i = 0; i < saveNameListDataCount; i++)
		{
			if (saveDataList[i].textureData != 0 && saveDataList[i].size != 0)
				kernelMemory.Free(saveDataList[i].textureData);
			saveDataList[i].textureData = 0;
		}

		delete[] saveDataList;
		saveDataList = 0;
		saveDataListCount = 0;
	}
	if (noSaveIcon)
	{
		if(noSaveIcon->textureData != 0)
			kernelMemory.Free(noSaveIcon->textureData);
		noSaveIcon->textureData = 0;
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
	if (param->saveNameList.Valid())
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
			for (int i = 0; i < saveDataListCount; i++)
			{
				// TODO: Maybe we should fill the list with existing files instead?
				if (strcmp(saveNameListData[i], "<>") == 0)
					continue;

				DEBUG_LOG(HLE,"Name : %s",saveNameListData[i]);

				std::string fileDataPath = savePath+GetGameName(param) + saveNameListData[i] + "/" + param->fileName;
				PSPFileInfo info = pspFileSystem.GetFileInfo(fileDataPath);
				if (info.exists)
				{
					SetFileInfo(realCount, info, saveNameListData[i]);

					DEBUG_LOG(HLE,"%s Exist",fileDataPath.c_str());
					realCount++;
				}
				else
				{
					if (listEmptyFile)
					{
						ClearFileInfo(saveDataList[realCount], saveNameListData[i]);
						DEBUG_LOG(HLE,"Don't Exist");
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
		DEBUG_LOG(HLE,"Name : %s",GetSaveName(param).c_str());

		std::string fileDataPath = savePath + GetGameName(param) + GetSaveName(param) + "/" + param->fileName;
		PSPFileInfo info = pspFileSystem.GetFileInfo(fileDataPath);
		if (info.exists)
		{
			SetFileInfo(0, info, GetSaveName(param));

			DEBUG_LOG(HLE,"%s Exist",fileDataPath.c_str());
			saveNameListDataCount = 1;
		}
		else
		{
			if (listEmptyFile)
			{
				ClearFileInfo(saveDataList[0], GetSaveName(param));
				DEBUG_LOG(HLE,"Don't Exist");
			}
			saveNameListDataCount = 0;
			return 0;
		}
	}
	return 0;
}

bool SavedataParam::CreatePNGIcon(u8* pngData, int pngSize, SaveFileInfo& info)
{
	unsigned char *textureData;
	int w,h;

	int success = pngLoadPtr(pngData, (int)pngSize, &w, &h, &textureData, false);

	u32 texSize = w*h*4;
	u32 atlasPtr;
	if (success)
		atlasPtr = kernelMemory.Alloc(texSize, true, "SaveData Icon");
	if (success && atlasPtr != (u32)-1)
	{
		info.textureData = atlasPtr;
		Memory::Memcpy(atlasPtr, textureData, texSize);
		free(textureData);
		info.textureWidth = w;
		info.textureHeight = h;
	}
	else
	{
		WARN_LOG(HLE, "Unable to load PNG data for savedata.");
		return false;
	}
	return true;
}

void SavedataParam::SetFileInfo(SaveFileInfo &saveInfo, PSPFileInfo &info, std::string saveName)
{
	saveInfo.size = info.size;
	saveInfo.saveName = saveName;
	saveInfo.idx = 0;
	saveInfo.modif_time = info.mtime;

	// Start with a blank slate.
	saveInfo.textureData = 0;
	saveInfo.title[0] = 0;
	saveInfo.saveTitle[0] = 0;
	saveInfo.saveDetail[0] = 0;

	// Search save image icon0
	// TODO : If icon0 don't exist, need to use icon1 which is a moving icon. Also play sound
	std::string fileDataPath2 = savePath + GetGameName(pspParam) + saveName + "/" + ICON0_FILENAME;
	PSPFileInfo info2 = pspFileSystem.GetFileInfo(fileDataPath2);
	if (info2.exists)
	{
		u8 *textureDataPNG = new u8[(size_t)info2.size];
		ReadPSPFile(fileDataPath2, &textureDataPNG, info2.size, NULL);
		CreatePNGIcon(textureDataPNG, (int)info2.size, saveInfo);
		delete[] textureDataPNG;
	}

	// Load info in PARAM.SFO
	fileDataPath2 = savePath + GetGameName(pspParam) + saveName + "/" + SFO_FILENAME;
	info2 = pspFileSystem.GetFileInfo(fileDataPath2);
	if (info2.exists)
	{
		u8 *sfoParam = new u8[(size_t)info2.size];
		ReadPSPFile(fileDataPath2, &sfoParam, info2.size, NULL);
		ParamSFOData sfoFile;
		if (sfoFile.ReadSFO(sfoParam,(size_t)info2.size))
		{
			SetStringFromSFO(sfoFile, "TITLE", saveInfo.title, sizeof(saveInfo.title));
			SetStringFromSFO(sfoFile, "SAVEDATA_TITLE", saveInfo.saveTitle, sizeof(saveInfo.saveTitle));
			SetStringFromSFO(sfoFile, "SAVEDATA_DETAIL", saveInfo.saveDetail, sizeof(saveInfo.saveDetail));
		}
		delete [] sfoParam;
	}
}

void SavedataParam::SetFileInfo(int idx, PSPFileInfo &info, std::string saveName)
{
	SetFileInfo(saveDataList[idx], info, saveName);
	saveDataList[idx].idx = idx;
}

void SavedataParam::ClearFileInfo(SaveFileInfo &saveInfo, std::string saveName)
{
	saveInfo.size = 0;
	saveInfo.saveName = saveName;
	saveInfo.idx = 0;
	saveInfo.textureData = 0;

	if (GetPspParam()->newData.Valid() && GetPspParam()->newData->buf.Valid())
	{
		// We have a png to show
		if (!noSaveIcon)
		{
			noSaveIcon = new SaveFileInfo();
			PspUtilitySavedataFileData *newData = GetPspParam()->newData;
			CreatePNGIcon(newData->buf, (int)newData->size, *noSaveIcon);
		}
		saveInfo.textureData = noSaveIcon->textureData;
		saveInfo.textureWidth = noSaveIcon->textureWidth;
		saveInfo.textureHeight = noSaveIcon->textureHeight;
	}
}

SceUtilitySavedataParam* SavedataParam::GetPspParam()
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
std::string SavedataParam::GetFilename(int idx)
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
	return saveDataListCount - 1;
}

int SavedataParam::GetLatestSave()
{
	int idx = 0;
	time_t idxTime = 0;
	for (int i = 0; i < saveDataListCount; ++i)
	{
		time_t thisTime = mktime(&saveDataList[i].modif_time);
		if (idxTime < thisTime)
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
	for (int i = 0; i < saveDataListCount; ++i)
	{
		time_t thisTime = mktime(&saveDataList[i].modif_time);
		if (idxTime > thisTime)
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
	for (int i = 0; i < saveDataListCount; ++i)
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
	for (int i = saveDataListCount; i > 0; )
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
	for (int i = 0; i < saveDataListCount; ++i)
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
	for (int i = saveDataListCount; i > 0; )
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

void SavedataParam::DoState(PointerWrap &p)
{
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
	p.DoMarker("SavedataParam");
}

bool SavedataParam::IsSaveEncrypted(SceUtilitySavedataParam* param, const std::string &saveDirName)
{
	bool isCrypted = false;

	ParamSFOData sfoFile;
	std::string dirPath = GetSaveFilePath(param, GetSaveDir(param, saveDirName));
	std::string sfopath = dirPath + "/" + SFO_FILENAME;
	PSPFileInfo sfoInfo = pspFileSystem.GetFileInfo(sfopath);
	if(sfoInfo.exists) // Read sfo
	{
		u8 *sfoData = new u8[(size_t)sfoInfo.size];
		size_t sfoSize = (size_t)sfoInfo.size;
		if(ReadPSPFile(sfopath,&sfoData,sfoSize, NULL))
		{
			sfoFile.ReadSFO(sfoData,sfoSize);

			// save created in PPSSPP and not encrypted has '0' in SAVEDATA_PARAMS
			u32 tmpDataSize = 0;
			u8* tmpDataOrig = sfoFile.GetValueData("SAVEDATA_PARAMS", &tmpDataSize);
			for(u32 i = 0; i < tmpDataSize; i++)
			{
				if(tmpDataOrig[i] != 0)
				{
					isCrypted = true;
					break;
				}
			}
		}
		delete[] sfoData;
	}
	return isCrypted;
}

