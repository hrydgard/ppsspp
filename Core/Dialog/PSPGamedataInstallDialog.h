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
	u32 unknown1;
	char gameName[13];
	char ignore1[3];
	char dataName[20];
	char gamedataParamsGameTitle[128];
	char gamedataParamsDataTitle[128];
	char gamedataParamsData[1024];
	u8 unknown2;
	char ignore2[7];
	u32 unknownResult1;
	u32 unknownResult2;
	char ignore[48];
};

class PSPGamedataInstallDialog: public PSPDialog {
public:
	PSPGamedataInstallDialog();
	virtual ~PSPGamedataInstallDialog();

	virtual int Init(u32 paramAddr);
	//virtual int Update();
	virtual int Shutdown(bool force = false);
	virtual void DoState(PointerWrap &p);

	int Abort();

private:
	SceUtilityGamedataInstallParam request;
};
