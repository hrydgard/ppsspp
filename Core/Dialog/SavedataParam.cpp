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
#include "../System.h"
#include "image/png_load.h"
#include "../HLE/sceKernelMemory.h"
#include "../ELF/ParamSFO.h"

std::string icon0Name = "ICON0.PNG";
std::string icon1Name = "ICON1.PMF";
std::string pic1Name = "PIC1.PNG";
std::string snd0Name = "SND0.AT3";
std::string sfoName = "PARAM.SFO";

std::string savePath = "ms0:/PSP/SAVEDATA/";

SavedataParam::SavedataParam()
	: pspParam(0)
	, selectedSave(0)
	, saveNameListData(0)
	, saveDataList(0)
	, saveNameListDataCount(0)
{

}

void SavedataParam::Init()
{
	if(!pspFileSystem.GetFileInfo(savePath).exists)
	{
		pspFileSystem.MkDir(savePath);
	}
}

std::string SavedataParam::GetSaveDir(SceUtilitySavedataParam* param, int saveId)
{
	if (!param) {
		return "";
	}

	std::string dirPath = GetGameName(param)+GetSaveName(param);
	if(saveId >= 0 && saveNameListDataCount > 0) // if user selection, use it
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
	if(saveId >= 0 && saveNameListDataCount > 0) // if user selection, use it
	{
		if(saveDataList[saveId].size == 0) // don't delete no existing file
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

	u8* data_ = (u8*)Memory::GetPointer(*((unsigned int*)&param->dataBuf));

	std::string dirPath = GetSaveFilePath(param, saveId);

	if(!pspFileSystem.GetFileInfo(dirPath).exists)
		pspFileSystem.MkDir(dirPath);

	std::string filePath = dirPath+"/"+GetFileName(param);
	INFO_LOG(HLE,"Saving file with size %u in %s",param->dataBufSize,filePath.c_str());
	unsigned int handle = pspFileSystem.OpenFile(filePath,(FileAccess)(FILEACCESS_WRITE | FILEACCESS_CREATE));
	if(handle == 0)
	{
		ERROR_LOG(HLE,"Error opening file %s",filePath.c_str());
		return false;
	}
	if(!pspFileSystem.WriteFile(handle, data_, param->dataBufSize))
	{
		pspFileSystem.CloseFile(handle);
		ERROR_LOG(HLE,"Error writing file %s",filePath.c_str());
		return false;
	}
	else
	{
		pspFileSystem.CloseFile(handle);

		// SAVE PARAM.SFO
		ParamSFOData sfoFile;
		sfoFile.SetValue("TITLE",param->sfoParam.title,128);
		sfoFile.SetValue("SAVEDATA_TITLE",param->sfoParam.savedataTitle,128);
		sfoFile.SetValue("SAVEDATA_DETAIL",param->sfoParam.detail,1024);
		sfoFile.SetValue("PARENTAL_LEVEL",param->sfoParam.parentalLevel,4);
		sfoFile.SetValue("CATEGORY","MS",4);
		sfoFile.SetValue("SAVEDATA_DIRECTORY",GetSaveDir(param,saveId),64);
		sfoFile.SetValue("SAVEDATA_FILE_LIST","",3168); // This need to be filed with the save filename and a hash
		sfoFile.SetValue("SAVEDATA_PARAMS","",128); // This need to be filled with a hash of the save file encrypted.
		u8* sfoData;
		size_t sfoSize;
		sfoFile.WriteSFO(&sfoData,&sfoSize);
		std::string sfopath = dirPath+"/"+sfoName;
		handle = pspFileSystem.OpenFile(sfopath,(FileAccess)(FILEACCESS_WRITE | FILEACCESS_CREATE));
		if(handle)
		{
			pspFileSystem.WriteFile(handle, sfoData, sfoSize);
			pspFileSystem.CloseFile(handle);
		}
		delete[] sfoData;

		// SAVE ICON0
		if(param->icon0FileData.buf)
		{
			data_ = (u8*)Memory::GetPointer(*((unsigned int*)&param->icon0FileData.buf));
			std::string icon0path = dirPath+"/"+icon0Name;
			handle = pspFileSystem.OpenFile(icon0path,(FileAccess)(FILEACCESS_WRITE | FILEACCESS_CREATE));
			if(handle)
			{
				pspFileSystem.WriteFile(handle, data_, param->icon0FileData.bufSize);
				pspFileSystem.CloseFile(handle);
			}
		}
		// SAVE ICON1
		if(param->icon1FileData.buf)
		{
			data_ = (u8*)Memory::GetPointer(*((unsigned int*)&param->icon1FileData.buf));
			std::string icon1path = dirPath+"/"+icon1Name;
			handle = pspFileSystem.OpenFile(icon1path,(FileAccess)(FILEACCESS_WRITE | FILEACCESS_CREATE));
			if(handle)
			{
				pspFileSystem.WriteFile(handle, data_, param->icon1FileData.bufSize);
				pspFileSystem.CloseFile(handle);
			}
		}
		// SAVE PIC1
		if(param->pic1FileData.buf)
		{
			data_ = (u8*)Memory::GetPointer(*((unsigned int*)&param->pic1FileData.buf));
			std::string pic1path = dirPath+"/"+pic1Name;
			handle = pspFileSystem.OpenFile(pic1path,(FileAccess)(FILEACCESS_WRITE | FILEACCESS_CREATE));
			if(handle)
			{
				pspFileSystem.WriteFile(handle, data_, param->pic1FileData.bufSize);
				pspFileSystem.CloseFile(handle);
			}
		}

		// Save SND
		if(param->snd0FileData.buf)
		{
			data_ = (u8*)Memory::GetPointer(*((unsigned int*)&param->snd0FileData.buf));
			std::string snd0path = dirPath+"/"+snd0Name;
			handle = pspFileSystem.OpenFile(snd0path,(FileAccess)(FILEACCESS_WRITE | FILEACCESS_CREATE));
			if(handle)
			{
				pspFileSystem.WriteFile(handle, data_, param->snd0FileData.bufSize);
				pspFileSystem.CloseFile(handle);
			}
		}
	}
	return true;
}

bool SavedataParam::Load(SceUtilitySavedataParam* param, int saveId)
{
	if (!param) {
		return false;
	}

	u8* data_ = (u8*)Memory::GetPointer(*((unsigned int*)&param->dataBuf));

	std::string dirPath = GetSaveFilePath(param, saveId);
	if(saveId >= 0 && saveNameListDataCount > 0) // if user selection, use it
	{
		if(saveDataList[saveId].size == 0) // don't read no existing file
		{
			return false;
		}
	}

	std::string filePath = dirPath+"/"+GetFileName(param);
	INFO_LOG(HLE,"Loading file with size %u in %s",param->dataBufSize,filePath.c_str());
	u32 handle = pspFileSystem.OpenFile(filePath,FILEACCESS_READ);
	if(!handle)
	{
		ERROR_LOG(HLE,"Error opening file %s",filePath.c_str());
		return false;
	}
	if(!pspFileSystem.ReadFile(handle, data_, param->dataBufSize))
	{
		pspFileSystem.CloseFile(handle);
		ERROR_LOG(HLE,"Error reading file %s",filePath.c_str());
		return false;
	}
	pspFileSystem.CloseFile(handle);
	return true;
}

bool SavedataParam::GetSizes(SceUtilitySavedataParam* param)
{
	if (!param) {
		return false;
	}

	if(Memory::IsValidAddress(param->msFree))
	{
		Memory::Write_U32(32768,param->msFree);
		Memory::Write_U32(32768,param->msFree+4);
		Memory::Write_U32(1048576,param->msFree+8);
		Memory::Write_U8(0,param->msFree+12);
	}
	if(Memory::IsValidAddress(param->msData))
	{
		Memory::Write_U32(0,param->msData+36);
		Memory::Write_U32(0,param->msData+40);
		Memory::Write_U8(0,param->msData+44);
		Memory::Write_U32(0,param->msData+52);
		Memory::Write_U8(0,param->msData+56);
	}
	if(Memory::IsValidAddress(param->utilityData))
	{
		Memory::Write_U32(13,param->utilityData);
		Memory::Write_U32(416,param->utilityData+4);
		Memory::Write_U8(0,param->utilityData+8);
		Memory::Write_U32(416,param->utilityData+16);
		Memory::Write_U8(0,param->utilityData+20);
	}
	return true;

}

bool SavedataParam::GetList(SceUtilitySavedataParam* param)
{
	if (!param) {
		return false;
	}

	if(Memory::IsValidAddress(param->idListAddr))
	{
		Memory::Write_U32(0,param->idListAddr+4);
	}
	return true;
}

void SavedataParam::Clear()
{
	if(saveDataList)
	{
		for(int i = 0; i < saveNameListDataCount; i++)
		{
			if(saveDataList[i].textureData != 0)
				kernelMemory.Free(saveDataList[i].textureData);
			saveDataList[i].textureData = 0;
		}

		delete[] saveDataList;
		saveDataList = 0;
	}
}

void SavedataParam::SetPspParam(SceUtilitySavedataParam* param)
{
	pspParam = param;
	if(!pspParam)
	{
		Clear();
		return;
	}

	bool listEmptyFile = true;
	if(param->mode == SCE_UTILITY_SAVEDATA_TYPE_LISTLOAD ||
			param->mode == SCE_UTILITY_SAVEDATA_TYPE_LISTDELETE)
	{
		listEmptyFile = false;
	}

	if(param->saveNameList != 0)
	{
		saveNameListData = (char(*)[20])Memory::GetPointer(param->saveNameList);

		// Get number of fileName in array
		int count = 0;
		do
		{
			count++;
		} while(saveNameListData[count][0] != 0);

		Clear();
		saveDataList = new SaveFileInfo[count];

		// get and stock file info for each file
		int realCount = 0;
		for(int i = 0; i <count; i++)
		{
			DEBUG_LOG(HLE,"Name : %s",saveNameListData[i]);

			std::string fileDataPath = savePath+GetGameName(param)+saveNameListData[i]+"/"+param->fileName;
			PSPFileInfo info = pspFileSystem.GetFileInfo(fileDataPath);
			if(info.exists)
			{
				saveDataList[realCount].size = info.size;
				saveDataList[realCount].saveName = saveNameListData[i];
				saveDataList[realCount].idx = i;
				saveDataList[realCount].modif_time = info.mtime;

				// Search save image icon0
				// TODO : If icon0 don't exist, need to use icon1 which is a moving icon. Also play sound
				std::string fileDataPath2 = savePath+GetGameName(param)+saveNameListData[i]+"/"+icon0Name;
				PSPFileInfo info2 = pspFileSystem.GetFileInfo(fileDataPath2);
				if(info2.exists)
				{
					u8* textureDataPNG = new u8[info2.size];
					int handle = pspFileSystem.OpenFile(fileDataPath2,FILEACCESS_READ);
					pspFileSystem.ReadFile(handle,textureDataPNG,info2.size);
					pspFileSystem.CloseFile(handle);
					unsigned char* textureData;
					int w,h;
					pngLoadPtr(textureDataPNG, info2.size, &w, &h, &textureData, false);
					delete[] textureDataPNG;
					u32 texSize = w*h*4;
					u32 atlasPtr = kernelMemory.Alloc(texSize, true, "SaveData Icon");
					saveDataList[realCount].textureData = atlasPtr;
					Memory::Memcpy(atlasPtr, textureData, texSize);
					free(textureData);
					saveDataList[realCount].textureWidth = w;
					saveDataList[realCount].textureHeight = h;
				}
				else
				{
					saveDataList[realCount].textureData = 0;
				}

				// Load info in PARAM.SFO
				fileDataPath2 = savePath+GetGameName(param)+saveNameListData[i]+"/"+sfoName;
				info2 = pspFileSystem.GetFileInfo(fileDataPath2);
				if(info2.exists)
				{
					u8* sfoParam = new u8[info2.size];
					int handle = pspFileSystem.OpenFile(fileDataPath2,FILEACCESS_READ);
					pspFileSystem.ReadFile(handle,sfoParam,info2.size);
					pspFileSystem.CloseFile(handle);
					ParamSFOData sfoFile;
					if(sfoFile.ReadSFO(sfoParam,info2.size))
					{
						std::string title = sfoFile.GetValueString("TITLE");
						memcpy(saveDataList[realCount].title,title.c_str(),title.size());
						saveDataList[realCount].title[title.size()] = 0;

						std::string savetitle = sfoFile.GetValueString("SAVEDATA_TITLE");
						memcpy(saveDataList[realCount].saveTitle,savetitle.c_str(),savetitle.size());
						saveDataList[realCount].saveTitle[savetitle.size()] = 0;

						std::string savedetail = sfoFile.GetValueString("SAVEDATA_DETAIL");
						memcpy(saveDataList[realCount].saveDetail,savedetail.c_str(),savedetail.size());
						saveDataList[realCount].saveDetail[savedetail.size()] = 0;
					}
					delete sfoParam;
				}

				DEBUG_LOG(HLE,"%s Exist",fileDataPath.c_str());
				realCount++;
			}
			else
			{
				if(listEmptyFile)
				{
					saveDataList[realCount].size = 0;
					saveDataList[realCount].saveName = saveNameListData[i];
					saveDataList[realCount].idx = i;
					saveDataList[realCount].textureData = 0;
					DEBUG_LOG(HLE,"Don't Exist");
					realCount++;
				}
			}
		}
		saveNameListDataCount = realCount;
	}
	else // Load info on only save
	{
		saveNameListData == 0;

		Clear();
		saveDataList = new SaveFileInfo[1];

		// get and stock file info for each file
		DEBUG_LOG(HLE,"Name : %s",GetSaveName(param).c_str());

		std::string fileDataPath = savePath+GetGameName(param)+GetSaveName(param)+"/"+param->fileName;
		PSPFileInfo info = pspFileSystem.GetFileInfo(fileDataPath);
		if(info.exists)
		{
			saveDataList[0].size = info.size;
			saveDataList[0].saveName = GetSaveName(param);
			saveDataList[0].idx = 0;
			saveDataList[0].modif_time = info.mtime;

			// Search save image icon0
			// TODO : If icon0 don't exist, need to use icon1 which is a moving icon. Also play sound
			std::string fileDataPath2 = savePath+GetGameName(param)+GetSaveName(param)+"/"+icon0Name;
			PSPFileInfo info2 = pspFileSystem.GetFileInfo(fileDataPath2);
			if(info2.exists)
			{
				u8* textureDataPNG = new u8[info2.size];
				int handle = pspFileSystem.OpenFile(fileDataPath2,FILEACCESS_READ);
				pspFileSystem.ReadFile(handle,textureDataPNG,info2.size);
				pspFileSystem.CloseFile(handle);
				unsigned char* textureData;
				int w,h;
				pngLoadPtr(textureDataPNG, info2.size, &w, &h, &textureData, false);
				delete[] textureDataPNG;
				u32 texSize = w*h*4;
				u32 atlasPtr = kernelMemory.Alloc(texSize, true, "SaveData Icon");
				saveDataList[0].textureData = atlasPtr;
				Memory::Memcpy(atlasPtr, textureData, texSize);
				free(textureData);
				saveDataList[0].textureWidth = w;
				saveDataList[0].textureHeight = h;
			}
			else
			{
				saveDataList[0].textureData = 0;
			}

			// Load info in PARAM.SFO
			fileDataPath2 = savePath+GetGameName(param)+GetSaveName(param)+"/"+sfoName;
			info2 = pspFileSystem.GetFileInfo(fileDataPath2);
			if(info2.exists)
			{
				u8* sfoParam = new u8[info2.size];
				int handle = pspFileSystem.OpenFile(fileDataPath2,FILEACCESS_READ);
				pspFileSystem.ReadFile(handle,sfoParam,info2.size);
				pspFileSystem.CloseFile(handle);
				ParamSFOData sfoFile;
				if(sfoFile.ReadSFO(sfoParam,info2.size))
				{
					std::string title = sfoFile.GetValueString("TITLE");
					memcpy(saveDataList[0].title,title.c_str(),title.size());
					saveDataList[0].title[title.size()] = 0;

					std::string savetitle = sfoFile.GetValueString("SAVEDATA_TITLE");
					memcpy(saveDataList[0].saveTitle,savetitle.c_str(),savetitle.size());
					saveDataList[0].saveTitle[savetitle.size()] = 0;

					std::string savedetail = sfoFile.GetValueString("SAVEDATA_DETAIL");
					memcpy(saveDataList[0].saveDetail,savedetail.c_str(),savedetail.size());
					saveDataList[0].saveDetail[savedetail.size()] = 0;
				}
				delete sfoParam;
			}

			DEBUG_LOG(HLE,"%s Exist",fileDataPath.c_str());
			saveNameListDataCount = 1;
		}
		else
		{
			if(listEmptyFile)
			{
				saveDataList[0].size = 0;
				saveDataList[0].saveName = GetSaveName(param);
				saveDataList[0].idx = 0;
				saveDataList[0].textureData = 0;
				DEBUG_LOG(HLE,"Don't Exist");
			}
			saveNameListDataCount = 0;
		}
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

