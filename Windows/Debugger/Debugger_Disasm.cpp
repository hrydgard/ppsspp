// NOTE: Apologies for the quality of this code, this is really from pre-opensource Dolphin - that is, 2003.

#include "Core/Config.h"
#include "Core/MemMap.h"
#include "../Resource.h"
#include "../InputBox.h"

#include "../../Core/Debugger/Breakpoints.h"
#include "../../Core/Debugger/SymbolMap.h"
#include "Debugger_MemoryDlg.h"
#include "Debugger_Disasm.h"
#include "Debugger_VFPUDlg.h"
#include "DebuggerShared.h"
#include "BreakpointWindow.h"

#include "../main.h"
#include "CtrlRegisterList.h"
#include "CtrlMemView.h"
#include "Debugger_Lists.h"

#include "../../Core/Core.h"
#include "../../Core/CPU.h"
#include "../../Core/HLE/HLE.h"
#include "../../Core/CoreTiming.h"

#include "base/stringutil.h"

#ifdef THEMES
#include "../XPTheme.h"
#endif

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>

char* breakpointColumns[] = {
	"Type", "Offset", "Size/Label", "Opcode", "Hits", "Enabled"
};

float breakpointColumnSizes[] = {
	0.12f, 0.2f, 0.2f, 0.3f, 0.1f, 0.08f
};

enum { BPL_TYPE, BPL_OFFSET, BPL_SIZELABEL, BPL_OPCODE, BPL_HITS, BPL_ENABLED, BPL_COLUMNCOUNT };

// How long (max) to wait for Core to pause before clearing temp breakpoints.
const int TEMP_BREAKPOINT_WAIT_MS = 100;

static FAR WNDPROC DefBreakpointListProc;

static LRESULT CALLBACK BreakpointListProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch(message)
	{
	case WM_KEYDOWN:
		if(wParam == VK_RETURN)
		{
			int index = ListView_GetSelectionMark(hDlg);
			SendMessage(GetParent(hDlg),WM_DEB_GOTOBREAKPOINT,index,0);
			return 0;
		} else if (wParam == VK_DELETE)
		{
			int index = ListView_GetSelectionMark(hDlg);
			SendMessage(GetParent(hDlg),WM_DEB_REMOVEBREAKPOINT,index,0);
			return 0;
		} else if (wParam == VK_TAB)
		{
			SendMessage(GetParent(hDlg),WM_DEB_TABPRESSED,0,0);
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

	return (LRESULT)CallWindowProc((WNDPROC)DefBreakpointListProc,hDlg,message,wParam,lParam);;
}



FAR WNDPROC DefGotoEditProc;

LRESULT CALLBACK GotoEditProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch(message)
	{
	case WM_KEYDOWN:
		if( wParam == VK_RETURN )
		{
			SendMessage(GetParent(hDlg),WM_DEB_GOTOADDRESSEDIT,0,0);
			return 0;
		}
		break;
	case WM_KEYUP:
		if( wParam == VK_RETURN ) return 0;
		break;
	case WM_CHAR:
		if( wParam == VK_RETURN ) return 0;
		break;
	case WM_GETDLGCODE:
		if (lParam && ((MSG*)lParam)->message == WM_KEYDOWN)
		{
			if (wParam == VK_RETURN) return DLGC_WANTMESSAGE;
		}
		break;
	};

	return (LRESULT)CallWindowProc((WNDPROC)DefGotoEditProc,hDlg,message,wParam,lParam);
}


CDisasm::CDisasm(HINSTANCE _hInstance, HWND _hParent, DebugInterface *_cpu) : Dialog((LPCSTR)IDD_DISASM, _hInstance, _hParent)
{
	cpu = _cpu;
	lastTicks = CoreTiming::GetTicks();

	SetWindowText(m_hDlg,_cpu->GetName());
#ifdef THEMES
	//if (WTL::CTheme::IsThemingSupported())
		//EnableThemeDialogTexture(m_hDlg ,ETDT_ENABLETAB);
#endif
	int x = g_Config.iDisasmWindowX == -1 ? 500 : g_Config.iDisasmWindowX;
	int y = g_Config.iDisasmWindowY == -1 ? 200 : g_Config.iDisasmWindowY;
	int w = g_Config.iDisasmWindowW;
	int h = g_Config.iDisasmWindowH;
	// Start with the initial size so we have the right minimum size from the rc.
	SetWindowPos(m_hDlg, 0, x, y, 0, 0, SWP_NOSIZE);

	CtrlDisAsmView *ptr = CtrlDisAsmView::getFrom(GetDlgItem(m_hDlg,IDC_DISASMVIEW));
	ptr->setDebugger(cpu);
	ptr->gotoAddr(0x00000000);

	CtrlRegisterList *rl = CtrlRegisterList::getFrom(GetDlgItem(m_hDlg,IDC_REGLIST));
	rl->setCPU(cpu);

	GetWindowRect(m_hDlg, &defaultRect);

	//symbolMap.FillSymbolListBox(GetDlgItem(m_hDlg, IDC_FUNCTIONLIST),ST_FUNCTION);
	symbolMap.FillSymbolComboBox(GetDlgItem(m_hDlg, IDC_FUNCTIONLIST),ST_FUNCTION);

	GetWindowRect(GetDlgItem(m_hDlg, IDC_REGLIST), &regRect);
	GetWindowRect(GetDlgItem(m_hDlg, IDC_DISASMVIEW), &disRect);
	GetWindowRect(GetDlgItem(m_hDlg, IDC_BREAKPOINTLIST), &breakpointRect);
	GetWindowRect(GetDlgItem(m_hDlg, IDC_BREAKPOINTLIST), &defaultBreakpointRect);

	HWND tabs = GetDlgItem(m_hDlg, IDC_LEFTTABS);

	TCITEM tcItem;
	ZeroMemory (&tcItem,sizeof (tcItem));
	tcItem.mask			= TCIF_TEXT;
	tcItem.dwState		= 0;
	tcItem.pszText		= "Regs";
	tcItem.cchTextMax	= (int)strlen(tcItem.pszText)+1;
	tcItem.iImage		= 0;
	int result1 = TabCtrl_InsertItem(tabs, TabCtrl_GetItemCount(tabs),&tcItem);
	tcItem.pszText		= "Funcs";
	tcItem.cchTextMax	= (int)strlen(tcItem.pszText)+1;
	int result2 = TabCtrl_InsertItem(tabs, TabCtrl_GetItemCount(tabs),&tcItem);
	ShowWindow(GetDlgItem(m_hDlg, IDC_REGLIST), SW_NORMAL);
	ShowWindow(GetDlgItem(m_hDlg, IDC_FUNCTIONLIST), SW_HIDE);
	SetTimer(m_hDlg,1,1000,0);
	
	// subclass the goto edit box
	HWND editWnd = GetDlgItem(m_hDlg,IDC_ADDRESS);
	DefGotoEditProc = (WNDPROC)GetWindowLongPtr(editWnd,GWLP_WNDPROC);
	SetWindowLongPtr(editWnd,GWLP_WNDPROC,(LONG_PTR)GotoEditProc); 
	
	// subclass the breakpoint list
	HWND breakpointHwnd = GetDlgItem(m_hDlg, IDC_BREAKPOINTLIST);
	DefBreakpointListProc = (WNDPROC)GetWindowLongPtr(breakpointHwnd,GWLP_WNDPROC);
	SetWindowLongPtr(breakpointHwnd,GWLP_WNDPROC,(LONG_PTR)BreakpointListProc); 

	// create columns for the breakpoint list
	SendMessage(breakpointHwnd, LVM_SETEXTENDEDLISTVIEWSTYLE, 0, LVS_EX_FULLROWSELECT);

	LVCOLUMN lvc; 
	lvc.mask = LVCF_FMT | LVCF_WIDTH | LVCF_TEXT | LVCF_SUBITEM;
	lvc.iSubItem = 0;
	lvc.fmt = LVCFMT_LEFT;
	
	int totalListSize = (breakpointRect.right-breakpointRect.left-20);
	for (int i = 0; i < BPL_COLUMNCOUNT; i++)
	{
		lvc.cx = breakpointColumnSizes[i] * totalListSize;
		lvc.pszText = breakpointColumns[i];
		ListView_InsertColumn(breakpointHwnd, i, &lvc);
	}

	// init memory viewer
	CtrlMemView *mem = CtrlMemView::getFrom(GetDlgItem(m_hDlg,IDC_DEBUGMEMVIEW));
	mem->setDebugger(_cpu);

	threadList = new CtrlThreadList();
	threadList->setDialogItem(GetDlgItem(m_hDlg,IDC_THREADLIST));
	threadList->reloadThreads();

	// init memory/breakpoint "tab"
	ShowWindow(GetDlgItem(m_hDlg, IDC_BREAKPOINTLIST), SW_HIDE);
	ShowWindow(GetDlgItem(m_hDlg, IDC_DEBUGMEMVIEW), SW_NORMAL);
	ShowWindow(GetDlgItem(m_hDlg, IDC_THREADLIST), SW_HIDE);

	// Actually resize the window to the proper size (after the above setup.)
	if (w != -1 && h != -1)
	{
		// this will also call UpdateSize
		SetWindowPos(m_hDlg, 0, x, y, w, h, 0);
	}

	SetDebugMode(true);
}

CDisasm::~CDisasm()
{
}

int CDisasm::getTotalBreakpointCount()
{
	int count = (int)CBreakPoints::GetMemChecks().size();
	for (size_t i = 0; i < CBreakPoints::GetBreakpoints().size(); i++)
	{
		if (!displayedBreakPoints_[i].temporary) count++;
	}

	return count;
}

void CDisasm::updateBreakpointList()
{
	// Update the items we're displaying from the debugger.
	displayedBreakPoints_ = CBreakPoints::GetBreakpoints();
	displayedMemChecks_= CBreakPoints::GetMemChecks();

	HWND breakpointHwnd = GetDlgItem(m_hDlg, IDC_BREAKPOINTLIST);
	int breakpointCount = getTotalBreakpointCount();
	int items = ListView_GetItemCount(breakpointHwnd);

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

		ListView_InsertItem(breakpointHwnd, &lvI);
		items++;
	}

	while (items > breakpointCount)
	{
		ListView_DeleteItem(breakpointHwnd,--items);
	}

	InvalidateRect(breakpointHwnd,NULL,true);
	UpdateWindow(breakpointHwnd);
}

int CDisasm::getBreakpointIndex(int itemIndex, bool& isMemory)
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

void CDisasm::removeBreakpoint(int itemIndex)
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

void CDisasm::gotoBreakpointAddress(int itemIndex)
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
		Goto(address);
		SetFocus(GetDlgItem(m_hDlg, IDC_DISASMVIEW));
	}
}

static char breakpointText[256];

void CDisasm::handleBreakpointNotify(LPARAM lParam)
{
	if (((LPNMHDR)lParam)->code == NM_DBLCLK)
	{
		LPNMITEMACTIVATE item = (LPNMITEMACTIVATE) lParam;
		gotoBreakpointAddress(item->iItem);
		return;
	}

	if (((LPNMHDR)lParam)->code == LVN_GETDISPINFO)
	{
		NMLVDISPINFO* dispInfo = (NMLVDISPINFO*)lParam;
		
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
					CtrlDisAsmView *ptr = CtrlDisAsmView::getFrom(GetDlgItem(m_hDlg,IDC_DISASMVIEW));
					ptr->getOpcodeText(displayedBreakPoints_[index].addr,breakpointText);
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

BOOL CDisasm::DlgProc(UINT message, WPARAM wParam, LPARAM lParam)
{
	//if (!m_hDlg) return FALSE;
	switch(message)
	{
	case WM_INITDIALOG:
		{
			return TRUE;
		}
		break;

	case WM_TIMER:
		{
			int iPage = TabCtrl_GetCurSel (GetDlgItem(m_hDlg, IDC_LEFTTABS));
			ShowWindow(GetDlgItem(m_hDlg, IDC_FUNCTIONLIST), iPage?SW_NORMAL:SW_HIDE);
			ShowWindow(GetDlgItem(m_hDlg, IDC_REGLIST),      iPage?SW_HIDE:SW_NORMAL);
		}
		break;

	case WM_NOTIFY:
		switch (wParam)
		{
		case IDC_LEFTTABS:
			{
				HWND tabs = GetDlgItem(m_hDlg, IDC_LEFTTABS);
				NMHDR* pNotifyMessage = NULL;
				pNotifyMessage = (LPNMHDR)lParam; 		
				if (pNotifyMessage->hwndFrom == tabs)
				{
					int iPage = TabCtrl_GetCurSel (tabs);
					ShowWindow(GetDlgItem(m_hDlg, IDC_FUNCTIONLIST), iPage?SW_NORMAL:SW_HIDE);
					ShowWindow(GetDlgItem(m_hDlg, IDC_REGLIST),      iPage?SW_HIDE:SW_NORMAL);
				}
			}
			break;
		case IDC_BREAKPOINTLIST:
			handleBreakpointNotify(lParam);
			break;
		case IDC_THREADLIST:
			threadList->handleNotify(lParam);
			break;
		}
		break;
	case WM_COMMAND:
		{
			CtrlDisAsmView *ptr = CtrlDisAsmView::getFrom(GetDlgItem(m_hDlg,IDC_DISASMVIEW));
			CtrlRegisterList *reglist = CtrlRegisterList::getFrom(GetDlgItem(m_hDlg,IDC_REGLIST));
			switch(LOWORD(wParam))
			{
			case IDC_SHOWVFPU:
				vfpudlg->Show(true);
				break;

			case IDC_FUNCTIONLIST: 
				switch (HIWORD(wParam))
				{
				case CBN_DBLCLK:
					{
						HWND lb = GetDlgItem(m_hDlg,LOWORD(wParam));
						int n = ListBox_GetCurSel(lb);
						if (n!=-1)
						{
							unsigned int addr = (unsigned int)ListBox_GetItemData(lb,n);
							ptr->gotoAddr(addr);
							SetFocus(GetDlgItem(m_hDlg, IDC_DISASMVIEW));
						}
					}
					break;
				};
				break;

			case IDC_GOTOINT:
				switch (HIWORD(wParam))
				{
				case LBN_SELCHANGE:
					{
						HWND lb =GetDlgItem(m_hDlg,LOWORD(wParam));
						int n = ComboBox_GetCurSel(lb);
						unsigned int addr = (unsigned int)ComboBox_GetItemData(lb,n);
						if (addr != 0xFFFFFFFF)
						{
							ptr->gotoAddr(addr);
							SetFocus(GetDlgItem(m_hDlg, IDC_DISASMVIEW));
						}
					}
					break;
				};
				break;

			case IDC_GO:
				{
					lastTicks = CoreTiming::GetTicks();

					// If the current PC is on a breakpoint, the user doesn't want to do nothing.
					CBreakPoints::SetSkipFirst(currentMIPS->pc);

					SetDebugMode(false);
					Core_EnableStepping(false);
				}
				break;

			case IDC_STEP:
				{
					if (Core_IsActive()) break;
					lastTicks = CoreTiming::GetTicks();

					// If the current PC is on a breakpoint, the user doesn't want to do nothing.
					CBreakPoints::SetSkipFirst(currentMIPS->pc);

					Core_DoSingleStep();		
					Sleep(1);
					_dbg_update_();
					ptr->gotoPC();
					UpdateDialog();
					vfpudlg->Update();

					CtrlMemView::getFrom(GetDlgItem(m_hDlg,IDC_DEBUGMEMVIEW))->redraw();
					threadList->reloadThreads();
					updateThreadLabel(false);
				}
				break;

			case IDC_STEPOVER:
				{
					if (Core_IsActive()) break;
					lastTicks = CoreTiming::GetTicks();

					// If the current PC is on a breakpoint, the user doesn't want to do nothing.
					CBreakPoints::SetSkipFirst(currentMIPS->pc);

					const char* dis = cpu->disasm(cpu->GetPC(),4);
					const char* pos = strstr(dis,"->$");
					const char* reg = strstr(dis,"->");
					
					ptr->setDontRedraw(true);
					u32 breakpointAddress = cpu->GetPC()+cpu->getInstructionSize(0);
					if (memcmp(dis,"jal\t",4) == 0 || memcmp(dis,"jalr\t",5) == 0)
					{
						// it's a function call with a delay slot - skip that too
						breakpointAddress += cpu->getInstructionSize(0);
					} else if (memcmp(dis,"j\t",2) == 0 || memcmp(dis,"b\t",2) == 0)
					{
						// in case of absolute branches, set the breakpoint at the branch target
						sscanf(pos+3,"%08x",&breakpointAddress);
					} else if (memcmp(dis,"jr\t",3) == 0)
					{
						// the same for jumps to registers
						int regNum = -1;
						for (int i = 0; i < 32; i++)
						{
							if (strcasecmp(reg+2,cpu->GetRegName(0,i)) == 0)
							{
								regNum = i;
								break;
							}
						}
						if (regNum == -1) break;
						breakpointAddress = cpu->GetRegValue(0,regNum);
					} else if (pos != NULL)
					{
						// get branch target
						sscanf(pos+3,"%08x",&breakpointAddress);
						CBreakPoints::AddBreakPoint(breakpointAddress,true);

						// also add a breakpoint after the delay slot
						breakpointAddress = cpu->GetPC()+2*cpu->getInstructionSize(0);						
					}

					SetDebugMode(false);
					CBreakPoints::AddBreakPoint(breakpointAddress,true);
					_dbg_update_();
					Core_EnableStepping(false);
					Sleep(1);
					ptr->gotoAddr(breakpointAddress);
					UpdateDialog();
				}
				break;
				
			case IDC_STEPHLE:
				{
					if (Core_IsActive())
						break;
					lastTicks = CoreTiming::GetTicks();

					// If the current PC is on a breakpoint, the user doesn't want to do nothing.
					CBreakPoints::SetSkipFirst(currentMIPS->pc);

					hleDebugBreak();
					SetDebugMode(false);
					_dbg_update_();
					Core_EnableStepping(false);
				}
				break;

			case IDC_STOP:
				{				
					ptr->setDontRedraw(false);
					SetDebugMode(true);
					Core_EnableStepping(true);
					_dbg_update_();
					Sleep(1); //let cpu catch up
					ptr->gotoPC();
					UpdateDialog();
					vfpudlg->Update();
				}
				break;

			case IDC_SKIP:
				{
					cpu->SetPC(cpu->GetPC() + cpu->getInstructionSize(0));
					Sleep(1);
					ptr->gotoPC();
					UpdateDialog();
				}
				break;

			case IDC_MEMCHECK:
				{
					bool isRunning = Core_IsActive();
					if (isRunning)
					{
						SetDebugMode(true);
						Core_EnableStepping(true);
						Core_WaitInactive(200);
					}

					BreakpointWindow bpw(m_hDlg,cpu);
					if (bpw.exec()) bpw.addBreakpoint();

					if (isRunning)
					{
						SetDebugMode(false);
						Core_EnableStepping(false);
					}
				}
				break;
			case IDC_UPDATECALLSTACK:
				{
					HWND hDlg = m_hDlg;
					HWND list = GetDlgItem(hDlg,IDC_CALLSTACK);
					ComboBox_ResetContent(list);
					
					u32 pc = currentMIPS->pc;
					u32 ra = currentMIPS->r[MIPS_REG_RA];
					DWORD addr = Memory::ReadUnchecked_U32(pc);
					int count=1;
					ComboBox_SetItemData(list,ComboBox_AddString(list,symbolMap.GetDescription(pc)),pc);
					if (symbolMap.GetDescription(pc) != symbolMap.GetDescription(ra))
					{
						ComboBox_SetItemData(list,ComboBox_AddString(list,symbolMap.GetDescription(ra)),ra);
						count++;
					}
					//walk the stack chain
					while (addr != 0xFFFFFFFF && addr!=0 && count++<20)
					{
						DWORD fun = Memory::ReadUnchecked_U32(addr+4);
						const char *str = symbolMap.GetDescription(fun);
						if (strlen(str)==0)
							str = "(unknown)";
						ComboBox_SetItemData(list, ComboBox_AddString(list,str), fun);
						addr = Memory::ReadUnchecked_U32(addr);
					}
					ComboBox_SetCurSel(list,0);
				}
				break;

			case IDC_GOTOPC:
				{
					ptr->gotoPC();	
					SetFocus(GetDlgItem(m_hDlg, IDC_DISASMVIEW));
					UpdateDialog();
				}
				break;
			case IDC_GOTOLR:
				{
					ptr->gotoAddr(cpu->GetLR());
					SetFocus(GetDlgItem(m_hDlg, IDC_DISASMVIEW));
				}
				break;

			case IDC_BACKWARDLINKS:
				{
					HWND box = GetDlgItem(m_hDlg, IDC_FUNCTIONLIST); 
					int funcnum = symbolMap.GetSymbolNum(ListBox_GetItemData(box,ListBox_GetCurSel(box)));
					if (funcnum!=-1)
						symbolMap.FillListBoxBLinks(box,funcnum);
					break;
				}

			case IDC_ALLFUNCTIONS:
				{
					symbolMap.FillSymbolListBox(GetDlgItem(m_hDlg, IDC_FUNCTIONLIST),ST_FUNCTION);
					break;
				}
			default:
				return FALSE;
			}
			return TRUE;
		}

	case WM_DEB_MAPLOADED:
		NotifyMapLoaded();
		break;
	case WM_DEB_RUNTOWPARAM:
	{
		lastTicks = CoreTiming::GetTicks();
		CtrlDisAsmView *ptr = CtrlDisAsmView::getFrom(GetDlgItem(m_hDlg,IDC_DISASMVIEW));
		ptr->setDontRedraw(true);
		SetDebugMode(false);
		CBreakPoints::AddBreakPoint(wParam,true);
		_dbg_update_();
		Core_EnableStepping(false);
		break;
	}
	case WM_DEB_GOTOWPARAM:
	{
		CtrlDisAsmView *ptr = CtrlDisAsmView::getFrom(GetDlgItem(m_hDlg,IDC_DISASMVIEW));
		ptr->gotoAddr(wParam);
		SetFocus(GetDlgItem(m_hDlg,IDC_DISASMVIEW));
		break;
	}
	case WM_DEB_GOTOBREAKPOINT:
		gotoBreakpointAddress(wParam);
		break;
	case WM_DEB_REMOVEBREAKPOINT:
		removeBreakpoint(wParam);
		break;
	case WM_DEB_GOTOADDRESSEDIT:
		{
			char szBuffer[256];
			CtrlDisAsmView *ptr = CtrlDisAsmView::getFrom(GetDlgItem(m_hDlg,IDC_DISASMVIEW));
			GetWindowText(GetDlgItem(m_hDlg,IDC_ADDRESS),szBuffer,256);

			u32 addr;
			if (parseExpression(szBuffer,cpu,addr) == false)
			{
				displayExpressionError(GetDlgItem(m_hDlg,IDC_ADDRESS));
			} else {
				ptr->gotoAddr(addr);
				SetFocus(GetDlgItem(m_hDlg, IDC_DISASMVIEW));
			}
			UpdateDialog();
		}
		break;

	case WM_DEB_SETDEBUGLPARAM:
		SetDebugMode(lParam != 0);
		return TRUE;

	case WM_DEB_UPDATE:
		Update();
		return TRUE;

	case WM_DEB_TABPRESSED:
		{
			HWND bp = GetDlgItem(m_hDlg, IDC_BREAKPOINTLIST);
			HWND mem = GetDlgItem(m_hDlg, IDC_DEBUGMEMVIEW);
			HWND threads = GetDlgItem(m_hDlg, IDC_THREADLIST);
			
			if (IsWindowVisible(bp))
			{
				ShowWindow(bp,SW_HIDE);
				ShowWindow(mem,SW_HIDE);
				ShowWindow(threads,SW_NORMAL);
				SetFocus(threads);
			} else if (IsWindowVisible(threads))
			{
				ShowWindow(bp,SW_HIDE);
				ShowWindow(mem,SW_NORMAL);
				ShowWindow(threads,SW_HIDE);
				SetFocus(mem);
			} else {
				ShowWindow(bp,SW_NORMAL);
				ShowWindow(mem,SW_HIDE);
				ShowWindow(threads,SW_HIDE);
				SetFocus(bp);
			}
		}
		break;
	case WM_SIZE:
		{
			UpdateSize(LOWORD(lParam), HIWORD(lParam));
			SavePosition();
			return TRUE;
		}

	case WM_MOVE:
		SavePosition();
		break;
	case WM_GETMINMAXINFO:
		{
			MINMAXINFO *m = (MINMAXINFO *)lParam;
			// Reduce the minimum size slightly, so they can size it however they like.
			m->ptMinTrackSize.x = defaultRect.right - defaultRect.left - 100;
			//m->ptMaxTrackSize.x = m->ptMinTrackSize.x;
			m->ptMinTrackSize.y = defaultRect.bottom - defaultRect.top - 200;
		}
		return TRUE;
	case WM_CLOSE:
		Show(false);
		return TRUE;
	}
	return FALSE;
}

void CDisasm::updateThreadLabel(bool clear)
{
	char label[512];
	if (clear)
	{
		sprintf(label,"Thread: -");
	} else {
		sprintf(label,"Thread: %s",threadList->getCurrentThreadName());
	}

	SetDlgItemText(m_hDlg, IDC_THREADNAME,label);
}

void CDisasm::UpdateSize(WORD width, WORD height)
{
	HWND disasm = GetDlgItem(m_hDlg, IDC_DISASMVIEW);
	HWND funclist = GetDlgItem(m_hDlg, IDC_FUNCTIONLIST);
	HWND regList = GetDlgItem(m_hDlg, IDC_REGLIST);
	HWND breakpointList = GetDlgItem(m_hDlg, IDC_BREAKPOINTLIST);
	HWND memView = GetDlgItem(m_hDlg, IDC_DEBUGMEMVIEW);
	HWND threads = GetDlgItem(m_hDlg, IDC_THREADLIST);

	int defaultHeight = defaultRect.bottom - defaultRect.top;
	int breakpointHeight = defaultBreakpointRect.bottom - defaultBreakpointRect.top;
	if (height < defaultHeight)
		breakpointHeight -= defaultHeight - height;

	int breakpointTop = height-breakpointHeight-8;
	int regWidth = regRect.right - regRect.left;
	int regTop = 138;
	int disasmWidth = width-regWidth;
	int disasmTop = 25;

	MoveWindow(regList, 8, regTop, regWidth, height-regTop-breakpointHeight-12, TRUE);
	MoveWindow(funclist, 8, regTop, regWidth, height-regTop-breakpointHeight-12, TRUE);
	MoveWindow(disasm,regWidth+15,disasmTop,disasmWidth-20,height-disasmTop-breakpointHeight-12,TRUE);
	MoveWindow(breakpointList,8,breakpointTop,width-16,breakpointHeight,TRUE);
	MoveWindow(memView,8,breakpointTop,width-16,breakpointHeight,TRUE);
	MoveWindow(threads,8,breakpointTop,width-16,breakpointHeight,TRUE);

	GetWindowRect(GetDlgItem(m_hDlg, IDC_REGLIST),&regRect);
	GetWindowRect(GetDlgItem(m_hDlg, IDC_DISASMVIEW),&disRect);
	GetWindowRect(GetDlgItem(m_hDlg, IDC_BREAKPOINTLIST),&breakpointRect);

	int totalListSize = (breakpointRect.right-breakpointRect.left-20);
	for (int i = 0; i < BPL_COLUMNCOUNT; i++)
	{
		ListView_SetColumnWidth(breakpointList,i,breakpointColumnSizes[i] * totalListSize);
	}		
}

void CDisasm::SavePosition()
{
	RECT rc;
	if (GetWindowRect(m_hDlg, &rc))
	{
		g_Config.iDisasmWindowX = rc.left;
		g_Config.iDisasmWindowY = rc.top;
		g_Config.iDisasmWindowW = rc.right - rc.left;
		g_Config.iDisasmWindowH = rc.bottom - rc.top;
	}
}

void CDisasm::SetDebugMode(bool _bDebug)
{
	HWND hDlg = m_hDlg;

	// Update Dialog Windows
	if (_bDebug)
	{
		Core_WaitInactive(TEMP_BREAKPOINT_WAIT_MS);
		CBreakPoints::ClearTemporaryBreakPoints();
		updateBreakpointList();
		threadList->reloadThreads();
		updateThreadLabel(false);

		EnableWindow( GetDlgItem(hDlg, IDC_GO),	  TRUE);
		EnableWindow( GetDlgItem(hDlg, IDC_STEP), TRUE);
		EnableWindow( GetDlgItem(hDlg, IDC_STEPOVER), TRUE);
		EnableWindow( GetDlgItem(hDlg, IDC_STEPHLE), TRUE);
		EnableWindow( GetDlgItem(hDlg, IDC_STOP), FALSE);
		EnableWindow( GetDlgItem(hDlg, IDC_SKIP), TRUE);
		CtrlDisAsmView *ptr = CtrlDisAsmView::getFrom(GetDlgItem(m_hDlg,IDC_DISASMVIEW));
		ptr->setDontRedraw(false);
		ptr->gotoPC();
		
		CtrlMemView *mem = CtrlMemView::getFrom(GetDlgItem(m_hDlg,IDC_DEBUGMEMVIEW));
		mem->redraw();

		// update the callstack
		//CDisam::blah blah
		UpdateDialog();
	}
	else
	{
		updateThreadLabel(true);

		EnableWindow( GetDlgItem(hDlg, IDC_GO),	  FALSE);
		EnableWindow( GetDlgItem(hDlg, IDC_STEP), FALSE);
		EnableWindow( GetDlgItem(hDlg, IDC_STEPOVER), FALSE);
		EnableWindow( GetDlgItem(hDlg, IDC_STEPHLE), FALSE);
		EnableWindow( GetDlgItem(hDlg, IDC_STOP), TRUE);
		EnableWindow( GetDlgItem(hDlg, IDC_SKIP), FALSE);		
		CtrlRegisterList *reglist = CtrlRegisterList::getFrom(GetDlgItem(m_hDlg,IDC_REGLIST));
		reglist->redraw();
	}
}

void CDisasm::NotifyMapLoaded()
{
	symbolMap.FillSymbolListBox(GetDlgItem(m_hDlg, IDC_FUNCTIONLIST),ST_FUNCTION);
	CtrlDisAsmView *ptr = CtrlDisAsmView::getFrom(GetDlgItem(m_hDlg,IDC_DISASMVIEW));
	ptr->redraw();
}

void CDisasm::Goto(u32 addr)
{
	CtrlDisAsmView *ptr = CtrlDisAsmView::getFrom(GetDlgItem(m_hDlg,IDC_DISASMVIEW));
	ptr->gotoAddr(addr);
	SetFocus(GetDlgItem(m_hDlg, IDC_DISASMVIEW));
	ptr->redraw();
	
}

void CDisasm::UpdateDialog(bool _bComplete)
{
	HWND gotoInt = GetDlgItem(m_hDlg, IDC_GOTOINT);
	/*
	ComboBox_ResetContent(gotoInt);
	for (int i=0; i<numRegions; i++)
	{
		int n = ComboBox_AddString(gotoInt,regions[i].name);
		ComboBox_SetItemData(gotoInt,n,regions[i].start);
	}
	ComboBox_InsertString(gotoInt,0,"[Goto Rgn]");
	ComboBox_SetItemData(gotoInt,0,0xFFFFFFFF);
	ComboBox_SetCurSel(gotoInt,0);
*/
	CtrlDisAsmView *ptr = CtrlDisAsmView::getFrom(GetDlgItem(m_hDlg,IDC_DISASMVIEW));
	ptr->redraw();
	CtrlRegisterList *rl = CtrlRegisterList::getFrom(GetDlgItem(m_hDlg,IDC_REGLIST));
	rl->redraw();						
	// Update Debug Counter
	char tempTicks[24];
	sprintf(tempTicks, "%lld", CoreTiming::GetTicks()-lastTicks);
	SetDlgItemText(m_hDlg, IDC_DEBUG_COUNT, tempTicks);

	// Update Register Dialog
	for (int i=0; i<numCPUs; i++)
		if (memoryWindow[i])
			memoryWindow[i]->Update();
}