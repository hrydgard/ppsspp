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
#include "Common/CommonTypes.h"
#include "Common/Serialize/Serializer.h"
#include "Common/Serialize/SerializeFuncs.h"
#include "Core/ELF/ParamSFO.h"
#include "Core/MemMapHelpers.h"
#include "Core/Reporting.h"
#include "Core/System.h"
#include "Core/FileSystems/MetaFileSystem.h"
#include "Core/Dialog/PSPGamedataInstallDialog.h"
#include "Common/Data/Text/I18n.h"
#include "UI/OnScreenDisplay.h"

std::string saveBasePath = "ms0:/PSP/SAVEDATA/";

// Guesses.
const static int GAMEDATA_INIT_DELAY_US = 200000;
const static int GAMEDATA_SHUTDOWN_DELAY_US = 2000;
const static u32 GAMEDATA_BYTES_PER_READ = 32768;
// TODO: Could adjust based on real-time into frame?  Or eat cycles?
// If this is too high, some games (e.g. Senjou no Valkyria 3) will lag.
const static u32 GAMEDATA_READS_PER_UPDATE = 20;

const u32 PSP_UTILITY_GAMEDATA_MODE_SHOW_PROGRESS = 1;

static const std::string SFO_FILENAME = "PARAM.SFO";

namespace
{
	std::vector<std::string> GetPSPFileList (const std::string &dirpath) {
		std::vector<std::string> FileList;
		auto Fileinfos = pspFileSystem.GetDirListing(dirpath);
		FileList.reserve(Fileinfos.size());

		for (auto it = Fileinfos.begin(); it != Fileinfos.end(); ++it) {
			std::string info = (*it).name;
			FileList.push_back(info);
		}
		return FileList;
	}
}

PSPGamedataInstallDialog::PSPGamedataInstallDialog(UtilityDialogType type) : PSPDialog(type) {
}

PSPGamedataInstallDialog::~PSPGamedataInstallDialog() {
}

int PSPGamedataInstallDialog::Init(u32 paramAddr) {
	if (GetStatus() != SCE_UTILITY_STATUS_NONE) {
		ERROR_LOG_REPORT(Log::sceUtility, "A game install request is already running, not starting a new one");
		return SCE_ERROR_UTILITY_INVALID_STATUS;
	}

	param.ptr = paramAddr;
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
		ERROR_LOG_REPORT(Log::sceUtility, "Game install with no files / data");
		// Getting a lot of reports of this from patched football games. Can probably ignore. https://report.ppsspp.org/logs/kind/793
		return -1;
	}

	int size = Memory::Read_U32(paramAddr);
	if (size != 1424 && size != 1432) {
		ERROR_LOG_REPORT(Log::sceUtility, "sceGamedataInstallInitStart: invalid param size %d", size);
		return SCE_ERROR_UTILITY_INVALID_PARAM_SIZE;
	}

	memset(&request, 0, sizeof(request));
	// Only copy the right size to support different request format
	Memory::Memcpy(&request, paramAddr, size, "sceGamedataInstallInitStart");
	InitCommon();

	ChangeStatusInit(GAMEDATA_INIT_DELAY_US);
	return 0;
}

int PSPGamedataInstallDialog::Update(int animSpeed) {
	if (GetStatus() != SCE_UTILITY_STATUS_RUNNING)
		return SCE_ERROR_UTILITY_INVALID_STATUS;

	if (param->mode >= 2) {
		param->common.result = SCE_ERROR_UTILITY_GAMEDATA_INVALID_MODE;
		param.NotifyWrite("DialogResult");
		ChangeStatus(SCE_UTILITY_STATUS_FINISHED, 0);
		WARN_LOG_REPORT(Log::sceUtility, "sceUtilityGamedataInstallUpdate: invalid mode %d", param->mode);
		return 0;
	}

	UpdateCommon();

	// TODO: param->mode == 1 should show a prompt to confirm, then a progress bar.
	// Any other mode (i.e. 0 or negative) should proceed and show no UI.

	// TODO: This should return error codes in some cases, like write failure.
	// request.common.result must be updated for errors as well.
	
	if (readFiles < numFiles) {
		if (currentInputFile != 0 && currentOutputFile != 0) {
			// Continue copying, this will close once done automatically.
			CopyCurrentFileData();
		} else {
			OpenNextFile();
		}

		UpdateProgress();
	} else {
		WriteSfoFile();

		// TODO: What is this?  Should one of these update per file or anything?
		param->unknownResult1 = readFiles;
		param->unknownResult2 = readFiles;
		param.NotifyWrite("DialogResult");

		ChangeStatus(SCE_UTILITY_STATUS_FINISHED, 0);
	}
	return 0;
}

void PSPGamedataInstallDialog::OpenNextFile() {
	std::string inputFileName = "disc0:/PSP_GAME/INSDIR/" + inFileNames[readFiles];
	std::string outputFileName = GetGameDataInstallFileName(&request, inFileNames[readFiles]);

	currentInputFile = pspFileSystem.OpenFile(inputFileName, FILEACCESS_READ);
	if (currentInputFile < 0) {
		// TODO: Generate an error code?
		ERROR_LOG_REPORT(Log::sceUtility, "Unable to read from install file: %s", inFileNames[readFiles].c_str());
		++readFiles;
		currentInputFile = 0;
		return;
	}
	currentOutputFile = pspFileSystem.OpenFile(outputFileName, (FileAccess)(FILEACCESS_WRITE | FILEACCESS_CREATE | FILEACCESS_TRUNCATE));
	if (currentOutputFile < 0) {
		// TODO: Generate an error code?
		ERROR_LOG(Log::sceUtility, "Unable to write to install file: %s", inFileNames[readFiles].c_str());
		pspFileSystem.CloseFile(currentInputFile);
		currentInputFile = 0;
		currentOutputFile = 0;
		++readFiles;
		return;
	}

	currentInputBytesLeft = (u32)pspFileSystem.GetFileInfo(inputFileName).size;
}

void PSPGamedataInstallDialog::CopyCurrentFileData() {
	u8 buffer[GAMEDATA_BYTES_PER_READ];
	for (u32 i = 0; i < GAMEDATA_READS_PER_UPDATE; ++i) {
		if (currentInputBytesLeft <= 0) {
			break;
		}

		const u32 bytesToRead = std::min(GAMEDATA_BYTES_PER_READ, currentInputBytesLeft);
		size_t readSize = pspFileSystem.ReadFile(currentInputFile, buffer, bytesToRead);
		if (readSize > 0) {
			pspFileSystem.WriteFile(currentOutputFile, buffer, readSize);
			currentInputBytesLeft -= (u32)readSize;
			allReadSize += readSize;
		} else {
			break;
		}
	}

	if (currentInputBytesLeft <= 0) {
		CloseCurrentFile();
	}
}

void PSPGamedataInstallDialog::CloseCurrentFile() {
	if (currentOutputFile >= 0)
		pspFileSystem.CloseFile(currentOutputFile);
	currentOutputFile = 0;

	if (currentInputFile >= 0)
		pspFileSystem.CloseFile(currentInputFile);
	currentInputFile = 0;

	++readFiles;
}

void PSPGamedataInstallDialog::WriteSfoFile() {
	ParamSFOData sfoFile;
	std::string sfopath = GetGameDataInstallFileName(&request, SFO_FILENAME);
	std::vector<u8> sfoFileData;
	if (pspFileSystem.ReadEntireFile(sfopath, sfoFileData) >= 0) {
		sfoFile.ReadSFO(sfoFileData);
	}

	// Update based on the just-saved data.
	sfoFile.SetValue("TITLE", param->sfoParam.title, 128);
	sfoFile.SetValue("SAVEDATA_TITLE", param->sfoParam.savedataTitle, 128);
	sfoFile.SetValue("SAVEDATA_DETAIL", param->sfoParam.detail, 1024);
	sfoFile.SetValue("PARENTAL_LEVEL", param->sfoParam.parentalLevel, 4);
	// TODO: Verify category.
	sfoFile.SetValue("CATEGORY", "MS", 4);
	sfoFile.SetValue("SAVEDATA_DIRECTORY", std::string(param->gameName) + param->dataName, 64);

	// TODO: Maybe there should be other things in the SFO file?  Needs testing.

	u8 *sfoData;
	size_t sfoSize;
	sfoFile.WriteSFO(&sfoData,&sfoSize);

	int handle = pspFileSystem.OpenFile(sfopath, (FileAccess)(FILEACCESS_WRITE | FILEACCESS_CREATE | FILEACCESS_TRUNCATE));
	if (handle >= 0) {
		pspFileSystem.WriteFile(handle, sfoData, sfoSize);
		pspFileSystem.CloseFile(handle);
	}

	delete[] sfoData;
}

int PSPGamedataInstallDialog::Abort() {
	param->common.result = 1;
	param.NotifyWrite("DialogResult");

	// TODO: Delete the files or anything?
	return PSPDialog::Shutdown();
}

int PSPGamedataInstallDialog::Shutdown(bool force) {
	if (GetStatus() != SCE_UTILITY_STATUS_FINISHED && !force)
		return SCE_ERROR_UTILITY_INVALID_STATUS;

	return PSPDialog::Shutdown(force);
}

std::string PSPGamedataInstallDialog::GetGameDataInstallFileName(const SceUtilityGamedataInstallParam *param, const std::string &filename) {
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
		progressValue = (int)((allReadSize * 100) / allFilesSize);
	else 
		progressValue = 100;

	if (param->mode == PSP_UTILITY_GAMEDATA_MODE_SHOW_PROGRESS) {
		RenderProgress(progressValue);
	}

	param->progress = progressValue;
	param.NotifyWrite("DialogResult");
}

void PSPGamedataInstallDialog::RenderProgress(int percentage) {
	StartDraw();

	float barWidth = 380;
	float barX = (480 - barWidth) / 2;
	float barWidthDone = barWidth * percentage / 100;
	float barH = 10.0;
	float barY = 272 / 2 - barH / 2;

	PPGeDrawRect(barX - 3, barY - 3, barX + barWidth + 3, barY + barH + 3, 0x30000000);
	PPGeDrawRect(barX, barY, barX + barWidth, barY + barH, 0xFF707070);
	PPGeDrawRect(barX, barY, barX + barWidthDone, barY + barH, 0xFFE0E0E0);

	auto di = GetI18NCategory(I18NCat::DIALOG);

	fadeValue = 255;
	PPGeStyle textStyle = FadedStyle(PPGeAlign::BOX_HCENTER, 0.6f);

	PPGeDrawText(di->T("Installing..."), 480 / 2, barY + barH + 10, textStyle);

	EndDraw();
}

void PSPGamedataInstallDialog::DoState(PointerWrap &p) {
	auto s = p.Section("PSPGamedataInstallDialog", 0, 4);
	if (!s)
		return;

	// This was included in version 1 and higher.
	PSPDialog::DoState(p);
	Do(p, request);

	// This was included in version 2 and higher, but for BC reasons we use 3+.
	if (s >= 3) {
		Do(p, param.ptr);
		Do(p, inFileNames);
		Do(p, numFiles);
		Do(p, readFiles);
		Do(p, allFilesSize);
		Do(p, allReadSize);
		Do(p, progressValue);
	} else {
		param.ptr = 0;
	}

	if (s >= 4) {
		Do(p, currentInputFile);
		Do(p, currentInputBytesLeft);
		Do(p, currentOutputFile);
	} else {
		currentInputFile = 0;
		currentInputBytesLeft = 0;
		currentOutputFile = 0;
	}
}
