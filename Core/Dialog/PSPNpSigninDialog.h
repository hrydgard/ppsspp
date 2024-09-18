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
#include "Core/MemMapHelpers.h"


struct SceUtilityNpSigninParam {
	pspUtilityDialogCommon common;
	// Initially all zero? Or is there a possibility for one of these unknown to be a buffer to a packet data if it wasn't null?
	int npSigninStatus;
	int unknown1;
	int unknown2;
	int unknown3;
};


class PSPNpSigninDialog: public PSPDialog {
public:
	PSPNpSigninDialog(UtilityDialogType type);
	~PSPNpSigninDialog();

	int Init(u32 paramAddr);
	int Update(int animSpeed) override;
	int Shutdown(bool force = false) override;
	void DoState(PointerWrap &p) override;
	pspUtilityDialogCommon* GetCommonParam() override;

protected:
	bool UseAutoStatus() override {
		return false;
	}

private:
	void DisplayMessage(std::string_view text1, std::string_view text2a = "", std::string_view text2b = "", std::string_view text3a = "", std::string_view text3b = "", bool hasYesNo = false, bool hasOK = false);
	void DrawBanner();
	void DrawIndicator();
	void DrawLogo();

	SceUtilityNpSigninParam request = {};
	u32 requestAddr = 0;
	//int npSigninResult = -1;

	int yesnoChoice = 0;
	float scrollPos_ = 0.0f;
	int framesUpHeld_ = 0;
	int framesDownHeld_ = 0;

	u64 startTime = 0;
	int step = 0;
};
