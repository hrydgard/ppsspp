#include "Windows/GEDebugger/TabDisplayLists.h"
#include "Windows/GEDebugger/GEDebugger.h"
#include "Windows/GEDebugger/CtrlDisplayListView.h"
#include "Windows/WindowsHost.h"
#include "Windows/WndMainWindow.h"
#include "Windows/main.h"
#include "GPU/GPUInterface.h"
#include "GPU/Common/GPUDebugInterface.h"
#include "GPU/GPUState.h"
#include "Core/Config.h"
#include <windowsx.h>
#include <commctrl.h>

enum { WM_GEDBG_LISTS_CHANGELIST = WM_USER+400, WM_GEDBG_LISTS_GOTOSTACKPC };

//
// CtrlDisplayListStack
//

enum { DLS_PC = 0, DLS_BASE, DLS_OFFSET, DLS_COLUMNCOUNT };

const GenericListViewColumn displayListStackColumns[3] = {
	{ L"PC", 0.34f },
	{ L"Base", 0.33f },
	{ L"Offset", 0.33f },
};

GenericListViewDef displayListStackListDef = {
	displayListStackColumns,	ARRAY_SIZE(displayListStackColumns),	NULL,	false
};

CtrlDisplayListStack::CtrlDisplayListStack(HWND hwnd): GenericListControl(hwnd,displayListStackListDef)
{
	list.stackptr = 0;
	Update();
}

void CtrlDisplayListStack::GetColumnText(wchar_t* dest, int row, int col)
{
	if (row < 0 || row >= (int)ARRAY_SIZE(list.stack)) {
		return;
	}
	DisplayListStackEntry value = list.stack[row];

	switch (col)
	{
	case DLS_BASE:
		wsprintf(dest,L"0x%08X",value.baseAddr);
		break;
	case DLS_OFFSET:
		wsprintf(dest,L"0x%08X",value.offsetAddr);
		break;
	case DLS_PC:
		wsprintf(dest,L"0x%08X",value.pc);
		break;
	}
}

void CtrlDisplayListStack::OnDoubleClick(int itemIndex, int column)
{
	SendMessage(GetParent(GetHandle()),WM_GEDBG_LISTS_GOTOSTACKPC,itemIndex,0);
}


//
// CtrlDisplayListStack
//

enum { ADL_STARTPC = 0, ADL_PC, ADL_STALL, ADL_STATE, APL_STARTED, APL_INTERRUPTED, ADL_COLUMNCOUNT };

const GenericListViewColumn allDisplayListsColumns[ADL_COLUMNCOUNT] = {
	{ L"Start PC", 0.18f },
	{ L"PC", 0.18f },
	{ L"Stall", 0.18f },
	{ L"State", 0.16f },
	{ L"Started", 0.15f },
	{ L"Interrupted", 0.15f },
};

GenericListViewDef allDisplayListsListDef = {
	allDisplayListsColumns,	ARRAY_SIZE(allDisplayListsColumns),	NULL,	false
};

CtrlAllDisplayLists::CtrlAllDisplayLists(HWND hwnd): GenericListControl(hwnd,allDisplayListsListDef)
{
	Update();
}

void CtrlAllDisplayLists::GetColumnText(wchar_t* dest, int row, int col)
{
	if (row < 0 || row >= (int)lists.size()) {
		return;
	}
	DisplayList& list = lists[row];

	switch (col)
	{
	case ADL_STALL:
		wsprintf(dest,L"0x%08X",list.stall);
		break;
	case ADL_PC:
		wsprintf(dest,L"0x%08X",list.pc);
		break;
	case ADL_STARTPC:
		wsprintf(dest,L"0x%08X",list.startpc);
		break;
	case ADL_STATE:
		switch (list.state)
		{
		case PSP_GE_DL_STATE_NONE:
			wcscpy(dest,L"None");
			break;
		case PSP_GE_DL_STATE_QUEUED:
			wcscpy(dest,L"Queued");
			break;
		case PSP_GE_DL_STATE_RUNNING:
			wcscpy(dest,L"Running");
			break;
		case PSP_GE_DL_STATE_COMPLETED:
			wcscpy(dest,L"Completed");
			break;
		case PSP_GE_DL_STATE_PAUSED:
			wcscpy(dest,L"Paused");
			break;
		}
		break;
	case APL_STARTED:
		wcscpy(dest,list.started ? L"Yes" : L"No");
		break;
	case APL_INTERRUPTED:
		wcscpy(dest,list.interrupted ? L"Yes" : L"No");
		break;
	}
}

bool CtrlAllDisplayLists::WindowMessage(UINT msg, WPARAM wParam, LPARAM lParam, LRESULT& returnValue)
{
	switch (msg)
	{
	case WM_KEYDOWN:
		if (wParam == VK_RETURN)
		{
			int item = GetSelectedIndex();
			SendMessage(GetParent(GetHandle()),WM_GEDBG_LISTS_CHANGELIST,item,0);
		}
		break;
	case WM_GETDLGCODE:
		if (lParam && ((MSG*)lParam)->message == WM_KEYDOWN)
		{
			if (wParam == VK_RETURN)
			{
				returnValue = DLGC_WANTMESSAGE;
				return true;
			}
		}
	}

	return false;
}

void CtrlAllDisplayLists::OnDoubleClick(int itemIndex, int column)
{
	SendMessage(GetParent(GetHandle()),WM_GEDBG_LISTS_CHANGELIST,itemIndex,0);
}


//
// TabDisplayLists
//

TabDisplayLists::TabDisplayLists(HINSTANCE _hInstance, HWND _hParent)
	: Dialog((LPCSTR)IDD_TABDISPLAYLISTS, _hInstance, _hParent)
{
	displayList = CtrlDisplayListView::getFrom(GetDlgItem(m_hDlg,IDC_GEDBG_LISTS_SELECTEDLIST));

	stack = new CtrlDisplayListStack(GetDlgItem(m_hDlg,IDC_GEDBG_LISTS_STACK));
	allLists = new CtrlAllDisplayLists(GetDlgItem(m_hDlg,IDC_GEDBG_LISTS_ALLLISTS));

	activeList = -1;
}

TabDisplayLists::~TabDisplayLists()
{
	delete stack;
	delete allLists;
}

void TabDisplayLists::UpdateSize(WORD width, WORD height)
{
	struct Position
	{
		int x,y;
		int w,h;
	};

	Position positions[3];
	int borderMargin = 5;
	int betweenControlsMargin = 5;

	// All Lists
	positions[0].x = borderMargin;
	positions[0].y = borderMargin;
	positions[0].w = width*2/3;
	positions[0].h = min(height*2/5,200);

	// Stack
	positions[1].x = positions[0].x+positions[0].w+betweenControlsMargin;
	positions[1].y = borderMargin;
	positions[1].w = width-positions[1].x-borderMargin;
	positions[1].h = positions[0].h;

	// Current List
	positions[2].x = borderMargin;
	positions[2].y = positions[0].y+positions[0].h+betweenControlsMargin;
	positions[2].w = width-2*borderMargin;
	positions[2].h = height-positions[2].y-borderMargin;

	HWND handles[3] = {
		GetDlgItem(m_hDlg,IDC_GEDBG_LISTS_ALLLISTS),
		GetDlgItem(m_hDlg,IDC_GEDBG_LISTS_STACK),
		GetDlgItem(m_hDlg,IDC_GEDBG_LISTS_SELECTEDLIST)
	};

	for (int i = 0; i < 3; i++)
	{
		MoveWindow(handles[i],positions[i].x,positions[i].y,positions[i].w,positions[i].h,TRUE);
	}
}

void TabDisplayLists::Update(bool reload)
{
	if (reload && gpuDebug != NULL)
	{
		lists = gpuDebug->ActiveDisplayLists();
	}

	if (activeList != -1)
	{
		DisplayList currentList = lists[activeList];
		
		displayList->setDisplayList(currentList);
		stack->setDisplayList(currentList);
	}

	allLists->setDisplayLists(lists);
}

BOOL TabDisplayLists::DlgProc(UINT message, WPARAM wParam, LPARAM lParam) {
	switch (message) {
	case WM_INITDIALOG:
		return TRUE;

	case WM_SIZE:
		UpdateSize(LOWORD(lParam), HIWORD(lParam));
		return TRUE;

	case WM_NOTIFY:
		switch (wParam)
		{
		case IDC_GEDBG_LISTS_STACK:
			stack->HandleNotify(lParam);
			break;
		case IDC_GEDBG_LISTS_ALLLISTS:
			allLists->HandleNotify(lParam);
			break;
		}
		break;

	case WM_GEDBG_LISTS_CHANGELIST:
		activeList = wParam;
		Update();
		break;

	case WM_GEDBG_LISTS_GOTOSTACKPC:
		{
			u32 pc = lists[activeList].stack[wParam].pc;
			displayList->gotoAddr(pc);
		}
		break;

	case WM_GEDBG_TOGGLEPCBREAKPOINT:
		SendMessage(GetParent(m_hDlg),message,wParam,lParam);
		break;

	}

	return FALSE;
}
