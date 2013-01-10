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

#include "PSPSaveDialog.h"
#include "../Util/PPGeDraw.h"
#include "../HLE/sceCtrl.h"
#include "../Core/MemMap.h"

PSPSaveDialog::PSPSaveDialog()
	: PSPDialog()
	, display(DS_NONE)
	, currentSelectedSave(0)
{
	param.SetPspParam(0);
}

PSPSaveDialog::~PSPSaveDialog() {
}

int PSPSaveDialog::Init(int paramAddr)
{
	// Ignore if already running
	if (status != SCE_UTILITY_STATUS_NONE && status != SCE_UTILITY_STATUS_SHUTDOWN)
	{
		ERROR_LOG(HLE,"A save request is already running !");
		return 0;
	}

	int size = Memory::Read_U32(paramAddr);
	memset(&request,0,sizeof(request));
	// Only copy the right size to support different save request format
	Memory::Memcpy(&request,paramAddr,size);
	requestAddr = paramAddr;

	u32 retval = param.SetPspParam(&request);

	INFO_LOG(HLE,"sceUtilitySavedataInitStart(%08x)", paramAddr);
	INFO_LOG(HLE,"Mode: %i", param.GetPspParam()->mode);

	switch(param.GetPspParam()->mode)
	{
		case SCE_UTILITY_SAVEDATA_TYPE_AUTOLOAD:
		case SCE_UTILITY_SAVEDATA_TYPE_LOAD:
			DEBUG_LOG(HLE, "Loading. Title: %s Save: %s File: %s", param.GetGameName(param.GetPspParam()).c_str(), param.GetSaveName(param.GetPspParam()).c_str(), param.GetFileName(param.GetPspParam()).c_str());
			display = DS_NONE;
			break;
		case SCE_UTILITY_SAVEDATA_TYPE_LISTLOAD:
			DEBUG_LOG(HLE, "Loading. Title: %s Save: %s File: %s", param.GetGameName(param.GetPspParam()).c_str(), param.GetGameName(param.GetPspParam()).c_str(), param.GetFileName(param.GetPspParam()).c_str());
			if(param.GetFilenameCount() == 0)
				display = DS_LOAD_NODATA;
			else
				display = DS_LOAD_LIST_CHOICE;
			break;
		case SCE_UTILITY_SAVEDATA_TYPE_AUTOSAVE:
		case SCE_UTILITY_SAVEDATA_TYPE_SAVE:
			DEBUG_LOG(HLE, "Saving. Title: %s Save: %s File: %s", param.GetGameName(param.GetPspParam()).c_str(), param.GetGameName(param.GetPspParam()).c_str(), param.GetFileName(param.GetPspParam()).c_str());
			display = DS_NONE;
			break;
		case SCE_UTILITY_SAVEDATA_TYPE_LISTSAVE:
			DEBUG_LOG(HLE, "Saving. Title: %s Save: %s File: %s", param.GetGameName(param.GetPspParam()).c_str(), param.GetGameName(param.GetPspParam()).c_str(), param.GetFileName(param.GetPspParam()).c_str());
			display = DS_SAVE_LIST_CHOICE;
			break;
		case SCE_UTILITY_SAVEDATA_TYPE_LISTDELETE:
			DEBUG_LOG(HLE, "Delete. Title: %s Save: %s File: %s", param.GetGameName(param.GetPspParam()).c_str(), param.GetGameName(param.GetPspParam()).c_str(), param.GetFileName(param.GetPspParam()).c_str());
			if(param.GetFilenameCount() == 0)
				display = DS_DELETE_NODATA;
			else
				display = DS_DELETE_LIST_CHOICE;
			break;
		case SCE_UTILITY_SAVEDATA_TYPE_SIZES:
		case SCE_UTILITY_SAVEDATA_TYPE_LIST:
		case SCE_UTILITY_SAVEDATA_TYPE_FILES:
		case SCE_UTILITY_SAVEDATA_TYPE_GETSIZE:
		case SCE_UTILITY_SAVEDATA_TYPE_MAKEDATASECURE:
		case SCE_UTILITY_SAVEDATA_TYPE_WRITEDATASECURE:
		case SCE_UTILITY_SAVEDATA_TYPE_READDATASECURE:
			display = DS_NONE;
			break;
		case SCE_UTILITY_SAVEDATA_TYPE_DELETE: // This run on PSP display a list of all save on the PSP. Weird. (Not really, it's to let you free up space)
			display = DS_DELETE_LIST_CHOICE;
			break;
		default:
		{
			ERROR_LOG(HLE, "Load/Save function %d not coded. Title: %s Save: %s File: %s", param.GetPspParam()->mode, param.GetGameName(param.GetPspParam()).c_str(), param.GetGameName(param.GetPspParam()).c_str(), param.GetFileName(param.GetPspParam()).c_str());
			param.GetPspParam()->result = 0;
			status = SCE_UTILITY_STATUS_INITIALIZE;
			display = DS_NONE;
			return 0; // Return 0 should allow the game to continue, but missing function must be implemented and returning the right value or the game can block.
		}
		break;
	}

	status = (int)retval < 0 ? SCE_UTILITY_STATUS_SHUTDOWN : SCE_UTILITY_STATUS_INITIALIZE;

	currentSelectedSave = 0;
	lastButtons = __CtrlPeekButtons();

	/*INFO_LOG(HLE,"Dump Param :");
	INFO_LOG(HLE,"size : %d",param.GetPspParam()->size);
	INFO_LOG(HLE,"language : %d",param.GetPspParam()->language);
	INFO_LOG(HLE,"buttonSwap : %d",param.GetPspParam()->buttonSwap);
	INFO_LOG(HLE,"result : %d",param.GetPspParam()->result);
	INFO_LOG(HLE,"mode : %d",param.GetPspParam()->mode);
	INFO_LOG(HLE,"bind : %d",param.GetPspParam()->bind);
	INFO_LOG(HLE,"overwriteMode : %d",param.GetPspParam()->overwriteMode);
	INFO_LOG(HLE,"gameName : %s",param.GetGameName(param.GetPspParam()).c_str());
	INFO_LOG(HLE,"saveName : %s",param.GetPspParam()->saveName);
	INFO_LOG(HLE,"saveNameList : %08x",*((unsigned int*)&param.GetPspParam()->saveNameList));
	INFO_LOG(HLE,"fileName : %s",param.GetPspParam()->fileName);
	INFO_LOG(HLE,"dataBuf : %08x",*((unsigned int*)&param.GetPspParam()->dataBuf));
	INFO_LOG(HLE,"dataBufSize : %u",param.GetPspParam()->dataBufSize);
	INFO_LOG(HLE,"dataSize : %u",param.GetPspParam()->dataSize);

	INFO_LOG(HLE,"sfo title : %s",param.GetPspParam()->sfoParam.title);
	INFO_LOG(HLE,"sfo savedataTitle : %s",param.GetPspParam()->sfoParam.savedataTitle);
	INFO_LOG(HLE,"sfo detail : %s",param.GetPspParam()->sfoParam.detail);

	INFO_LOG(HLE,"icon0 data : %08x",*((unsigned int*)&param.GetPspParam()->icon0FileData.buf));
	INFO_LOG(HLE,"icon0 size : %u",param.GetPspParam()->icon0FileData.bufSize);

	INFO_LOG(HLE,"icon1 data : %08x",*((unsigned int*)&param.GetPspParam()->icon1FileData.buf));
	INFO_LOG(HLE,"icon1 size : %u",param.GetPspParam()->icon1FileData.bufSize);

	INFO_LOG(HLE,"pic1 data : %08x",*((unsigned int*)&param.GetPspParam()->pic1FileData.buf));
	INFO_LOG(HLE,"pic1 size : %u",param.GetPspParam()->pic1FileData.bufSize);

	INFO_LOG(HLE,"snd0 data : %08x",*((unsigned int*)&param.GetPspParam()->snd0FileData.buf));
	INFO_LOG(HLE,"snd0 size : %u",param.GetPspParam()->snd0FileData.bufSize);*/
	return retval;
}

void PSPSaveDialog::DisplaySaveList(bool canMove)
{
	int displayCount = 0;
	for(int i = 0; i < param.GetFilenameCount(); i++)
	{
		int textureColor = 0xFFFFFFFF;

		if(param.GetFileInfo(i).size == 0)
		{
			textureColor = 0xFF777777;
		}

		// Calc save image position on screen
		float w = 150;
		float h = 80;
		float x = 20;
		if(displayCount != currentSelectedSave)
		{
			w = 80;
			h = 40;
			x = 50;
		}
		float y = 80;
		if(displayCount < currentSelectedSave)
			y -= 50 * (currentSelectedSave - displayCount);
		else if(displayCount > currentSelectedSave)
		{
			y += 90 + 50 * (displayCount - currentSelectedSave - 1);
		}

		int tw = 256;
		int th = 256;
		if(param.GetFileInfo(i).textureData != 0)
		{
			tw = param.GetFileInfo(i).textureWidth;
			th = param.GetFileInfo(i).textureHeight;
			PPGeSetTexture(param.GetFileInfo(i).textureData, param.GetFileInfo(i).textureWidth, param.GetFileInfo(i).textureHeight);
		}
		else
		{
			PPGeDisableTexture();
		}
		PPGeDrawImage(x, y, w, h, 0, 0 ,1 ,1 ,tw, th, textureColor);
		PPGeSetDefaultTexture();
		displayCount++;
	}

	if(canMove)
	{
		if (IsButtonPressed(CTRL_UP) && currentSelectedSave > 0)
		{
			currentSelectedSave--;
		}
		else if (IsButtonPressed(CTRL_DOWN) && currentSelectedSave < (param.GetFilenameCount()-1))
		{
			currentSelectedSave++;
		}
	}
}

void PSPSaveDialog::DisplaySaveIcon()
{
	int textureColor = 0xFFFFFFFF;

	if(param.GetFileInfo(currentSelectedSave).size == 0)
	{
		textureColor = 0xFF777777;
	}

	// Calc save image position on screen
	float w = 150;
	float h = 80;
	float x = 20;
	float y = 80;

	int tw = 256;
	int th = 256;
	if(param.GetFileInfo(currentSelectedSave).textureData != 0)
	{
		tw = param.GetFileInfo(currentSelectedSave).textureWidth;
		th = param.GetFileInfo(currentSelectedSave).textureHeight;
		PPGeSetTexture(param.GetFileInfo(currentSelectedSave).textureData, param.GetFileInfo(currentSelectedSave).textureWidth, param.GetFileInfo(currentSelectedSave).textureHeight);
	}
	else
	{
		PPGeDisableTexture();
	}
	PPGeDrawImage(x, y, w, h, 0, 0 ,1 ,1 ,tw, th, textureColor);
	if(param.GetFileInfo(currentSelectedSave).textureData != 0)
	{
		PPGeSetDefaultTexture();
	}
}

void PSPSaveDialog::DisplaySaveDataInfo1()
{
	if(param.GetFileInfo(currentSelectedSave).size == 0)
	{
		PPGeDrawText("New Save", 200, 110, PPGE_ALIGN_LEFT, 0.5f, 0xFFFFFFFF);
	}
	else
	{
		char txt[2048];
		_dbg_assert_msg_(HLE, sizeof(txt) > sizeof(SaveFileInfo), "Local buffer is too small.");

		snprintf(txt,2048,"%s\n%02d/%02d/%d %02d:%02d %lld KB\n%s\n%s"
				, param.GetFileInfo(currentSelectedSave).title
				, param.GetFileInfo(currentSelectedSave).modif_time.tm_mday
				, param.GetFileInfo(currentSelectedSave).modif_time.tm_mon + 1
				, param.GetFileInfo(currentSelectedSave).modif_time.tm_year + 1900
				, param.GetFileInfo(currentSelectedSave).modif_time.tm_hour
				, param.GetFileInfo(currentSelectedSave).modif_time.tm_min
				, param.GetFileInfo(currentSelectedSave).size / 1024
				, param.GetFileInfo(currentSelectedSave).saveTitle
				, param.GetFileInfo(currentSelectedSave).saveDetail
				);
		std::string saveinfoTxt = txt;
		PPGeDrawText(saveinfoTxt.c_str(), 200, 80, PPGE_ALIGN_LEFT, 0.5f, 0xFFFFFFFF);
	}
}

void PSPSaveDialog::DisplaySaveDataInfo2()
{
	if(param.GetFileInfo(currentSelectedSave).size == 0)
	{
	}
	else
	{
		char txt[1024];
		snprintf(txt,1024,"%s\n%02d/%02d/%d %02d:%02d\n%lld KB"
						, param.GetFileInfo(currentSelectedSave).saveTitle
						, param.GetFileInfo(currentSelectedSave).modif_time.tm_mday
						, param.GetFileInfo(currentSelectedSave).modif_time.tm_mon + 1
						, param.GetFileInfo(currentSelectedSave).modif_time.tm_year + 1900
						, param.GetFileInfo(currentSelectedSave).modif_time.tm_hour
						, param.GetFileInfo(currentSelectedSave).modif_time.tm_min
						, param.GetFileInfo(currentSelectedSave).size / 1024
						);
		std::string saveinfoTxt = txt;
		PPGeDrawText(saveinfoTxt.c_str(), 20, 180, PPGE_ALIGN_LEFT, 0.5f, 0xFFFFFFFF);
	}
}

void PSPSaveDialog::DisplayConfirmationYesNo(std::string text)
{
	PPGeDrawText(text.c_str(), 200, 90, PPGE_ALIGN_LEFT, 0.5f, 0xFFFFFFFF);

	PPGeDrawText("Yes", 250, 150, PPGE_ALIGN_LEFT, 0.5f, (yesnoChoice == 1?0xFF0000FF:0xFFFFFFFF));
	PPGeDrawText("No", 350, 150, PPGE_ALIGN_LEFT, 0.5f, (yesnoChoice == 0?0xFF0000FF:0xFFFFFFFF));

	if (IsButtonPressed(CTRL_LEFT) && yesnoChoice == 0)
	{
		yesnoChoice = 1;
	}
	else if (IsButtonPressed(CTRL_RIGHT) && yesnoChoice == 1)
	{
		yesnoChoice = 0;
	}
}

void PSPSaveDialog::DisplayInfo(std::string text)
{
	PPGeDrawText(text.c_str(), 200, 90, PPGE_ALIGN_LEFT, 0.5f, 0xFFFFFFFF);
}
void PSPSaveDialog::DisplayTitle(std::string name)
{
	PPGeDrawText(name.c_str(), 10, 10, PPGE_ALIGN_LEFT, 0.5f, 0xFFFFFFFF);
}
void PSPSaveDialog::DisplayEnterBack()
{
	PPGeDrawImage(okButtonImg, 200, 220, 20, 20, 0, 0xFFFFFFFF);
	PPGeDrawText("Enter", 230, 220, PPGE_ALIGN_LEFT, 0.5f, 0xFFFFFFFF);
	PPGeDrawImage(cancelButtonImg, 290, 220, 20, 20, 0, 0xFFFFFFFF);
	PPGeDrawText("Back", 320, 220, PPGE_ALIGN_LEFT, 0.5f, 0xFFFFFFFF);
}
void PSPSaveDialog::DisplayBack()
{
	PPGeDrawImage(cancelButtonImg, 250, 220, 20, 20, 0, 0xFFFFFFFF);
	PPGeDrawText("Back", 270, 220, PPGE_ALIGN_LEFT, 0.5f, 0xFFFFFFFF);
}

int PSPSaveDialog::Update()
{
	switch (status) {
	case SCE_UTILITY_STATUS_FINISHED:
		status = SCE_UTILITY_STATUS_SHUTDOWN;
		break;
	default:
		break;
	}

	if (status != SCE_UTILITY_STATUS_RUNNING)
	{
		return 0;
	}

	if (!param.GetPspParam()) {
		status = SCE_UTILITY_STATUS_SHUTDOWN;
		return 0;
	}

	buttons = __CtrlPeekButtons();

	okButtonImg = I_CIRCLE;
	cancelButtonImg = I_CROSS;
	okButtonFlag = CTRL_CIRCLE;
	cancelButtonFlag = CTRL_CROSS;
	if(param.GetPspParam()->buttonSwap == 1)
	{
		okButtonImg = I_CROSS;
		cancelButtonImg = I_CIRCLE;
		okButtonFlag = CTRL_CROSS;
		cancelButtonFlag = CTRL_CIRCLE;
	}

	switch(display)
	{
		case DS_SAVE_LIST_CHOICE:
			StartDraw();
			DisplayTitle("Save");

			// TODO : use focus for selected save by default, and don't modify global selected save,use local var
			DisplaySaveList();
			DisplaySaveDataInfo1();

			// TODO : Dialogs should take control over input and not send them to the game while displaying
			DisplayEnterBack();
			if (IsButtonPressed(cancelButtonFlag))
			{
				status = SCE_UTILITY_STATUS_FINISHED;
				param.GetPspParam()->result = SCE_UTILITY_DIALOG_RESULT_CANCEL;
			}
			else if (IsButtonPressed(okButtonFlag))
			{
				// Save exist, ask user confirm
				if(param.GetFileInfo(currentSelectedSave).size > 0)
				{
					yesnoChoice = 0;
					display = DS_SAVE_CONFIRM_OVERWRITE;
				}
				else
				{
					display = DS_SAVE_SAVING;
					if(param.Save(param.GetPspParam(),currentSelectedSave))
					{
						param.SetPspParam(param.GetPspParam()); // Optim : Just Update modified save
						display = DS_SAVE_DONE;
					}
					else
					{
						display = DS_SAVE_LIST_CHOICE; // This will probably need error message ?
					}
				}
			}
			EndDraw();
		break;
		case DS_SAVE_CONFIRM_OVERWRITE:
			StartDraw();
			DisplayTitle("Save");

			DisplaySaveIcon();
			DisplaySaveDataInfo2();

			DisplayConfirmationYesNo("Do you want to overwrite the data ?");

			DisplayEnterBack();
			if (IsButtonPressed(cancelButtonFlag))
			{
				display = DS_SAVE_LIST_CHOICE;
			}
			else if (IsButtonPressed(okButtonFlag))
			{
				if(yesnoChoice == 0)
				{
					display = DS_SAVE_LIST_CHOICE;
				}
				else
				{
					display = DS_SAVE_SAVING;
					if(param.Save(param.GetPspParam(),currentSelectedSave))
					{
						param.SetPspParam(param.GetPspParam()); // Optim : Just Update modified save
						display = DS_SAVE_DONE;
					}
					else
					{
						display = DS_SAVE_LIST_CHOICE; // This will probably need error message ?
					}
				}
			}

			EndDraw();
		break;
		case DS_SAVE_SAVING:
			StartDraw();
			DisplayTitle("Save");

			DisplaySaveIcon();
			DisplaySaveDataInfo2();

			DisplayInfo("Saving\nPlease Wait...");

			EndDraw();
		break;
		case DS_SAVE_DONE:
			StartDraw();
			DisplayTitle("Save");

			DisplaySaveIcon();
			DisplaySaveDataInfo2();
			DisplayBack();

			DisplayInfo("Save completed");

			if (IsButtonPressed(cancelButtonFlag))
			{
				status = SCE_UTILITY_STATUS_FINISHED;
				param.GetPspParam()->result = SCE_UTILITY_DIALOG_RESULT_SUCCESS;
				// Set the save to use for autosave and autoload
				param.SetSelectedSave(param.GetFileInfo(currentSelectedSave).idx);
			}

			EndDraw();
		break;

		case DS_LOAD_LIST_CHOICE:
			StartDraw();
			DisplayTitle("Load");
			DisplaySaveList();
			DisplaySaveDataInfo1();

			// TODO : Dialogs should take control over input and not send them to the game while displaying
			DisplayEnterBack();
			if (IsButtonPressed(cancelButtonFlag))
			{
				status = SCE_UTILITY_STATUS_FINISHED;
				param.GetPspParam()->result = SCE_UTILITY_DIALOG_RESULT_CANCEL;
			}
			else if (IsButtonPressed(okButtonFlag))
			{
				display = DS_LOAD_LOADING;
				if(param.Load(param.GetPspParam(),currentSelectedSave))
				{
					display = DS_LOAD_DONE;
				}
			}

			EndDraw();
		break;
		case DS_LOAD_LOADING:
			StartDraw();
			DisplayTitle("Load");

			DisplaySaveIcon();
			DisplaySaveDataInfo2();

			DisplayInfo("Loading\nPlease Wait...");

			EndDraw();
		break;
		case DS_LOAD_DONE:
			StartDraw();
			DisplayTitle("Load");

			DisplaySaveIcon();
			DisplaySaveDataInfo2();
			DisplayBack();

			DisplayInfo("Load completed");

			if (IsButtonPressed(cancelButtonFlag))
			{
				status = SCE_UTILITY_STATUS_FINISHED;
				param.GetPspParam()->result = SCE_UTILITY_DIALOG_RESULT_SUCCESS;
				// Set the save to use for autosave and autoload
				param.SetSelectedSave(param.GetFileInfo(currentSelectedSave).idx);
			}

			EndDraw();
		break;
		case DS_LOAD_NODATA:
			StartDraw();
			DisplayTitle("Load");

			DisplayBack();

			DisplayInfo("There is no data");

			if (IsButtonPressed(cancelButtonFlag))
			{
				status = SCE_UTILITY_STATUS_FINISHED;
				param.GetPspParam()->result = SCE_UTILITY_SAVEDATA_ERROR_LOAD_NO_DATA;
			}

			EndDraw();
		break;

		case DS_DELETE_LIST_CHOICE:
			StartDraw();
			DisplayTitle("Delete");
			DisplaySaveList();
			DisplaySaveDataInfo1();

			// TODO : Dialogs should take control over input and not send them to the game while displaying
			DisplayEnterBack();
			if (IsButtonPressed(cancelButtonFlag))
			{
				status = SCE_UTILITY_STATUS_FINISHED;
				param.GetPspParam()->result = SCE_UTILITY_DIALOG_RESULT_CANCEL;
			}
			else if (IsButtonPressed(okButtonFlag))
			{
				yesnoChoice = 0;
				display = DS_DELETE_CONFIRM;
			}

			EndDraw();
		break;
		case DS_DELETE_CONFIRM:
			StartDraw();
			DisplayTitle("Delete");

			DisplaySaveIcon();
			DisplaySaveDataInfo2();

			DisplayConfirmationYesNo("The data will be deleted.\nAre you sure you want to continue?");

			DisplayEnterBack();
			if (IsButtonPressed(cancelButtonFlag))
			{
				display = DS_DELETE_LIST_CHOICE;
			}
			else if (IsButtonPressed(okButtonFlag))
			{
				if(yesnoChoice == 0)
				{
					display = DS_DELETE_LIST_CHOICE;
				}
				else
				{
					display = DS_DELETE_DELETING;
					if(param.Delete(param.GetPspParam(),currentSelectedSave))
					{
						param.SetPspParam(param.GetPspParam()); // Optim : Just Update modified save
						display = DS_DELETE_DONE;
					}
					else
					{
						display = DS_DELETE_LIST_CHOICE; // This will probably need error message ?
					}
				}
			}

			EndDraw();
		break;
		case DS_DELETE_DELETING:
			StartDraw();
			DisplayTitle("Delete");

			DisplayInfo("Deleting\nPlease Wait...");

			EndDraw();
		break;
		case DS_DELETE_DONE:
			StartDraw();
			DisplayTitle("Delete");

			DisplayBack();

			DisplayInfo("Delete completed");

			if (IsButtonPressed(cancelButtonFlag))
			{
				if(param.GetFilenameCount() == 0)
					display = DS_DELETE_NODATA;
				else
					display = DS_DELETE_LIST_CHOICE;
			}

			EndDraw();
		break;
		case DS_DELETE_NODATA:
			StartDraw();
			DisplayTitle("Delete");

			DisplayBack();

			DisplayInfo("There is no data");

			if (IsButtonPressed(cancelButtonFlag))
			{
				status = SCE_UTILITY_STATUS_FINISHED;
				param.GetPspParam()->result = SCE_UTILITY_SAVEDATA_ERROR_DELETE_NO_DATA;
			}

			EndDraw();
		break;

		case DS_NONE: // For action which display nothing
		{
			switch(param.GetPspParam()->mode)
			{
				case SCE_UTILITY_SAVEDATA_TYPE_LOAD: // Only load and exit
				case SCE_UTILITY_SAVEDATA_TYPE_AUTOLOAD:
					if(param.Load(param.GetPspParam(),param.GetSelectedSave()))
						param.GetPspParam()->result = 0;
					else
						param.GetPspParam()->result = SCE_UTILITY_SAVEDATA_ERROR_LOAD_NO_DATA;
					status = SCE_UTILITY_STATUS_FINISHED;
				break;
				case SCE_UTILITY_SAVEDATA_TYPE_SAVE: // Only save and exit
				case SCE_UTILITY_SAVEDATA_TYPE_AUTOSAVE:
					if(param.Save(param.GetPspParam(),param.GetSelectedSave()))
						param.GetPspParam()->result = 0;
					else
						param.GetPspParam()->result = SCE_UTILITY_SAVEDATA_ERROR_SAVE_MS_NOSPACE;
					status = SCE_UTILITY_STATUS_FINISHED;
				break;
				case SCE_UTILITY_SAVEDATA_TYPE_SIZES:
					if(param.GetSizes(param.GetPspParam()))
					{
						param.GetPspParam()->result = 0;
					}
					else
					{
						param.GetPspParam()->result = SCE_UTILITY_SAVEDATA_ERROR_SIZES_NO_DATA;
					}
					status = SCE_UTILITY_STATUS_FINISHED;
				break;
				case SCE_UTILITY_SAVEDATA_TYPE_LIST:
					param.GetList(param.GetPspParam());
					param.GetPspParam()->result = 0;
					status = SCE_UTILITY_STATUS_FINISHED;
				break;
				case SCE_UTILITY_SAVEDATA_TYPE_FILES:
					if(param.GetFilesList(param.GetPspParam()))
					{
						param.GetPspParam()->result = 0;
					}
					else
					{
						param.GetPspParam()->result = SCE_UTILITY_SAVEDATA_ERROR_RW_NO_DATA;
					}
					status = SCE_UTILITY_STATUS_FINISHED;
				break;
				case SCE_UTILITY_SAVEDATA_TYPE_GETSIZE:
					if(param.GetSize(param.GetPspParam()))
					{
						param.GetPspParam()->result = 0;
					}
					else
					{
						param.GetPspParam()->result = SCE_UTILITY_SAVEDATA_ERROR_RW_NO_DATA;
					}
					status = SCE_UTILITY_STATUS_FINISHED;
				break;
				case SCE_UTILITY_SAVEDATA_TYPE_MAKEDATASECURE:
				case SCE_UTILITY_SAVEDATA_TYPE_WRITEDATASECURE:
					if(param.Save(param.GetPspParam(),param.GetSelectedSave()))
						param.GetPspParam()->result = 0;
					else
						param.GetPspParam()->result = SCE_UTILITY_SAVEDATA_ERROR_RW_NO_DATA;
					status = SCE_UTILITY_STATUS_FINISHED;
				break;
				case SCE_UTILITY_SAVEDATA_TYPE_READDATASECURE:
					if(param.Load(param.GetPspParam(),param.GetSelectedSave()))
						param.GetPspParam()->result = 0;
					else
						param.GetPspParam()->result = SCE_UTILITY_SAVEDATA_ERROR_RW_NO_DATA; // not sure if correct code
					status = SCE_UTILITY_STATUS_FINISHED;
				break;
				default:
					status = SCE_UTILITY_STATUS_FINISHED;
				break;
			}
		}
		break;
		default:
			status = SCE_UTILITY_STATUS_FINISHED;
		break;
	}

	lastButtons = buttons;

	if(status == SCE_UTILITY_STATUS_FINISHED)
	{
		Memory::Memcpy(requestAddr,&request,request.size);
	}
	
	return 0;
}

int PSPSaveDialog::Shutdown()
{
	PSPDialog::Shutdown();
	param.SetPspParam(0);

	return 0;
}

void PSPSaveDialog::DoState(PointerWrap &p)
{
	PSPDialog::DoState(p);
	p.Do(display);
	param.DoState(p);
	p.Do(request);
	// Just reset it.
	bool hasParam = param.GetPspParam() != NULL;
	p.Do(hasParam);
	if (hasParam)
		param.SetPspParam(&request);
	p.Do(requestAddr);
	p.Do(currentSelectedSave);
	p.Do(yesnoChoice);
	p.Do(okButtonImg);
	p.Do(cancelButtonImg);
	p.Do(okButtonFlag);
	p.Do(cancelButtonFlag);
	p.DoMarker("PSPSaveDialog");
}
