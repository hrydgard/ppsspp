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

#include "PSPGamedataInstallDialog.h"
#include "ChunkFile.h"
#include "../Core/MemMap.h"

std::string saveBasePath = "ms0:/PSP/SAVEDATA/";

namespace
{
	bool ReadPSPFile(std::string filename, u8 **data, s64 *readSize, s64 dataSize = -1 ) // we should read whole file here.
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

	bool WritePSPFile(std::string filename, u8 *data, u32 dataSize)
	{
		u32 handle = pspFileSystem.OpenFile(filename, (FileAccess)(FILEACCESS_WRITE | FILEACCESS_CREATE));
		if (handle == 0)
			return false;

		size_t result = pspFileSystem.WriteFile(handle, data, dataSize);
		pspFileSystem.CloseFile(handle);

		return result != 0;
	}

	std::vector<std::string> GetPSPFileList (std::string dirpath) {
		std::vector<std::string> FileList;
		auto Fileinfos = pspFileSystem.GetDirListing(dirpath);
		std::string info;

		for (auto it = Fileinfos.begin(); it != Fileinfos.end(); ++it) {
			std::string info = (*it).name;
			FileList.push_back(info);
		}
		return FileList;
	}
}

PSPGamedataInstallDialog::PSPGamedataInstallDialog() {
}

PSPGamedataInstallDialog::~PSPGamedataInstallDialog() {
}

int PSPGamedataInstallDialog::Init(u32 paramAddr) {
	this->paramAddr = paramAddr;
	// Already running
	if (status != SCE_UTILITY_STATUS_NONE && status != SCE_UTILITY_STATUS_SHUTDOWN)
		return SCE_ERROR_UTILITY_INVALID_STATUS;

	int size = Memory::Read_U32(paramAddr);
	memset(&request, 0, sizeof(request));
	// Only copy the right size to support different request format
	Memory::Memcpy(&request, paramAddr, size);

	status = SCE_UTILITY_STATUS_INITIALIZE;
	return 0;
}

int PSPGamedataInstallDialog::Update() {
	auto inFileNames = GetPSPFileList ("disc0:/PSP_GAME/INSDIR");
	std::string fullinFileName;
	std::string desFileName;
	u8 *temp;
	s64 readSize;

	if (status == SCE_UTILITY_STATUS_INITIALIZE){
		status = SCE_UTILITY_STATUS_RUNNING;
	} else if (status == SCE_UTILITY_STATUS_RUNNING) {
		for (auto it = inFileNames.begin(); it != inFileNames.end();++it) {
			fullinFileName = "disc0:/PSP_GAME/INSDIR/" + (*it);
			desFileName = GetGameDataInstallFileName(&request, *it);
			if (!ReadPSPFile(fullinFileName, &temp, &readSize)) {
				delete temp;
				continue;
			}
			if (!WritePSPFile(desFileName,temp, pspFileSystem.GetFileInfo(fullinFileName).size)) {
				delete temp;
				continue;
			}
			delete temp;
		}
	} else if (status == SCE_UTILITY_STATUS_FINISHED) {
		status = SCE_UTILITY_STATUS_SHUTDOWN;
	}
	//What is this?
	request.unknownResult1 = inFileNames.size();
	request.unknownResult2 = inFileNames.size();
	Memory::WriteStruct(paramAddr,&request);

	status = SCE_UTILITY_STATUS_FINISHED;
	return 0;
}

int PSPGamedataInstallDialog::Abort() {
	return PSPDialog::Shutdown();
}

int PSPGamedataInstallDialog::Shutdown(bool force) {
	if (status != SCE_UTILITY_STATUS_FINISHED && !force)
		return SCE_ERROR_UTILITY_INVALID_STATUS;

	return PSPDialog::Shutdown();
}

std::string PSPGamedataInstallDialog::GetGameDataInstallFileName(SceUtilityGamedataInstallParam *param, std::string filename){
	if (!param)
		return "";
	std::string GameDataInstallPath = saveBasePath + param->gameName + param->dataName + "/";
	if (!pspFileSystem.GetFileInfo(GameDataInstallPath).exists)
		pspFileSystem.MkDir(GameDataInstallPath);

	return GameDataInstallPath + filename;
}

void PSPGamedataInstallDialog::DoState(PointerWrap &p) {
	auto s = p.Section("PSPGamedataInstallDialog", 0, 1);
	if (!s)
		return;

	PSPDialog::DoState(p);
	p.Do(request);
}
