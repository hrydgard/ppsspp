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

#include <string>

#include "Common/Swap.h"
#include "Core/Dialog/PSPDialog.h"

#define SCE_UTILITY_MSGDIALOG_OPTION_ERRORSOUND         0x00000000
#define SCE_UTILITY_MSGDIALOG_OPTION_TEXTSOUND          0x00000001
#define SCE_UTILITY_MSGDIALOG_OPTION_NOSOUND            0x00000002
#define SCE_UTILITY_MSGDIALOG_OPTION_YESNO              0x00000010
#define SCE_UTILITY_MSGDIALOG_OPTION_OK                 0x00000020
#define SCE_UTILITY_MSGDIALOG_OPTION_NOCANCEL           0x00000080
#define SCE_UTILITY_MSGDIALOG_OPTION_DEFAULT_NO         0x00000100

#define SCE_UTILITY_MSGDIALOG_SIZE_V1                   572
#define SCE_UTILITY_MSGDIALOG_SIZE_V2                   580
#define SCE_UTILITY_MSGDIALOG_SIZE_V3                   708

#define SCE_UTILITY_MSGDIALOG_OPTION_SUPPORTED          0x000001B3 // OR of all options coded to display warning

#define SCE_UTILITY_MSGDIALOG_ERROR_BADOPTION           0x80110501
#define SCE_UTILITY_MSGDIALOG_ERROR_ERRORCODEINVALID    0x80110502

struct pspMessageDialog
{
	pspUtilityDialogCommon common;
	s32_le result;
	s32_le type;
	u32_le errorNum;
	char string[512];
	// End of request V1 (Size 572)
	u32_le options;
	u32_le buttonPressed;
	// End of request V2 (Size 580)
	char okayButton[64];
	char cancelButton[64];
	// End of request V3 (Size 708)
};


class PSPMsgDialog: public PSPDialog {
public:
	PSPMsgDialog(UtilityDialogType type);
	~PSPMsgDialog();

	int Init(unsigned int paramAddr);
	int Update(int animSpeed) override;
	int Shutdown(bool force = false) override;
	void DoState(PointerWrap &p) override;
	pspUtilityDialogCommon *GetCommonParam() override;

	int Abort();

protected:
	bool UseAutoStatus() override {
		return false;
	}

private:
	void FormatErrorCode(uint32_t code);
	void DisplayMessage(const std::string &text, bool hasYesNo = false, bool hasOK = false);

	enum Flags
	{
		DS_MSG          = 0x1,
		DS_ERRORMSG     = 0x2,
		DS_YESNO        = 0x4,
		DS_DEFNO        = 0x8,
		DS_OK           = 0x10,
		DS_VALIDBUTTON  = 0x20,
		DS_CANCELBUTTON = 0x40,
		DS_NOSOUND      = 0x80,
		DS_ERROR        = 0x100,
		DS_ABORT        = 0x200,
	};

	u32 flag = 0;

	pspMessageDialog messageDialog{};
	int messageDialogAddr = 0;

	char msgText[512];
	int yesnoChoice = 0;
	float scrollPos_ = 0.0f;
	int framesUpHeld_ = 0;
	int framesDownHeld_ = 0;
};

