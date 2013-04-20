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

#include "PSPMsgDialog.h"
#include "../Util/PPGeDraw.h"
#include "../HLE/sceCtrl.h"
#include "../Core/MemMap.h"
#include "Core/Reporting.h"
#include "ChunkFile.h"
#include "i18n/i18n.h"

PSPMsgDialog::PSPMsgDialog()
	: PSPDialog()
	, flag(0)
{
}

PSPMsgDialog::~PSPMsgDialog() {
}

int PSPMsgDialog::Init(unsigned int paramAddr)
{
	// Ignore if already running
	if (status != SCE_UTILITY_STATUS_NONE && status != SCE_UTILITY_STATUS_SHUTDOWN)
	{
		return 0;
	}

	messageDialogAddr = paramAddr;
	if (!Memory::IsValidAddress(messageDialogAddr))
	{
		return 0;
	}
	int size = Memory::Read_U32(paramAddr);
	memset(&messageDialog,0,sizeof(messageDialog));
	// Only copy the right size to support different request format
	Memory::Memcpy(&messageDialog,paramAddr,size);

	// debug info
	int optionsNotCoded = ((messageDialog.options | SCE_UTILITY_MSGDIALOG_DEBUG_OPTION_CODED) ^ SCE_UTILITY_MSGDIALOG_DEBUG_OPTION_CODED);
	if(optionsNotCoded)
	{
		ERROR_LOG_REPORT(HLE, "PSPMsgDialog options not coded : 0x%08x", optionsNotCoded);
	}

	flag = 0;

	// Check request invalidity
	if(messageDialog.type == 0 && !(messageDialog.errorNum & 0x80000000))
	{
		flag |= DS_ERROR;
		messageDialog.result = SCE_UTILITY_MSGDIALOG_ERROR_ERRORCODEINVALID;
	}
	else if(size == SCE_UTILITY_MSGDIALOG_SIZE_V2 && messageDialog.type == 1)
	{
		unsigned int validOp = SCE_UTILITY_MSGDIALOG_OPTION_TEXT |
				SCE_UTILITY_MSGDIALOG_OPTION_YESNO |
				SCE_UTILITY_MSGDIALOG_OPTION_DEFAULT_NO;
		if((messageDialog.options | validOp) ^ validOp)
		{
			flag |= DS_ERROR;
			messageDialog.result = SCE_UTILITY_MSGDIALOG_ERROR_BADOPTION;
		}
	}
	else if(size == SCE_UTILITY_MSGDIALOG_SIZE_V3)
	{
		if((messageDialog.options & SCE_UTILITY_MSGDIALOG_OPTION_DEFAULT_NO) &&
				!(messageDialog.options & SCE_UTILITY_MSGDIALOG_OPTION_YESNO))
		{
			flag |= DS_ERROR;
			messageDialog.result = SCE_UTILITY_MSGDIALOG_ERROR_BADOPTION;
		}
	}

	if(flag == 0)
	{
		yesnoChoice = 1;
		if(messageDialog.type == 1)
			flag |= DS_MSG;
		if(messageDialog.type == 0)
			flag |= DS_ERRORMSG;
		if((messageDialog.options & SCE_UTILITY_MSGDIALOG_OPTION_YESNO) &&
				((size == SCE_UTILITY_MSGDIALOG_SIZE_V3) ||
						(size == SCE_UTILITY_MSGDIALOG_SIZE_V2 && messageDialog.type == 1)))
			flag |= DS_YESNO;
		if(messageDialog.options & SCE_UTILITY_MSGDIALOG_OPTION_DEFAULT_NO)
		{
			yesnoChoice = 0;
			flag |= DS_DEFNO;
		}
		if((messageDialog.options & SCE_UTILITY_MSGDIALOG_OPTION_OK) && (size == SCE_UTILITY_MSGDIALOG_SIZE_V3))
		{
			yesnoChoice = 1;
			flag |= DS_OK;
		}
		if((flag & DS_YESNO) || (flag & DS_OK))
			flag |= DS_VALIDBUTTON;
		if(!((messageDialog.options & SCE_UTILITY_MSGDIALOG_OPTION_NOCANCEL)  && (size == SCE_UTILITY_MSGDIALOG_SIZE_V3)))
			flag |= DS_CANCELBUTTON;
		if(messageDialog.options & SCE_UTILITY_MSGDIALOG_OPTION_NOSOUND)
			flag |= DS_NOSOUND;
	}

	if (flag & DS_ERRORMSG) {
		snprintf(msgText, 512, "Error code: %08x", messageDialog.errorNum);
	} else {
		strncpy(msgText, messageDialog.string, 512);
	}

	status = SCE_UTILITY_STATUS_INITIALIZE;

	lastButtons = __CtrlPeekButtons();
	StartFade(true);
	return 0;
}

void PSPMsgDialog::DisplayBack()
{
	I18NCategory *m = GetI18NCategory("Dialog");
	PPGeDrawImage(cancelButtonImg, 290, 220, 20, 20, 0, CalcFadedColor(0xFFFFFFFF));
	PPGeDrawText(m->T("Back"), 320, 220, PPGE_ALIGN_LEFT, 0.5f, CalcFadedColor(0xFFFFFFFF));
}

void PSPMsgDialog::DisplayYesNo()
{
	I18NCategory *m = GetI18NCategory("Dialog");
	PPGeDrawText(m->T("Yes"), 200, 150, PPGE_ALIGN_LEFT, 0.55f, CalcFadedColor(yesnoChoice == 1?0xFF0000FF:0xFFFFFFFF));
	PPGeDrawText(m->T("No"), 320, 150, PPGE_ALIGN_LEFT, 0.55f, CalcFadedColor(yesnoChoice == 0?0xFF0000FF:0xFFFFFFFF));

	if (IsButtonPressed(CTRL_LEFT) && yesnoChoice == 0)
	{
		yesnoChoice = 1;
	}
	else if (IsButtonPressed(CTRL_RIGHT) && yesnoChoice == 1)
	{
		yesnoChoice = 0;
	}
}

void PSPMsgDialog::DisplayOk()
{
	I18NCategory *m = GetI18NCategory("Dialog");
	PPGeDrawImage(okButtonImg, 200, 220, 20, 20, 0, CalcFadedColor(0xFFFFFFFF));
	PPGeDrawText(m->T("Enter"), 230, 220, PPGE_ALIGN_LEFT, 0.5f, CalcFadedColor(0xFFFFFFFF));
}

int PSPMsgDialog::Update()
{

	if (status != SCE_UTILITY_STATUS_RUNNING)
	{
		return 0;
	}

	if((flag & DS_ERROR))
	{
		status = SCE_UTILITY_STATUS_FINISHED;
	}
	else
	{
		UpdateFade();

		buttons = __CtrlPeekButtons();

		okButtonImg = I_CIRCLE;
		cancelButtonImg = I_CROSS;
		okButtonFlag = CTRL_CIRCLE;
		cancelButtonFlag = CTRL_CROSS;
		if(messageDialog.common.buttonSwap == 1)
		{
			okButtonImg = I_CROSS;
			cancelButtonImg = I_CIRCLE;
			okButtonFlag = CTRL_CROSS;
			cancelButtonFlag = CTRL_CIRCLE;
		}

		StartDraw();

		if((flag & DS_MSG) || (flag & DS_ERRORMSG))
			DisplayMessage(msgText);

		if(flag & DS_YESNO)
			DisplayYesNo();
		if (flag & (DS_OK | DS_VALIDBUTTON)) 
			DisplayOk();

		if(flag & DS_CANCELBUTTON)
			DisplayBack();

		if (IsButtonPressed(cancelButtonFlag) && (flag & DS_CANCELBUTTON))
		{
			if(messageDialog.common.size == SCE_UTILITY_MSGDIALOG_SIZE_V3 ||
					((messageDialog.common.size == SCE_UTILITY_MSGDIALOG_SIZE_V2) && (flag & DS_YESNO)))
				messageDialog.buttonPressed = 3;
			else
				messageDialog.buttonPressed = 0;
			StartFade(false);
		}
		else if(IsButtonPressed(okButtonFlag) && (flag & DS_VALIDBUTTON))
		{
			if(yesnoChoice == 0)
			{
				messageDialog.buttonPressed = 2;
			}
			else
			{
				messageDialog.buttonPressed = 1;
			}
			StartFade(false);
		}


		EndDraw();

		lastButtons = buttons;
	}

	Memory::Memcpy(messageDialogAddr,&messageDialog,messageDialog.common.size);
	return 0;
}

int PSPMsgDialog::Abort()
{
	// TODO: Probably not exactly the same?
	return PSPDialog::Shutdown();
}

int PSPMsgDialog::Shutdown()
{
	return PSPDialog::Shutdown();
}

void PSPMsgDialog::DoState(PointerWrap &p)
{
	PSPDialog::DoState(p);
	p.Do(flag);
	p.Do(messageDialog);
	p.Do(messageDialogAddr);
	p.DoArray(msgText, sizeof(msgText));
	p.Do(yesnoChoice);
	p.Do(okButtonImg);
	p.Do(cancelButtonImg);
	p.Do(okButtonFlag);
	p.Do(cancelButtonFlag);
	p.DoMarker("PSPMsgDialog");
}
