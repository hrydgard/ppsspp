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

#pragma once

#include "Core/Dialog/PSPDialog.h"
#include "Core/Dialog/SavedataParam.h"

struct SceUtilityGamedataInstallParam {
	pspUtilityDialogCommon common;
	s32_le mode;
	char gameName[13];
	char ignore1[3];
	char dataName[20];
	PspUtilitySavedataSFOParam sfoParam;
	int progress;
	u32_le unknownResult1;
	u32_le unknownResult2;
	char ignore3[48];
};

class PSPGamedataInstallDialog: public PSPDialog {
public:
	PSPGamedataInstallDialog(UtilityDialogType type);
	~PSPGamedataInstallDialog();

	int Init(u32 paramAddr);
	int Update(int animSpeed) override;
	int Shutdown(bool force = false) override;
	void DoState(PointerWrap &p) override;

	int Abort();
	std::string GetGameDataInstallFileName(const SceUtilityGamedataInstallParam *param, const std::string &filename);

protected:
	// TODO: Manage status correctly.
	bool UseAutoStatus() override {
		return true;
	}

private:
	void UpdateProgress();
	void RenderProgress(int percentage);
	void OpenNextFile();
	void CopyCurrentFileData();
	void CloseCurrentFile();
	void WriteSfoFile();

	SceUtilityGamedataInstallParam request{};
	PSPPointer<SceUtilityGamedataInstallParam> param;
	std::vector<std::string> inFileNames;
	int numFiles = 0;
	int readFiles = 0;
	u64 allFilesSize = 0;  // use this to calculate progress value.
	u64 allReadSize = 0;   // use this to calculate progress value.
	int progressValue = 0;

	int currentInputFile = 0;
	u32 currentInputBytesLeft = 0;
	int currentOutputFile = 0;
};
