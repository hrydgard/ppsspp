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
#define SCE_UTILITY_MSGDIALOG_OPTION_DEFAULT_NO			0x00000100

typedef struct
{
	unsigned int size;	/** Size of the structure */
	int language;		/** Language */
	int buttonSwap;		/** Set to 1 for X/O button swap */
	int graphicsThread;	/** Graphics thread priority */
	int accessThread;	/** Access/fileio thread priority (SceJobThread) */
	int fontThread;		/** Font thread priority (ScePafThread) */
	int soundThread;	/** Sound thread priority */
	int result;			/** Result */
	int reserved[4];	/** Set to 0 */

} pspUtilityDialogCommon;

struct pspMessageDialog
{
	pspUtilityDialogCommon common;
	int result;
	int type;
	unsigned int errorNum;
	char string[512];
	unsigned int options;
	unsigned int buttonPressed;	// 0=?, 1=Yes, 2=No, 3=Back
};


class PSPMsgDialog: public PSPDialog {
public:
	PSPMsgDialog();
	virtual ~PSPMsgDialog();

	virtual void Init(unsigned int paramAddr);
	virtual void Update();
	void Shutdown();

private :
	void DisplayMessage(std::string text);
	void DisplayBack();
	void DisplayYesNo();
	void DisplayEnterBack();

	enum DisplayState
	{
		DS_NONE,

		DS_MESSAGE,
		DS_ERROR,
		DS_YESNO,
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

