// NOTE: Apologies for the quality of this code, this is really from pre-opensource Dolphin - that is, 2003.

#include "../resource.h"
#include "../../Core/MemMap.h"
#include "../../Core/MIPS/JitCommon/JitCommon.h"
#include "../W32Util/Misc.h"
#include "../WndMainWindow.h"
#include "../InputBox.h"

#include "Core/Config.h"
#include "CtrlDisAsmView.h"
#include "Debugger_MemoryDlg.h"
#include "DebuggerShared.h"
#include "../../Core/Debugger/SymbolMap.h"
#include "../../globals.h"
#include "../main.h"

#include <windows.h>
#include <tchar.h>
#include <set>

TCHAR CtrlDisAsmView::szClassName[] = _T("CtrlDisAsmView");
extern HMENU g_hPopupMenus;

void CtrlDisAsmView::init()
{
    WNDCLASSEX wc;
    
    wc.cbSize         = sizeof(wc);
    wc.lpszClassName  = szClassName;
    wc.hInstance      = GetModuleHandle(0);
    wc.lpfnWndProc    = CtrlDisAsmView::wndProc;
    wc.hCursor        = LoadCursor (NULL, IDC_ARROW);
    wc.hIcon          = 0;
    wc.lpszMenuName   = 0;
    wc.hbrBackground  = (HBRUSH)GetSysColorBrush(COLOR_WINDOW);
    wc.style          = 0;
    wc.cbClsExtra     = 0;
    wc.cbWndExtra     = sizeof( CtrlDisAsmView * );
    wc.hIconSm        = 0;
	
    RegisterClassEx(&wc);
}

void CtrlDisAsmView::deinit()
{
	//UnregisterClass(szClassName, hInst)
}



LRESULT CALLBACK CtrlDisAsmView::wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	CtrlDisAsmView *ccp = CtrlDisAsmView::getFrom(hwnd);
	static bool lmbDown=false,rmbDown=false;
    switch(msg)
    {
    case WM_NCCREATE:
        // Allocate a new CustCtrl structure for this window.
        ccp = new CtrlDisAsmView(hwnd);
		
        // Continue with window creation.
        return ccp != NULL;
		
		// Clean up when the window is destroyed.
    case WM_NCDESTROY:
        delete ccp;
        break;
	case WM_SETFONT:
		break;
	case WM_SIZE:
		ccp->redraw();
		break;
	case WM_PAINT:
		ccp->onPaint(wParam,lParam);
		break;	
	case WM_VSCROLL:
		ccp->onVScroll(wParam,lParam);
		break;
	case WM_MOUSEWHEEL:
		if (GET_WHEEL_DELTA_WPARAM(wParam) > 0)
		{
			ccp->scrollWindow(-3);
		} else if (GET_WHEEL_DELTA_WPARAM(wParam) < 0) {
			ccp->scrollWindow(3);
		}
		break;
	case WM_ERASEBKGND:
		return FALSE;
	case WM_KEYDOWN:
		ccp->onKeyDown(wParam,lParam);
		return 0;
	case WM_SYSKEYDOWN:
		ccp->onKeyDown(wParam,lParam);
		return 0;		// return a value so that windows doesn't execute the standard syskey action
	case WM_KEYUP:
		ccp->onKeyUp(wParam,lParam);
		return 0;
	case WM_LBUTTONDOWN: lmbDown=true; ccp->onMouseDown(wParam,lParam,1); break;
	case WM_RBUTTONDOWN: rmbDown=true; ccp->onMouseDown(wParam,lParam,2); break;
	case WM_MOUSEMOVE:   ccp->onMouseMove(wParam,lParam,(lmbDown?1:0) | (rmbDown?2:0)); break;
	case WM_LBUTTONUP:   lmbDown=false; ccp->onMouseUp(wParam,lParam,1); break;
	case WM_RBUTTONUP:   rmbDown=false; ccp->onMouseUp(wParam,lParam,2); break;
	case WM_SETFOCUS:
		SetFocus(hwnd);
		ccp->hasFocus=true;
		ccp->redraw();
		break;
	case WM_KILLFOCUS:
		ccp->hasFocus=false;
		ccp->redraw();
		break;
	case WM_GETDLGCODE:
		if (lParam && ((MSG*)lParam)->message == WM_KEYDOWN)
		{
			switch (wParam)
			{
			case VK_F9:
			case VK_F10:
			case VK_F11:
			case VK_TAB:
				return DLGC_WANTMESSAGE;
			default:
				return DLGC_WANTCHARS|DLGC_WANTARROWS;
			}
		}
		return DLGC_WANTCHARS|DLGC_WANTARROWS;
    default:
        break;
    }
	
    return DefWindowProc(hwnd, msg, wParam, lParam);
}


CtrlDisAsmView *CtrlDisAsmView::getFrom(HWND hwnd)
{
    return (CtrlDisAsmView *)GetWindowLongPtr(hwnd, GWLP_USERDATA);
}




CtrlDisAsmView::CtrlDisAsmView(HWND _wnd)
{
	wnd=_wnd;
	SetWindowLongPtr(wnd, GWLP_USERDATA, (LONG)this);
	SetWindowLong(wnd, GWL_STYLE, GetWindowLong(wnd,GWL_STYLE) | WS_VSCROLL);
	SetScrollRange(wnd, SB_VERT, -1,1,TRUE);

	charWidth = g_Config.iFontWidth;
	rowHeight = g_Config.iFontHeight+2;

	font = CreateFont(rowHeight-2,charWidth,0,0,FW_DONTCARE,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,DEFAULT_QUALITY,DEFAULT_PITCH,
		"Lucida Console");
	boldfont = CreateFont(rowHeight-2,charWidth,0,0,FW_DEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,DEFAULT_QUALITY,DEFAULT_PITCH,
		"Lucida Console");
	curAddress=0;
	instructionSize=4;
	showHex=false;
	hasFocus = false;
	controlHeld = false;
	dontRedraw = false;

	matchAddress = -1;
	searching = false;
	searchQuery[0] = 0;
	windowStart = curAddress;
	whiteBackground = false;
	displaySymbols = true;
	calculatePixelPositions();
}


CtrlDisAsmView::~CtrlDisAsmView()
{
	DeleteObject(font);
	DeleteObject(boldfont);
}

COLORREF scaleColor(COLORREF color, float factor)
{
	unsigned char r = color & 0xFF;
	unsigned char g = (color >> 8) & 0xFF;
	unsigned char b = (color >> 16) & 0xFF;

	r = min(255,max((int)(r*factor),0));
	g = min(255,max((int)(g*factor),0));
	b = min(255,max((int)(b*factor),0));

	return (color & 0xFF000000) | (b << 16) | (g << 8) | r;
}

bool CtrlDisAsmView::getDisasmAddressText(u32 address, char* dest, bool abbreviateLabels)
{
	if (displaySymbols)
	{
		const char* addressSymbol = debugger->findSymbolForAddress(address);
		if (addressSymbol != NULL)
		{
			for (int k = 0; addressSymbol[k] != 0; k++)
			{
				// abbreviate long names
				if (abbreviateLabels && k == 16 && addressSymbol[k+1] != 0)
				{
					*dest++ = '+';
					break;
				}
				*dest++ = addressSymbol[k];
			}
			*dest++ = ':';
			*dest = 0;
			return true;
		} else {
			sprintf(dest,"    %08X",address);
			return false;
		}
	} else {
		sprintf(dest,"%08X %08X",address,Memory::ReadUnchecked_U32(address));
		return false;
	}
}

void CtrlDisAsmView::parseDisasm(const char* disasm, char* opcode, char* arguments)
{
	branchTarget = -1;
	branchRegister = -1;

	// copy opcode
	while (*disasm != 0 && *disasm != '\t')
	{
		*opcode++ = *disasm++;
	}
	*opcode = 0;

	if (*disasm++ == 0)
	{
		*arguments = 0;
		return;
	}

	const char* jumpAddress = strstr(disasm,"->$");
	const char* jumpRegister = strstr(disasm,"->");
	while (*disasm != 0)
	{
		// parse symbol
		if (disasm == jumpAddress)
		{
			sscanf(disasm+3,"%08x",&branchTarget);

			const char* addressSymbol = debugger->findSymbolForAddress(branchTarget);
			if (addressSymbol != NULL && displaySymbols)
			{
				arguments += sprintf(arguments,"%s",addressSymbol);
			} else {
				arguments += sprintf(arguments,"0x%08X",branchTarget);
			}
			
			disasm += 3+8;
			continue;
		}

		if (disasm == jumpRegister)
		{
			disasm += 2;
			for (int i = 0; i < 32; i++)
			{
				if (strcasecmp(jumpRegister+2,debugger->GetRegName(0,i)) == 0)
				{
					branchRegister = i;
					break;
				}
			}
		}

		if (*disasm == ' ')
		{
			disasm++;
			continue;
		}
		*arguments++ = *disasm++;
	}

	*arguments = 0;
}

void CtrlDisAsmView::onPaint(WPARAM wParam, LPARAM lParam)
{
	if (!debugger->isAlive()) return;

	struct branch
	{
		int src,dst,srcAddr;
	};
	branch branches[256];
	int numBranches=0;

	PAINTSTRUCT ps;
	HDC actualHdc = BeginPaint(wnd, &ps);
	HDC hdc = CreateCompatibleDC(actualHdc);
	HBITMAP hBM = CreateCompatibleBitmap(actualHdc, rect.right-rect.left, rect.bottom-rect.top);
	SelectObject(hdc, hBM);

	SetBkMode(hdc, TRANSPARENT);

	HPEN nullPen=CreatePen(0,0,0xffffff);
	HPEN condPen=CreatePen(0,0,0xFF3020);
	HBRUSH nullBrush=CreateSolidBrush(0xffffff);
	HBRUSH currentBrush=CreateSolidBrush(0xFFEfE8);

	HPEN oldPen=(HPEN)SelectObject(hdc,nullPen);
	HBRUSH oldBrush=(HBRUSH)SelectObject(hdc,nullBrush);
	HFONT oldFont = (HFONT)SelectObject(hdc,(HGDIOBJ)font);
	HICON breakPoint = (HICON)LoadIcon(GetModuleHandle(0),(LPCSTR)IDI_STOP);
	HICON breakPointDisable = (HICON)LoadIcon(GetModuleHandle(0),(LPCSTR)IDI_STOPDISABLE);

	for (int i = 0; i < visibleRows+2; i++)
	{
		unsigned int address=windowStart + i*instructionSize;

		int rowY1 = rowHeight*i;
		int rowY2 = rowHeight*(i+1);

		// draw background
		COLORREF backgroundColor = whiteBackground ? 0xFFFFFF : debugger->getColor(address);
		COLORREF textColor = 0x000000;

		if (address == debugger->getPC())
		{
			backgroundColor = scaleColor(backgroundColor,1.05f);
		}

		if (address == curAddress && searching == false)
		{
			if (hasFocus)
			{
				backgroundColor = 0xFF9933;
				textColor = 0xFFFFFF;
			} else {
				backgroundColor = 0xC0C0C0;
			}
		}
		
		HBRUSH backgroundBrush = CreateSolidBrush(backgroundColor);
		HPEN backgroundPen = CreatePen(0,0,backgroundColor);
		SelectObject(hdc,backgroundBrush);
		SelectObject(hdc,backgroundPen);
		Rectangle(hdc,0,rowY1,rect.right,rowY1+rowHeight);
		
		SelectObject(hdc,currentBrush);
		SelectObject(hdc,nullPen);

		DeleteObject(backgroundBrush);
		DeleteObject(backgroundPen);

		// display address/symbol
		bool enabled;
		if (CBreakPoints::IsAddressBreakPoint(address,&enabled))
		{
			if (enabled) textColor = 0x0000FF;
			int yOffset = max(-1,(rowHeight-14+1)/2);
			DrawIconEx(hdc,2,rowY1+1+yOffset,enabled ? breakPoint : breakPointDisable,32,32,0,0,DI_NORMAL);
		}
		SetTextColor(hdc,textColor);

		char addressText[64];
		getDisasmAddressText(address,addressText,true);
		TextOut(hdc,pixelPositions.addressStart,rowY1+2,addressText,(int)strlen(addressText));

		if (address == debugger->getPC())
		{
			TextOutW(hdc,pixelPositions.opcodeStart-8,rowY1,L"■",1);
		}

		// display opcode
		char opcode[64],arguments[256];
		const char *dizz = debugger->disasm(address, instructionSize);
		parseDisasm(dizz,opcode,arguments);

		int length = (int) strlen(arguments);
		if (length != 0) TextOut(hdc,pixelPositions.argumentsStart,rowY1+2,arguments,length);
			
		SelectObject(hdc,boldfont);
		TextOut(hdc,pixelPositions.opcodeStart,rowY1+2,opcode,(int)strlen(opcode));
		SelectObject(hdc,font);

		if (branchTarget != -1 && strcmp(opcode,"jal") != 0 && opcode[1] != 0) //unconditional 'b/j' branch
		{
			branches[numBranches].src=rowY1 + rowHeight/2;
			branches[numBranches].srcAddr=address/instructionSize;
			branches[numBranches].dst=(int)(rowY1+((__int64)branchTarget-(__int64)address)*rowHeight/instructionSize + rowHeight/2);
			numBranches++;
		}
	}

	for (int i=0; i < numBranches; i++)
	{
		SelectObject(hdc,condPen);
		int x=pixelPositions.arrowsStart+i*8;
		MoveToEx(hdc,x-2,branches[i].src,0);

		if (branches[i].dst < 0)
		{
			LineTo(hdc,x+2,branches[i].src);
			LineTo(hdc,x+2,0);
		} else if (branches[i].dst > rect.bottom)
		{
			LineTo(hdc,x+2,branches[i].src);
			LineTo(hdc,x+2,rect.bottom);
		} else {
			LineTo(hdc,x+2,branches[i].src);
			LineTo(hdc,x+2,branches[i].dst);
			LineTo(hdc,x-4,branches[i].dst);
			
			MoveToEx(hdc,x,branches[i].dst-4,0);
			LineTo(hdc,x-4,branches[i].dst);
			LineTo(hdc,x+1,branches[i].dst+5);
		}
	}

	SelectObject(hdc,oldFont);
	SelectObject(hdc,oldPen);
	SelectObject(hdc,oldBrush);

	// copy bitmap to the actual hdc
	BitBlt(actualHdc,0,0,rect.right,rect.bottom,hdc,0,0,SRCCOPY);
	DeleteObject(hBM);
	DeleteDC(hdc);

	DeleteObject(nullPen);
	DeleteObject(condPen);

	DeleteObject(nullBrush);
	DeleteObject(currentBrush);
	
	DestroyIcon(breakPoint);
	DestroyIcon(breakPointDisable);
	
	EndPaint(wnd, &ps);
}



void CtrlDisAsmView::onVScroll(WPARAM wParam, LPARAM lParam)
{
	switch (wParam & 0xFFFF)
	{
	case SB_LINEDOWN:
		windowStart += instructionSize;
		break;
	case SB_LINEUP:
		windowStart -= instructionSize;
		break;
	case SB_PAGEDOWN:
		windowStart += visibleRows*instructionSize;
		break;
	case SB_PAGEUP:
		windowStart -= visibleRows*instructionSize;
		break;
	default:
		return;
	}
	redraw();
}

void CtrlDisAsmView::followBranch()
{
	char opcode[64],arguments[256];
	const char *dizz = debugger->disasm(curAddress, instructionSize);
	parseDisasm(dizz,opcode,arguments);

	if (branchTarget != -1)
	{
		jumpStack.push_back(curAddress);
		gotoAddr(branchTarget);
	} else if (branchRegister != -1)
	{
		jumpStack.push_back(curAddress);
		gotoAddr(debugger->GetRegValue(0,branchRegister));
	}
}


void CtrlDisAsmView::onKeyDown(WPARAM wParam, LPARAM lParam)
{
	u32 windowEnd = windowStart+visibleRows*instructionSize;

	if (controlHeld)
	{
		switch (tolower(wParam & 0xFFFF))
		{
		case 's':
			search(false);
			break;
		case 'c':
			search(true);
			break;
		case 'x':
			disassembleToFile();
			break;
		case 'g':
			{
				u32 addr;
				controlHeld = false;
				if (executeExpressionWindow(wnd,debugger,addr) == false) return;
				gotoAddr(addr);
			}
			break;
		}
	} else {
		switch (wParam & 0xFFFF)
		{
		case VK_DOWN:
			curAddress += instructionSize;
			scrollAddressIntoView();
			break;
		case VK_UP:
			curAddress-=instructionSize;
			scrollAddressIntoView();
			break;
		case VK_NEXT:
			if (curAddress != windowEnd-instructionSize && curAddressIsVisible()) {
				curAddress = windowEnd-instructionSize;
				scrollAddressIntoView();
			} else {
				curAddress += visibleRows*instructionSize;
				scrollAddressIntoView();
			}
			break;
		case VK_PRIOR:
			if (curAddress != windowStart && curAddressIsVisible()) {
				curAddress = windowStart;
				scrollAddressIntoView();
			} else {
				curAddress -= visibleRows*instructionSize;
				scrollAddressIntoView();
			}
			break;
		case VK_LEFT:
			if (jumpStack.empty())
			{
				gotoPC();
			} else {
				u32 addr = jumpStack[jumpStack.size()-1];
				jumpStack.pop_back();
				gotoAddr(addr);
			}
			return;
		case VK_RIGHT:
			followBranch();
			return;
		case VK_TAB:
			displaySymbols = !displaySymbols;
			break;
		case VK_CONTROL:
			controlHeld = true;
			break;
		case VK_F9:
			if (debugger->GetPC() != curAddress)
			{
				SendMessage(GetParent(wnd),WM_DEB_RUNTOWPARAM,curAddress,0);
			}
			break;
		case VK_F10:
			SendMessage(GetParent(wnd),WM_COMMAND,IDC_STEPOVER,0);
			return;
		case VK_F11:
			SendMessage(GetParent(wnd),WM_COMMAND,IDC_STEP,0);
			return;
		case VK_SPACE:
			debugger->toggleBreakpoint(curAddress);
			break;
		default:
			return;
		}
	}
	redraw();
}

void CtrlDisAsmView::onKeyUp(WPARAM wParam, LPARAM lParam)
{
	switch (wParam & 0xFFFF)
	{
	case VK_CONTROL:
		controlHeld = false;
		break;
	}
}

void CtrlDisAsmView::scrollAddressIntoView()
{
	u32 windowEnd = windowStart + visibleRows * instructionSize;

	if (curAddress < windowStart)
		windowStart = curAddress;
	else if (curAddress >= windowEnd)
		windowStart = curAddress - visibleRows * instructionSize + instructionSize;
}

bool CtrlDisAsmView::curAddressIsVisible()
{
	u32 windowEnd = windowStart + visibleRows * instructionSize;
	return curAddress >= windowStart && curAddress < windowEnd;
}

void CtrlDisAsmView::redraw()
{
	if (dontRedraw == true) return;

	GetClientRect(wnd, &rect);
	visibleRows = rect.bottom/rowHeight;

	InvalidateRect(wnd, NULL, FALSE);
	UpdateWindow(wnd); 
}

void CtrlDisAsmView::toggleBreakpoint()
{
	bool enabled;
	if (CBreakPoints::IsAddressBreakPoint(curAddress,&enabled))
	{
		if (!enabled)
		{
			// enable disabled breakpoints
			CBreakPoints::ChangeBreakPoint(curAddress,true);
		} else if (CBreakPoints::GetBreakPointCondition(curAddress) != NULL)
		{
			// don't just delete a breakpoint with a custom condition
			int ret = MessageBox(wnd,"This breakpoint has a custom condition.\nDo you want to remove it?","Confirmation",MB_YESNO);
			if (ret != IDYES) return;
			CBreakPoints::RemoveBreakPoint(curAddress);
		} else {
			// otherwise just remove breakpoint
			CBreakPoints::RemoveBreakPoint(curAddress);
		}
	} else {
		CBreakPoints::AddBreakPoint(curAddress);
	}
}

void CtrlDisAsmView::onMouseDown(WPARAM wParam, LPARAM lParam, int button)
{
	int x = LOWORD(lParam); 
	int y = HIWORD(lParam);

	int newAddress = yToAddress(y);
	if (button == 1)
	{
		if (newAddress == curAddress && hasFocus)
		{
			toggleBreakpoint();
		}
	}

	curAddress = newAddress;
	SetFocus(wnd);
	redraw();
}

void CtrlDisAsmView::onMouseUp(WPARAM wParam, LPARAM lParam, int button)
{
	if (button==2)
	{
		//popup menu?
		POINT pt;
		GetCursorPos(&pt);
		switch(TrackPopupMenuEx(GetSubMenu(g_hPopupMenus,1),TPM_RIGHTBUTTON|TPM_RETURNCMD,pt.x,pt.y,wnd,0))
		{
		case ID_DISASM_GOTOINMEMORYVIEW:
			for (int i=0; i<numCPUs; i++)
				if (memoryWindow[i])
					memoryWindow[i]->Goto(curAddress);
			break;
		case ID_DISASM_ADDHLE:
			break;
		case ID_DISASM_TOGGLEBREAKPOINT:
			toggleBreakpoint();
			redraw();
			break;
		case ID_DISASM_COPYINSTRUCTIONDISASM:
			{
				char temp[256],opcode[64],arguments[256];
				const char *dizz = debugger->disasm(curAddress, instructionSize);
				parseDisasm(dizz,opcode,arguments);
				sprintf(temp,"%s\t%s",opcode,arguments);

				W32Util::CopyTextToClipboard(wnd, temp);
			}
			break;
		case ID_DISASM_COPYADDRESS:
			{
				char temp[16];
				sprintf(temp,"%08X",curAddress);
				W32Util::CopyTextToClipboard(wnd, temp);
			}
			break;
		case ID_DISASM_SETPCTOHERE:
			debugger->setPC(curAddress);
			redraw();
			break;
		case ID_DISASM_FOLLOWBRANCH:
			followBranch();
			break;
		case ID_DISASM_COPYINSTRUCTIONHEX:
			{
				char temp[24];
				sprintf(temp,"%08X",debugger->readMemory(curAddress));
				W32Util::CopyTextToClipboard(wnd,temp);
			}
			break;
		case ID_DISASM_RUNTOHERE:
			{
				debugger->setBreakpoint(curAddress);
				debugger->runToBreakpoint();
				redraw();
			}
			break;
		case ID_DISASM_RENAMEFUNCTION:
			{
				int sym = symbolMap.GetSymbolNum(curAddress);
				if (sym != -1)
				{
					char name[256];
					char newname[256];
					strncpy_s(name, symbolMap.GetSymbolName(sym),_TRUNCATE);
					if (InputBox_GetString(MainWindow::GetHInstance(), MainWindow::GetHWND(), "New function name", name, newname))
					{
						symbolMap.SetSymbolName(sym,newname);
						redraw();
						SendMessage(GetParent(wnd),WM_DEB_MAPLOADED,0,0);
					}
				}
				else
				{
					MessageBox(MainWindow::GetHWND(),"No symbol selected",0,0);
				}
			}
			break;
		case ID_DISASM_DISASSEMBLETOFILE:
			disassembleToFile();
			break;
		}
		return;
	}

	redraw();
}

void CtrlDisAsmView::onMouseMove(WPARAM wParam, LPARAM lParam, int button)
{

}	


int CtrlDisAsmView::yToAddress(int y)
{
	int line = y/rowHeight;
	return windowStart + line*instructionSize;
}

void CtrlDisAsmView::calculatePixelPositions()
{
	pixelPositions.addressStart = 16;
	pixelPositions.opcodeStart = pixelPositions.addressStart + 18*charWidth;
	pixelPositions.argumentsStart = pixelPositions.opcodeStart + 9*charWidth;
	pixelPositions.arrowsStart = pixelPositions.argumentsStart + 30*charWidth;
}

void CtrlDisAsmView::search(bool continueSearch)
{
	u32 searchAddress;

	if (continueSearch == false || searchQuery[0] == 0)
	{
		if (InputBox_GetString(MainWindow::GetHInstance(),MainWindow::GetHWND(),"Search for:","",searchQuery) == false
			|| searchQuery[0] == 0)
		{
			SetFocus(wnd);
			return;
		}

		for (int i = 0; searchQuery[i] != 0; i++)
		{
			searchQuery[i] = tolower(searchQuery[i]);
		}
		SetFocus(wnd);
		searchAddress = curAddress+instructionSize;
	} else {
		searchAddress = matchAddress+instructionSize;
	}

	// limit address to sensible ranges
	if (searchAddress < 0x04000000) searchAddress = 0x04000000;
	if (searchAddress >= 0x04200000 && searchAddress < 0x08000000) searchAddress = 0x08000000;
	if (searchAddress >= 0x0A000000)
	{	
		MessageBox(wnd,"Not found","Search",MB_OK);
		return;
	}

	searching = true;
	redraw();	// so the cursor is disabled
	while (searchAddress < 0x0A000000)
	{
		char addressText[64],opcode[64],arguments[256];
		const char *dis = debugger->disasm(searchAddress, instructionSize);
		parseDisasm(dis,opcode,arguments);
		getDisasmAddressText(searchAddress,addressText,true);

		char merged[512];
		int mergePos = 0;

		// I'm doing it manually to convert everything to lowercase at the same time
		for (int i = 0; addressText[i] != 0; i++) merged[mergePos++] = tolower(addressText[i]);
		merged[mergePos++] = ' ';
		for (int i = 0; opcode[i] != 0; i++) merged[mergePos++] = tolower(opcode[i]);
		merged[mergePos++] = ' ';
		for (int i = 0; arguments[i] != 0; i++) merged[mergePos++] = tolower(arguments[i]);
		merged[mergePos] = 0;

		// match!
		if (strstr(merged,searchQuery) != NULL)
		{
			matchAddress = searchAddress;
			searching = false;
			gotoAddr(searchAddress);
			return;
		}

		// cancel search
		if ((searchAddress % 256) == 0 && GetAsyncKeyState(VK_ESCAPE))
		{
			searching = false;
			return;
		}

		searchAddress += instructionSize;
		if (searchAddress >= 0x04200000 && searchAddress < 0x08000000) searchAddress = 0x08000000;
	}
	
	MessageBox(wnd,"Not found","Search",MB_OK);
	searching = false;
}

void CtrlDisAsmView::disassembleToFile()
{
	char fileName[MAX_PATH];
	u32 size;

	// get size
	if (executeExpressionWindow(wnd,debugger,size) == false) return;
	if (size == 0 || size > 10*1024*1024)
	{
		MessageBox(wnd,"Invalid size!","Error",MB_OK);
		return;
	}

	// get file name
	OPENFILENAME ofn;
	ZeroMemory( &ofn , sizeof( ofn));
	ofn.lStructSize = sizeof ( ofn );
	ofn.hwndOwner = NULL ;
	ofn.lpstrFile = fileName ;
	ofn.lpstrFile[0] = '\0';
	ofn.nMaxFile = sizeof( fileName );
	ofn.lpstrFilter = "All files";
	ofn.nFilterIndex = 1;
	ofn.lpstrFileTitle = NULL ;
	ofn.nMaxFileTitle = 0 ;
	ofn.lpstrInitialDir = NULL ;
	ofn.Flags = OFN_PATHMUSTEXIST|OFN_FILEMUSTEXIST|OFN_OVERWRITEPROMPT;

	if (GetSaveFileName(&ofn) == false) return;

	FILE* output = fopen(fileName,"w");
	if (output == NULL)
	{
		MessageBox(wnd,"Could not open file!","Error",MB_OK);
		return;
	}

	// gather all branch targets without labels
	std::set<u32> branchAddresses;
	for (u32 i = 0; i < size; i += instructionSize)
	{
		char opcode[64],arguments[256];
		const char *dis = debugger->disasm(curAddress+i, instructionSize);
		parseDisasm(dis,opcode,arguments);

		if (branchTarget != -1 && debugger->findSymbolForAddress(branchTarget) == NULL)
		{
			if (branchAddresses.find(branchTarget) == branchAddresses.end())
			{
				branchAddresses.insert(branchTarget);
			}
		}
	}

	bool previousLabel = true;
	for (u32 i = 0; i < size; i += instructionSize)
	{
		u32 disAddress = curAddress+i;

		char addressText[64],opcode[64],arguments[256];
		const char *dis = debugger->disasm(disAddress, instructionSize);
		parseDisasm(dis,opcode,arguments);
		bool isLabel = getDisasmAddressText(disAddress,addressText,false);

		if (isLabel)
		{
			if (!previousLabel) fprintf(output,"\n");
			fprintf(output,"%s\n\n",addressText);
		} else if (branchAddresses.find(disAddress) != branchAddresses.end())
		{
			if (!previousLabel) fprintf(output,"\n");
			fprintf(output,"pos_%08X:\n\n",disAddress);
		}

		if (branchTarget != -1 && debugger->findSymbolForAddress(branchTarget) == NULL)
		{
			char* str = strstr(arguments,"0x");
			sprintf(str,"pos_%08X",branchTarget);
		}

		fprintf(output,"\t%s\t%s\n",opcode,arguments);
		previousLabel = isLabel;
	}

	fclose(output);
	MessageBox(wnd,"Finished!","Done",MB_OK);
}

void CtrlDisAsmView::getOpcodeText(u32 address, char* dest)
{
	char opcode[64],arguments[256];
	const char *dis = debugger->disasm(address, instructionSize);
	parseDisasm(dis,opcode,arguments);

	sprintf(dest,"%s  %s",opcode,arguments);
}