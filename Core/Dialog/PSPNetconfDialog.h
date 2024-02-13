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

struct SceUtilityNetconfData {
	char groupName[8];
	int timeout;
};

struct SceUtilityNetconfParam {
	pspUtilityDialogCommon common;
	int netAction;				// sets how to connect
	PSPPointer<SceUtilityNetconfData> NetconfData;
	int netHotspot;				// Flag to allow hotspot connections
	int netHotspotConnected;	// Flag to check if a hotspot connection is active
	int netWifiSpot;			// Flag to allow WIFI connections
};


class PSPNetconfDialog: public PSPDialog {
public:
	PSPNetconfDialog(UtilityDialogType type);
	~PSPNetconfDialog();

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

	SceUtilityNetconfParam request = {};
	u32 requestAddr = 0;
	int connResult = -1;
	bool hideNotice = false;

	int yesnoChoice = 0;
	float scrollPos_ = 0.0f;
	int framesUpHeld_ = 0;
	int framesDownHeld_ = 0;

	u32 scanInfosAddr = 0;
	int scanStep = 0;
	u64 startTime = 0;
};
