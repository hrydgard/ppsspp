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

PSPMsgDialog::PSPMsgDialog()
	: PSPDialog()
	, display(DS_NONE)
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
	Memory::ReadStruct(messageDialogAddr, &messageDialog);

	// debug info
	int optionsNotCoded = ((messageDialog.options | SCE_UTILITY_MSGDIALOG_DEBUG_OPTION_CODED) ^ SCE_UTILITY_MSGDIALOG_DEBUG_OPTION_CODED);
	if(optionsNotCoded)
	{
		ERROR_LOG(HLE,"PSPMsgDialog options not coded : 0x%08x",optionsNotCoded);
	}

	yesnoChoice = 1;
	if (messageDialog.type == 0) // number
	{
		INFO_LOG(HLE, "MsgDialog: %08x", messageDialog.errorNum);
		display = DS_ERROR;
	}
	else
	{
		INFO_LOG(HLE, "MsgDialog: %s", messageDialog.string);
		display = DS_MESSAGE;
		if(messageDialog.options & SCE_UTILITY_MSGDIALOG_OPTION_YESNO)
			display = DS_YESNO;
		if(messageDialog.options & SCE_UTILITY_MSGDIALOG_OPTION_OK)
			display = DS_OK;
		if(messageDialog.options & SCE_UTILITY_MSGDIALOG_OPTION_DEFAULT_NO)
			yesnoChoice = 0;
	}

	status = SCE_UTILITY_STATUS_INITIALIZE;

	lastButtons = __CtrlPeekButtons();
	return 0;
}

void PSPMsgDialog::DisplayBack()
{
	PPGeDrawImage(cancelButtonImg, 250, 220, 20, 20, 0, 0xFFFFFFFF);
	PPGeDrawText("Back", 270, 220, PPGE_ALIGN_LEFT, 0.5f, 0xFFFFFFFF);
}

void PSPMsgDialog::DisplayYesNo()
{

	PPGeDrawText("Yes", 200, 150, PPGE_ALIGN_LEFT, 0.5f, (yesnoChoice == 1?0xFF0000FF:0xFFFFFFFF));
	PPGeDrawText("No", 320, 150, PPGE_ALIGN_LEFT, 0.5f, (yesnoChoice == 0?0xFF0000FF:0xFFFFFFFF));

	if (IsButtonPressed(CTRL_LEFT) && yesnoChoice == 0)
	{
		yesnoChoice = 1;
	}
	else if (IsButtonPressed(CTRL_RIGHT) && yesnoChoice == 1)
	{
		yesnoChoice = 0;
	}
}
void PSPMsgDialog::DisplayEnterBack()
{
	PPGeDrawImage(okButtonImg, 200, 220, 20, 20, 0, 0xFFFFFFFF);
	PPGeDrawText("Enter", 230, 220, PPGE_ALIGN_LEFT, 0.5f, 0xFFFFFFFF);
	PPGeDrawImage(cancelButtonImg, 290, 220, 20, 20, 0, 0xFFFFFFFF);
	PPGeDrawText("Back", 320, 220, PPGE_ALIGN_LEFT, 0.5f, 0xFFFFFFFF);
}

int PSPMsgDialog::Update()
{
	switch (status) {
	case SCE_UTILITY_STATUS_FINISHED:
		status = SCE_UTILITY_STATUS_SHUTDOWN;
		break;
	}

	if (status != SCE_UTILITY_STATUS_RUNNING)
	{
		return 0;
	}

	const char *text;
	if (messageDialog.type == 0) {
		char temp[256];
		sprintf(temp, "Error code: %08x", messageDialog.errorNum);
		text = temp;
	} else {
		text = messageDialog.string;
	}

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

	switch(display)
	{
		case DS_MESSAGE:
			StartDraw();

			DisplayMessage(text);

			// TODO : Dialogs should take control over input and not send them to the game while displaying
			DisplayBack();
			if (IsButtonPressed(cancelButtonFlag))
			{
				status = SCE_UTILITY_STATUS_FINISHED;
				messageDialog.buttonPressed = 0;
			}
			EndDraw();
		break;
		case DS_ERROR:
			StartDraw();

			DisplayMessage(text);

			// TODO : Dialogs should take control over input and not send them to the game while displaying
			DisplayBack();
			if (IsButtonPressed(cancelButtonFlag))
			{
				status = SCE_UTILITY_STATUS_FINISHED;
				messageDialog.buttonPressed = 3;
			}
			EndDraw();
		break;
		case DS_YESNO:
			StartDraw();

			DisplayMessage(text);
			DisplayYesNo();

			// TODO : Dialogs should take control over input and not send them to the game while displaying
			DisplayEnterBack();
			if (IsButtonPressed(cancelButtonFlag))
			{
				status = SCE_UTILITY_STATUS_FINISHED;
				messageDialog.buttonPressed = 3;
			}
			else if (IsButtonPressed(okButtonFlag))
			{
				if(yesnoChoice == 0)
				{
					status = SCE_UTILITY_STATUS_FINISHED;
					messageDialog.buttonPressed = 2;
				}
				else
				{
					status = SCE_UTILITY_STATUS_FINISHED;
					messageDialog.buttonPressed = 1;
				}
			}
			EndDraw();
		break;
		case DS_OK:
			StartDraw();

			DisplayMessage(text);

			// TODO : Dialogs should take control over input and not send them to the game while displaying
			DisplayEnterBack();
			if (IsButtonPressed(cancelButtonFlag))
			{
				status = SCE_UTILITY_STATUS_FINISHED;
				messageDialog.buttonPressed = 3;
			}
			else if (IsButtonPressed(okButtonFlag))
			{
				status = SCE_UTILITY_STATUS_FINISHED;
				messageDialog.buttonPressed = 1;
			}
			EndDraw();
		break;
		default:
			status = SCE_UTILITY_STATUS_FINISHED;
			return 0;
		break;
	}

	lastButtons = buttons;

	Memory::WriteStruct(messageDialogAddr, &messageDialog);
	return 0;
}

int PSPMsgDialog::Shutdown()
{
	return PSPDialog::Shutdown();
}

void PSPMsgDialog::DoState(PointerWrap &p)
{
	PSPDialog::DoState(p);
	p.Do(display);
	p.Do(messageDialog);
	p.Do(messageDialogAddr);
	p.Do(yesnoChoice);
	p.Do(okButtonImg);
	p.Do(cancelButtonImg);
	p.Do(okButtonFlag);
	p.Do(cancelButtonFlag);
	p.DoMarker("PSPMsgDialog");
}
