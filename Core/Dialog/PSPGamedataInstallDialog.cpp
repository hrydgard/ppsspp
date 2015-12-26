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

#include "Common/Common.h"
#include "Common/ChunkFile.h"
#include "Core/MemMapHelpers.h"
#include "Core/Reporting.h"
#include "Core/System.h"
#include "Core/FileSystems/MetaFileSystem.h"
#include "Core/Dialog/PSPGamedataInstallDialog.h"

std::string saveBasePath = "ms0:/PSP/SAVEDATA/";

// Guesses.
const static int GAMEDATA_INIT_DELAY_US = 200000;
const static int GAMEDATA_SHUTDOWN_DELAY_US = 2000;

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
	if (GetStatus() != SCE_UTILITY_STATUS_NONE) {
		ERROR_LOG_REPORT(SCEUTILITY, "A game install request is already running, not starting a new one");
		return SCE_ERROR_UTILITY_INVALID_STATUS;
	}

	this->paramAddr = paramAddr;
	inFileNames = GetPSPFileList("disc0:/PSP_GAME/INSDIR");
	numFiles = (int)inFileNames.size();
	readFiles = 0;
	progressValue = 0;
	allFilesSize = 0;
	allReadSize = 0;
	currentInputFile = 0;
	currentOutputFile = 0;

	for (std::string filename : inFileNames) {
		allFilesSize += pspFileSystem.GetFileInfo("disc0:/PSP_GAME/INSDIR/" + filename).size;
	}

	if (allFilesSize == 0) {
		ERROR_LOG_REPORT(SCEUTILITY, "Game install with no files / data");
		// TODO: What happens here?
		return -1;
	}

	int size = Memory::Read_U32(paramAddr);
	memset(&request, 0, sizeof(request));
	// Only copy the right size to support different request format
	Memory::Memcpy(&request, paramAddr, size);

	ChangeStatusInit(GAMEDATA_INIT_DELAY_US);
	return 0;
}

int PSPGamedataInstallDialog::Update(int animSpeed) {
	if (GetStatus() != SCE_UTILITY_STATUS_RUNNING)
		return SCE_ERROR_UTILITY_INVALID_STATUS;

	u32 bytesToRead = 4096;
	size_t readSize;
	
	if (readFiles < numFiles) {
		OpenNextFile();

		if (currentInputFile != 0 && currentOutputFile != 0) {
			u8 *temp = new u8[4096];

			while (currentInputBytesLeft > 0) {
				if (currentInputBytesLeft < bytesToRead)
					bytesToRead = currentInputBytesLeft;
				readSize = pspFileSystem.ReadFile(currentInputFile, temp, bytesToRead);
				if (readSize > 0) {
					pspFileSystem.WriteFile(currentOutputFile, temp, readSize);
					currentInputBytesLeft -= readSize;
					allReadSize += readSize;
				} else {
					break;
				}
			}
			pspFileSystem.CloseFile(currentOutputFile);
			currentOutputFile = 0;

			pspFileSystem.CloseFile(currentInputFile);
			currentInputFile = 0;

			delete[] temp;

			++readFiles;
		}

		UpdateProgress();
	} else {
		//What is this?
		request.unknownResult1 = readFiles;
		request.unknownResult2 = readFiles;
		Memory::WriteStruct(paramAddr,&request);

		ChangeStatus(SCE_UTILITY_STATUS_FINISHED, 0);
	}
	return 0;
}

void PSPGamedataInstallDialog::OpenNextFile() {
	std::string inputFileName = "disc0:/PSP_GAME/INSDIR/" + inFileNames[readFiles];
	std::string outputFileName = GetGameDataInstallFileName(&request, inFileNames[readFiles]);

	currentInputFile = pspFileSystem.OpenFile(inputFileName, FILEACCESS_READ);
	if (!currentInputFile) {
		// TODO: Generate an error code?
		ERROR_LOG_REPORT(SCEUTILITY, "Unable to read from install file: %s", inFileNames[readFiles].c_str());
		++readFiles;
		return;
	}
	currentOutputFile = pspFileSystem.OpenFile(outputFileName, (FileAccess)(FILEACCESS_WRITE | FILEACCESS_CREATE | FILEACCESS_TRUNCATE));
	if (!currentOutputFile) {
		// TODO: Generate an error code?
		ERROR_LOG(SCEUTILITY, "Unable to write to install file: %s", inFileNames[readFiles].c_str());
		pspFileSystem.CloseFile(currentInputFile);
		currentInputFile = 0;
		++readFiles;
		return;
	}

	currentInputBytesLeft = (u32)pspFileSystem.GetFileInfo(inputFileName).size;
}

int PSPGamedataInstallDialog::Abort() {
	// TODO: Delete the files or anything?
	return PSPDialog::Shutdown();
}

int PSPGamedataInstallDialog::Shutdown(bool force) {
	if (GetStatus() != SCE_UTILITY_STATUS_FINISHED && !force)
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

void PSPGamedataInstallDialog::UpdateProgress() {
	// Update progress bar(if there is).
	// We only should update progress[0] here as the max progress value is 100.
	if (allFilesSize != 0)
		progressValue = (int)(allReadSize / allFilesSize) * 100;
	else 
		progressValue = 100;
	request.progress = progressValue;
	Memory::WriteStruct(paramAddr, &request);
}

void PSPGamedataInstallDialog::DoState(PointerWrap &p) {
	auto s = p.Section("PSPGamedataInstallDialog", 0, 3);
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

	if (s > 3) {
		p.Do(currentInputFile);
		p.Do(currentInputBytesLeft);
		p.Do(currentOutputFile);
	} else {
		currentInputFile = 0;
		currentInputBytesLeft = 0;
		currentOutputFile = 0;
	}
}
