// NOTE: Apologies for the quality of this code, this is really from pre-opensource Dolphin - that is, 2003.

#include "Core/Config.h"
#include "Core/MemMap.h"
#include "Windows/Resource.h"
#include "Windows/InputBox.h"

#include "Core/Debugger/Breakpoints.h"
#include "Core/Debugger/SymbolMap.h"
#include "Windows/Debugger/Debugger_MemoryDlg.h"
#include "Windows/Debugger/Debugger_Disasm.h"
#include "Windows/Debugger/Debugger_VFPUDlg.h"
#include "Windows/Debugger/DebuggerShared.h"
#include "Windows/Debugger/BreakpointWindow.h"

#include "Windows/main.h"
#include "Windows/Debugger/CtrlRegisterList.h"
#include "Windows/Debugger/CtrlMemView.h"
#include "Windows/Debugger/Debugger_Lists.h"
#include "Windows/WndMainWindow.h"

#include "Core/Core.h"
#include "Core/CPU.h"
#include "Core/HLE/HLE.h"
#include "Core/CoreTiming.h"
#include "Core/MIPS/MIPSAnalyst.h"

#include "base/stringutil.h"
#include "util/text/utf8.h"

#ifdef THEMES
#include "Windows/XPTheme.h"
#endif

#include "Common/CommonWindows.h"
#include <windowsx.h>
#include <commctrl.h>

// How long (max) to wait for Core to pause before clearing temp breakpoints.
const int TEMP_BREAKPOINT_WAIT_MS = 100;

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
	keepStatusBarText = false;

	SetWindowText(m_hDlg, ConvertUTF8ToWString(_cpu->GetName()).c_str());
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
	tcItem.pszText		= L"Regs";
	tcItem.cchTextMax	= (int)wcslen(tcItem.pszText)+1;
	tcItem.iImage		= 0;
	int result1 = TabCtrl_InsertItem(tabs, TabCtrl_GetItemCount(tabs),&tcItem);
	tcItem.pszText		= L"Funcs";
	tcItem.cchTextMax	= (int)wcslen(tcItem.pszText)+1;
	int result2 = TabCtrl_InsertItem(tabs, TabCtrl_GetItemCount(tabs),&tcItem);
	ShowWindow(GetDlgItem(m_hDlg, IDC_REGLIST), SW_NORMAL);
	ShowWindow(GetDlgItem(m_hDlg, IDC_FUNCTIONLIST), SW_HIDE);
	SetTimer(m_hDlg,1,1000,0);
	
	// subclass the goto edit box
	HWND editWnd = GetDlgItem(m_hDlg,IDC_ADDRESS);
	DefGotoEditProc = (WNDPROC)GetWindowLongPtr(editWnd,GWLP_WNDPROC);
	SetWindowLongPtr(editWnd,GWLP_WNDPROC,(LONG_PTR)GotoEditProc); 
	
	// init memory viewer
	CtrlMemView *mem = CtrlMemView::getFrom(GetDlgItem(m_hDlg,IDC_DEBUGMEMVIEW));
	mem->setDebugger(_cpu);
	
	breakpointList = new CtrlBreakpointList();
	breakpointList->setCpu(cpu);
	breakpointList->setDisasm(ptr);
	breakpointList->setDialogItem(GetDlgItem(m_hDlg,IDC_BREAKPOINTLIST));
	breakpointList->update();

	threadList = new CtrlThreadList();
	threadList->setDialogItem(GetDlgItem(m_hDlg,IDC_THREADLIST));
	threadList->reloadThreads();

	stackTraceView = new CtrlStackTraceView();
	stackTraceView->setCpu(cpu);
	stackTraceView->setDisasm(ptr);
	stackTraceView->setDialogItem(GetDlgItem(m_hDlg,IDC_STACKFRAMES));
	stackTraceView->loadStackTrace();
	
	// init bottom "tab"
	changeSubWindow(SUBWIN_FIRST);

	// init status bar
	statusBarWnd = CreateStatusWindow(WS_CHILD | WS_VISIBLE, L"", m_hDlg, IDC_DISASMSTATUSBAR);
	if (g_Config.bDisplayStatusBar == false)
	{
		ShowWindow(statusBarWnd,SW_HIDE);
	}

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


void CDisasm::changeSubWindow(SubWindowType type)
{
	HWND bp = GetDlgItem(m_hDlg, IDC_BREAKPOINTLIST);
	HWND mem = GetDlgItem(m_hDlg, IDC_DEBUGMEMVIEW);
	HWND threads = GetDlgItem(m_hDlg, IDC_THREADLIST);
	HWND stackFrames = GetDlgItem(m_hDlg, IDC_STACKFRAMES);

	// determine if any of the windows are focused, if not
	// then leave the focus unchanged
	HWND focus = GetFocus();
	bool changeFocus = (focus == bp || focus == mem || focus == threads || focus == stackFrames);

	if (type == SUBWIN_FIRST)
	{
		type = SUBWIN_MEM;
	} else if (type == SUBWIN_NEXT)
	{
		if (IsWindowVisible(mem))
		{
			type = SUBWIN_BREAKPOINT;
		} else if (IsWindowVisible(bp))
		{
			type = SUBWIN_THREADS;
		} else if (IsWindowVisible(threads))
		{
			type = SUBWIN_STACKFRAMES;
		} else {
			type = SUBWIN_MEM;
		}
	}

	ShowWindow(mem,type == SUBWIN_MEM ? SW_NORMAL : SW_HIDE);
	ShowWindow(bp,type == SUBWIN_BREAKPOINT ? SW_NORMAL : SW_HIDE);
	ShowWindow(threads,type == SUBWIN_THREADS ? SW_NORMAL : SW_HIDE);
	ShowWindow(stackFrames,type == SUBWIN_STACKFRAMES ? SW_NORMAL : SW_HIDE);

	if (changeFocus)
	{
		switch (type)
		{
		case SUBWIN_MEM:
			SetFocus(mem);
			break;
		case SUBWIN_BREAKPOINT:
			SetFocus(bp);
			break;
		case SUBWIN_THREADS:
			SetFocus(threads);
			break;
		case SUBWIN_STACKFRAMES:
			SetFocus(stackFrames);
			break;
		}
	}
}

void CDisasm::stepInto()
{
	if (Core_IsActive()) return;

	CtrlDisAsmView *ptr = CtrlDisAsmView::getFrom(GetDlgItem(m_hDlg,IDC_DISASMVIEW));
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
	stackTraceView->loadStackTrace();
	updateThreadLabel(false);
}

void CDisasm::stepOver()
{
	if (Core_IsActive()) return;
	
	CtrlDisAsmView *ptr = CtrlDisAsmView::getFrom(GetDlgItem(m_hDlg,IDC_DISASMVIEW));
	lastTicks = CoreTiming::GetTicks();

	// If the current PC is on a breakpoint, the user doesn't want to do nothing.
	CBreakPoints::SetSkipFirst(currentMIPS->pc);

	MIPSAnalyst::MipsOpcodeInfo info = MIPSAnalyst::GetOpcodeInfo(cpu,cpu->GetPC());
	ptr->setDontRedraw(true);
	u32 breakpointAddress = cpu->GetPC()+cpu->getInstructionSize(0);
	if (info.isBranch)
	{
		if (info.isConditional == false)
		{
			if (info.isLinkedBranch)	// jal, jalr
			{
				// it's a function call with a delay slot - skip that too
				breakpointAddress += cpu->getInstructionSize(0);
			} else {					// j, ...
				// in case of absolute branches, set the breakpoint at the branch target
				breakpointAddress = info.branchTarget;
			}
		} else {						// beq, ...
			// set breakpoint at branch target
			breakpointAddress = info.branchTarget;
			CBreakPoints::AddBreakPoint(breakpointAddress,true);

			// and after the delay slot
			breakpointAddress = cpu->GetPC()+2*cpu->getInstructionSize(0);	
		}
	}

	SetDebugMode(false);
	CBreakPoints::AddBreakPoint(breakpointAddress,true);
	_dbg_update_();
	Core_EnableStepping(false);
	Sleep(1);
	ptr->gotoAddr(breakpointAddress);
	UpdateDialog();
}

void CDisasm::stepOut()
{
	auto threads = GetThreadsInfo();

	u32 entry, stackTop;
	for (size_t i = 0; i < threads.size(); i++)
	{
		if (threads[i].isCurrent)
		{
			entry = threads[i].entrypoint;
			stackTop = threads[i].initialStack;
			break;
		}
	}

	auto frames = MIPSStackWalk::Walk(cpu->GetPC(),cpu->GetRegValue(0,31),cpu->GetRegValue(0,29),entry,stackTop);
	if (frames.size() < 2) return;
	u32 breakpointAddress = frames[1].pc;
	
	// If the current PC is on a breakpoint, the user doesn't want to do nothing.
	CBreakPoints::SetSkipFirst(currentMIPS->pc);
	
	CtrlDisAsmView *ptr = CtrlDisAsmView::getFrom(GetDlgItem(m_hDlg,IDC_DISASMVIEW));
	ptr->setDontRedraw(true);

	SetDebugMode(false);
	CBreakPoints::AddBreakPoint(breakpointAddress,true);
	_dbg_update_();
	Core_EnableStepping(false);
	Sleep(1);
	ptr->gotoAddr(breakpointAddress);
	UpdateDialog();
}

void CDisasm::runToLine()
{
	CtrlDisAsmView *ptr = CtrlDisAsmView::getFrom(GetDlgItem(m_hDlg,IDC_DISASMVIEW));
	u32 pos = ptr->getSelection();

	lastTicks = CoreTiming::GetTicks();
	ptr->setDontRedraw(true);
	SetDebugMode(false);
	CBreakPoints::AddBreakPoint(pos,true);
	_dbg_update_();
	Core_EnableStepping(false);
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
			breakpointList->handleNotify(lParam);
			break;
		case IDC_THREADLIST:
			threadList->handleNotify(lParam);
			break;
		case IDC_STACKFRAMES:
			stackTraceView->handleNotify(lParam);
			break;
		}
		break;
	case WM_COMMAND:
		{
			CtrlDisAsmView *ptr = CtrlDisAsmView::getFrom(GetDlgItem(m_hDlg,IDC_DISASMVIEW));
			CtrlRegisterList *reglist = CtrlRegisterList::getFrom(GetDlgItem(m_hDlg,IDC_REGLIST));
			switch(LOWORD(wParam))
			{
			case ID_TOGGLE_PAUSE:
				SendMessage(MainWindow::GetHWND(),WM_COMMAND,ID_TOGGLE_PAUSE,0);
				break;
				
			case ID_DEBUG_DISPLAYMEMVIEW:
				changeSubWindow(SUBWIN_MEM);
				break;

			case ID_DEBUG_DISPLAYBREAKPOINTLIST:
				changeSubWindow(SUBWIN_BREAKPOINT);
				break;

			case ID_DEBUG_DISPLAYTHREADLIST:
				changeSubWindow(SUBWIN_THREADS);
				break;

			case ID_DEBUG_DISPLAYSTACKFRAMELIST:
				changeSubWindow(SUBWIN_STACKFRAMES);
				break;

			case ID_DEBUG_DSIPLAYREGISTERLIST:
				TabCtrl_SetCurSel(GetDlgItem(m_hDlg, IDC_LEFTTABS),0);
				ShowWindow(GetDlgItem(m_hDlg, IDC_REGLIST), SW_NORMAL);
				ShowWindow(GetDlgItem(m_hDlg, IDC_FUNCTIONLIST), SW_HIDE);
				break;
				
			case ID_DEBUG_DSIPLAYFUNCTIONLIST:
				TabCtrl_SetCurSel(GetDlgItem(m_hDlg, IDC_LEFTTABS),1);
				ShowWindow(GetDlgItem(m_hDlg, IDC_REGLIST), SW_HIDE);
				ShowWindow(GetDlgItem(m_hDlg, IDC_FUNCTIONLIST), SW_NORMAL);
				break;

			case ID_DEBUG_ADDBREAKPOINT:
				{
					keepStatusBarText = true;
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
					keepStatusBarText = false;
				}
				break;

			case ID_DEBUG_STEPOVER:
				if (GetFocus() == GetDlgItem(m_hDlg,IDC_DISASMVIEW)) stepOver();
				break;

			case ID_DEBUG_STEPINTO:
				if (GetFocus() == GetDlgItem(m_hDlg,IDC_DISASMVIEW)) stepInto();
				break;

			case ID_DEBUG_RUNTOLINE:
				if (GetFocus() == GetDlgItem(m_hDlg,IDC_DISASMVIEW)) runToLine();
				break;

			case ID_DEBUG_STEPOUT:
				if (GetFocus() == GetDlgItem(m_hDlg,IDC_DISASMVIEW)) stepOut();
				break;

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

			case IDC_STOPGO:
				{
					if (!Core_IsStepping())		// stop
					{
						ptr->setDontRedraw(false);
						SetDebugMode(true);
						Core_EnableStepping(true);
						_dbg_update_();
						Sleep(1); //let cpu catch up
						ptr->gotoPC();
						UpdateDialog();
						vfpudlg->Update();
					} else {					// go
						lastTicks = CoreTiming::GetTicks();

						// If the current PC is on a breakpoint, the user doesn't want to do nothing.
						CBreakPoints::SetSkipFirst(currentMIPS->pc);

						SetDebugMode(false);
						Core_EnableStepping(false);
					}
				}
				break;

			case IDC_STEP:
				stepInto();
				break;

			case IDC_STEPOVER:
				stepOver();
				break;

			case IDC_STEPOUT:
				stepOut();
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

			case IDC_MEMCHECK:
				SendMessage(m_hDlg,WM_COMMAND,ID_DEBUG_ADDBREAKPOINT,0);
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
					ComboBox_SetItemData(list, ComboBox_AddString(list, ConvertUTF8ToWString(symbolMap.GetDescription(pc)).c_str()), pc);
					if (symbolMap.GetDescription(pc) != symbolMap.GetDescription(ra))
					{
						ComboBox_SetItemData(list, ComboBox_AddString(list, ConvertUTF8ToWString(symbolMap.GetDescription(ra)).c_str()), ra);
						count++;
					}
					//walk the stack chain
					while (addr != 0xFFFFFFFF && addr!=0 && count++<20)
					{
						DWORD fun = Memory::ReadUnchecked_U32(addr+4);
						const wchar_t *str = ConvertUTF8ToWString(symbolMap.GetDescription(fun)).c_str();
						if (wcslen(str) == 0)
							str = L"(unknown)";
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

	case WM_DEB_GOTOWPARAM:
	{
		CtrlDisAsmView *ptr = CtrlDisAsmView::getFrom(GetDlgItem(m_hDlg,IDC_DISASMVIEW));
		ptr->gotoAddr(wParam);
		SetFocus(GetDlgItem(m_hDlg,IDC_DISASMVIEW));
		break;
	}
	case WM_DEB_GOTOADDRESSEDIT:
		{
			wchar_t szBuffer[256];
			CtrlDisAsmView *ptr = CtrlDisAsmView::getFrom(GetDlgItem(m_hDlg,IDC_DISASMVIEW));
			GetWindowText(GetDlgItem(m_hDlg,IDC_ADDRESS),szBuffer,256);

			u32 addr;
			if (parseExpression(ConvertWStringToUTF8(szBuffer).c_str(),cpu,addr) == false)
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
		changeSubWindow(SUBWIN_NEXT);
		break;
	case WM_DEB_SETSTATUSBARTEXT:
		if (!keepStatusBarText)
			SendMessage(statusBarWnd,WM_SETTEXT,0,(LPARAM)ConvertUTF8ToWString((const char *)lParam).c_str());
		break;
	case WM_DEB_GOTOHEXEDIT:
		{
			CtrlMemView *memory = CtrlMemView::getFrom(GetDlgItem(m_hDlg,IDC_DEBUGMEMVIEW));
			memory->gotoAddr(wParam);
			
			// display the memory viewer too
			HWND bp = GetDlgItem(m_hDlg, IDC_BREAKPOINTLIST);
			HWND mem = GetDlgItem(m_hDlg, IDC_DEBUGMEMVIEW);
			HWND threads = GetDlgItem(m_hDlg, IDC_THREADLIST);
			ShowWindow(bp,SW_HIDE);
			ShowWindow(mem,SW_NORMAL);
			ShowWindow(threads,SW_HIDE);
		}
		break;
	case WM_SIZE:
		{
			UpdateSize(LOWORD(lParam), HIWORD(lParam));
			SendMessage(statusBarWnd,WM_SIZE,0,10);
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
	case WM_ACTIVATE:
		if (wParam == WA_ACTIVE || wParam == WA_CLICKACTIVE)
		{
			g_debuggerActive = true;
		} else {
			g_debuggerActive = false;
		}
		break;
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

	SetDlgItemText(m_hDlg, IDC_THREADNAME, ConvertUTF8ToWString(label).c_str());
}

void CDisasm::UpdateSize(WORD width, WORD height)
{
	HWND disasm = GetDlgItem(m_hDlg, IDC_DISASMVIEW);
	HWND funclist = GetDlgItem(m_hDlg, IDC_FUNCTIONLIST);
	HWND regList = GetDlgItem(m_hDlg, IDC_REGLIST);
	HWND breakpointList = GetDlgItem(m_hDlg, IDC_BREAKPOINTLIST);
	HWND memView = GetDlgItem(m_hDlg, IDC_DEBUGMEMVIEW);
	HWND threads = GetDlgItem(m_hDlg, IDC_THREADLIST);
	HWND stackFrame = GetDlgItem(m_hDlg,IDC_STACKFRAMES);

	if (g_Config.bDisplayStatusBar)
	{
		RECT statusRect;
		GetWindowRect(statusBarWnd,&statusRect);
		height -= (statusRect.bottom-statusRect.top);
	} else {
		height -= 2;
	}

	int defaultHeight = defaultRect.bottom - defaultRect.top;
	int breakpointHeight = defaultBreakpointRect.bottom - defaultBreakpointRect.top;
	if (height < defaultHeight)
		breakpointHeight -= defaultHeight - height;

	int breakpointTop = height-breakpointHeight-4;
	int regWidth = regRect.right - regRect.left;
	int regTop = 138;
	int disasmWidth = width-regWidth;
	int disasmTop = 25;

	MoveWindow(regList, 8, regTop, regWidth, height-regTop-breakpointHeight-8, TRUE);
	MoveWindow(funclist, 8, regTop, regWidth, height-regTop-breakpointHeight-8, TRUE);
	MoveWindow(disasm,regWidth+15,disasmTop,disasmWidth-20,height-disasmTop-breakpointHeight-8,TRUE);
	MoveWindow(breakpointList,8,breakpointTop,width-16,breakpointHeight,TRUE);
	MoveWindow(memView,8,breakpointTop,width-16,breakpointHeight,TRUE);
	MoveWindow(threads,8,breakpointTop,width-16,breakpointHeight,TRUE);
	MoveWindow(stackFrame,8,breakpointTop,width-16,breakpointHeight,TRUE);

	GetWindowRect(GetDlgItem(m_hDlg, IDC_REGLIST),&regRect);
	GetWindowRect(GetDlgItem(m_hDlg, IDC_DISASMVIEW),&disRect);
	GetWindowRect(GetDlgItem(m_hDlg, IDC_BREAKPOINTLIST),&breakpointRect);
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
		breakpointList->update();
		threadList->reloadThreads();
		stackTraceView->loadStackTrace();
		updateThreadLabel(false);

		SetDlgItemText(m_hDlg, IDC_STOPGO, L"Go");
		EnableWindow( GetDlgItem(hDlg, IDC_STEP), TRUE);
		EnableWindow( GetDlgItem(hDlg, IDC_STEPOVER), TRUE);
		EnableWindow( GetDlgItem(hDlg, IDC_STEPHLE), TRUE);
		EnableWindow( GetDlgItem(hDlg, IDC_STEPOUT), TRUE);
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
		
		SetDlgItemText(m_hDlg, IDC_STOPGO, L"Stop");
		EnableWindow( GetDlgItem(hDlg, IDC_STEP), FALSE);
		EnableWindow( GetDlgItem(hDlg, IDC_STEPOVER), FALSE);
		EnableWindow( GetDlgItem(hDlg, IDC_STEPHLE), FALSE);
		EnableWindow( GetDlgItem(hDlg, IDC_STEPOUT), FALSE);
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
		// TODO: wchar_t
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
	wchar_t tempTicks[24];
	_snwprintf(tempTicks, 24, L"%lld", CoreTiming::GetTicks()-lastTicks);
	SetDlgItemText(m_hDlg, IDC_DEBUG_COUNT, tempTicks);

	// Update Register Dialog
	for (int i=0; i<numCPUs; i++)
		if (memoryWindow[i])
			memoryWindow[i]->Update();

	// repaint windows at the bottom. only the memory view needs to be forced to
	// redraw. all others are updated manually
	InvalidateRect (GetDlgItem(m_hDlg, IDC_DEBUGMEMVIEW), NULL, TRUE);
	UpdateWindow (GetDlgItem(m_hDlg, IDC_DEBUGMEMVIEW));
}