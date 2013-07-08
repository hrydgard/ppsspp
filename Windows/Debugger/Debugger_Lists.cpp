#include "Debugger_Lists.h"
#include <Windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include "DebuggerShared.h"
#include "Windows/resource.h"

enum { TL_NAME, TL_PROGRAMCOUNTER, TL_ENTRYPOINT, TL_PRIORITY, TL_STATE, TL_WAITTYPE, TL_COLUMNCOUNT };

char* threadColumns[] = {
	"Name", "PC", "Entry Point", "Priority","State", "Wait type"
};

const float threadColumnSizes[] = {
	0.20f, 0.15f, 0.15f, 0.15f, 0.15f, 0.20f
};

const int POPUP_SUBMENU_ID_THREADLIST = 6;

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

void CtrlThreadList::showMenu(int itemIndex, const POINT &pt)
{
	auto threadInfo = threads[itemIndex];

	// Can't do it, sorry.  Needs to not be running.
	if (Core_IsActive())
		return;

	POINT screenPt(pt);
	ClientToScreen(wnd, &screenPt);

	HMENU subMenu = GetSubMenu(g_hPopupMenus, POPUP_SUBMENU_ID_THREADLIST);
	switch (threadInfo.status) {
	case THREADSTATUS_DEAD:
	case THREADSTATUS_DORMANT:
	case THREADSTATUS_RUNNING:
		EnableMenuItem(subMenu, ID_DISASM_THREAD_FORCERUN, MF_BYCOMMAND | MF_DISABLED);
		EnableMenuItem(subMenu, ID_DISASM_THREAD_KILL, MF_BYCOMMAND | MF_DISABLED);
		break;
	case THREADSTATUS_READY:
		EnableMenuItem(subMenu, ID_DISASM_THREAD_FORCERUN, MF_BYCOMMAND | MF_DISABLED);
		EnableMenuItem(subMenu, ID_DISASM_THREAD_KILL, MF_BYCOMMAND | MF_ENABLED);
		break;
	case THREADSTATUS_SUSPEND:
	case THREADSTATUS_WAIT:
	case THREADSTATUS_WAITSUSPEND:
	default:
		EnableMenuItem(subMenu, ID_DISASM_THREAD_FORCERUN, MF_BYCOMMAND | MF_ENABLED);
		EnableMenuItem(subMenu, ID_DISASM_THREAD_KILL, MF_BYCOMMAND | MF_ENABLED);
		break;
	}

	switch (TrackPopupMenuEx(subMenu, TPM_RIGHTBUTTON | TPM_RETURNCMD, screenPt.x, screenPt.y, wnd, 0))
	{
	case ID_DISASM_THREAD_FORCERUN:
		__KernelResumeThreadFromWait(threadInfo.id, 0);
		reloadThreads();
		break;
	case ID_DISASM_THREAD_KILL:
		sceKernelTerminateThread(threadInfo.id);
		reloadThreads();
		break;
	}
}

void CtrlThreadList::handleNotify(LPARAM lParam)
{
	LPNMHDR mhdr = (LPNMHDR) lParam;

	if (mhdr->code == NM_DBLCLK)
	{
		LPNMITEMACTIVATE item = (LPNMITEMACTIVATE) lParam;

		u32 address;
		switch (threads[item->iItem].status)
		{
		case THREADSTATUS_DORMANT:
		case THREADSTATUS_DEAD:
			address = threads[item->iItem].entrypoint;
			break;
		default:
			address = threads[item->iItem].curPC;
			break;
		}

		SendMessage(GetParent(wnd),WM_DEB_GOTOWPARAM,address,0);
		return;
	}
	if (mhdr->code == NM_RCLICK)
	{
		const LPNMITEMACTIVATE item = (LPNMITEMACTIVATE)lParam;
		showMenu(item->iItem, item->ptAction);
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
			switch (threads[index].status)
			{
			case THREADSTATUS_DORMANT:
			case THREADSTATUS_DEAD:
				sprintf(stringBuffer,"N/A");
				break;
			default:
				sprintf(stringBuffer,"0x%08X",threads[index].curPC);
				break;
			};
			break;
		case TL_ENTRYPOINT:
			sprintf(stringBuffer,"0x%08X",threads[index].entrypoint);
			break;
		case TL_PRIORITY:
			sprintf(stringBuffer,"%d",threads[index].priority);
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
		case TL_WAITTYPE:
			strcpy(stringBuffer,getWaitTypeName(threads[index].waitType));
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
	while (items < (int)threads.size())
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

	while (items > (int)threads.size())
	{
		ListView_DeleteItem(wnd,--items);
	}

	InvalidateRect(wnd,NULL,true);
	UpdateWindow(wnd);
}

const char* CtrlThreadList::getCurrentThreadName()
{
	for (size_t i = 0; i < threads.size(); i++)
	{
		if (threads[i].isCurrent) return threads[i].name;
	}

	return "N/A";
}