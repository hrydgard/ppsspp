// NOTE: Apologies for the quality of this code, this is really from pre-opensource Dolphin - that is, 2003.

#include "../../Core/MemMap.h"
#include "../Resource.h"
#include "../InputBox.h"

#include "../../Core/Debugger/Breakpoints.h"
#include "../../Core/Debugger/SymbolMap.h"
#include "Debugger_MemoryDlg.h"
#include "Debugger_Disasm.h"
#include "Debugger_VFPUDlg.h"

#include "../main.h"
#include "CtrlRegisterList.h"

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

CDisasm::CDisasm(HINSTANCE _hInstance, HWND _hParent, DebugInterface *_cpu) : Dialog((LPCSTR)IDD_DISASM, _hInstance, _hParent)
{
	cpu = _cpu;

	SetWindowText(m_hDlg,_cpu->GetName());
#ifdef THEMES
	//if (WTL::CTheme::IsThemingSupported())
		//EnableThemeDialogTexture(m_hDlg ,ETDT_ENABLETAB);
#endif
	SetWindowPos(m_hDlg,0,500,200,0,0,SWP_NOSIZE);

	CtrlDisAsmView *ptr = CtrlDisAsmView::getFrom(GetDlgItem(m_hDlg,IDC_DISASMVIEW));
	ptr->setDebugger(cpu);
	ptr->gotoAddr(0x00000000);

	CtrlRegisterList *rl = CtrlRegisterList::getFrom(GetDlgItem(m_hDlg,IDC_REGLIST));

  rl->setCPU(cpu);

	GetWindowRect(m_hDlg,&minRect);

	//symbolMap.FillSymbolListBox(GetDlgItem(m_hDlg, IDC_FUNCTIONLIST),ST_FUNCTION);
	symbolMap.FillSymbolComboBox(GetDlgItem(m_hDlg, IDC_FUNCTIONLIST),ST_FUNCTION);

	GetWindowRect(GetDlgItem(m_hDlg, IDC_REGLIST),&regRect);
	GetWindowRect(GetDlgItem(m_hDlg, IDC_DISASMVIEW),&disRect);

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
	/*
	DWORD intAddress[14] =
	{0x100, 0x200,0x300,0x400,0x500,0x600,0x700,0x800,0x900,0xc00,0xd00,0xf00,0x1300,0x1700};
	char *intName[14] = 
	{"100 Reset","200 Mcheck", "300 DSI","400 ISI","500 External",
	"600 Align","700 Program","800 FPU N/A","900 DEC","C00 SC",
	"D00 Trace","F00 Perf","1300 Breakpt","1700 Thermal"};*/

	//
	// --- activate debug mode ---
	//

	SetDebugMode(true);
}

CDisasm::~CDisasm()
{
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
			break;
		}

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
				case CBN_SELCHANGE:
					{
						HWND lb = GetDlgItem(m_hDlg,LOWORD(wParam));
						int n = ListBox_GetCurSel(lb);
						if (n!=-1)
						{
							unsigned int addr = (unsigned int)ListBox_GetItemData(lb,n);
							ptr->gotoAddr(addr);
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
							ptr->gotoAddr(addr);
					}
					break;
				};
				break;

			case IDC_GO:
				{
					SetDebugMode(false);
					Core_EnableStepping(false);
				}
				break;

			case IDC_STEP:
				{
					Core_DoSingleStep();		
					Sleep(1);
					_dbg_update_();
					ptr->gotoPC();
					UpdateDialog();
					vfpudlg->Update();
				}
				break;

			case IDC_STEPOVER:
				{
					SetDebugMode(false);
					CBreakPoints::AddBreakPoint(cpu->GetPC()+cpu->getInstructionSize(0),true);
					_dbg_update_();
					Core_EnableStepping(false);
					Sleep(1);
					ptr->gotoPC();
					UpdateDialog();
				}
				break;
				
			case IDC_STEPHLE:
				{
					hleDebugBreak();
					SetDebugMode(false);
					_dbg_update_();
					Core_EnableStepping(false);
				}
				break;

			case IDC_STOP:
				{				
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

					MemCheck check;
					if (InputBox_GetHex(GetModuleHandle(NULL), m_hDlg, "JIT (and not HLE) only for now, no delete", 0, check.iStartAddress))
					{
						check.bBreak = true;
						check.bLog = true;
						check.bOnRead = true;
						check.bOnWrite = true;
						check.bRange = false;
						CBreakPoints::MemChecks.push_back(check);
						CBreakPoints::InvalidateJit();
					}

					if (isRunning)
					{
						SetDebugMode(false);
						Core_EnableStepping(false);
					}
				}
				break;

			case IDC_ADDRESS:
				{
					if (HIWORD(wParam) == EN_CHANGE ) 
					{
						char szBuffer[32];
						GetWindowText ((HWND)lParam, szBuffer, 32);
						ptr->gotoAddr(parseHex(szBuffer));
						UpdateDialog();
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
						char *str = symbolMap.GetDescription(fun);
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
					UpdateDialog();
				}
				break;
			case IDC_GOTOLR:
				{
					ptr->gotoAddr(cpu->GetLR());
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

	case WM_USER+1:
		NotifyMapLoaded();
		break;

	case WM_SIZE:
		{
			HWND disasm = GetDlgItem(m_hDlg, IDC_DISASMVIEW);
			HWND funclist = GetDlgItem(m_hDlg, IDC_FUNCTIONLIST);
			HWND regList = GetDlgItem(m_hDlg, IDC_REGLIST);
			int wf = regRect.right-regRect.left;
			int top = 138;
			MoveWindow(regList,8,top,wf,HIWORD(lParam)-top-8,TRUE);
			MoveWindow(funclist,8,top,wf,HIWORD(lParam)-top-8,TRUE);
			int w = LOWORD(lParam)-wf;
			top = 25;
			MoveWindow(disasm,wf+15,top, w-20,HIWORD(lParam)-top-8,TRUE);
			return TRUE;
		}

	case WM_GETMINMAXINFO:
		{
			MINMAXINFO *m = (MINMAXINFO *)lParam;
			m->ptMinTrackSize.x=minRect.right-minRect.left;
			//m->ptMaxTrackSize.x=m->ptMinTrackSize.x;
			m->ptMinTrackSize.y=minRect.bottom-minRect.top;
		}
		return TRUE;
	case WM_CLOSE:
		Show(false);
		return TRUE;
	}
	return FALSE;
}

void CDisasm::SetDebugMode(bool _bDebug)
{
	HWND hDlg = m_hDlg;

	// Update Dialog Windows
	if (_bDebug)
	{
		EnableWindow( GetDlgItem(hDlg, IDC_GO),	  TRUE);
		EnableWindow( GetDlgItem(hDlg, IDC_STEP), TRUE);
		EnableWindow( GetDlgItem(hDlg, IDC_STEPOVER), TRUE);
		EnableWindow( GetDlgItem(hDlg, IDC_STEPHLE), TRUE);
		EnableWindow( GetDlgItem(hDlg, IDC_STOP), FALSE);
		EnableWindow( GetDlgItem(hDlg, IDC_SKIP), TRUE);
		CtrlDisAsmView *ptr = CtrlDisAsmView::getFrom(GetDlgItem(m_hDlg,IDC_DISASMVIEW));
		ptr->gotoPC();
		// update the callstack
		//CDisam::blah blah
		UpdateDialog();
	}
	else
	{
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
	ptr->setAlign(cpu->getInstructionSize(0));
	ptr->redraw();
	CtrlRegisterList *rl = CtrlRegisterList::getFrom(GetDlgItem(m_hDlg,IDC_REGLIST));
	rl->redraw();						
	// Update Debug Counter
	char tempTicks[24];
	sprintf(tempTicks, "%lld", CoreTiming::GetTicks());
	SetDlgItemText(m_hDlg, IDC_DEBUG_COUNT, tempTicks);

	// Update Register Dialog
	for (int i=0; i<numCPUs; i++)
		if (memoryWindow[i])
			memoryWindow[i]->Update();
}