#include "Core/Config.h"
#include "Core/MemMap.h"
#include "Windows/resource.h"
#include "Windows/InputBox.h"

#include "Core/Debugger/Breakpoints.h"
#include "Core/Debugger/SymbolMap.h"
#include "Core/RetroAchievements.h"
#include "Windows/Debugger/BreakpointWindow.h"
#include "Windows/Debugger/CtrlDisAsmView.h"
#include "Windows/Debugger/Debugger_MemoryDlg.h"
#include "Windows/Debugger/Debugger_Disasm.h"
#include "Windows/Debugger/Debugger_VFPUDlg.h"
#include "Windows/Debugger/DebuggerShared.h"
// #include "Windows/W32Util/DarkMode.h"

#include "Windows/main.h"
#include "Windows/Debugger/CtrlRegisterList.h"
#include "Windows/Debugger/CtrlMemView.h"
#include "Windows/Debugger/Debugger_Lists.h"
#include "Windows/MainWindow.h"

#include "Core/Core.h"
#include "Core/HLE/HLE.h"
#include "Core/CoreTiming.h"
#include "Core/MIPS/MIPSAnalyst.h"

#include "Common/Data/Encoding/Utf8.h"

#include "Common/CommonWindows.h"
#include "Common/StringUtils.h"

#include <windowsx.h>
#include <commctrl.h>

static FAR WNDPROC DefGotoEditProc;

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

static FAR WNDPROC DefFuncListProc;

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

static constexpr UINT_PTR IDT_UPDATE = 0xC0DE0042;
static constexpr UINT UPDATE_DELAY = 1000 / 60;

CDisasm::CDisasm(HINSTANCE _hInstance, HWND _hParent, MIPSDebugInterface *_cpu) : Dialog((LPCSTR)IDD_DISASM, _hInstance, _hParent) {
	cpu = _cpu;
	lastTicks_ = PSP_IsInited() ? CoreTiming::GetTicks() : 0;
	breakpoints_ = &g_breakpoints;

	SetWindowText(m_hDlg, L"R4");

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
	if (g_Config.bDisplayStatusBar == false) {
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
	CtrlDisAsmView *ptr = DisAsmView();
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

	watchList_ = new CtrlWatchList(GetDlgItem(m_hDlg, IDC_WATCHLIST), cpu);
	bottomTabs->AddTab(watchList_->GetHandle(), L"Watch");

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

void CDisasm::step(CPUStepType stepType) {
	if (!PSP_IsInited() || !Core_IsStepping()) {
		return;
	}

	CtrlDisAsmView *ptr = DisAsmView();
	ptr->setDontRedraw(true);
	lastTicks_ = CoreTiming::GetTicks();

	u32 stepSize = ptr->getInstructionSizeAt(cpu->GetPC());
	Core_RequestCPUStep(stepType, stepSize);
}

void CDisasm::runToLine() {
	if (!PSP_IsInited()) {
		return;
	}

	CtrlDisAsmView *ptr = DisAsmView();
	u32 pos = ptr->getSelection();

	lastTicks_ = CoreTiming::GetTicks();
	ptr->setDontRedraw(true);
	breakpoints_->AddBreakPoint(pos,true);
	Core_Resume();
}

BOOL CDisasm::DlgProc(UINT message, WPARAM wParam, LPARAM lParam) {
	switch(message) {
	case WM_INITDIALOG:
		// DarkModeInitDialog(m_hDlg);
		return TRUE;

	case WM_NOTIFY:
		switch (wParam) {
		case IDC_LEFTTABS:
			leftTabs->HandleNotify(lParam);
			break;
		case IDC_BREAKPOINTLIST:
			SetWindowLongPtr(m_hDlg, DWLP_MSGRESULT, breakpointList->HandleNotify(lParam));
			return TRUE;
		case IDC_THREADLIST:
			SetWindowLongPtr(m_hDlg, DWLP_MSGRESULT, threadList->HandleNotify(lParam));
			return TRUE;
		case IDC_STACKFRAMES:
			SetWindowLongPtr(m_hDlg, DWLP_MSGRESULT, stackTraceView->HandleNotify(lParam));
			return TRUE;
		case IDC_MODULELIST:
			SetWindowLongPtr(m_hDlg, DWLP_MSGRESULT, moduleList->HandleNotify(lParam));
			return TRUE;
		case IDC_WATCHLIST:
			SetWindowLongPtr(m_hDlg, DWLP_MSGRESULT, watchList_->HandleNotify(lParam));
			return TRUE;
		case IDC_DEBUG_BOTTOMTABS:
			bottomTabs->HandleNotify(lParam);
			break;
		}
		break;
	case WM_COMMAND:
		{
			if (Achievements::HardcoreModeActive())
				return TRUE;

			CtrlDisAsmView *ptr = DisAsmView();
			switch (LOWORD(wParam)) {
			case ID_TOGGLE_BREAK:
				SendMessage(MainWindow::GetHWND(), WM_COMMAND, ID_TOGGLE_BREAK, 0);
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

			case ID_DEBUG_DISPLAYREGISTERLIST:
				leftTabs->ShowTab(0);
				break;
				
			case ID_DEBUG_DISPLAYFUNCTIONLIST:
				leftTabs->ShowTab(1);
				break;

			case ID_DEBUG_ADDBREAKPOINT:
				{
					CtrlDisAsmView *view = DisAsmView();
					keepStatusBarText = true;
					view->LockPosition();
					bool isRunning = Core_IsActive();
					if (isRunning)
					{
						Core_Break("cpu.breakpoint.add", 0);
						Core_WaitInactive();
					}

					BreakpointWindow bpw(m_hDlg,cpu);
					if (bpw.exec()) bpw.addBreakpoint();

					if (isRunning)
						Core_Resume();
					view->UnlockPosition();
					keepStatusBarText = false;
				}
				break;

			case ID_DEBUG_STEPOVER:
				if (GetFocus() == GetDlgItem(m_hDlg,IDC_DISASMVIEW)) step(CPUStepType::Over);
				break;

			case ID_DEBUG_STEPINTO:
				if (GetFocus() == GetDlgItem(m_hDlg,IDC_DISASMVIEW)) step(CPUStepType::Into);
				break;

			case ID_DEBUG_RUNTOLINE:
				if (GetFocus() == GetDlgItem(m_hDlg,IDC_DISASMVIEW)) runToLine();
				break;

			case ID_DEBUG_STEPOUT:
				if (GetFocus() == GetDlgItem(m_hDlg,IDC_DISASMVIEW)) step(CPUStepType::Out);
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
				MainWindow::CreateVFPUWindow();
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
					if (!Core_IsStepping())	{  // stop
						ptr->setDontRedraw(false);
						Core_Break("ui.break", 0);
					} else {					// go
						lastTicks_ = CoreTiming::GetTicks();

						Core_Resume();
					}
				}
				break;

			case IDC_STEP:
				step(CPUStepType::Into);
				break;

			case IDC_STEPOVER:
				step(CPUStepType::Over);
				break;

			case IDC_STEPOUT:
				step(CPUStepType::Out);
				break;
				
			case IDC_STEPHLE:
				{
					if (Core_IsActive())
						break;
					lastTicks_ = CoreTiming::GetTicks();

					hleDebugBreak();
					Core_Resume();
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
			case IDC_GOTORA:
				{
					ptr->gotoAddr(cpu->GetRA());
					SetFocus(GetDlgItem(m_hDlg, IDC_DISASMVIEW));
				}
				break;

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
		CtrlDisAsmView *ptr = DisAsmView();
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
			CtrlDisAsmView *ptr = DisAsmView();
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

	case WM_DEB_AFTERSTEP:
	{
		CtrlDisAsmView *ptr = DisAsmView();
		// At this point, the step should be done, and the new address is just PC.
		// Ideally, this part should be done as a reaction to an update message and not directly here.
		// That way we could get rid of the sleep.
		u32 oldAddress = ptr->getSelection();
		u32 newAddress = cpu->GetPC();
		if (newAddress > oldAddress && newAddress < oldAddress + 12) {
			// Heuristic for when to scroll at the edge rather than jump the window.
			ptr->scrollStepping(newAddress);
		}
		ptr->gotoAddr(newAddress);
		Update();
		break;
	}

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
		if (wParam == WA_ACTIVE || wParam == WA_CLICKACTIVE) {
			g_activeWindow = WINDOW_CPUDEBUGGER;
		} else {
			g_activeWindow = WINDOW_OTHER;
		}
		break;

	case WM_TIMER:
		if (wParam == IDT_UPDATE) {
			ProcessUpdateDialog();
			updateDialogScheduled_ = false;
			KillTimer(GetDlgHandle(), wParam);
		}
		break;
	}
	return 0; // DarkModeDlgProc(m_hDlg, message, wParam, lParam);
}

void CDisasm::updateThreadLabel(bool clear)
{
	char label[512];
	if (clear) {
		snprintf(label, sizeof(label), "Thread: -");
	} else {
		snprintf(label, sizeof(label), "Thread: %s", threadList->getCurrentThreadName());
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
	
	CtrlDisAsmView *ptr = DisAsmView();
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
	bool ingame = (GetUIState() == UISTATE_INGAME || GetUIState() == UISTATE_EXCEPTION) && PSP_IsInited();

	// If we're stepping, update debugging windows.
	// This is called potentially asynchronously, so state might've changed.
	if (Core_IsStepping() && ingame) {
		breakpointList->reloadBreakpoints();
		threadList->reloadThreads();
		stackTraceView->loadStackTrace();
		moduleList->loadModules();
		watchList_->RefreshValues();

		EnableWindow(GetDlgItem(hDlg, IDC_STOPGO), TRUE);
		EnableWindow(GetDlgItem(hDlg, IDC_STEP), TRUE);
		EnableWindow(GetDlgItem(hDlg, IDC_STEPOVER), TRUE);
		EnableWindow(GetDlgItem(hDlg, IDC_STEPHLE), TRUE);
		EnableWindow(GetDlgItem(hDlg, IDC_STEPOUT), TRUE);
		EnableWindow(GetDlgItem(hDlg, IDC_GOTOPC), TRUE);
		EnableWindow(GetDlgItem(hDlg, IDC_GOTORA), TRUE);
		CtrlDisAsmView *ptr = DisAsmView();
		ptr->setDontRedraw(false);
		if (switchPC)
			ptr->gotoPC();
		
		ptr->scanVisibleFunctions();
	}
	else
	{
		if (ingame)
			EnableWindow(GetDlgItem(hDlg, IDC_STOPGO), TRUE);
		else
			EnableWindow(GetDlgItem(hDlg, IDC_STOPGO), FALSE);
		EnableWindow(GetDlgItem(hDlg, IDC_STEP), FALSE);
		EnableWindow(GetDlgItem(hDlg, IDC_STEPOVER), FALSE);
		EnableWindow(GetDlgItem(hDlg, IDC_STEPHLE), FALSE);
		EnableWindow(GetDlgItem(hDlg, IDC_STEPOUT), FALSE);
		EnableWindow(GetDlgItem(hDlg, IDC_GOTOPC), FALSE);
		EnableWindow(GetDlgItem(hDlg, IDC_GOTORA), FALSE);
		CtrlRegisterList *reglist = CtrlRegisterList::getFrom(GetDlgItem(m_hDlg,IDC_REGLIST));
		reglist->redraw();
	}

	UpdateDialog();
}

void CDisasm::Show(bool bShow, bool includeToTop) {
	if (deferredSymbolFill_ && bShow) {
		if (g_symbolMap) {
			g_symbolMap->FillSymbolListBox(GetDlgItem(m_hDlg, IDC_FUNCTIONLIST), ST_FUNCTION);
			deferredSymbolFill_ = false;
		}
	}
	Dialog::Show(bShow, includeToTop);
}

void CDisasm::NotifyMapLoaded() {
	if (m_bShowState != SW_HIDE && g_symbolMap) {
		g_symbolMap->FillSymbolListBox(GetDlgItem(m_hDlg, IDC_FUNCTIONLIST), ST_FUNCTION);
	} else {
		deferredSymbolFill_ = true;
	}
	CtrlDisAsmView *ptr = DisAsmView();
	ptr->clearFunctions();
	ptr->redraw();
}

void CDisasm::Goto(u32 addr)
{
	CtrlDisAsmView *ptr = DisAsmView();
	ptr->gotoAddr(addr);
	SetFocus(GetDlgItem(m_hDlg, IDC_DISASMVIEW));
	ptr->redraw();
}

void CDisasm::UpdateDialog() {
	if (!updateDialogScheduled_) {
		SetTimer(GetDlgHandle(), IDT_UPDATE, UPDATE_DELAY, nullptr);
		updateDialogScheduled_ = true;
	}

	// Since these update on a delay, it's okay to do them immediately.
	CtrlDisAsmView *ptr = DisAsmView();
	ptr->redraw();
	CtrlRegisterList *rl = CtrlRegisterList::getFrom(GetDlgItem(m_hDlg, IDC_REGLIST));
	rl->redraw();

	// Repaint windows at the bottom. only the memory view needs to be forced to redraw.
	// All others are updated manually
	CtrlMemView *memview = CtrlMemView::getFrom(GetDlgItem(m_hDlg, IDC_DEBUGMEMVIEW));
	memview->redraw();

	// Update memory window too.
	if (memoryWindow)
		memoryWindow->Update();
	if (vfpudlg)
		vfpudlg->Update();
}

void CDisasm::ProcessUpdateDialog() {
	/*
	HWND gotoInt = GetDlgItem(m_hDlg, IDC_GOTOINT);
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

	// Update Debug Counter
	if (PSP_IsInited()) {
		wchar_t tempTicks[24]{};
		_snwprintf(tempTicks, 23, L"%lld", CoreTiming::GetTicks() - lastTicks_);
		SetDlgItemText(m_hDlg, IDC_DEBUG_COUNT, tempTicks);
	}

	bool ingame = (GetUIState() == UISTATE_INGAME || GetUIState() == UISTATE_EXCEPTION) && PSP_IsInited();
	if (Core_IsStepping() || !ingame) {
		SetDlgItemText(m_hDlg, IDC_STOPGO, L"Go");
	} else {
		SetDlgItemText(m_hDlg, IDC_STOPGO, L"Break");
	}

	updateThreadLabel(!ingame || !Core_IsStepping());
}

CtrlDisAsmView *CDisasm::DisAsmView() {
	return CtrlDisAsmView::getFrom(GetDlgItem(m_hDlg, IDC_DISASMVIEW));
}
