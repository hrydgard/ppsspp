#include "Debugger_Lists.h"
#include <Windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include "DebuggerShared.h"

enum { TL_NAME, TL_PROGRAMCOUNTER, TL_ENTRYPOINT, TL_STATE, TL_COLUMNCOUNT };

char* threadColumns[] = {
	"Name", "PC", "Entry Point", "State"
};

const float threadColumnSizes[] = {
	0.25f, 0.25f, 0.25f, 0.25f
};

void CtrlThreadList::setDialogItem(HWND hwnd)
{
	wnd = hwnd;

	SetWindowLongPtr(wnd,GWLP_USERDATA,(LONG_PTR)this);
	oldProc = (WNDPROC) SetWindowLongPtr(wnd,GWLP_WNDPROC,(LONG_PTR)wndProc);

	SendMessage(wnd, LVM_SETEXTENDEDLISTVIEWSTYLE, 0, LVS_EX_FULLROWSELECT);

	LVCOLUMN lvc; 
	lvc.mask = LVCF_FMT | LVCF_WIDTH | LVCF_TEXT | LVCF_SUBITEM;
	lvc.iSubItem = 0;
	lvc.fmt = LVCFMT_LEFT;
	
	RECT rect;
	GetWindowRect(wnd,&rect);

	int totalListSize = (rect.right-rect.left-20);
	for (int i = 0; i < TL_COLUMNCOUNT; i++)
	{
		lvc.cx = threadColumnSizes[i] * totalListSize;
		lvc.pszText = threadColumns[i];
		ListView_InsertColumn(wnd, i, &lvc);
	}
}

LRESULT CALLBACK CtrlThreadList::wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	CtrlThreadList* tl = (CtrlThreadList*) GetWindowLongPtr(hwnd,GWLP_USERDATA);

	switch (msg)
	{
	case WM_SIZE:
		{
			int width = LOWORD(lParam);
			RECT rect;
			GetWindowRect(hwnd,&rect);

			int totalListSize = (rect.right-rect.left-20);
			for (int i = 0; i < TL_COLUMNCOUNT; i++)
			{
				ListView_SetColumnWidth(hwnd,i,threadColumnSizes[i] * totalListSize);
			}
		}
		break;
	case WM_KEYDOWN:
		if (wParam == VK_TAB)
		{
			SendMessage(GetParent(hwnd),WM_DEB_TABPRESSED,0,0);
			return 0;
		}
		break;
	case WM_GETDLGCODE:
		if (lParam && ((MSG*)lParam)->message == WM_KEYDOWN)
		{
			if (wParam == VK_TAB) return DLGC_WANTMESSAGE;
		}
		break;
	}
	return (LRESULT)CallWindowProc((WNDPROC)tl->oldProc,hwnd,msg,wParam,lParam);
}

void CtrlThreadList::handleNotify(LPARAM lParam)
{
	LPNMHDR mhdr = (LPNMHDR) lParam;

	if (mhdr->code == NM_DBLCLK)
	{
		LPNMITEMACTIVATE item = (LPNMITEMACTIVATE) lParam;
		SendMessage(GetParent(wnd),WM_DEB_GOTOWPARAM,threads[item->iItem].curPC,0);
		return;
	}

	if (mhdr->code == LVN_GETDISPINFO)
	{
		NMLVDISPINFO* dispInfo = (NMLVDISPINFO*)lParam;
		int index = dispInfo->item.iItem;
		
		stringBuffer[0] = 0;
		switch (dispInfo->item.iSubItem)
		{
		case TL_NAME:
			strcpy(stringBuffer,threads[index].name);
			break;
		case TL_PROGRAMCOUNTER:
			sprintf(stringBuffer,"0x%08X",threads[index].curPC);
			break;
		case TL_ENTRYPOINT:
			sprintf(stringBuffer,"0x%08X",threads[index].entrypoint);
			break;
		case TL_STATE:
			switch (threads[index].status)
			{
			case THREADSTATUS_RUNNING:
				strcpy(stringBuffer,"Running");
				break;
			case THREADSTATUS_READY:
				strcpy(stringBuffer,"Ready");
				break;
			case THREADSTATUS_WAIT:
				strcpy(stringBuffer,"Waiting");
				break;
			case THREADSTATUS_SUSPEND:
				strcpy(stringBuffer,"Suspended");
				break;
			case THREADSTATUS_DORMANT:
				strcpy(stringBuffer,"Dormant");
				break;
			case THREADSTATUS_DEAD:
				strcpy(stringBuffer,"Dead");
				break;
			case THREADSTATUS_WAITSUSPEND:
				strcpy(stringBuffer,"Waiting/Suspended");
				break;
			default:
				strcpy(stringBuffer,"Invalid");
				break;
			}
			break;
		}

		if (stringBuffer[0] == 0) strcat(stringBuffer,"Invalid");
		dispInfo->item.pszText = stringBuffer;
	}
}

void CtrlThreadList::reloadThreads()
{
	threads = GetThreadsInfo();

	int items = ListView_GetItemCount(wnd);
	while (items < threads.size())
	{
	    LVITEM lvI;
		lvI.pszText   = LPSTR_TEXTCALLBACK; // Sends an LVN_GETDISPINFO message.
		lvI.mask      = LVIF_TEXT | LVIF_IMAGE |LVIF_STATE;
		lvI.stateMask = 0;
		lvI.iSubItem  = 0;
		lvI.state     = 0;
		lvI.iItem  = items;
		lvI.iImage = items;

		ListView_InsertItem(wnd, &lvI);
		items++;
	}

	while (items > threads.size())
	{
		ListView_DeleteItem(wnd,--items);
	}

	InvalidateRect(wnd,NULL,true);
	UpdateWindow(wnd);
}
