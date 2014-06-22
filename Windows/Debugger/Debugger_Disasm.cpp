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

static const int numCPUs = 1;

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

FAR WNDPROC DefFuncListProc;

LRESULT CALLBACK FuncListProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch(message)
	{
	case WM_KEYDOWN:
		if( wParam == VK_RETURN )
		{
			SendMessage(GetParent(hDlg),WM_COMMAND,MAKEWPARAM(IDC_FUNCTIONLIST,CBN_DBLCLK),0);
			SetFocus(hDlg);	// it's more natural to keep the focus when using keyboard controls
			return 0;
		}
		break;
	case WM_GETDLGCODE:
		if (lParam && ((MSG*)lParam)->message == WM_KEYDOWN)
		{
			if (wParam == VK_RETURN) return DLGC_WANTMESSAGE;
		}
		break;
	};

	return (LRESULT)CallWindowProc((WNDPROC)DefFuncListProc,hDlg,message,wParam,lParam);
}

CDisasm::CDisasm(HINSTANCE _hInstance, HWND _hParent, DebugInterface *_cpu) : Dialog((LPCSTR)IDD_DISASM, _hInstance, _hParent)
{
	cpu = _cpu;
	lastTicks = PSP_IsInited() ? CoreTiming::GetTicks() : 0;
	keepStatusBarText = false;
	hideBottomTabs = false;

	SetWindowText(m_hDlg, ConvertUTF8ToWString(_cpu->GetName()).c_str());
#ifdef THEMES
	//if (WTL::CTheme::IsThemingSupported())
		//EnableThemeDialogTexture(m_hDlg ,ETDT_ENABLETAB);
#endif

	RECT windowRect;
	GetWindowRect(m_hDlg,&windowRect);
	int defaultWidth = windowRect.right-windowRect.left;
	int defaultHeight = windowRect.bottom-windowRect.top;
	minWidth = defaultWidth - 100;
	minHeight = defaultHeight - 200;

	int x = g_Config.iDisasmWindowX == -1 ? windowRect.left : g_Config.iDisasmWindowX;
	int y = g_Config.iDisasmWindowY == -1 ? windowRect.top : g_Config.iDisasmWindowY;
	int w = g_Config.iDisasmWindowW == -1 ? defaultWidth : g_Config.iDisasmWindowW;
	int h = g_Config.iDisasmWindowH == -1 ? defaultHeight : g_Config.iDisasmWindowH;

	// init status bar
	statusBarWnd = CreateWindowEx(0, STATUSCLASSNAME, L"", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, m_hDlg, (HMENU)IDC_DISASMSTATUSBAR, _hInstance, NULL);
	if (g_Config.bDisplayStatusBar == false)
	{
		ShowWindow(statusBarWnd,SW_HIDE);
	}

	// set it to use two parts
	RECT statusBarRect;
	GetClientRect(statusBarWnd,&statusBarRect);
	
	int parts[2];
	parts[1] = statusBarRect.right-statusBarRect.left;
	parts[0] = parts[1]*2./3.;

	SendMessage(statusBarWnd, SB_SETPARTS, (WPARAM) 2, (LPARAM) parts);

	// init other controls
	CtrlDisAsmView *ptr = CtrlDisAsmView::getFrom(GetDlgItem(m_hDlg,IDC_DISASMVIEW));
	ptr->setDebugger(cpu);
	ptr->gotoAddr(0x00000000);

	CtrlRegisterList *rl = CtrlRegisterList::getFrom(GetDlgItem(m_hDlg,IDC_REGLIST));
	rl->setCPU(cpu);
	
	leftTabs = new TabControl(GetDlgItem(m_hDlg,IDC_LEFTTABS));
	leftTabs->SetIgnoreBottomMargin(true);
	leftTabs->AddTab(GetDlgItem(m_hDlg,IDC_REGLIST),L"Regs");
	leftTabs->AddTab(GetDlgItem(m_hDlg,IDC_FUNCTIONLIST),L"Funcs");
	leftTabs->ShowTab(0);

	// subclass the goto edit box
	HWND editWnd = GetDlgItem(m_hDlg,IDC_ADDRESS);
	DefGotoEditProc = (WNDPROC)GetWindowLongPtr(editWnd,GWLP_WNDPROC);
	SetWindowLongPtr(editWnd,GWLP_WNDPROC,(LONG_PTR)GotoEditProc); 
	
	// subclass the function list
	HWND funcListWnd = GetDlgItem(m_hDlg,IDC_FUNCTIONLIST);
	DefFuncListProc = (WNDPROC)GetWindowLongPtr(funcListWnd,GWLP_WNDPROC);
	SetWindowLongPtr(funcListWnd,GWLP_WNDPROC,(LONG_PTR)FuncListProc); 

	// init bottom tabs
	bottomTabs = new TabControl(GetDlgItem(m_hDlg,IDC_DEBUG_BOTTOMTABS));

	HWND memHandle = GetDlgItem(m_hDlg,IDC_DEBUGMEMVIEW);
	CtrlMemView *mem = CtrlMemView::getFrom(memHandle);
	mem->setDebugger(_cpu);
	bottomTabs->AddTab(memHandle,L"Memory");
	
	breakpointList = new CtrlBreakpointList(GetDlgItem(m_hDlg,IDC_BREAKPOINTLIST),cpu,ptr);
	breakpointList->reloadBreakpoints();
	bottomTabs->AddTab(breakpointList->GetHandle(),L"Breakpoints");

	threadList = new CtrlThreadList(GetDlgItem(m_hDlg,IDC_THREADLIST));
	threadList->reloadThreads();
	bottomTabs->AddTab(threadList->GetHandle(),L"Threads");

	stackTraceView = new CtrlStackTraceView(GetDlgItem(m_hDlg,IDC_STACKFRAMES),cpu,ptr);
	stackTraceView->loadStackTrace();
	bottomTabs->AddTab(stackTraceView->GetHandle(),L"Stack frames");
	
	moduleList = new CtrlModuleList(GetDlgItem(m_hDlg,IDC_MODULELIST),cpu);
	moduleList->loadModules();
	bottomTabs->AddTab(moduleList->GetHandle(),L"Modules");

	bottomTabs->SetShowTabTitles(g_Config.bShowBottomTabTitles);
	bottomTabs->ShowTab(memHandle);
	
	// Actually resize the window to the proper size (after the above setup.)
	// do it twice so that the window definitely receives a WM_SIZE message with
	// the correct size (the default from the .rc tends to be off)
	MoveWindow(m_hDlg,x,y,1,1,FALSE);
	MoveWindow(m_hDlg,x,y,w,h,TRUE);
	SetDebugMode(true, true);
}

CDisasm::~CDisasm()
{
	DestroyWindow(statusBarWnd);

	delete leftTabs;
	delete bottomTabs;
	delete breakpointList;
	delete threadList;
	delete stackTraceView;
	delete moduleList;
}

void CDisasm::stepInto()
{
	if (!PSP_IsInited() || !Core_IsStepping()) {
		return;
	}

	CtrlDisAsmView *ptr = CtrlDisAsmView::getFrom(GetDlgItem(m_hDlg,IDC_DISASMVIEW));
	lastTicks = CoreTiming::GetTicks();
	u32 currentPc = cpu->GetPC();

	// If the current PC is on a breakpoint, the user doesn't want to do nothing.
	CBreakPoints::SetSkipFirst(currentMIPS->pc);
	u32 newAddress = currentPc+ptr->getInstructionSizeAt(currentPc);

	MIPSAnalyst::MipsOpcodeInfo info = MIPSAnalyst::GetOpcodeInfo(cpu,currentPc);
	if (info.isBranch)
	{
		ptr->scrollStepping(newAddress);
	} else {
		bool scroll = true;
		if (currentMIPS->inDelaySlot)
		{
			MIPSAnalyst::MipsOpcodeInfo prevInfo = MIPSAnalyst::GetOpcodeInfo(cpu,currentPc-cpu->getInstructionSize(0));
			if (!prevInfo.isConditional || prevInfo.conditionMet)
				scroll = false;
		}

		if (scroll)
		{
			ptr->scrollStepping(newAddress);
		}
	}

	for (u32 i = 0; i < (newAddress-currentPc)/4; i++)
	{
		Core_DoSingleStep();
		Sleep(1);
	}

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
	if (!PSP_IsInited() || Core_IsActive()) {
		return;
	}
	
	CtrlDisAsmView *ptr = CtrlDisAsmView::getFrom(GetDlgItem(m_hDlg,IDC_DISASMVIEW));
	lastTicks = CoreTiming::GetTicks();

	// If the current PC is on a breakpoint, the user doesn't want to do nothing.
	CBreakPoints::SetSkipFirst(currentMIPS->pc);
	u32 currentPc = cpu->GetPC();

	MIPSAnalyst::MipsOpcodeInfo info = MIPSAnalyst::GetOpcodeInfo(cpu,cpu->GetPC());
	ptr->setDontRedraw(true);
	u32 breakpointAddress = currentPc+ptr->getInstructionSizeAt(currentPc);
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
			if (info.conditionMet)
			{
				breakpointAddress = info.branchTarget;
			} else {
				breakpointAddress = currentPc+2*cpu->getInstructionSize(0);
				ptr->scrollStepping(breakpointAddress);
			}
		}
	} else {
		ptr->scrollStepping(breakpointAddress);
	}

	SetDebugMode(false, true);
	CBreakPoints::AddBreakPoint(breakpointAddress,true);
	_dbg_update_();
	Core_EnableStepping(false);
	Sleep(1);
	ptr->gotoAddr(breakpointAddress);
	UpdateDialog();
}

void CDisasm::stepOut()
{
	if (!PSP_IsInited()) {
		return;
	}

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
	lastTicks = CoreTiming::GetTicks();
	
	// If the current PC is on a breakpoint, the user doesn't want to do nothing.
	CBreakPoints::SetSkipFirst(currentMIPS->pc);
	
	CtrlDisAsmView *ptr = CtrlDisAsmView::getFrom(GetDlgItem(m_hDlg,IDC_DISASMVIEW));
	ptr->setDontRedraw(true);

	SetDebugMode(false, true);
	CBreakPoints::AddBreakPoint(breakpointAddress,true);
	_dbg_update_();
	Core_EnableStepping(false);
	Sleep(1);
	ptr->gotoAddr(breakpointAddress);
	UpdateDialog();
}

void CDisasm::runToLine()
{
	if (!PSP_IsInited()) {
		return;
	}

	CtrlDisAsmView *ptr = CtrlDisAsmView::getFrom(GetDlgItem(m_hDlg,IDC_DISASMVIEW));
	u32 pos = ptr->getSelection();

	lastTicks = CoreTiming::GetTicks();
	ptr->setDontRedraw(true);
	SetDebugMode(false, true);
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

	case WM_NOTIFY:
		switch (wParam)
		{
		case IDC_LEFTTABS:
			leftTabs->HandleNotify(lParam);
			break;
		case IDC_BREAKPOINTLIST:
			breakpointList->HandleNotify(lParam);
			break;
		case IDC_THREADLIST:
			threadList->HandleNotify(lParam);
			break;
		case IDC_STACKFRAMES:
			stackTraceView->HandleNotify(lParam);
			break;
		case IDC_MODULELIST:
			moduleList->HandleNotify(lParam);
			break;
		case IDC_DEBUG_BOTTOMTABS:
			bottomTabs->HandleNotify(lParam);
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
				bottomTabs->ShowTab(GetDlgItem(m_hDlg,IDC_DEBUGMEMVIEW));
				break;

			case ID_DEBUG_DISPLAYBREAKPOINTLIST:
				bottomTabs->ShowTab(breakpointList->GetHandle());
				break;

			case ID_DEBUG_DISPLAYTHREADLIST:
				bottomTabs->ShowTab(threadList->GetHandle());
				break;

			case ID_DEBUG_DISPLAYSTACKFRAMELIST:
				bottomTabs->ShowTab(stackTraceView->GetHandle());
				break;

			case ID_DEBUG_DSIPLAYREGISTERLIST:
				leftTabs->ShowTab(0);
				break;
				
			case ID_DEBUG_DSIPLAYFUNCTIONLIST:
				leftTabs->ShowTab(1);
				break;

			case ID_DEBUG_ADDBREAKPOINT:
				{
					keepStatusBarText = true;
					bool isRunning = Core_IsActive();
					if (isRunning)
					{
						SetDebugMode(true, false);
						Core_EnableStepping(true);
						Core_WaitInactive(200);
					}

					BreakpointWindow bpw(m_hDlg,cpu);
					if (bpw.exec()) bpw.addBreakpoint();

					if (isRunning)
					{
						SetDebugMode(false, false);
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

			case ID_DEBUG_HIDEBOTTOMTABS:
				{
					RECT rect;
					hideBottomTabs = !hideBottomTabs;
					GetClientRect(m_hDlg,&rect);
					UpdateSize(rect.right-rect.left,rect.bottom-rect.top);
				}
				break;

			case ID_DEBUG_TOGGLEBOTTOMTABTITLES:
				bottomTabs->SetShowTabTitles(!bottomTabs->GetShowTabTitles());
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
				case CBN_SELCHANGE:
					{
						HWND lb = GetDlgItem(m_hDlg,LOWORD(wParam));
						int n = ListBox_GetCurSel(lb);

						wchar_t buffer[512];
						ListBox_GetText(lb,n,buffer);
						SendMessage(statusBarWnd,SB_SETTEXT,1,(LPARAM) buffer);
					}
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
					if (!PSP_IsInited()) {
						break;
					}
					if (!Core_IsStepping())		// stop
					{
						ptr->setDontRedraw(false);
						SetDebugMode(true, true);
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

						SetDebugMode(false, true);
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
					SetDebugMode(false, true);
					_dbg_update_();
					Core_EnableStepping(false);
				}
				break;

			case IDC_MEMCHECK:
				SendMessage(m_hDlg,WM_COMMAND,ID_DEBUG_ADDBREAKPOINT,0);
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
			if (!PSP_IsInited()) {
				break;
			}
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
		SetDebugMode(lParam != 0, true);
		return TRUE;

	case WM_DEB_UPDATE:
		Update();
		return TRUE;

	case WM_DEB_TABPRESSED:
		bottomTabs->NextTab(true);
		SetFocus(bottomTabs->CurrentTabHandle());
		break;

	case WM_DEB_SETSTATUSBARTEXT:
		if (!keepStatusBarText)
		{
			if (wParam == 0)
			{
				// erase the second part if the first is set
				SendMessage(statusBarWnd,SB_SETTEXT,0,(LPARAM)ConvertUTF8ToWString((const char *)lParam).c_str());
				SendMessage(statusBarWnd,SB_SETTEXT,1,(LPARAM)L"");
			} else if (wParam == 1)
			{
				SendMessage(statusBarWnd,SB_SETTEXT,1,(LPARAM)ConvertUTF8ToWString((const char *)lParam).c_str());
			}
		}
		break;
	case WM_DEB_GOTOHEXEDIT:
		{
			CtrlMemView *memory = CtrlMemView::getFrom(GetDlgItem(m_hDlg,IDC_DEBUGMEMVIEW));
			memory->gotoAddr(wParam);
			
			// display the memory viewer too
			bottomTabs->ShowTab(GetDlgItem(m_hDlg,IDC_DEBUGMEMVIEW));
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
			m->ptMinTrackSize.x = minWidth;
			//m->ptMaxTrackSize.x = m->ptMinTrackSize.x;
			m->ptMinTrackSize.y = minHeight;
		}
		return TRUE;
	case WM_CLOSE:
		Show(false);
		return TRUE;
	case WM_ACTIVATE:
		if (wParam == WA_ACTIVE || wParam == WA_CLICKACTIVE)
		{
			g_activeWindow = WINDOW_CPUDEBUGGER;
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
	struct Position
	{
		int x,y;
		int w,h;
	};
	
	RECT windowRect;
	Position positions[3];
	
	HWND disasm = GetDlgItem(m_hDlg, IDC_DISASMVIEW);
	HWND leftTabs = GetDlgItem(m_hDlg,IDC_LEFTTABS);
	HWND bottomTabs = GetDlgItem(m_hDlg, IDC_DEBUG_BOTTOMTABS);

	// ignore the status bar
	int topHeightOffset = 0;
	if (g_Config.bDisplayStatusBar)
	{
		GetWindowRect(statusBarWnd,&windowRect);
		topHeightOffset = (windowRect.bottom-windowRect.top);
	}
	
	CtrlDisAsmView *ptr = CtrlDisAsmView::getFrom(GetDlgItem(m_hDlg,IDC_DISASMVIEW));
	int disassemblyRowHeight = ptr->getRowHeight();

	// disassembly
	GetWindowRect(disasm,&windowRect);
	MapWindowPoints(HWND_DESKTOP,m_hDlg,(LPPOINT)&windowRect,2);
	positions[0].x = windowRect.left;
	positions[0].y = windowRect.top;
	
	// compute border height of the disassembly
	int totalHeight = windowRect.bottom-windowRect.top;
	GetClientRect(disasm,&windowRect);
	int clientHeight = windowRect.bottom-windowRect.top;
	int borderHeight = totalHeight-clientHeight;

	// left tabs
	GetWindowRect(leftTabs,&windowRect);
	MapWindowPoints(HWND_DESKTOP,m_hDlg,(LPPOINT)&windowRect,2);
	positions[1].x = windowRect.left;
	positions[1].y = windowRect.top;
	positions[1].w = positions[0].x-2*windowRect.left;
	int borderMargin = positions[1].x;

	float weight = hideBottomTabs ? 1.f : 390.f/500.f;

	// don't use the part above the disassembly for the computations
	int bottomHeightOffset = positions[0].y;
	positions[0].w = width-borderMargin-positions[0].x;
	positions[0].h = (height-bottomHeightOffset-topHeightOffset) * weight;
	positions[0].h = ((positions[0].h-borderHeight)/disassemblyRowHeight)*disassemblyRowHeight+borderHeight;
	positions[1].h = positions[0].h-(positions[1].y-positions[0].y);

	// bottom tabs
	positions[2].x = borderMargin;
	positions[2].y = positions[0].y+positions[0].h+borderMargin;
	positions[2].w = width-2*borderMargin;
	positions[2].h = hideBottomTabs ? 0 : height-bottomHeightOffset-positions[2].y;

	// now actually move all the windows
	MoveWindow(disasm,positions[0].x,positions[0].y,positions[0].w,positions[0].h,TRUE);
	MoveWindow(leftTabs,positions[1].x,positions[1].y,positions[1].w,positions[1].h,TRUE);
	MoveWindow(bottomTabs,positions[2].x,positions[2].y,positions[2].w,positions[2].h,TRUE);
	ShowWindow(bottomTabs,hideBottomTabs ? SW_HIDE : SW_NORMAL);
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

void CDisasm::SetDebugMode(bool _bDebug, bool switchPC)
{
	HWND hDlg = m_hDlg;

	// Update Dialog Windows
	if (_bDebug && GetUIState() == UISTATE_INGAME && PSP_IsInited())
	{
		Core_WaitInactive(TEMP_BREAKPOINT_WAIT_MS);
		CBreakPoints::ClearTemporaryBreakPoints();
		breakpointList->reloadBreakpoints();
		threadList->reloadThreads();
		stackTraceView->loadStackTrace();
		moduleList->loadModules();
		updateThreadLabel(false);

		SetDlgItemText(m_hDlg, IDC_STOPGO, L"Go");
		EnableWindow( GetDlgItem(hDlg, IDC_STOPGO), TRUE);
		EnableWindow( GetDlgItem(hDlg, IDC_STEP), TRUE);
		EnableWindow( GetDlgItem(hDlg, IDC_STEPOVER), TRUE);
		EnableWindow( GetDlgItem(hDlg, IDC_STEPHLE), TRUE);
		EnableWindow( GetDlgItem(hDlg, IDC_STEPOUT), TRUE);
		CtrlDisAsmView *ptr = CtrlDisAsmView::getFrom(GetDlgItem(m_hDlg,IDC_DISASMVIEW));
		ptr->setDontRedraw(false);
		if (switchPC)
			ptr->gotoPC();
		
		ptr->scanFunctions();
		CtrlMemView *mem = CtrlMemView::getFrom(GetDlgItem(m_hDlg,IDC_DEBUGMEMVIEW));
		mem->redraw();

		// update the callstack
		//CDisam::blah blah
		UpdateDialog();
	}
	else
	{
		updateThreadLabel(true);
		
		if (GetUIState() == UISTATE_INGAME && PSP_IsInited())
		{
			SetDlgItemText(m_hDlg, IDC_STOPGO, L"Stop");
			EnableWindow( GetDlgItem(hDlg, IDC_STOPGO), TRUE);
		}
		else
		{
			SetDlgItemText(m_hDlg, IDC_STOPGO, L"Go");
			EnableWindow( GetDlgItem(hDlg, IDC_STOPGO), FALSE);
		}
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
	ptr->clearFunctions();
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
	if (PSP_IsInited()) {
		_snwprintf(tempTicks, 24, L"%lld", CoreTiming::GetTicks() - lastTicks);
		SetDlgItemText(m_hDlg, IDC_DEBUG_COUNT, tempTicks);
	}
	// Update Register Dialog
	for (int i=0; i<numCPUs; i++)
		if (memoryWindow[i])
			memoryWindow[i]->Update();

	// repaint windows at the bottom. only the memory view needs to be forced to
	// redraw. all others are updated manually
	InvalidateRect (GetDlgItem(m_hDlg, IDC_DEBUGMEMVIEW), NULL, TRUE);
	UpdateWindow (GetDlgItem(m_hDlg, IDC_DEBUGMEMVIEW));
}