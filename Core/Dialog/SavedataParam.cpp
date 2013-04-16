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

#include "SavedataParam.h"
#include "image/png_load.h"
#include "../HLE/sceKernelMemory.h"
#include "../ELF/ParamSFO.h"
#include "../HLE/sceChnnlsv.h"
#include "Core/HW/MemoryStick.h"
#include "PSPSaveDialog.h"

std::string icon0Name = "ICON0.PNG";
std::string icon1Name = "ICON1.PMF";
std::string pic1Name = "PIC1.PNG";
std::string snd0Name = "SND0.AT3";
std::string sfoName = "PARAM.SFO";

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

	struct EncryptFileInfo
	{
		int fileVersion;
		u8 key[16];
		int sdkVersion;
	};

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

std::string SavedataParam::GetSaveDir(SceUtilitySavedataParam* param, int saveId)
{
	if (!param) {
		return "";
	}

	std::string dirPath = GetGameName(param)+GetSaveName(param);
	if (saveId >= 0 && saveNameListDataCount > 0) // if user selection, use it
		dirPath = std::string(GetGameName(param))+GetFilename(saveId);

	return dirPath;
}

std::string SavedataParam::GetSaveFilePath(SceUtilitySavedataParam* param, int saveId)
{
	if (!param) {
		return "";
	}

	return savePath + GetSaveDir(param,saveId);
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

bool SavedataParam::Save(SceUtilitySavedataParam* param, int saveId)
{
	if (!param) {
		return false;
	}

	std::string dirPath = GetSaveFilePath(param, saveId);

	if (!pspFileSystem.GetFileInfo(dirPath).exists)
		pspFileSystem.MkDir(dirPath);

	u8* cryptedData = 0;
	int cryptedSize = 0;
	u8 cryptedHash[0x10];
	memset(cryptedHash,0,0x10);
	// Encrypt save.
	if(param->dataBuf != 0 && g_Config.bEncryptSave)
	{
		cryptedSize = param->dataSize;
		if(cryptedSize == 0 || (SceSize)cryptedSize > param->dataBufSize)
			cryptedSize = param->dataBufSize; // fallback, should never use this
		u8* data_ = (u8*)Memory::GetPointer(param->dataBuf);

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
	std::string sfopath = dirPath+"/"+sfoName;
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
	sfoFile.SetValue("SAVEDATA_DIRECTORY",GetSaveDir(param,saveId),64);

	// For each file, 13 bytes for filename, 16 bytes for file hash (0 in PPSSPP), 3 byte for padding
	const int FILE_LIST_ITEM_SIZE = 13 + 16 + 3;
	const int FILE_LIST_COUNT_MAX = 99;
	const int FILE_LIST_TOTAL_SIZE = FILE_LIST_ITEM_SIZE * FILE_LIST_COUNT_MAX;
	u32 tmpDataSize = 0;
	u8* tmpDataOrig = sfoFile.GetValueData("SAVEDATA_FILE_LIST", &tmpDataSize);
	u8* tmpData = new u8[FILE_LIST_TOTAL_SIZE];

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

	// Init param with 0. This will be used to detect crypted save or not on loading
	tmpData = new u8[128];
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
			UpdateHash(sfoData, sfoSize, offset, (param->key[0]?3:1));
	}
	WritePSPFile(sfopath, sfoData, (SceSize)sfoSize);
	delete[] sfoData;

	if(param->dataBuf != 0)	// Can launch save without save data in mode 13
	{
		std::string filePath = dirPath+"/"+GetFileName(param);
		u8* data_ = 0;
		SceSize saveSize = 0;
		if(cryptedData == 0) // Save decrypted data
		{
			saveSize = param->dataSize;
			if(saveSize == 0 || saveSize > param->dataBufSize)
				saveSize = param->dataBufSize; // fallback, should never use this

			data_ = (u8*)Memory::GetPointer(param->dataBuf);
		}
		else
		{
			data_ = cryptedData;
			saveSize = cryptedSize;
		}

		INFO_LOG(HLE,"Saving file with size %u in %s",saveSize,filePath.c_str());

		// copy back save name in request
		strncpy(param->saveName,GetSaveDirName(param, saveId).c_str(),20);

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
	if (param->icon0FileData.buf)
	{
		u8* data_ = (u8*)Memory::GetPointer(param->icon0FileData.buf);
		std::string icon0path = dirPath+"/"+icon0Name;
		WritePSPFile(icon0path, data_, param->icon0FileData.bufSize);
	}
	// SAVE ICON1
	if (param->icon1FileData.buf)
	{
		u8* data_ = (u8*)Memory::GetPointer(param->icon1FileData.buf);
		std::string icon1path = dirPath+"/"+icon1Name;
		WritePSPFile(icon1path, data_, param->icon1FileData.bufSize);
	}
	// SAVE PIC1
	if (param->pic1FileData.buf)
	{
		u8* data_ = (u8*)Memory::GetPointer(param->pic1FileData.buf);
		std::string pic1path = dirPath+"/"+pic1Name;
		WritePSPFile(pic1path, data_, param->pic1FileData.bufSize);
	}

	// Save SND
	if (param->snd0FileData.buf)
	{
		u8* data_ = (u8*)Memory::GetPointer(param->snd0FileData.buf);
		std::string snd0path = dirPath+"/"+snd0Name;
		WritePSPFile(snd0path, data_, param->snd0FileData.bufSize);
	}

	// Save Encryption Data
	{
		EncryptFileInfo encryptInfo;
		SceSize dataSize = sizeof(encryptInfo); // version + key + sdkVersion
		memset(&encryptInfo,0,dataSize);

		encryptInfo.fileVersion = 1;
		encryptInfo.sdkVersion = sceKernelGetCompiledSdkVersion();
		if(param->size > 1500)
			memcpy(encryptInfo.key,param->key,16);

		std::string encryptInfoPath = dirPath+"/"+"ENCRYPT_INFO.BIN";
		WritePSPFile(encryptInfoPath, (u8*)&encryptInfo, dataSize);
	}
	return true;
}

bool SavedataParam::Load(SceUtilitySavedataParam *param, int saveId)
{
	if (!param) {
		return false;
	}

	u8 *data_ = (u8*)Memory::GetPointer(param->dataBuf);

	std::string dirPath = GetSaveFilePath(param, saveId);
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
	strncpy(param->saveName,GetSaveDirName(param, saveId).c_str(),20);

	ParamSFOData sfoFile;
	std::string sfopath = dirPath+"/"+sfoName;
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

	bool isCrypted = IsSaveEncrypted(param,saveId);
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

bool SavedataParam::GetSizes(SceUtilitySavedataParam *param)
{
	if (!param) {
		return false;
	}

	bool ret = true;

	if (Memory::IsValidAddress(param->msFree))
	{
		Memory::Write_U32((u32)MemoryStick_SectorSize(),param->msFree); // cluster Size
		Memory::Write_U32((u32)(MemoryStick_FreeSpace() / MemoryStick_SectorSize()),param->msFree+4);	// Free cluster
		Memory::Write_U32((u32)(MemoryStick_FreeSpace() / 0x400),param->msFree+8); // Free space (in KB)
		std::string spaceTxt = SavedataParam::GetSpaceText((int)MemoryStick_FreeSpace());
		Memory::Memset(param->msFree+12,0,(u32)spaceTxt.size()+1);
		Memory::Memcpy(param->msFree+12,spaceTxt.c_str(),(u32)spaceTxt.size()); // Text representing free space
	}
	if (Memory::IsValidAddress(param->msData))
	{
		std::string path = GetSaveFilePath(param,0);
		PSPFileInfo finfo = pspFileSystem.GetFileInfo(path);
		if(finfo.exists)
		{
			// TODO : fill correctly with the total save size, be aware of crypted file size
			Memory::Write_U32(1,param->msData+36);	//1
			Memory::Write_U32(0x20,param->msData+40);	// 0x20
			Memory::Write_U8(0,param->msData+44);	// "32 KB" // 8 u8
			Memory::Write_U32(0x20,param->msData+52);	//  0x20
			Memory::Write_U8(0,param->msData+56);	// "32 KB" // 8 u8
		}
		else
		{
			Memory::Write_U32(0,param->msData+36);
			Memory::Write_U32(0,param->msData+40);
			Memory::Write_U8(0,param->msData+44);
			Memory::Write_U32(0,param->msData+52);
			Memory::Write_U8(0,param->msData+56);
			ret = false;
			// this should return SCE_UTILITY_SAVEDATA_ERROR_SIZES_NO_DATA
		}
	}
	if (Memory::IsValidAddress(param->utilityData))
	{
		int total_size = 0;
		total_size += getSizeNormalized(1); // SFO;
		total_size += getSizeNormalized(param->dataSize); // Save Data
		total_size += getSizeNormalized(param->icon0FileData.size);
		total_size += getSizeNormalized(param->icon1FileData.size);
		total_size += getSizeNormalized(param->pic1FileData.size);
		total_size += getSizeNormalized(param->snd0FileData.size);

		Memory::Write_U32(total_size / (u32)MemoryStick_SectorSize(),param->utilityData);	// num cluster
		Memory::Write_U32(total_size / 0x400,param->utilityData+4);	// save size in KB
		std::string spaceTxt = SavedataParam::GetSpaceText(total_size);
		Memory::Memset(param->utilityData+8,0,(u32)spaceTxt.size()+1);
		Memory::Memcpy(param->utilityData+8,spaceTxt.c_str(),(u32)spaceTxt.size()); // save size in text
		Memory::Write_U32(total_size / 0x400,param->utilityData+16);	// save size in KB
		spaceTxt = SavedataParam::GetSpaceText(total_size);
		Memory::Memset(param->utilityData+20,0,(u32)spaceTxt.size()+1);
		Memory::Memcpy(param->utilityData+20,spaceTxt.c_str(),(u32)spaceTxt.size()); // save size in text
	}
	return ret;

}

bool SavedataParam::GetList(SceUtilitySavedataParam *param)
{
	if (!param) {
		return false;
	}

	if (Memory::IsValidAddress(param->idListAddr))
	{
		u32 outputBuffer = Memory::Read_U32(param->idListAddr + 8);
		u32 maxFile = Memory::Read_U32(param->idListAddr + 0);

		std::vector<PSPFileInfo> validDir;
		std::vector<PSPFileInfo> allDir = pspFileSystem.GetDirListing(savePath);

		if (Memory::IsValidAddress(outputBuffer))
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

			for (u32 i = 0; i < (u32)validDir.size(); i++)
			{
				u32 baseAddr = outputBuffer + (i*72);
				Memory::Write_U32(0x11FF,baseAddr + 0); // mode
				Memory::Write_U64(0,baseAddr + 4); // TODO ctime
				Memory::Write_U64(0,baseAddr + 12); // TODO unknow
				Memory::Write_U64(0,baseAddr + 20); // TODO atime
				Memory::Write_U64(0,baseAddr + 28); // TODO unknow
				Memory::Write_U64(0,baseAddr + 36); // TODO mtime
				Memory::Write_U64(0,baseAddr + 44); // TODO unknow
				// folder name without gamename (max 20 u8)
				std::string outName = validDir[i].name.substr(GetGameName(param).size());
				Memory::Memset(baseAddr + 52,0,20);
				Memory::Memcpy(baseAddr + 52, outName.c_str(), (u32)outName.size());
			}
		}
		// Save num of folder found
		Memory::Write_U32((u32)validDir.size(), param->idListAddr + 4);
	}
	return true;
}

bool SavedataParam::GetFilesList(SceUtilitySavedataParam *param)
{
	if (!param)
	{
		return false;
	}

	u32 dataAddr = param->fileListAddr;
	if (!Memory::IsValidAddress(dataAddr))
		return false;

	// TODO : Need to be checked against more game

	u32 fileInfosAddr = Memory::Read_U32(dataAddr + 24);

	//for Valkyria2, dataAddr+0 and dataAddr+12 has "5" for 5 files
	int numFiles = Memory::Read_U32(dataAddr+12);
	int foundFiles = 0;
	for (int i = 0; i < numFiles; i++)
	{
		// for each file (80 bytes):
		// u32 mode, u32 ??, u64 size, u64 ctime, u64 ??, u64 atime, u64 ???, u64 mtime, u64 ???
		// u8[16] filename (or 13 + padding?)
		u32 curFileInfoAddr = fileInfosAddr + i*80;

		char fileName[16];
		strncpy(fileName, Memory::GetCharPointer(curFileInfoAddr + 64),16);
		std::string filePath = savePath + GetGameName(param) + GetSaveName(param) + "/" + fileName;
		PSPFileInfo info = pspFileSystem.GetFileInfo(filePath);
		if (info.exists)
		{
			bool isCrypted = IsSaveEncrypted(param,0);
			Memory::Write_U32(0x21FF, curFileInfoAddr+0);
			if(isCrypted)	// Crypted save are 16 bytes bigger
				Memory::Write_U64(info.size - 0x10, curFileInfoAddr+8);
			else
				Memory::Write_U64(info.size, curFileInfoAddr+8);
			Memory::Write_U64(0,curFileInfoAddr + 16); // TODO ctime
			Memory::Write_U64(0,curFileInfoAddr + 24); // TODO unknow
			Memory::Write_U64(0,curFileInfoAddr + 32); // TODO atime
			Memory::Write_U64(0,curFileInfoAddr + 40); // TODO unknow
			Memory::Write_U64(0,curFileInfoAddr + 48); // TODO mtime
			Memory::Write_U64(0,curFileInfoAddr + 56); // TODO unknow
			foundFiles++;
		}
	}

	// TODO : verify if return true if at least 1 file found or only if all found
	return foundFiles > 0;
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

	if (Memory::IsValidAddress(param->sizeAddr))
	{
		PspUtilitySavedataSizeInfo sizeInfo;
		Memory::ReadStruct(param->sizeAddr, &sizeInfo);

		// TODO: Read the entries and count up the size vs. existing size?

		sizeInfo.sectorSize = (int)MemoryStick_SectorSize();
		sizeInfo.freeSectors = (int)(MemoryStick_FreeSpace() / MemoryStick_SectorSize());

		// TODO: Is this after the specified files?  Before?
		sizeInfo.freeKB = (int)(MemoryStick_FreeSpace() / 1024);
		std::string spaceTxt = SavedataParam::GetSpaceText((int)MemoryStick_FreeSpace());
		strncpy(sizeInfo.freeString, spaceTxt.c_str(), 8);
		sizeInfo.freeString[7] = '\0';

		// TODO.
		sizeInfo.neededKB = 0;
		strcpy(sizeInfo.neededString, "0 KB");
		sizeInfo.overwriteKB = 0;
		strcpy(sizeInfo.overwriteString, "0 KB");

		Memory::WriteStruct(param->sizeAddr, &sizeInfo);
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
	if(noSaveIcon)
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

	char (*saveNameListData)[20];
	bool hasMultipleFileName = false;
	if (param->saveNameList != 0)
	{
		Clear();

		saveNameListData = (char(*)[20])Memory::GetPointer(param->saveNameList);

		// Get number of fileName in array
		saveDataListCount = 0;
		while(saveNameListData[saveDataListCount][0] != 0)
		{
			saveDataListCount++;
		}

		if(saveDataListCount > 0)
		{
			hasMultipleFileName = true;
			saveDataList = new SaveFileInfo[saveDataListCount];

			// get and stock file info for each file
			int realCount = 0;
			for (int i = 0; i < saveDataListCount; i++)
			{
				DEBUG_LOG(HLE,"Name : %s",saveNameListData[i]);

				std::string fileDataPath = savePath+GetGameName(param)+saveNameListData[i]+"/"+param->fileName;
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
						saveDataList[realCount].size = 0;
						saveDataList[realCount].saveName = saveNameListData[i];
						saveDataList[realCount].idx = i;
						saveDataList[realCount].textureData = 0;

						if(Memory::IsValidAddress(param->newData))
						{
							// We have a png to show
							if(!noSaveIcon)
							{
								noSaveIcon = new SaveFileInfo();
								PspUtilitySavedataFileData newData;
								Memory::ReadStruct(param->newData, &newData);
								CreatePNGIcon(Memory::GetPointer(newData.buf), (int)newData.size, *noSaveIcon);
							}
							saveDataList[realCount].textureData = noSaveIcon->textureData;
							saveDataList[realCount].textureWidth = noSaveIcon->textureWidth;
							saveDataList[realCount].textureHeight = noSaveIcon->textureHeight;
						}
						DEBUG_LOG(HLE,"Don't Exist");
						realCount++;
					}
				}
			}
			saveNameListDataCount = realCount;
		}
	}
	if(!hasMultipleFileName) // Load info on only save
	{
		saveNameListData = 0;

		Clear();
		saveDataList = new SaveFileInfo[1];
		saveDataListCount = 1;

		// get and stock file info for each file
		DEBUG_LOG(HLE,"Name : %s",GetSaveName(param).c_str());

		std::string fileDataPath = savePath+GetGameName(param)+GetSaveName(param)+"/"+param->fileName;
		PSPFileInfo info = pspFileSystem.GetFileInfo(fileDataPath);
		if (info.exists)
		{
			SetFileInfo(0, info, GetSaveName(pspParam));

			DEBUG_LOG(HLE,"%s Exist",fileDataPath.c_str());
			saveNameListDataCount = 1;
		}
		else
		{
			if (listEmptyFile)
			{
				saveDataList[0].size = 0;
				saveDataList[0].saveName = GetSaveName(param);
				saveDataList[0].idx = 0;
				saveDataList[0].textureData = 0;

				if(Memory::IsValidAddress(param->newData))
				{
					// We have a png to show
					if(!noSaveIcon)
					{
						noSaveIcon = new SaveFileInfo();
						PspUtilitySavedataFileData newData;
						Memory::ReadStruct(param->newData, &newData);
						CreatePNGIcon(Memory::GetPointer(newData.buf), (int)newData.size, *noSaveIcon);
					}
					saveDataList[0].textureData = noSaveIcon->textureData;
					saveDataList[0].textureWidth = noSaveIcon->textureWidth;
					saveDataList[0].textureHeight = noSaveIcon->textureHeight;
				}
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

void SavedataParam::SetFileInfo(int idx, PSPFileInfo &info, std::string saveName)
{
	saveDataList[idx].size = info.size;
	saveDataList[idx].saveName = saveName;
	saveDataList[idx].idx = 0;
	saveDataList[idx].modif_time = info.mtime;

	// Start with a blank slate.
	saveDataList[idx].textureData = 0;
	saveDataList[idx].title[0] = 0;
	saveDataList[idx].saveTitle[0] = 0;
	saveDataList[idx].saveDetail[0] = 0;

	// Search save image icon0
	// TODO : If icon0 don't exist, need to use icon1 which is a moving icon. Also play sound
	std::string fileDataPath2 = savePath + GetGameName(pspParam) + saveName + "/" + icon0Name;
	PSPFileInfo info2 = pspFileSystem.GetFileInfo(fileDataPath2);
	if (info2.exists)
	{
		u8 *textureDataPNG = new u8[(size_t)info2.size];
		ReadPSPFile(fileDataPath2, &textureDataPNG, info2.size, NULL);
		CreatePNGIcon(textureDataPNG, (int)info2.size, saveDataList[idx]);
		delete[] textureDataPNG;
	}

	// Load info in PARAM.SFO
	fileDataPath2 = savePath + GetGameName(pspParam) + saveName + "/" + sfoName;
	info2 = pspFileSystem.GetFileInfo(fileDataPath2);
	if (info2.exists)
	{
		u8 *sfoParam = new u8[(size_t)info2.size];
		ReadPSPFile(fileDataPath2, &sfoParam, info2.size, NULL);
		ParamSFOData sfoFile;
		if (sfoFile.ReadSFO(sfoParam,(size_t)info2.size))
		{
			SetStringFromSFO(sfoFile, "TITLE", saveDataList[idx].title, sizeof(saveDataList[idx].title));
			SetStringFromSFO(sfoFile, "SAVEDATA_TITLE", saveDataList[idx].saveTitle, sizeof(saveDataList[idx].saveTitle));
			SetStringFromSFO(sfoFile, "SAVEDATA_DETAIL", saveDataList[idx].saveDetail, sizeof(saveDataList[idx].saveDetail));
		}
		delete [] sfoParam;
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
	return selectedSave;
}

void SavedataParam::SetSelectedSave(int idx)
{
	selectedSave = idx;
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

bool SavedataParam::IsSaveEncrypted(SceUtilitySavedataParam* param, int saveId)
{

	bool isCrypted = false;

	ParamSFOData sfoFile;
	std::string dirPath = GetSaveFilePath(param, saveId);
	std::string sfopath = dirPath+"/"+sfoName;
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

