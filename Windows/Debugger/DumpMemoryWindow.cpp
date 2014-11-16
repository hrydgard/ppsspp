#include "DumpMemoryWindow.h"
#include "../resource.h"
#include <stdio.h>
#include "Core/MemMap.h"
#include "Windows/W32Util/ShellUtil.h"
#include "Core/Core.h"

DumpMemoryWindow* DumpMemoryWindow::bp;
	
INT_PTR CALLBACK DumpMemoryWindow::dlgFunc(HWND hwnd, UINT iMsg, WPARAM wParam, LPARAM lParam)
{
	switch (iMsg)
	{
	case WM_INITDIALOG:
		bp->changeMode(hwnd,bp->selectedMode);
		return TRUE;
	case WM_COMMAND:
		switch (LOWORD(wParam))
		{
		case IDC_DUMP_USERMEMORY:
			switch (HIWORD(wParam))
			{
			case BN_CLICKED:
				bp->changeMode(hwnd,MODE_RAM);
				break;
			}
			break;
		case IDC_DUMP_VRAM:
			switch (HIWORD(wParam))
			{
			case BN_CLICKED:
				bp->changeMode(hwnd,MODE_VRAM);
				break;
			}
			break;
		case IDC_DUMP_SCRATCHPAD:
			switch (HIWORD(wParam))
			{
			case BN_CLICKED:
				bp->changeMode(hwnd,MODE_SCRATCHPAD);
				break;
			}
			break;
		case IDC_DUMP_CUSTOMRANGE:
			switch (HIWORD(wParam))
			{
			case BN_CLICKED:
				bp->changeMode(hwnd,MODE_CUSTOM);
				break;
			}
			break;
		case IDC_DUMP_BROWSEFILENAME:
			switch (HIWORD(wParam))
			{
			case BN_CLICKED:
				char str[MAX_PATH];
				GetWindowTextA(GetDlgItem(hwnd,IDC_DUMP_FILENAME),str,MAX_PATH);
				std::string fn = str;

				bool result = W32Util::BrowseForFileName(false, hwnd, L"Select filename", NULL,NULL,NULL,fn);
				if (result)
				{
					bp->filenameChosen = true;
					SetWindowTextA(GetDlgItem(hwnd,IDC_DUMP_FILENAME),fn.c_str());
				}
				break;
			}
			break;
		case IDOK:
			if (bp->fetchDialogData(hwnd))
			{
				auto memLock = Memory::Lock();
				if (!PSP_IsInited())
					break;

				FILE* output = fopen(bp->fileName,"wb");
				if (output == NULL) {
					char errorMessage[2048];
					snprintf(errorMessage, sizeof(errorMessage), "Could not open file \"%s\".",bp->fileName);
					MessageBoxA(hwnd,errorMessage,"Error",MB_OK);
					break;
				}
				
				bool priorDumpWasStepping = Core_IsStepping();
				if (!priorDumpWasStepping) Core_EnableStepping(true); // If emulator isn't paused force paused state
				fwrite(Memory::GetPointer(bp->start), 1, bp->size, output);
				fclose(output);
				if (!priorDumpWasStepping) Core_EnableStepping(false); // If emulator wasn't paused before memory dump resume emulation automatically.
				MessageBoxA(hwnd,"Done.","Information",MB_OK);
				EndDialog(hwnd,true);
			}
			break;
		case IDCANCEL:
			EndDialog(hwnd,false);
			break;
		}

	case WM_KEYDOWN:

		break;
	}

	return FALSE;
}

static bool isInInterval(u32 start, u32 end, u32 value)
{
	return start <= value && value < end;
}

bool DumpMemoryWindow::fetchDialogData(HWND hwnd)
{
	char str[256],errorMessage[256];
	PostfixExpression exp;

	// parse start address
	GetWindowTextA(GetDlgItem(hwnd,IDC_DUMP_STARTADDRESS),str,256);
	if (cpu->initExpression(str,exp) == false
		|| cpu->parseExpression(exp,start) == false)
	{
		sprintf(errorMessage,"Invalid address expression \"%s\".",str);
		MessageBoxA(hwnd,errorMessage,"Error",MB_OK);
		return false;
	}
	
	// parse size
	GetWindowTextA(GetDlgItem(hwnd,IDC_DUMP_SIZE),str,256);
	if (cpu->initExpression(str,exp) == false
		|| cpu->parseExpression(exp,size) == false)
	{
		sprintf(errorMessage,"Invalid size expression \"%s\".",str);
		MessageBoxA(hwnd,errorMessage,"Error",MB_OK);
		return false;
	}

	if (size == 0)
	{
		MessageBoxA(hwnd,"Invalid size 0.","Error",MB_OK);
		return false;
	}
	
	// get filename
	GetWindowTextA(GetDlgItem(hwnd,IDC_DUMP_FILENAME),fileName,MAX_PATH);
	if (strlen(fileName) == 0) return false;

	// now check if data makes sense...
	bool invalidSize = false;
	bool invalidAddress = false;
	if (isInInterval(PSP_GetScratchpadMemoryBase(),PSP_GetScratchpadMemoryEnd(),start))
	{
		invalidSize = !isInInterval(PSP_GetScratchpadMemoryBase(),PSP_GetScratchpadMemoryEnd(),start+size-1);
	} else if (isInInterval(PSP_GetVidMemBase(),PSP_GetVidMemEnd(),start))
	{
		invalidSize = !isInInterval(PSP_GetVidMemBase(),PSP_GetVidMemEnd(),start+size-1);
	} else if (isInInterval(PSP_GetKernelMemoryBase(),PSP_GetUserMemoryEnd(),start))
	{
		invalidSize = !isInInterval(PSP_GetKernelMemoryBase(),PSP_GetUserMemoryEnd(),start+size-1);
	} else 
	{
		invalidAddress = true;
	}

	if (invalidAddress)
	{
		sprintf(errorMessage,"Invalid address 0x%08X.",start);
		MessageBoxA(hwnd,errorMessage,"Error",MB_OK);
		return false;
	} else if (invalidSize)
	{
		sprintf(errorMessage,"Invalid end address 0x%08X.",start+size);
		MessageBoxA(hwnd,errorMessage,"Error",MB_OK);
		return false;
	}

	return true;
}

void DumpMemoryWindow::changeMode(HWND hwnd, Mode newMode)
{
	char buffer[128];
	selectedMode = newMode;
	
	SendMessage(GetDlgItem(hwnd,IDC_DUMP_USERMEMORY),BM_SETCHECK,selectedMode == MODE_RAM ? BST_CHECKED : BST_UNCHECKED,0);
	SendMessage(GetDlgItem(hwnd,IDC_DUMP_VRAM),BM_SETCHECK,selectedMode == MODE_VRAM ? BST_CHECKED : BST_UNCHECKED,0);
	SendMessage(GetDlgItem(hwnd,IDC_DUMP_SCRATCHPAD),BM_SETCHECK,selectedMode == MODE_SCRATCHPAD ? BST_CHECKED : BST_UNCHECKED,0);
	SendMessage(GetDlgItem(hwnd,IDC_DUMP_CUSTOMRANGE),BM_SETCHECK,selectedMode == MODE_CUSTOM ? BST_CHECKED : BST_UNCHECKED,0);

	if (selectedMode == MODE_CUSTOM)
	{
		EnableWindow(GetDlgItem(hwnd,IDC_DUMP_STARTADDRESS),TRUE);
		EnableWindow(GetDlgItem(hwnd,IDC_DUMP_SIZE),TRUE);

		if (filenameChosen == false)
			SetWindowTextA(GetDlgItem(hwnd,IDC_DUMP_FILENAME),"Custom.dump");
	} else {
		u32 start, size;
		const char* defaultFileName;

		switch (selectedMode)
		{
		case MODE_RAM:
			start = PSP_GetUserMemoryBase();
			size = PSP_GetUserMemoryEnd()-start;
			defaultFileName = "RAM.dump";
			break;
		case MODE_VRAM:
			start = PSP_GetVidMemBase();
			size = PSP_GetVidMemEnd()-start;
			defaultFileName = "VRAM.dump";
			break;
		case MODE_SCRATCHPAD:
			start = PSP_GetScratchpadMemoryBase();
			size = PSP_GetScratchpadMemoryEnd()-start;
			defaultFileName = "Scratchpad.dump";
			break;
		}
		
		sprintf(buffer,"0x%08X",start);
		SetWindowTextA(GetDlgItem(hwnd,IDC_DUMP_STARTADDRESS),buffer);
		EnableWindow(GetDlgItem(hwnd,IDC_DUMP_STARTADDRESS),FALSE);

		sprintf(buffer,"0x%08X",size);
		SetWindowTextA(GetDlgItem(hwnd,IDC_DUMP_SIZE),buffer);
		EnableWindow(GetDlgItem(hwnd,IDC_DUMP_SIZE),FALSE);
		
		if (filenameChosen == false)
			SetWindowTextA(GetDlgItem(hwnd,IDC_DUMP_FILENAME),defaultFileName);
	}
}

bool DumpMemoryWindow::exec()
{
	bp = this;
	bool result = DialogBoxParam(GetModuleHandle(0),MAKEINTRESOURCE(IDD_DUMPMEMORY),parentHwnd,dlgFunc,(LPARAM)this) != 0;
	return result;
}
