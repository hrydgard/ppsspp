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

#include "Common/ChunkFile.h"
#include "Core/MemMapHelpers.h"
#include "Core/System.h"
#include "Core/FileSystems/MetaFileSystem.h"
#include "Core/Dialog/PSPGamedataInstallDialog.h"

std::string saveBasePath = "ms0:/PSP/SAVEDATA/";

namespace
{
	std::vector<std::string> GetPSPFileList (std::string dirpath) {
		std::vector<std::string> FileList;
		auto Fileinfos = pspFileSystem.GetDirListing(dirpath);

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
///////////////////////////////////////////////////////
	this->paramAddr = paramAddr;
	inFileNames = GetPSPFileList ("disc0:/PSP_GAME/INSDIR");
	numFiles = (int)inFileNames.size();
	readFiles = 0;
	progressValue = 0;
	allFilesSize = 0;
	allReadSize = 0;
	for (auto it = inFileNames.begin(); it != inFileNames.end(); ++it) {
		allFilesSize += pspFileSystem.GetFileInfo("disc0:/PSP_GAME/INSDIR/" + (*it)).size;
	}
//////////////////////////////////////////////////////
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

int PSPGamedataInstallDialog::Update(int animSpeed) {
	if (status == SCE_UTILITY_STATUS_INITIALIZE){
		status = SCE_UTILITY_STATUS_RUNNING;
	} else if (status == SCE_UTILITY_STATUS_RUNNING) {
		std::string fullinFileName;
		std::string outFileName;
		u64 totalLength;
		u64 restLength;
		u32 bytesToRead = 4096;
		u32 inhandle;
		u32 outhandle;	
		size_t readSize;
	
		if (readFiles < numFiles) {
			u8 *temp = new u8[4096];
			fullinFileName = "disc0:/PSP_GAME/INSDIR/" + inFileNames[readFiles];
			outFileName = GetGameDataInstallFileName(&request, inFileNames[readFiles]);
			totalLength = pspFileSystem.GetFileInfo(fullinFileName).size;
			restLength = totalLength;	
			inhandle = pspFileSystem.OpenFile(fullinFileName, FILEACCESS_READ);
			if (inhandle != 0) {
				outhandle = pspFileSystem.OpenFile(outFileName, (FileAccess)(FILEACCESS_WRITE | FILEACCESS_CREATE | FILEACCESS_TRUNCATE));
				if (outhandle != 0) {
					while (restLength > 0) {
						if (restLength < bytesToRead) 
							bytesToRead = (u32)restLength;
						readSize = pspFileSystem.ReadFile(inhandle, temp, bytesToRead);
						if(readSize > 0) {
							pspFileSystem.WriteFile(outhandle, temp, readSize);
							restLength -= readSize;
							allReadSize += readSize;							
						} else
							break;
					}
					pspFileSystem.CloseFile(outhandle);
				}
				++readFiles;
				pspFileSystem.CloseFile(inhandle);
			}
			updateProgress();
			delete[] temp;
		} else {
			//What is this?
			request.unknownResult1 = readFiles;
			request.unknownResult2 = readFiles;
			Memory::WriteStruct(paramAddr,&request);

			status = SCE_UTILITY_STATUS_FINISHED;
		}
	} else if (status == SCE_UTILITY_STATUS_FINISHED) {
		status = SCE_UTILITY_STATUS_SHUTDOWN;
	}	
	return 0;
}

int PSPGamedataInstallDialog::Abort() {
	return PSPDialog::Shutdown();
}

int PSPGamedataInstallDialog::Shutdown(bool force) {
	if (status != SCE_UTILITY_STATUS_FINISHED && !force)
		return SCE_ERROR_UTILITY_INVALID_STATUS;

	return PSPDialog::Shutdown(force);
}

std::string PSPGamedataInstallDialog::GetGameDataInstallFileName(SceUtilityGamedataInstallParam *param, std::string filename){
	if (!param)
		return "";
	std::string GameDataInstallPath = saveBasePath + param->gameName + param->dataName + "/";
	if (!pspFileSystem.GetFileInfo(GameDataInstallPath).exists)
		pspFileSystem.MkDir(GameDataInstallPath);

	return GameDataInstallPath + filename;
}

void PSPGamedataInstallDialog::updateProgress() {
	// Update progress bar(if there is).
	// progress value is progress[3] << 24 | progress[2] << 16 | progress[1] << 8 | progress[0].
	// We only should update progress[0] here as the max progress value is 100.
	if (allFilesSize != 0)
		progressValue = (int)(allReadSize / allFilesSize) * 100;
	else 
		progressValue = 100;
	request.progress[0] = progressValue;
	Memory::WriteStruct(paramAddr,&request);
}

void PSPGamedataInstallDialog::DoState(PointerWrap &p) {
	auto s = p.Section("PSPGamedataInstallDialog", 0, 2);
	if (!s)
		return;

	// This was included in version 1 and higher.
	PSPDialog::DoState(p);
	p.Do(request);

	// This was included in version 2 and higher.
	if (s > 2) {
		p.Do(paramAddr);
		p.Do(inFileNames);
		p.Do(numFiles);
		p.Do(readFiles);
		p.Do(allFilesSize);
		p.Do(allReadSize);
		p.Do(progressValue);
	} else {
		paramAddr = 0;
	}
}
