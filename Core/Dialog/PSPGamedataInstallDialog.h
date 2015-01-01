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

struct SceUtilityGamedataInstallParam {
	pspUtilityDialogCommon common;
	u32_le unknown1;
	char gameName[13];
	char ignore1[3];
	char dataName[20];
	char gamedataParamsGameTitle[128];
	char gamedataParamsDataTitle[128];
	char gamedataParamsData[1024];
	u8 unknown2;
	char ignore2[3];
	char progress[4]; // This is progress value,should be updated.
	u32_le unknownResult1;
	u32_le unknownResult2;
	char ignore3[48];
};

class PSPGamedataInstallDialog: public PSPDialog {
public:
	PSPGamedataInstallDialog();
	virtual ~PSPGamedataInstallDialog();

	virtual int Init(u32 paramAddr);
	virtual int Update(int animSpeed) override;
	virtual int Shutdown(bool force = false) override;
	virtual void DoState(PointerWrap &p) override;

	int Abort();
	std::string GetGameDataInstallFileName(SceUtilityGamedataInstallParam *param, std::string filename);

private:
	SceUtilityGamedataInstallParam request;
	u32 paramAddr;
	std::vector<std::string> inFileNames;
	int numFiles;
	int readFiles;
	u64 allFilesSize;  // use this to calculate progress value.
	u64 allReadSize;   // use this to calculate progress value.
	int progressValue;

	void updateProgress();
};
