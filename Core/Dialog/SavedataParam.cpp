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

std::string icon0Name = "ICON0.PNG";
std::string icon1Name = "ICON1.PNG";
std::string pic1Name = "PIC1.PNG";
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


std::string SavedataParam::GetSaveFilePath(SceUtilitySavedataParam* param, int saveId)
{
	if (!param) {
		return "";
	}

	std::string dirPath = std::string(param->gameName)+param->saveName;
	if(saveId >= 0 && saveNameListDataCount > 0) // if user selection, use it
		dirPath = std::string(param->gameName)+GetFilename(saveId);

	return savePath + dirPath;
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

	std::string filePath = dirPath+"/"+param->fileName;
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

		// TODO SAVE PARAM.SFO
		/*data_ = (u8*)Memory::GetPointer(*((unsigned int*)&param->dataBuf));
		writeDataToFile(false, );*/

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

		// TODO Save SND
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
	if(saveId >= 0) // if user selection, use it
	{
		if(saveDataList[saveId].size == 0) // don't read no existing file
		{
			return false;
		}
	}

	std::string filePath = dirPath+"/"+param->fileName;
	INFO_LOG(HLE,"Loading file with size %u in %s",param->dataBufSize,filePath.c_str());
	u32 handle = pspFileSystem.OpenFile(filePath,FILEACCESS_READ);
	if(!handle)
	{
		ERROR_LOG(HLE,"Error opening file %s",filePath.c_str());
		return false;
	}
	if(!pspFileSystem.ReadFile(handle, data_, param->dataBufSize))
	{
		ERROR_LOG(HLE,"Error reading file %s",filePath.c_str());
		return false;
	}
	return true;
}

void SavedataParam::SetPspParam(SceUtilitySavedataParam* param)
{
	pspParam = param;
	if(!pspParam) return;

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

		if(saveDataList)
			delete[] saveDataList;
		saveDataList = new SaveFileInfo[count];

		// get and stock file info for each file
		int realCount = 0;
		for(int i = 0; i <count; i++)
		{
			DEBUG_LOG(HLE,"Name : %s",saveNameListData[i]);

			std::string fileDataPath = savePath+param->gameName+saveNameListData[i]+"/"+param->fileName;
			PSPFileInfo info = pspFileSystem.GetFileInfo(fileDataPath);
			if(info.exists)
			{
				// TODO : Load PARAM.SFO when saved and save title and save info
				saveDataList[realCount].size = info.size;
				saveDataList[realCount].saveName = saveNameListData[i];
				saveDataList[realCount].idx = i;
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
					DEBUG_LOG(HLE,"Don't Exist");
					realCount++;
				}
			}
		}
		saveNameListDataCount = realCount;
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
char* SavedataParam::GetFilename(int idx)
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

