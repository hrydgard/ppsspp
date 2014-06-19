// NOTE: Apologies for the quality of this code, this is really from pre-opensource Dolphin - that is, 2003.

#include "Windows/resource.h"
#include "Core/MemMap.h"
#include "Core/MIPS/JitCommon/JitCommon.h"
#include "Windows/W32Util/Misc.h"
#include "Windows/WndMainWindow.h"
#include "Windows/InputBox.h"

#include "Core/MIPS/MIPSAsm.h"
#include "Core/MIPS/MIPSAnalyst.h"
#include "Core/Config.h"
#include "Windows/Debugger/CtrlDisAsmView.h"
#include "Windows/Debugger/Debugger_MemoryDlg.h"
#include "Windows/Debugger/DebuggerShared.h"
#include "Windows/Debugger/BreakpointWindow.h"
#include "Core/Debugger/SymbolMap.h"
#include "Globals.h"
#include "Windows/main.h"

#include "Common/CommonWindows.h"
#include "util/text/utf8.h"
#include "ext/xxhash.h"

#include <CommDlg.h>
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

void CtrlDisAsmView::scanFunctions()
{
	manager.analyze(windowStart,manager.getNthNextAddress(windowStart,visibleRows)-windowStart);
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
		ccp->dontRedraw = false;
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
	case WM_CHAR:
		ccp->onChar(wParam,lParam);
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
		L"Lucida Console");
	boldfont = CreateFont(rowHeight-2,charWidth,0,0,FW_DEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,DEFAULT_QUALITY,DEFAULT_PITCH,
		L"Lucida Console");
	curAddress=0;
	showHex=false;
	hasFocus = false;
	dontRedraw = false;
	keyTaken = false;

	matchAddress = -1;
	searching = false;
	searchQuery = "";
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

bool CtrlDisAsmView::getDisasmAddressText(u32 address, char* dest, bool abbreviateLabels, bool showData)
{
	if (displaySymbols)
	{
		const std::string addressSymbol = symbolMap.GetLabelString(address);
		if (!addressSymbol.empty())
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
		if (showData)
			sprintf(dest,"%08X %08X",address,Memory::Read_U32(address));
		else
			sprintf(dest,"%08X",address);
		return false;
	}
}

std::string trimString(std::string input)
{
	size_t pos = input.find_first_not_of(" \t");
	if (pos != 0 && pos != std::string::npos)
	{
		input = input.erase(0,pos);
	}

	pos = input.find_last_not_of(" \t");
	if (pos != std::string::npos)
	{
		size_t size = input.length()-pos-1;
		input = input.erase(pos+1,size);
	}

	return input;
}

void CtrlDisAsmView::assembleOpcode(u32 address, std::string defaultText)
{
	u32 encoded;

	if (Core_IsStepping() == false) {
		MessageBox(wnd,L"Cannot change code while the core is running!",L"Error",MB_OK);
		return;
	}
	std::string op;
	bool result = InputBox_GetString(MainWindow::GetHInstance(),wnd,L"Assemble opcode",defaultText, op, false);
	if (!result)
		return;

	// check if it changes registers first
	auto seperator = op.find('=');
	if (seperator != std::string::npos)
	{
		std::string registerName = trimString(op.substr(0,seperator));
		std::string expression = trimString(op.substr(seperator+1));

		u32 value;
		if (parseExpression(expression.c_str(),debugger,value) == true)
		{
			for (int cat = 0; cat < debugger->GetNumCategories(); cat++)
			{
				for (int reg = 0; reg < debugger->GetNumRegsInCategory(cat); reg++)
				{
					if (strcasecmp(debugger->GetRegName(cat,reg),registerName.c_str()) == 0)
					{
						debugger->SetRegValue(cat,reg,value);
						SendMessage(GetParent(wnd),WM_DEB_UPDATE,0,0);
						return;
					}
				}
			}
		}

		// try to assemble the input if it failed
	}

	result = MIPSAsm::MipsAssembleOpcode(op.c_str(),debugger,address,encoded);
	if (result == true)
	{
		Memory::Write_U32(encoded, address);
		// In case this is a delay slot or combined instruction, clear cache above it too.
		if (MIPSComp::jit)
			MIPSComp::jit->InvalidateCacheAt(address - 4, 8);
		scanFunctions();

		if (address == curAddress)
			gotoAddr(manager.getNthNextAddress(curAddress,1));

		redraw();
	} else {
		std::wstring error = ConvertUTF8ToWString(MIPSAsm::GetAssembleError());
		MessageBox(wnd,error.c_str(),L"Error",MB_OK);
	}
}


void CtrlDisAsmView::drawBranchLine(HDC hdc, std::map<u32,int>& addressPositions, BranchLine& line)
{
	HPEN pen;
	u32 windowEnd = manager.getNthNextAddress(windowStart,visibleRows);
	
	int topY;
	int bottomY;
	if (line.first < windowStart)
	{
		topY = -1;
	} else if (line.first >= windowEnd)
	{
		topY = rect.bottom+1;
	} else {
		topY = addressPositions[line.first] + rowHeight/2;
	}
			
	if (line.second < windowStart)
	{
		bottomY = -1;
	} else if (line.second >= windowEnd)
	{
		bottomY = rect.bottom+1;
	} else {
		bottomY = addressPositions[line.second] + rowHeight/2;
	}

	if ((topY < 0 && bottomY < 0) || (topY > rect.bottom && bottomY > rect.bottom))
	{
		return;
	}

	// highlight line in a different color if it affects the currently selected opcode
	if (line.first == curAddress || line.second == curAddress)
	{
		pen = CreatePen(0,0,0x257AFA);
	} else {
		pen = CreatePen(0,0,0xFF3020);
	}
	
	HPEN oldPen = (HPEN) SelectObject(hdc,pen);
	int x = pixelPositions.arrowsStart+line.laneIndex*8;

	if (topY < 0)	// first is not visible, but second is
	{
		MoveToEx(hdc,x-2,bottomY,0);
		LineTo(hdc,x+2,bottomY);
		LineTo(hdc,x+2,0);

		if (line.type == LINE_DOWN)
		{
			MoveToEx(hdc,x,bottomY-4,0);
			LineTo(hdc,x-4,bottomY);
			LineTo(hdc,x+1,bottomY+5);
		}
	} else if (bottomY > rect.bottom) // second is not visible, but first is
	{
		MoveToEx(hdc,x-2,topY,0);
		LineTo(hdc,x+2,topY);
		LineTo(hdc,x+2,rect.bottom);
				
		if (line.type == LINE_UP)
		{
			MoveToEx(hdc,x,topY-4,0);
			LineTo(hdc,x-4,topY);
			LineTo(hdc,x+1,topY+5);
		}
	} else { // both are visible
		if (line.type == LINE_UP)
		{
			MoveToEx(hdc,x-2,bottomY,0);
			LineTo(hdc,x+2,bottomY);
			LineTo(hdc,x+2,topY);
			LineTo(hdc,x-4,topY);
			
			MoveToEx(hdc,x,topY-4,0);
			LineTo(hdc,x-4,topY);
			LineTo(hdc,x+1,topY+5);
		} else {
			MoveToEx(hdc,x-2,topY,0);
			LineTo(hdc,x+2,topY);
			LineTo(hdc,x+2,bottomY);
			LineTo(hdc,x-4,bottomY);
			
			MoveToEx(hdc,x,bottomY-4,0);
			LineTo(hdc,x-4,bottomY);
			LineTo(hdc,x+1,bottomY+5);
		}
	}

	SelectObject(hdc,oldPen);
	DeleteObject(pen);
}

std::set<std::string> CtrlDisAsmView::getSelectedLineArguments() {
	std::set<std::string> args;

	DisassemblyLineInfo line;
	for (u32 addr = selectRangeStart; addr < selectRangeEnd; addr += 4) {
		manager.getLine(addr, displaySymbols, line);
		size_t p = 0, nextp = line.params.find(',');
		while (nextp != line.params.npos) {
			args.insert(line.params.substr(p, nextp - p));
			p = nextp + 1;
			nextp = line.params.find(',', p);
		}
		if (p < line.params.size()) {
			args.insert(line.params.substr(p));
		}
	}

	return args;
}

void CtrlDisAsmView::drawArguments(HDC hdc, const DisassemblyLineInfo &line, int x, int y, int textColor, const std::set<std::string> &currentArguments) {
	if (line.params.empty()) {
		return;
	}
	// Don't highlight the selected lines.
	if (isInInterval(selectRangeStart, selectRangeEnd - selectRangeStart, line.info.opcodeAddress)) {
		TextOutA(hdc, x, y, line.params.c_str(), (int)line.params.size());
		return;
	}

	int highlightedColor = 0xaabb00;
	if (textColor == 0x0000ff) {
		highlightedColor = 0xaabb77;
	}

	UINT prevAlign = SetTextAlign(hdc, TA_UPDATECP);
	MoveToEx(hdc, x, y, NULL);

	size_t p = 0, nextp = line.params.find(',');
	while (nextp != line.params.npos) {
		const std::string arg = line.params.substr(p, nextp - p);
		if (currentArguments.find(arg) != currentArguments.end() && textColor != 0xffffff) {
			SetTextColor(hdc, highlightedColor);
		}
		TextOutA(hdc, 0, 0, arg.c_str(), (int)arg.size());
		SetTextColor(hdc,textColor);
		p = nextp + 1;
		nextp = line.params.find(',', p);
		TextOutA(hdc, 0, 0, ",", 1);
	}
	if (p < line.params.size()) {
		const std::string arg = line.params.substr(p);
		if (currentArguments.find(arg) != currentArguments.end() && textColor != 0xffffff) {
			SetTextColor(hdc, highlightedColor);
		}
		TextOutA(hdc, 0, 0, arg.c_str(), (int)arg.size());
		SetTextColor(hdc,textColor);
	}

	SetTextAlign(hdc, prevAlign);
}

void CtrlDisAsmView::onPaint(WPARAM wParam, LPARAM lParam)
{
	if (!debugger->isAlive()) return;

	PAINTSTRUCT ps;
	HDC actualHdc = BeginPaint(wnd, &ps);
	HDC hdc = CreateCompatibleDC(actualHdc);
	HBITMAP hBM = CreateCompatibleBitmap(actualHdc, rect.right-rect.left, rect.bottom-rect.top);
	SelectObject(hdc, hBM);

	SetBkMode(hdc, TRANSPARENT);

	HPEN nullPen=CreatePen(0,0,0xffffff);
	HBRUSH nullBrush=CreateSolidBrush(0xffffff);
	HBRUSH currentBrush=CreateSolidBrush(0xffefe8);

	HPEN oldPen=(HPEN)SelectObject(hdc,nullPen);
	HBRUSH oldBrush=(HBRUSH)SelectObject(hdc,nullBrush);
	HFONT oldFont = (HFONT)SelectObject(hdc,(HGDIOBJ)font);
	HICON breakPoint = (HICON)LoadIcon(GetModuleHandle(0),(LPCWSTR)IDI_STOP);
	HICON breakPointDisable = (HICON)LoadIcon(GetModuleHandle(0),(LPCWSTR)IDI_STOPDISABLE);

	unsigned int address = windowStart;
	std::map<u32,int> addressPositions;

	const std::set<std::string> currentArguments = getSelectedLineArguments();
	DisassemblyLineInfo line;
	for (int i = 0; i < visibleRows; i++)
	{
		manager.getLine(address,displaySymbols,line);

		int rowY1 = rowHeight*i;
		int rowY2 = rowHeight*(i+1);

		addressPositions[address] = rowY1;

		// draw background
		COLORREF backgroundColor = whiteBackground ? 0xFFFFFF : debugger->getColor(address);
		COLORREF textColor = 0x000000;

		if (isInInterval(address,line.totalSize,debugger->getPC()))
		{
			backgroundColor = scaleColor(backgroundColor,1.05f);
		}

		if (address >= selectRangeStart && address < selectRangeEnd && searching == false)
		{
			if (hasFocus)
			{
				backgroundColor = address == curAddress ? 0xFF8822 : 0xFF9933;
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
			if (!enabled) yOffset++;
			DrawIconEx(hdc,2,rowY1+1+yOffset,enabled ? breakPoint : breakPointDisable,32,32,0,0,DI_NORMAL);
		}
		SetTextColor(hdc,textColor);

		char addressText[64];
		getDisasmAddressText(address,addressText,true,line.type == DISTYPE_OPCODE);
		TextOutA(hdc,pixelPositions.addressStart,rowY1+2,addressText,(int)strlen(addressText));
		
		if (isInInterval(address,line.totalSize,debugger->getPC()))
		{
			TextOut(hdc,pixelPositions.opcodeStart-8,rowY1,L"■",1);
		}

		// display whether the condition of a branch is met
		if (line.info.isConditional && address == debugger->getPC())
		{
			line.params += line.info.conditionMet ? "  ; true" : "  ; false";
		}

		drawArguments(hdc, line, pixelPositions.argumentsStart, rowY1 + 2, textColor, currentArguments);
			
		SelectObject(hdc,boldfont);
		TextOutA(hdc,pixelPositions.opcodeStart,rowY1+2,line.name.c_str(),(int)line.name.size());
		SelectObject(hdc,font);

		address += line.totalSize;
	}

	std::vector<BranchLine> branchLines = manager.getBranchLines(windowStart,address-windowStart);
	for (size_t i = 0; i < branchLines.size(); i++)
	{
		drawBranchLine(hdc,addressPositions,branchLines[i]);
	}

	SelectObject(hdc,oldFont);
	SelectObject(hdc,oldPen);
	SelectObject(hdc,oldBrush);

	// copy bitmap to the actual hdc
	BitBlt(actualHdc, 0, 0, rect.right, rect.bottom, hdc, 0, 0, SRCCOPY);
	DeleteObject(hBM);
	DeleteDC(hdc);

	DeleteObject(nullPen);

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
		windowStart = manager.getNthNextAddress(windowStart,1);
		break;
	case SB_LINEUP:
		windowStart = manager.getNthPreviousAddress(windowStart,1);
		break;
	case SB_PAGEDOWN:
		windowStart = manager.getNthNextAddress(windowStart,visibleRows);
		break;
	case SB_PAGEUP:
		windowStart = manager.getNthPreviousAddress(windowStart,visibleRows);
		break;
	default:
		return;
	}

	scanFunctions();
	redraw();
}

void CtrlDisAsmView::followBranch()
{
	DisassemblyLineInfo line;
	manager.getLine(curAddress,true,line);

	if (line.type == DISTYPE_OPCODE || line.type == DISTYPE_MACRO)
	{
		if (line.info.isBranch)
		{
			jumpStack.push_back(curAddress);
			gotoAddr(line.info.branchTarget);
		} else if (line.info.hasRelevantAddress)
		{
			// well, not  exactly a branch, but we can do something anyway
			SendMessage(GetParent(wnd),WM_DEB_GOTOHEXEDIT,line.info.releventAddress,0);
			SetFocus(wnd);
		}
	} else if (line.type == DISTYPE_DATA)
	{
		// jump to the start of the current line
		SendMessage(GetParent(wnd),WM_DEB_GOTOHEXEDIT,curAddress,0);
		SetFocus(wnd);
	}
}

void CtrlDisAsmView::onChar(WPARAM wParam, LPARAM lParam)
{
	if (keyTaken) return;

	char str[2];
	str[0] = wParam;
	str[1] = 0;
	assembleOpcode(curAddress,str);
}


void CtrlDisAsmView::editBreakpoint()
{
	BreakpointWindow win(wnd,debugger);

	bool exists = false;
	if (CBreakPoints::IsAddressBreakPoint(curAddress))
	{
		auto breakpoints = CBreakPoints::GetBreakpoints();
		for (size_t i = 0; i < breakpoints.size(); i++)
		{
			if (breakpoints[i].addr == curAddress)
			{
				win.loadFromBreakpoint(breakpoints[i]);
				exists = true;
				break;
			}
		}
	}

	if (!exists)
		win.initBreakpoint(curAddress);

	if (win.exec())
	{
		if (exists)
			CBreakPoints::RemoveBreakPoint(curAddress);
		win.addBreakpoint();
	}
}

void CtrlDisAsmView::onKeyDown(WPARAM wParam, LPARAM lParam)
{
	dontRedraw = false;
	u32 windowEnd = manager.getNthNextAddress(windowStart,visibleRows);
	keyTaken = true;

	if (KeyDownAsync(VK_CONTROL))
	{
		switch (tolower(wParam & 0xFFFF))
		{
		case 'f':
		case 's':
			search(false);
			break;
		case 'c':
		case VK_INSERT:
			copyInstructions(selectRangeStart, selectRangeEnd, true);
			break;
		case 'x':
			disassembleToFile();
			break;
		case 'a':
			assembleOpcode(curAddress,"");
			break;
		case 'g':
			{
				u32 addr;
				if (executeExpressionWindow(wnd,debugger,addr) == false) return;
				gotoAddr(addr);
			}
			break;
		case 'e':	// edit breakpoint
			editBreakpoint();
			break;
		case 'd':	// toogle breakpoint enabled
			toggleBreakpoint(true);
			break;
		case VK_UP:
			scrollWindow(-1);
			scanFunctions();
			break;
		case VK_DOWN:
			scrollWindow(1);
			scanFunctions();
			break;
		case VK_NEXT:
			setCurAddress(manager.getNthPreviousAddress(windowEnd,1),KeyDownAsync(VK_SHIFT));
			break;
		case VK_PRIOR:
			setCurAddress(windowStart,KeyDownAsync(VK_SHIFT));
			break;
		}
	} else {
		switch (wParam & 0xFFFF)
		{
		case VK_DOWN:
			setCurAddress(manager.getNthNextAddress(curAddress,1), KeyDownAsync(VK_SHIFT));
			scrollAddressIntoView();
			break;
		case VK_UP:
			setCurAddress(manager.getNthPreviousAddress(curAddress,1), KeyDownAsync(VK_SHIFT));
			scrollAddressIntoView();
			break;
		case VK_NEXT:
			if (manager.getNthNextAddress(curAddress,1) != windowEnd && curAddressIsVisible()) {
				setCurAddress(manager.getNthPreviousAddress(windowEnd,1), KeyDownAsync(VK_SHIFT));
				scrollAddressIntoView();
			} else {
				setCurAddress(manager.getNthNextAddress(windowEnd,visibleRows-1), KeyDownAsync(VK_SHIFT));
				scrollAddressIntoView();
			}
			break;
		case VK_PRIOR:
			if (curAddress != windowStart && curAddressIsVisible()) {
				setCurAddress(windowStart, KeyDownAsync(VK_SHIFT));
				scrollAddressIntoView();
			} else {
				setCurAddress(manager.getNthPreviousAddress(windowStart,visibleRows), KeyDownAsync(VK_SHIFT));
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
		case VK_SPACE:
			debugger->toggleBreakpoint(curAddress);
			break;
		case VK_F3:
			search(true);
			break;
		default:
			keyTaken = false;
			return;
		}
	}
	redraw();
}

void CtrlDisAsmView::onKeyUp(WPARAM wParam, LPARAM lParam)
{

}

void CtrlDisAsmView::scrollAddressIntoView()
{
	u32 windowEnd = manager.getNthNextAddress(windowStart,visibleRows);

	if (curAddress < windowStart)
		windowStart = curAddress;
	else if (curAddress >= windowEnd)
		windowStart =  manager.getNthPreviousAddress(curAddress,visibleRows-1);

	scanFunctions();
}

bool CtrlDisAsmView::curAddressIsVisible()
{
	u32 windowEnd = manager.getNthNextAddress(windowStart,visibleRows);
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

void CtrlDisAsmView::toggleBreakpoint(bool toggleEnabled)
{
	bool enabled;
	if (CBreakPoints::IsAddressBreakPoint(curAddress,&enabled))
	{
		if (!enabled)
		{
			// enable disabled breakpoints
			CBreakPoints::ChangeBreakPoint(curAddress,true);
		} else if (!toggleEnabled && CBreakPoints::GetBreakPointCondition(curAddress) != NULL)
		{
			// don't just delete a breakpoint with a custom condition
			int ret = MessageBox(wnd,L"This breakpoint has a custom condition.\nDo you want to remove it?",L"Confirmation",MB_YESNO);
			if (ret == IDYES)
				CBreakPoints::RemoveBreakPoint(curAddress);
		} else if (toggleEnabled)
		{
			// disable breakpoint
			CBreakPoints::ChangeBreakPoint(curAddress,false);
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
	dontRedraw = false;
	int x = LOWORD(lParam);
	int y = HIWORD(lParam);

	u32 newAddress = yToAddress(y);
	bool extend = KeyDownAsync(VK_SHIFT);
	if (button == 1)
	{
		if (newAddress == curAddress && hasFocus)
		{
			toggleBreakpoint();
		}
	}
	else if (button == 2)
	{
		// Maintain the current selection if right clicking into it.
		if (newAddress >= selectRangeStart && newAddress < selectRangeEnd)
			extend = true;
	}
	setCurAddress(newAddress, extend);

	SetFocus(wnd);
	redraw();
}

void CtrlDisAsmView::copyInstructions(u32 startAddr, u32 endAddr, bool withDisasm)
{
	if (withDisasm == false)
	{
		int instructionSize = debugger->getInstructionSize(0);
		int count = (endAddr - startAddr) / instructionSize;
		int space = count * 32;
		char *temp = new char[space];

		char *p = temp, *end = temp + space;
		for (u32 pos = startAddr; pos < endAddr; pos += instructionSize)
		{
			p += snprintf(p, end - p, "%08X", debugger->readMemory(pos));

			// Don't leave a trailing newline.
			if (pos + instructionSize < endAddr)
				p += snprintf(p, end - p, "\r\n");
		}
		W32Util::CopyTextToClipboard(wnd, temp);
		delete [] temp;
	} else
	{
		std::string disassembly = disassembleRange(startAddr,endAddr-startAddr);
		W32Util::CopyTextToClipboard(wnd, disassembly.c_str());
	}
}

void CtrlDisAsmView::onMouseUp(WPARAM wParam, LPARAM lParam, int button)
{
	if (button == 1)
	{
		int x = LOWORD(lParam);
		int y = HIWORD(lParam);
		setCurAddress(yToAddress(y), KeyDownAsync(VK_SHIFT));
		redraw();
	}
	else if (button == 2)
	{
		//popup menu?
		POINT pt;
		GetCursorPos(&pt);
		switch(TrackPopupMenuEx(GetSubMenu(g_hPopupMenus,1),TPM_RIGHTBUTTON|TPM_RETURNCMD,pt.x,pt.y,wnd,0))
		{
		case ID_DISASM_GOTOINMEMORYVIEW:
			SendMessage(GetParent(wnd),WM_DEB_GOTOHEXEDIT,curAddress,0);
			break;
		case ID_DISASM_ADDHLE:
			break;
		case ID_DISASM_TOGGLEBREAKPOINT:
			toggleBreakpoint();
			redraw();
			break;
		case ID_DISASM_ASSEMBLE:
			assembleOpcode(curAddress,"");
			break;
		case ID_DISASM_COPYINSTRUCTIONDISASM:
			copyInstructions(selectRangeStart, selectRangeEnd, true);
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
			copyInstructions(selectRangeStart, selectRangeEnd, false);
			break;
		case ID_DISASM_RUNTOHERE:
			{
				SendMessage(GetParent(wnd), WM_COMMAND, ID_DEBUG_RUNTOLINE, 0);
				redraw();
			}
			break;
		case ID_DISASM_RENAMEFUNCTION:
			{
				u32 funcBegin = symbolMap.GetFunctionStart(curAddress);
				if (funcBegin != -1)
				{
					char name[256];
					std::string newname;
					strncpy_s(name, symbolMap.GetLabelString(funcBegin).c_str(),_TRUNCATE);
					if (InputBox_GetString(MainWindow::GetHInstance(), MainWindow::GetHWND(), L"New function name", name, newname)) {
						symbolMap.SetLabelName(newname.c_str(),funcBegin);
						u32 funcSize = symbolMap.GetFunctionSize(curAddress);
						MIPSAnalyst::RegisterFunction(funcBegin, funcSize, newname.c_str());
						MIPSAnalyst::UpdateHashMap();
						MIPSAnalyst::ApplyHashMap();
						SendMessage(GetParent(wnd),WM_DEB_MAPLOADED,0,0);
						redraw();
					}
				}
				else
				{
					MessageBox(MainWindow::GetHWND(), L"No symbol selected",0,0);
				}
			}
			break;
		case ID_DISASM_REMOVEFUNCTION:
			{
				char statusBarTextBuff[256];
				u32 funcBegin = symbolMap.GetFunctionStart(curAddress);
				if (funcBegin != -1)
				{
					u32 prevBegin = symbolMap.GetFunctionStart(funcBegin-1);
					if (prevBegin != -1)
					{
						u32 expandedSize = symbolMap.GetFunctionSize(prevBegin)+symbolMap.GetFunctionSize(funcBegin);
						symbolMap.SetFunctionSize(prevBegin,expandedSize);
					}
					
					symbolMap.RemoveFunction(funcBegin,true);
					symbolMap.SortSymbols();
					manager.clear();

					SendMessage(GetParent(wnd), WM_DEB_MAPLOADED, 0, 0);
				}
				else
				{
					snprintf(statusBarTextBuff,256, "WARNING: unable to find function symbol here");
					SendMessage(GetParent(wnd), WM_DEB_SETSTATUSBARTEXT, 0, (LPARAM) statusBarTextBuff);
				}
				redraw();
			}
			break;
		case ID_DISASM_ADDFUNCTION:
			{
				char statusBarTextBuff[256];
				u32 prevBegin = symbolMap.GetFunctionStart(curAddress);
				if (prevBegin != -1)
				{
					if (prevBegin == curAddress)
					{
						snprintf(statusBarTextBuff,256, "WARNING: There's already a function entry point at this adress");
						SendMessage(GetParent(wnd), WM_DEB_SETSTATUSBARTEXT, 0, (LPARAM) statusBarTextBuff);
					}
					else
					{
						char symname[128];
						u32 prevSize = symbolMap.GetFunctionSize(prevBegin);
						u32 newSize = curAddress-prevBegin;
						symbolMap.SetFunctionSize(prevBegin,newSize);

						newSize = prevSize-newSize;
						snprintf(symname,128,"u_un_%08X",curAddress);
						symbolMap.AddFunction(symname,curAddress,newSize);
						symbolMap.SortSymbols();
						manager.clear();

						SendMessage(GetParent(wnd), WM_DEB_MAPLOADED, 0, 0);
					}
				}
				else
				{
					char symname[128];
					int newSize = selectRangeEnd - selectRangeStart;
					snprintf(symname, 128, "u_un_%08X", selectRangeStart);
					symbolMap.AddFunction(symname, selectRangeStart, newSize);
					symbolMap.SortSymbols();

					SendMessage(GetParent(wnd), WM_DEB_MAPLOADED, 0, 0);
				}
				redraw();
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
	if ((button & 1) != 0)
	{
		int x = LOWORD(lParam);
		int y = HIWORD(lParam);
		setCurAddress(yToAddress(y), KeyDownAsync(VK_SHIFT));
		// TODO: Perhaps don't do this every time, but on a timer?
		redraw();
	}
}	

void CtrlDisAsmView::updateStatusBarText()
{
	char text[512];
	DisassemblyLineInfo line;
	manager.getLine(curAddress,true,line);
	
	text[0] = 0;
	if (line.type == DISTYPE_OPCODE || line.type == DISTYPE_MACRO)
	{
		if (line.info.isDataAccess)
		{
			if (!Memory::IsValidAddress(line.info.dataAddress))
			{
				sprintf(text,"Invalid address %08X",line.info.dataAddress);
			} else {
				switch (line.info.dataSize)
				{
				case 1:
					sprintf(text,"[%08X] = %02X",line.info.dataAddress,Memory::Read_U8(line.info.dataAddress));
					break;
				case 2:
					sprintf(text,"[%08X] = %04X",line.info.dataAddress,Memory::Read_U16(line.info.dataAddress));
					break;
				case 4:
					// TODO: Could also be a float...
					{
						u32 data = Memory::Read_U32(line.info.dataAddress);
						const std::string addressSymbol = symbolMap.GetLabelString(data);
						if (!addressSymbol.empty())
						{
							sprintf(text,"[%08X] = %s (%08X)",line.info.dataAddress,addressSymbol.c_str(),data);
						} else {
							sprintf(text,"[%08X] = %08X",line.info.dataAddress,data);
						}
						break;
					}
				case 16:
					// TODO: vector
					break;
				}
			}
		}

		if (line.info.isBranch)
		{
			const std::string addressSymbol = symbolMap.GetLabelString(line.info.branchTarget);
			if (addressSymbol.empty())
			{
				sprintf(text,"%08X",line.info.branchTarget);
			} else {
				sprintf(text,"%08X = %s",line.info.branchTarget,addressSymbol.c_str());
			}
		}
	} else if (line.type == DISTYPE_DATA)
	{
		u32 start = symbolMap.GetDataStart(curAddress);
		if (start == -1)
			start = curAddress;

		u32 diff = curAddress-start;
		const std::string label = symbolMap.GetLabelString(start);

		if (!label.empty())
		{
			if (diff != 0)
				sprintf(text,"%08X (%s) + %08X",start,label.c_str(),diff);
			else
				sprintf(text,"%08X (%s)",start,label.c_str());
		} else {
			if (diff != 0)
				sprintf(text,"%08X + %08X",start,diff);
			else
				sprintf(text,"%08X",start);
		}
	}

	SendMessage(GetParent(wnd),WM_DEB_SETSTATUSBARTEXT,0,(LPARAM)text);

	const std::string label = symbolMap.GetLabelString(line.info.opcodeAddress);
	if (!label.empty())
	{
		SendMessage(GetParent(wnd),WM_DEB_SETSTATUSBARTEXT,1,(LPARAM)label.c_str());
	}
}

u32 CtrlDisAsmView::yToAddress(int y)
{
	int line = y/rowHeight;
	return manager.getNthNextAddress(windowStart,line);
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
		if (InputBox_GetString(MainWindow::GetHInstance(),MainWindow::GetHWND(),L"Search for:","",searchQuery) == false
			|| searchQuery[0] == 0)
		{
			SetFocus(wnd);
			return;
		}

		for (size_t i = 0; i < searchQuery.size(); i++)
		{
			searchQuery[i] = tolower(searchQuery[i]);
		}
		SetFocus(wnd);
		searchAddress = manager.getNthNextAddress(curAddress,1);
	} else {
		searchAddress = manager.getNthNextAddress(matchAddress,1);
	}

	// limit address to sensible ranges
	if (searchAddress < 0x04000000) searchAddress = 0x04000000;
	if (searchAddress >= 0x04200000 && searchAddress < 0x08000000) searchAddress = 0x08000000;
	if (searchAddress >= 0x0A000000) {	
		MessageBox(wnd,L"Not found",L"Search",MB_OK);
		return;
	}

	searching = true;
	redraw();	// so the cursor is disabled

	DisassemblyLineInfo lineInfo;
	while (searchAddress < 0x0A000000)
	{
		manager.getLine(searchAddress,displaySymbols,lineInfo);

		char addressText[64];
		getDisasmAddressText(searchAddress,addressText,true,lineInfo.type == DISTYPE_OPCODE);

		const char* opcode = lineInfo.name.c_str();
		const char* arguments = lineInfo.params.c_str();

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
		if (strstr(merged, searchQuery.c_str()) != NULL)
		{
			matchAddress = searchAddress;
			searching = false;
			gotoAddr(searchAddress);
			return;
		}

		// cancel search
		if ((searchAddress % 256) == 0 && KeyDownAsync(VK_ESCAPE))
		{
			searching = false;
			return;
		}

		searchAddress = manager.getNthNextAddress(searchAddress,1);
		if (searchAddress >= 0x04200000 && searchAddress < 0x08000000) searchAddress = 0x08000000;
	}
	
	MessageBox(wnd,L"Not found",L"Search",MB_OK);
	searching = false;
}

std::string CtrlDisAsmView::disassembleRange(u32 start, u32 size)
{
	std::string result;

	// gather all branch targets without labels
	std::set<u32> branchAddresses;
	for (u32 i = 0; i < size; i += debugger->getInstructionSize(0))
	{
		MIPSAnalyst::MipsOpcodeInfo info = MIPSAnalyst::GetOpcodeInfo(debugger,start+i);

		if (info.isBranch && symbolMap.GetLabelString(info.branchTarget).empty())
		{
			if (branchAddresses.find(info.branchTarget) == branchAddresses.end())
			{
				branchAddresses.insert(info.branchTarget);
			}
		}
	}

	u32 disAddress = start;
	bool previousLabel = true;
	DisassemblyLineInfo line;
	while (disAddress < start+size)
	{
		char addressText[64],buffer[512];

		manager.getLine(disAddress,displaySymbols,line);
		bool isLabel = getDisasmAddressText(disAddress,addressText,false,line.type == DISTYPE_OPCODE);

		if (isLabel)
		{
			if (!previousLabel) result += "\r\n";
			sprintf(buffer,"%s\r\n\r\n",addressText);
			result += buffer;
		} else if (branchAddresses.find(disAddress) != branchAddresses.end())
		{
			if (!previousLabel) result += "\r\n";
			sprintf(buffer,"pos_%08X:\r\n\r\n",disAddress);
			result += buffer;
		}

		if (line.info.isBranch && !line.info.isBranchToRegister
			&& symbolMap.GetLabelString(line.info.branchTarget).empty()
			&& branchAddresses.find(line.info.branchTarget) != branchAddresses.end())
		{
			sprintf(buffer,"pos_%08X",line.info.branchTarget);
			line.params = line.params.substr(0,line.params.find("0x")) + buffer;
		}

		sprintf(buffer,"\t%s\t%s\r\n",line.name.c_str(),line.params.c_str());
		result += buffer;
		previousLabel = isLabel;
		disAddress += line.totalSize;
	}

	return result;
}

void CtrlDisAsmView::disassembleToFile()
{
	wchar_t fileName[MAX_PATH];
	u32 size;

	// get size
	if (executeExpressionWindow(wnd,debugger,size) == false) return;
	if (size == 0 || size > 10*1024*1024)
	{
		MessageBox(wnd,L"Invalid size!",L"Error",MB_OK);
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
	ofn.lpstrFilter = L"All Files\0*.*\0\0";
	ofn.nFilterIndex = 1;
	ofn.lpstrFileTitle = NULL ;
	ofn.nMaxFileTitle = 0 ;
	ofn.lpstrInitialDir = NULL ;
	ofn.Flags = OFN_PATHMUSTEXIST|OFN_FILEMUSTEXIST|OFN_OVERWRITEPROMPT;

	if (GetSaveFileName(&ofn) == false) return;

	FILE* output = _wfopen(fileName, L"wb");
	if (output == NULL) {
		MessageBox(wnd,L"Could not open file!",L"Error",MB_OK);
		return;
	}

	std::string disassembly = disassembleRange(curAddress,size);
	fprintf(output,"%s",disassembly.c_str());

	fclose(output);
	MessageBox(wnd,L"Finished!",L"Done",MB_OK);
}

void CtrlDisAsmView::getOpcodeText(u32 address, char* dest)
{
	DisassemblyLineInfo line;
	address = manager.getStartAddress(address);
	manager.getLine(address,displaySymbols,line);
	sprintf(dest,"%s  %s",line.name.c_str(),line.params.c_str());
}

void CtrlDisAsmView::scrollStepping(u32 newPc)
{
	u32 windowEnd = manager.getNthNextAddress(windowStart,visibleRows);

	newPc = manager.getStartAddress(newPc);
	if (newPc >= windowEnd || newPc >= manager.getNthPreviousAddress(windowEnd,1))
	{
		windowStart = manager.getNthPreviousAddress(newPc,visibleRows-2);
	}
}

u32 CtrlDisAsmView::getInstructionSizeAt(u32 address)
{
	u32 start = manager.getStartAddress(address);
	u32 next  = manager.getNthNextAddress(start,1);
	return next-address;
}
