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

	bool ReadPSPFile(std::string filename, u8 *data, s64 dataSize, s64 *readSize)
	{
		u32 handle = pspFileSystem.OpenFile(filename, FILEACCESS_READ);
		if (handle == 0)
			return false;

		int result = pspFileSystem.ReadFile(handle, data, dataSize);
		pspFileSystem.CloseFile(handle);
		if(readSize)
			*readSize = result;

		return result != 0;
	}

	bool WritePSPFile(std::string filename, u8 *data, int dataSize)
	{
		u32 handle = pspFileSystem.OpenFile(filename, (FileAccess)(FILEACCESS_WRITE | FILEACCESS_CREATE));
		if (handle == 0)
			return false;

		int result = pspFileSystem.WriteFile(handle, data, dataSize);
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
}

SavedataParam::SavedataParam()
	: pspParam(0)
	, selectedSave(0)
	, saveDataList(0)
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

	if(param->dataBuf != 0)	// Can launch save without save data in mode 13
	{
		std::string filePath = dirPath+"/"+GetFileName(param);
		int saveSize = param->dataSize;
		if(saveSize == 0 || saveSize > param->dataBufSize)
			saveSize = param->dataBufSize; // fallback, should never use this
		INFO_LOG(HLE,"Saving file with size %u in %s",saveSize,filePath.c_str());
		u8 *data_ = (u8*)Memory::GetPointer(param->dataBuf);

		// copy back save name in request
		strncpy(param->saveName,GetSaveDirName(param, saveId).c_str(),20);

		if (!WritePSPFile(filePath, data_, saveSize))
		{
			ERROR_LOG(HLE,"Error writing file %s",filePath.c_str());
			return false;
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
		if(ReadPSPFile(sfopath,sfoData,sfoSize, NULL))
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
				break; // File already in SFO

			fName += FILE_LIST_ITEM_SIZE;
		}

		if (fName + 20 <= (char*)tmpData + FILE_LIST_TOTAL_SIZE)
			snprintf(fName, 20, "%s",GetFileName(param).c_str());
	}
	sfoFile.SetValue("SAVEDATA_FILE_LIST", tmpData, FILE_LIST_TOTAL_SIZE, FILE_LIST_TOTAL_SIZE);
	delete[] tmpData;

	// No crypted save, so fill with 0
	tmpData = new u8[128];
	memset(tmpData, 0, 128);
	sfoFile.SetValue("SAVEDATA_PARAMS", tmpData, 128, 128);
	delete[] tmpData;

	u8 *sfoData;
	size_t sfoSize;
	sfoFile.WriteSFO(&sfoData,&sfoSize);
	WritePSPFile(sfopath, sfoData, sfoSize);
	delete[] sfoData;

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
		int dataSize = sizeof(encryptInfo); // version + key + sdkVersion
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
	if (!ReadPSPFile(filePath, data_, param->dataBufSize, &readSize))
	{
		ERROR_LOG(HLE,"Error reading file %s",filePath.c_str());
		return false;
	}
	param->dataSize = readSize;

	// copy back save name in request
	strncpy(param->saveName,GetSaveDirName(param, saveId).c_str(),20);

	ParamSFOData sfoFile;
	std::string sfopath = dirPath+"/"+sfoName;
	PSPFileInfo sfoInfo = pspFileSystem.GetFileInfo(sfopath);
	if(sfoInfo.exists) // Read sfo
	{
		u8 *sfoData = new u8[(size_t)sfoInfo.size];
		size_t sfoSize = (size_t)sfoInfo.size;
		if(ReadPSPFile(sfopath,sfoData,sfoSize, NULL))
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

	return true;
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
		Memory::Memset(param->msFree+12,0,spaceTxt.size()+1);
		Memory::Memcpy(param->msFree+12,spaceTxt.c_str(),spaceTxt.size()); // Text representing free space
	}
	if (Memory::IsValidAddress(param->msData))
	{
		std::string path = GetSaveFilePath(param,0);
		PSPFileInfo finfo = pspFileSystem.GetFileInfo(path);
		if(finfo.exists)
		{
			// TODO : fill correctly with the total save size
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
		Memory::Memset(param->utilityData+8,0,spaceTxt.size()+1);
		Memory::Memcpy(param->utilityData+8,spaceTxt.c_str(),spaceTxt.size()); // save size in text
		Memory::Write_U32(total_size / 0x400,param->utilityData+16);	// save size in KB
		spaceTxt = SavedataParam::GetSpaceText(total_size);
		Memory::Memset(param->utilityData+20,0,spaceTxt.size()+1);
		Memory::Memcpy(param->utilityData+20,spaceTxt.c_str(),spaceTxt.size()); // save size in text
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
			for (size_t i = 0; i < allDir.size() && i < maxFile; i++)
			{
				std::string dirName = allDir[i].name;
				if(PSPMatch(dirName, searchString))
				{
					validDir.push_back(allDir[i]);
				}
			}

			for (size_t i = 0; i < validDir.size(); i++)
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
				Memory::Memcpy(baseAddr + 52, outName.c_str(), outName.size());
			}
		}
		// Save num of folder found
		Memory::Write_U32(validDir.size(),param->idListAddr+4);
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
			Memory::Write_U32(0x21FF, curFileInfoAddr+0);
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

	// TODO code this

	return false;
}

void SavedataParam::Clear()
{
	if (saveDataList)
	{
		for (int i = 0; i < saveNameListDataCount; i++)
		{
			if (saveDataList[i].textureData != 0)
				kernelMemory.Free(saveDataList[i].textureData);
			saveDataList[i].textureData = 0;
		}

		delete[] saveDataList;
		saveDataList = 0;
		saveDataListCount = 0;
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
							PspUtilitySavedataFileData newData;
							Memory::ReadStruct(param->newData, &newData);
							CreatePNGIcon(Memory::GetPointer(newData.buf),newData.size,saveDataList[realCount]);
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
					PspUtilitySavedataFileData newData;
					Memory::ReadStruct(param->newData, &newData);
					CreatePNGIcon(Memory::GetPointer(newData.buf),newData.size,saveDataList[0]);
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
	if (success && atlasPtr != -1)
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
		ReadPSPFile(fileDataPath2, textureDataPNG, info2.size, NULL);
		CreatePNGIcon(textureDataPNG, info2.size, saveDataList[idx]);
		delete[] textureDataPNG;
	}

	// Load info in PARAM.SFO
	fileDataPath2 = savePath + GetGameName(pspParam) + saveName + "/" + sfoName;
	info2 = pspFileSystem.GetFileInfo(fileDataPath2);
	if (info2.exists)
	{
		u8 *sfoParam = new u8[(size_t)info2.size];
		ReadPSPFile(fileDataPath2, sfoParam, info2.size, NULL);
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
		saveDataList = new SaveFileInfo[saveDataListCount];
	}
	p.DoArray(saveDataList, saveDataListCount);
	p.DoMarker("SavedataParam");
}
