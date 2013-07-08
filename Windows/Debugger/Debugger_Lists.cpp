#include "Debugger_Lists.h"
#include <Windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include "DebuggerShared.h"
#include "CtrlDisAsmView.h"
#include "Windows/resource.h"
#include "../main.h"

typedef struct
{
	char* name;
	float size;
} ListViewColumn;

enum { TL_NAME, TL_PROGRAMCOUNTER, TL_ENTRYPOINT, TL_PRIORITY, TL_STATE, TL_WAITTYPE, TL_COLUMNCOUNT };
enum { BPL_TYPE, BPL_OFFSET, BPL_SIZELABEL, BPL_OPCODE, BPL_HITS, BPL_ENABLED, BPL_COLUMNCOUNT };

ListViewColumn threadColumns[TL_COLUMNCOUNT] = {
	{ "Name",			0.20f },
	{ "PC",				0.15f },
	{ "Entry Point",	0.15f },
	{ "Priority",		0.15f },
	{ "State",			0.15f },
	{ "Wait type",		0.20f }
};

ListViewColumn breakpointColumns[BPL_COLUMNCOUNT] = {
	{ "Type",			0.12f },
	{ "Offset",			0.20f },
	{ "Size/Label",		0.20f },
	{ "Opcode",			0.30f },
	{ "Hits",			0.10f },
	{ "Enabled",		0.08f }
};

const int POPUP_SUBMENU_ID_BREAKPOINTLIST = 5;
const int POPUP_SUBMENU_ID_THREADLIST = 6;

//
// CtrlThreadList
//

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
		lvc.cx = threadColumns[i].size * totalListSize;
		lvc.pszText = threadColumns[i].name;
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
				ListView_SetColumnWidth(hwnd,i,threadColumns[i].size * totalListSize);
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


//
// CtrlBreakpointList
//

void CtrlBreakpointList::setDialogItem(HWND hwnd)
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
	for (int i = 0; i < BPL_COLUMNCOUNT; i++)
	{
		lvc.cx = breakpointColumns[i].size * totalListSize;
		lvc.pszText = breakpointColumns[i].name;
		ListView_InsertColumn(wnd, i, &lvc);
	}
}

LRESULT CALLBACK CtrlBreakpointList::wndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	CtrlBreakpointList* bp = (CtrlBreakpointList*) GetWindowLongPtr(hwnd,GWLP_USERDATA);

	switch(message)
	{
	case WM_SIZE:
		{
			int width = LOWORD(lParam);
			RECT rect;
			GetWindowRect(hwnd,&rect);

			int totalListSize = (rect.right-rect.left-20);
			for (int i = 0; i < BPL_COLUMNCOUNT; i++)
			{
				ListView_SetColumnWidth(hwnd,i,breakpointColumns[i].size * totalListSize);
			}
		}
		break;
	case WM_KEYDOWN:
		if(wParam == VK_RETURN)
		{
			int index = ListView_GetSelectionMark(hwnd);
			bp->gotoBreakpointAddress(index);
			return 0;
		} else if (wParam == VK_DELETE)
		{
			int index = ListView_GetSelectionMark(hwnd);
			bp->removeBreakpoint(index);
			return 0;
		} else if (wParam == VK_TAB)
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
	};
	
	return (LRESULT)CallWindowProc((WNDPROC)bp->oldProc,hwnd,message,wParam,lParam);
}

void CtrlBreakpointList::update()
{
	// Update the items we're displaying from the debugger.
	displayedBreakPoints_ = CBreakPoints::GetBreakpoints();
	displayedMemChecks_= CBreakPoints::GetMemChecks();

	int breakpointCount = getTotalBreakpointCount();
	int items = ListView_GetItemCount(wnd);

	while (items < breakpointCount)
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

	while (items > breakpointCount)
	{
		ListView_DeleteItem(wnd,--items);
	}

	InvalidateRect(wnd,NULL,true);
	UpdateWindow(wnd);
}

void CtrlBreakpointList::gotoBreakpointAddress(int itemIndex)
{
	bool isMemory;
	int index = getBreakpointIndex(itemIndex,isMemory);
	if (index == -1) return;

	if (isMemory)
	{
		u32 address = displayedMemChecks_[index].start;
			
		for (int i=0; i<numCPUs; i++)
			if (memoryWindow[i])
				memoryWindow[i]->Goto(address);
	} else {
		u32 address = displayedBreakPoints_[index].addr;
		
		for (int i=0; i<numCPUs; i++)
			if (disasmWindow[i])
				disasmWindow[i]->Goto(address);
	}
}

void CtrlBreakpointList::removeBreakpoint(int itemIndex)
{
	bool isMemory;
	int index = getBreakpointIndex(itemIndex,isMemory);
	if (index == -1) return;

	if (isMemory)
	{
		auto mc = displayedMemChecks_[index];
		CBreakPoints::RemoveMemCheck(mc.start, mc.end);
	} else {
		u32 address = displayedBreakPoints_[index].addr;
		CBreakPoints::RemoveBreakPoint(address);
	}
}

int CtrlBreakpointList::getTotalBreakpointCount()
{
	int count = (int)CBreakPoints::GetMemChecks().size();
	for (size_t i = 0; i < CBreakPoints::GetBreakpoints().size(); i++)
	{
		if (!displayedBreakPoints_[i].temporary) count++;
	}

	return count;
}

int CtrlBreakpointList::getBreakpointIndex(int itemIndex, bool& isMemory)
{
	// memory breakpoints first
	if (itemIndex < (int)displayedMemChecks_.size())
	{
		isMemory = true;
		return itemIndex;
	}

	itemIndex -= (int)displayedMemChecks_.size();

	size_t i = 0;
	while (i < displayedBreakPoints_.size())
	{
		if (displayedBreakPoints_[i].temporary)
		{
			i++;
			continue;
		}

		// the index is 0 when there are no more breakpoints to skip
		if (itemIndex == 0)
		{
			isMemory = false;
			return (int)i;
		}

		i++;
		itemIndex--;
	}

	return -1;
}

void CtrlBreakpointList::handleNotify(LPARAM lParam)
{
	const LPNMHDR header = (LPNMHDR)lParam;
	if (header->code == NM_DBLCLK)
	{
		const LPNMITEMACTIVATE item = (LPNMITEMACTIVATE) lParam;
		gotoBreakpointAddress(item->iItem);
		return;
	}
	if (header->code == NM_RCLICK)
	{
		const LPNMITEMACTIVATE item = (LPNMITEMACTIVATE)lParam;
		showBreakpointMenu(item->iItem, item->ptAction);
		return;
	}

	if (header->code == LVN_GETDISPINFO)
	{
		NMLVDISPINFO *dispInfo = (NMLVDISPINFO *)lParam;
		
		bool isMemory;
		int index = getBreakpointIndex(dispInfo->item.iItem,isMemory);
		if (index == -1) return;
		
		breakpointText[0] = 0;
		switch (dispInfo->item.iSubItem)
		{
		case BPL_TYPE:
			{
				if (isMemory)
				{
					switch (displayedMemChecks_[index].cond)
					{
					case MEMCHECK_READ:
						strcpy(breakpointText, "Read");
						break;
					case MEMCHECK_WRITE:
						strcpy(breakpointText, "Write");
						break;
					case MEMCHECK_READWRITE:
						strcpy(breakpointText, "Read/Write");
						break;
					}
				} else {
					strcpy(breakpointText,"Execute");
				}
			}
			break;
		case BPL_OFFSET:
			{
				if (isMemory)
				{
					sprintf(breakpointText,"0x%08X",displayedMemChecks_[index].start);
				} else {
					sprintf(breakpointText,"0x%08X",displayedBreakPoints_[index].addr);
				}
			}
			break;
		case BPL_SIZELABEL:
			{
				if (isMemory)
				{
					auto mc = displayedMemChecks_[index];
					if (mc.end == 0) sprintf(breakpointText,"0x%08X",1);
					else sprintf(breakpointText,"0x%08X",mc.end-mc.start);
				} else {
					const char* sym = cpu->findSymbolForAddress(displayedBreakPoints_[index].addr);
					if (sym != NULL)
					{
						strcpy(breakpointText,sym);
					} else {
						strcpy(breakpointText,"-");
					}
				}
			}
			break;
		case BPL_OPCODE:
			{
				if (isMemory)
				{
					strcpy(breakpointText,"-");
				} else {
					disasm->getOpcodeText(displayedBreakPoints_[index].addr,breakpointText);
				}
			}
			break;
		case BPL_HITS:
			{
				if (isMemory)
				{
					sprintf(breakpointText,"%d",displayedMemChecks_[index].numHits);
				} else {
					strcpy(breakpointText,"-");
				}
			}
			break;
		case BPL_ENABLED:
			{
				if (isMemory)
				{
					strcpy(breakpointText,displayedMemChecks_[index].result & MEMCHECK_BREAK ? "True" : "False");
				} else {
					strcpy(breakpointText,displayedBreakPoints_[index].enabled ? "True" : "False");
				}
			}
			break;
		default:
			return;
		}
				
		if (breakpointText[0] == 0) strcat(breakpointText,"Invalid");
		dispInfo->item.pszText = breakpointText;
	}
}

void CtrlBreakpointList::showBreakpointMenu(int itemIndex, const POINT &pt)
{
	bool isMemory;
	int index = getBreakpointIndex(itemIndex, isMemory);
	if (index == -1) return;

	MemCheck mcPrev;
	BreakPoint bpPrev;
	if (isMemory) {
		mcPrev = displayedMemChecks_[index];
	} else {
		bpPrev = displayedBreakPoints_[index];
	}

	POINT screenPt(pt);
	ClientToScreen(wnd, &screenPt);

	HMENU subMenu = GetSubMenu(g_hPopupMenus, POPUP_SUBMENU_ID_BREAKPOINTLIST);
	if (isMemory) {
		CheckMenuItem(subMenu, ID_DISASM_DISABLEBREAKPOINT, MF_BYCOMMAND | (mcPrev.result & MEMCHECK_BREAK ? MF_CHECKED : MF_UNCHECKED));
	} else {
		CheckMenuItem(subMenu, ID_DISASM_DISABLEBREAKPOINT, MF_BYCOMMAND | (bpPrev.enabled ? MF_CHECKED : MF_UNCHECKED));
	}

	switch (TrackPopupMenuEx(subMenu, TPM_RIGHTBUTTON | TPM_RETURNCMD, screenPt.x, screenPt.y, wnd, 0))
	{
	case ID_DISASM_DISABLEBREAKPOINT:
		if (isMemory) {
			CBreakPoints::ChangeMemCheck(mcPrev.start, mcPrev.end, mcPrev.cond, MemCheckResult(mcPrev.result ^ MEMCHECK_BREAK));
		} else {
			CBreakPoints::ChangeBreakPoint(bpPrev.addr, !bpPrev.enabled);
		}
		break;
	}
}
