#include "Windows/Debugger/Debugger_Lists.h"
#include "Common/CommonWindows.h"
#include <windowsx.h>
#include <commctrl.h>
#include "Windows/Debugger/DebuggerShared.h"
#include "Windows/Debugger/CtrlDisAsmView.h"
#include "Windows/resource.h"
#include "Windows/main.h"
#include "BreakpointWindow.h"
#include "../../Core/HLE/sceKernelThread.h"
#include "util/text/utf8.h"

static const int numCPUs = 1;

enum { TL_NAME, TL_PROGRAMCOUNTER, TL_ENTRYPOINT, TL_PRIORITY, TL_STATE, TL_WAITTYPE, TL_COLUMNCOUNT };
enum { BPL_ENABLED, BPL_TYPE, BPL_OFFSET, BPL_SIZELABEL, BPL_OPCODE, BPL_CONDITION, BPL_HITS, BPL_COLUMNCOUNT };
enum { SF_ENTRY, SF_ENTRYNAME, SF_CURPC, SF_CUROPCODE, SF_CURSP, SF_FRAMESIZE, SF_COLUMNCOUNT };
enum { ML_NAME, ML_ADDRESS, ML_SIZE, ML_ACTIVE, ML_COLUMNCOUNT };

GenericListViewColumn threadColumns[TL_COLUMNCOUNT] = {
	{ L"Name",			0.20f },
	{ L"PC",			0.15f },
	{ L"Entry Point",	0.15f },
	{ L"Priority",		0.15f },
	{ L"State",			0.15f },
	{ L"Wait type",		0.20f }
};

GenericListViewDef threadListDef = {
	threadColumns,	ARRAY_SIZE(threadColumns),	NULL,	false
};

GenericListViewColumn breakpointColumns[BPL_COLUMNCOUNT] = {
	{ L"",				0.03f },	// enabled
	{ L"Type",			0.15f },
	{ L"Offset",		0.12f },
	{ L"Size/Label",	0.20f },
	{ L"Opcode",		0.28f },
	{ L"Condition",		0.17f },
	{ L"Hits",			0.05f },
};

GenericListViewDef breakpointListDef = {
	breakpointColumns,	ARRAY_SIZE(breakpointColumns),	NULL,	true
};

GenericListViewColumn stackTraceColumns[SF_COLUMNCOUNT] = {
	{ L"Entry",			0.12f },
	{ L"Name",			0.24f },
	{ L"PC",			0.12f },
	{ L"Opcode",		0.28f },
	{ L"SP",			0.12f },
	{ L"Frame Size",	0.12f }
};

GenericListViewDef stackTraceListDef = {
	stackTraceColumns,	ARRAY_SIZE(stackTraceColumns),	NULL,	false
};

GenericListViewColumn moduleListColumns[ML_COLUMNCOUNT] = {
	{ L"Name",			0.25f },
	{ L"Address",		0.25f },
	{ L"Size",			0.25f },
	{ L"Active",		0.25f },
};

GenericListViewDef moduleListDef = {
	moduleListColumns,	ARRAY_SIZE(moduleListColumns),	NULL,	false
};

const int POPUP_SUBMENU_ID_BREAKPOINTLIST = 5;
const int POPUP_SUBMENU_ID_THREADLIST = 6;
const int POPUP_SUBMENU_ID_NEWBREAKPOINT = 7;

//
// CtrlThreadList
//

CtrlThreadList::CtrlThreadList(HWND hwnd): GenericListControl(hwnd,threadListDef)
{
	Update();
}

bool CtrlThreadList::WindowMessage(UINT msg, WPARAM wParam, LPARAM lParam, LRESULT& returnValue)
{
	switch (msg)
	{
	case WM_KEYDOWN:
		if (wParam == VK_TAB)
		{
			SendMessage(GetParent(GetHandle()),WM_DEB_TABPRESSED,0,0);
			returnValue = 0;
			return true;
		}
		break;
	case WM_GETDLGCODE:
		if (lParam && ((MSG*)lParam)->message == WM_KEYDOWN)
		{
			if (wParam == VK_TAB)
			{
				returnValue = DLGC_WANTMESSAGE;
				return true;
			}
		}
		break;
	}

	return false;
}

void CtrlThreadList::showMenu(int itemIndex, const POINT &pt)
{
	auto threadInfo = threads[itemIndex];

	// Can't do it, sorry.  Needs to not be running.
	if (Core_IsActive())
		return;

	POINT screenPt(pt);
	ClientToScreen(GetHandle(), &screenPt);

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

	switch (TrackPopupMenuEx(subMenu, TPM_RIGHTBUTTON | TPM_RETURNCMD, screenPt.x, screenPt.y, GetHandle(), 0))
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

void CtrlThreadList::GetColumnText(wchar_t* dest, int row, int col)
{
	if (row < 0 || row >= (int)threads.size()) {
		return;
	}

	switch (col)
	{
	case TL_NAME:
		wcscpy(dest, ConvertUTF8ToWString(threads[row].name).c_str());
		break;
	case TL_PROGRAMCOUNTER:
		switch (threads[row].status)
		{
		case THREADSTATUS_DORMANT:
		case THREADSTATUS_DEAD:
			wcscpy(dest, L"N/A");
			break;
		default:
			wsprintf(dest, L"0x%08X",threads[row].curPC);
			break;
		};
		break;
	case TL_ENTRYPOINT:
		wsprintf(dest,L"0x%08X",threads[row].entrypoint);
		break;
	case TL_PRIORITY:
		wsprintf(dest,L"%d",threads[row].priority);
		break;
	case TL_STATE:
		switch (threads[row].status)
		{
		case THREADSTATUS_RUNNING:
			wcscpy(dest,L"Running");
			break;
		case THREADSTATUS_READY:
			wcscpy(dest,L"Ready");
			break;
		case THREADSTATUS_WAIT:
			wcscpy(dest,L"Waiting");
			break;
		case THREADSTATUS_SUSPEND:
			wcscpy(dest,L"Suspended");
			break;
		case THREADSTATUS_DORMANT:
			wcscpy(dest,L"Dormant");
			break;
		case THREADSTATUS_DEAD:
			wcscpy(dest,L"Dead");
			break;
		case THREADSTATUS_WAITSUSPEND:
			wcscpy(dest,L"Waiting/Suspended");
			break;
		default:
			wcscpy(dest,L"Invalid");
			break;
		}
		break;
	case TL_WAITTYPE:
		wcscpy(dest, ConvertUTF8ToWString(getWaitTypeName(threads[row].waitType)).c_str());
		break;
	}
}

void CtrlThreadList::OnDoubleClick(int itemIndex, int column)
{
	u32 address;
	switch (threads[itemIndex].status)
	{
	case THREADSTATUS_DORMANT:
	case THREADSTATUS_DEAD:
		address = threads[itemIndex].entrypoint;
		break;
	default:
		address = threads[itemIndex].curPC;
		break;
	}

	SendMessage(GetParent(GetHandle()),WM_DEB_GOTOWPARAM,address,0);
}

void CtrlThreadList::OnRightClick(int itemIndex, int column, const POINT& point)
{
	showMenu(itemIndex,point);
}

void CtrlThreadList::reloadThreads()
{
	threads = GetThreadsInfo();
	Update();
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

CtrlBreakpointList::CtrlBreakpointList(HWND hwnd, DebugInterface* cpu, CtrlDisAsmView* disasm)
	: GenericListControl(hwnd,breakpointListDef),cpu(cpu),disasm(disasm)
{
	SetSendInvalidRows(true);
	Update();
}

bool CtrlBreakpointList::WindowMessage(UINT msg, WPARAM wParam, LPARAM lParam, LRESULT& returnValue)
{
	switch(msg)
	{
	case WM_KEYDOWN:
		returnValue = 0;
		if(wParam == VK_RETURN)
		{
			int index = GetSelectedIndex();
			editBreakpoint(index);
			return true;
		} else if (wParam == VK_DELETE)
		{
			int index = GetSelectedIndex();
			removeBreakpoint(index);
			return true;
		} else if (wParam == VK_TAB)
		{
			SendMessage(GetParent(GetHandle()),WM_DEB_TABPRESSED,0,0);
			return true;
		} else if (wParam == VK_SPACE)
		{
			int index = GetSelectedIndex();
			toggleEnabled(index);
			return true;
		}
		break;
	case WM_GETDLGCODE:
		if (lParam && ((MSG*)lParam)->message == WM_KEYDOWN)
		{
			if (wParam == VK_TAB || wParam == VK_RETURN)
			{
				returnValue = DLGC_WANTMESSAGE;
				return true;
			}
		}
		break;
	}

	return false;
}

void CtrlBreakpointList::reloadBreakpoints()
{
	// Update the items we're displaying from the debugger.
	displayedBreakPoints_ = CBreakPoints::GetBreakpoints();
	displayedMemChecks_= CBreakPoints::GetMemChecks();
	Update();

	for (int i = 0; i < GetRowCount(); i++)
	{
		bool isMemory;
		int index = getBreakpointIndex(i, isMemory);

		if (isMemory)
			SetCheckState(i,(displayedMemChecks_[index].result & MEMCHECK_BREAK) != 0);
		else
			SetCheckState(i,displayedBreakPoints_[index].enabled);
	}
}

void CtrlBreakpointList::editBreakpoint(int itemIndex)
{
	bool isMemory;
	int index = getBreakpointIndex(itemIndex, isMemory);
	if (index == -1) return;

	BreakpointWindow win(GetHandle(),cpu);
	if (isMemory)
	{
		auto mem = displayedMemChecks_[index];
		win.loadFromMemcheck(mem);
		if (win.exec())
		{
			CBreakPoints::RemoveMemCheck(mem.start,mem.end);
			win.addBreakpoint();
		}
	} else {
		auto bp = displayedBreakPoints_[index];
		win.loadFromBreakpoint(bp);
		if (win.exec())
		{
			CBreakPoints::RemoveBreakPoint(bp.addr);
			win.addBreakpoint();
		}
	}
}

void CtrlBreakpointList::toggleEnabled(int itemIndex)
{
	bool isMemory;
	int index = getBreakpointIndex(itemIndex, isMemory);
	if (index == -1) return;

	if (isMemory) {
		MemCheck mcPrev = displayedMemChecks_[index];
		CBreakPoints::ChangeMemCheck(mcPrev.start, mcPrev.end, mcPrev.cond, MemCheckResult(mcPrev.result ^ MEMCHECK_BREAK));
	} else {
		BreakPoint bpPrev = displayedBreakPoints_[index];
		CBreakPoints::ChangeBreakPoint(bpPrev.addr, !bpPrev.enabled);
	}
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

void CtrlBreakpointList::GetColumnText(wchar_t* dest, int row, int col)
{
	if (!PSP_IsInited()) {
		return;
	}
	bool isMemory;
	int index = getBreakpointIndex(row,isMemory);
	if (index == -1) return;
		
	switch (col)
	{
	case BPL_TYPE:
		{
			if (isMemory) {
				switch ((int)displayedMemChecks_[index].cond) {
				case MEMCHECK_READ:
					wcscpy(dest,L"Read");
					break;
				case MEMCHECK_WRITE:
					wcscpy(dest,L"Write");
					break;
				case MEMCHECK_READWRITE:
					wcscpy(dest,L"Read/Write");
					break;
				case MEMCHECK_WRITE | MEMCHECK_WRITE_ONCHANGE:
					wcscpy(dest,L"Write Change");
					break;
				case MEMCHECK_READWRITE | MEMCHECK_WRITE_ONCHANGE:
					wcscpy(dest,L"Read/Write Change");
					break;
				}
			} else {
				wcscpy(dest,L"Execute");
			}
		}
		break;
	case BPL_OFFSET:
		{
			if (isMemory) {
				wsprintf(dest,L"0x%08X",displayedMemChecks_[index].start);
			} else {
				wsprintf(dest,L"0x%08X",displayedBreakPoints_[index].addr);
			}
		}
		break;
	case BPL_SIZELABEL:
		{
			if (isMemory) {
				auto mc = displayedMemChecks_[index];
				if (mc.end == 0)
					wsprintf(dest,L"0x%08X",1);
				else
					wsprintf(dest,L"0x%08X",mc.end-mc.start);
			} else {
				const std::string sym = symbolMap.GetLabelString(displayedBreakPoints_[index].addr);
				if (!sym.empty())
				{
					std::wstring s = ConvertUTF8ToWString(sym);
					wcscpy(dest,s.c_str());
				} else {
					wcscpy(dest,L"-");
				}
			}
		}
		break;
	case BPL_OPCODE:
		{
			if (isMemory) {
				wcscpy(dest,L"-");
			} else {
				char temp[256];
				disasm->getOpcodeText(displayedBreakPoints_[index].addr, temp);
				std::wstring s = ConvertUTF8ToWString(temp);
				wcscpy(dest,s.c_str());
			}
		}
		break;
	case BPL_CONDITION:
		{
			if (isMemory || displayedBreakPoints_[index].hasCond == false) {
				wcscpy(dest,L"-");
			} else {
				std::wstring s = ConvertUTF8ToWString(displayedBreakPoints_[index].cond.expressionString);
				wcscpy(dest,s.c_str());
			}
		}
		break;
	case BPL_HITS:
		{
			if (isMemory) {
				wsprintf(dest,L"%d",displayedMemChecks_[index].numHits);
			} else {
				wsprintf(dest,L"-");
			}
		}
		break;
	case BPL_ENABLED:
		{
			wsprintf(dest,L"\xFFFE");
		}
		break;
	}
}

void CtrlBreakpointList::OnDoubleClick(int itemIndex, int column)
{
	gotoBreakpointAddress(itemIndex);
}

void CtrlBreakpointList::OnRightClick(int itemIndex, int column, const POINT& point)
{
	showBreakpointMenu(itemIndex,point);
}

void CtrlBreakpointList::OnToggle(int item, bool newValue)
{
	toggleEnabled(item);
}

void CtrlBreakpointList::showBreakpointMenu(int itemIndex, const POINT &pt)
{
	POINT screenPt(pt);
	ClientToScreen(GetHandle(), &screenPt);

	bool isMemory;
	int index = getBreakpointIndex(itemIndex, isMemory);
	if (index == -1)
	{
		HMENU subMenu = GetSubMenu(g_hPopupMenus, POPUP_SUBMENU_ID_NEWBREAKPOINT);
		
		switch (TrackPopupMenuEx(subMenu, TPM_RIGHTBUTTON | TPM_RETURNCMD, screenPt.x, screenPt.y, GetHandle(), 0))
		{
		case ID_DISASM_ADDNEWBREAKPOINT:
			{		
				BreakpointWindow bpw(GetHandle(),cpu);
				if (bpw.exec()) bpw.addBreakpoint();
			}
			break;
		}
	} else {
		MemCheck mcPrev;
		BreakPoint bpPrev;
		if (isMemory) {
			mcPrev = displayedMemChecks_[index];
		} else {
			bpPrev = displayedBreakPoints_[index];
		}

		HMENU subMenu = GetSubMenu(g_hPopupMenus, POPUP_SUBMENU_ID_BREAKPOINTLIST);
		if (isMemory) {
			CheckMenuItem(subMenu, ID_DISASM_DISABLEBREAKPOINT, MF_BYCOMMAND | (mcPrev.result & MEMCHECK_BREAK ? MF_CHECKED : MF_UNCHECKED));
		} else {
			CheckMenuItem(subMenu, ID_DISASM_DISABLEBREAKPOINT, MF_BYCOMMAND | (bpPrev.enabled ? MF_CHECKED : MF_UNCHECKED));
		}

		switch (TrackPopupMenuEx(subMenu, TPM_RIGHTBUTTON | TPM_RETURNCMD, screenPt.x, screenPt.y, GetHandle(), 0))
		{
		case ID_DISASM_DISABLEBREAKPOINT:
			if (isMemory) {
				CBreakPoints::ChangeMemCheck(mcPrev.start, mcPrev.end, mcPrev.cond, MemCheckResult(mcPrev.result ^ MEMCHECK_BREAK));
			} else {
				CBreakPoints::ChangeBreakPoint(bpPrev.addr, !bpPrev.enabled);
			}
			break;
		case ID_DISASM_EDITBREAKPOINT:
			editBreakpoint(itemIndex);
			break;
		case ID_DISASM_ADDNEWBREAKPOINT:
			{		
				BreakpointWindow bpw(GetHandle(),cpu);
				if (bpw.exec()) bpw.addBreakpoint();
			}
			break;
		}
	}
}

//
// CtrlStackTraceView
//

CtrlStackTraceView::CtrlStackTraceView(HWND hwnd, DebugInterface* cpu, CtrlDisAsmView* disasm)
	: GenericListControl(hwnd,stackTraceListDef),cpu(cpu),disasm(disasm)
{
	Update();
}

bool CtrlStackTraceView::WindowMessage(UINT msg, WPARAM wParam, LPARAM lParam, LRESULT& returnValue)
{
	switch(msg)
	{
	case WM_KEYDOWN:
		if (wParam == VK_TAB)
		{
			returnValue = 0;
			SendMessage(GetParent(GetHandle()),WM_DEB_TABPRESSED,0,0);
			return true;
		}
		break;
	case WM_GETDLGCODE:
		if (lParam && ((MSG*)lParam)->message == WM_KEYDOWN)
		{
			if (wParam == VK_TAB || wParam == VK_RETURN)
			{
				returnValue = DLGC_WANTMESSAGE;
				return true;
			}
		}
		break;
	}

	return false;
}

void CtrlStackTraceView::GetColumnText(wchar_t* dest, int row, int col)
{
	if (row < 0 || row >= (int)frames.size()) {
		return;
	}

	switch (col)
	{
	case SF_ENTRY:
		wsprintf(dest,L"%08X",frames[row].entry);
		break;
	case SF_ENTRYNAME:
		{
			const std::string sym = symbolMap.GetLabelString(frames[row].entry);
			if (!sym.empty()) {
				wcscpy(dest, ConvertUTF8ToWString(sym).c_str());
			} else {
				wcscpy(dest,L"-");
			}
		}
		break;
	case SF_CURPC:
		wsprintf(dest,L"%08X",frames[row].pc);
		break;
	case SF_CUROPCODE:
		{
			char temp[512];
			disasm->getOpcodeText(frames[row].pc,temp);
			wcscpy(dest, ConvertUTF8ToWString(temp).c_str());
		}
		break;
	case SF_CURSP:
		wsprintf(dest,L"%08X",frames[row].sp);
		break;
	case SF_FRAMESIZE:
		wsprintf(dest,L"%08X",frames[row].stackSize);
		break;
	}
}

void CtrlStackTraceView::OnDoubleClick(int itemIndex, int column)
{
	SendMessage(GetParent(GetHandle()),WM_DEB_GOTOWPARAM,frames[itemIndex].pc,0);
}

void CtrlStackTraceView::loadStackTrace()
{
	auto threads = GetThreadsInfo();

	u32 entry = 0, stackTop = 0;
	for (size_t i = 0; i < threads.size(); i++)
	{
		if (threads[i].isCurrent)
		{
			entry = threads[i].entrypoint;
			stackTop = threads[i].initialStack;
			break;
		}
	}

	if (entry != 0) {
		frames = MIPSStackWalk::Walk(cpu->GetPC(),cpu->GetRegValue(0,31),cpu->GetRegValue(0,29),entry,stackTop);
	} else {
		frames.clear();
	}
	Update();
}

//
// CtrlModuleList
//

CtrlModuleList::CtrlModuleList(HWND hwnd, DebugInterface* cpu)
	: GenericListControl(hwnd,moduleListDef),cpu(cpu)
{
	Update();
}

bool CtrlModuleList::WindowMessage(UINT msg, WPARAM wParam, LPARAM lParam, LRESULT& returnValue)
{
	switch(msg)
	{
	case WM_KEYDOWN:
		if (wParam == VK_TAB)
		{
			returnValue = 0;
			SendMessage(GetParent(GetHandle()),WM_DEB_TABPRESSED,0,0);
			return true;
		}
		break;
	case WM_GETDLGCODE:
		if (lParam && ((MSG*)lParam)->message == WM_KEYDOWN)
		{
			if (wParam == VK_TAB || wParam == VK_RETURN)
			{
				returnValue = DLGC_WANTMESSAGE;
				return true;
			}
		}
		break;
	}

	return false;
}

void CtrlModuleList::GetColumnText(wchar_t* dest, int row, int col)
{
	if (row < 0 || row >= (int)modules.size()) {
		return;
	}

	switch (col)
	{
	case ML_NAME:
		wcscpy(dest,ConvertUTF8ToWString(modules[row].name).c_str());
		break;
	case ML_ADDRESS:
		wsprintf(dest,L"%08X",modules[row].address);
		break;
	case ML_SIZE:
		wsprintf(dest,L"%08X",modules[row].size);
		break;
	case ML_ACTIVE:
		wcscpy(dest,modules[row].active ? L"true" : L"false");
		break;
	}
}

void CtrlModuleList::OnDoubleClick(int itemIndex, int column)
{
	SendMessage(GetParent(GetHandle()),WM_DEB_GOTOWPARAM,modules[itemIndex].address,0);
}

void CtrlModuleList::loadModules()
{
	modules = symbolMap.getAllModules();
	Update();
}
