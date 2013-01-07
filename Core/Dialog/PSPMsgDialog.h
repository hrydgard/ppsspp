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

#include "PSPDialog.h"

#define SCE_UTILITY_MSGDIALOG_OPTION_ERROR				0 // Do nothing
#define SCE_UTILITY_MSGDIALOG_OPTION_TEXT				0x00000001
#define SCE_UTILITY_MSGDIALOG_OPTION_YESNO				0x00000010
#define SCE_UTILITY_MSGDIALOG_OPTION_OK					0x00000020
#define SCE_UTILITY_MSGDIALOG_OPTION_DEFAULT_NO			0x00000100

#define SCE_UTILITY_MSGDIALOG_DEBUG_OPTION_CODED		0x00000131 // OR of all options coded to display warning

struct pspMessageDialog
{
	pspUtilityDialogCommon common;
	int result;
	int type;
	unsigned int errorNum;
	char string[512];
	unsigned int options;
	unsigned int buttonPressed;	// 0=?, 1=Yes/OK, 2=No, 3=Back
};


class PSPMsgDialog: public PSPDialog {
public:
	PSPMsgDialog();
	virtual ~PSPMsgDialog();

	virtual int Init(unsigned int paramAddr);
	virtual int Update();
	virtual int Shutdown();
	virtual void DoState(PointerWrap &p);

private :
	void DisplayBack();
	void DisplayYesNo();
	void DisplayEnterBack();

	enum DisplayState
	{
		DS_NONE,

		DS_MESSAGE,
		DS_ERROR,
		DS_YESNO,
		DS_OK
	};

	DisplayState display;

	pspMessageDialog messageDialog;
	int messageDialogAddr;

	int yesnoChoice;

	int okButtonImg;
	int cancelButtonImg;
	int okButtonFlag;
	int cancelButtonFlag;
};

