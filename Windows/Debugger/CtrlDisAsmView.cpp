#include "Windows/resource.h"
#include "Core/MemMap.h"
#include "Core/MIPS/JitCommon/JitCommon.h"
#include "Windows/W32Util/ContextMenu.h"
#include "Windows/W32Util/Misc.h"
#include "Windows/W32Util/ShellUtil.h"
#include "Windows/MainWindow.h"
#include "Windows/InputBox.h"

#include "Core/MIPS/MIPSAsm.h"
#include "Core/MIPS/MIPSAnalyst.h"
#include "Core/MIPS/MIPSTables.h"
#include "Core/Config.h"
#include "Core/Debugger/SymbolMap.h"
#include "Core/Reporting.h"
#include "Common/StringUtils.h"
#include "Windows/Debugger/CtrlDisAsmView.h"
#include "Windows/Debugger/Debugger_MemoryDlg.h"
#include "Windows/Debugger/DebuggerShared.h"
#include "Windows/Debugger/BreakpointWindow.h"
#include "Windows/Debugger/EditSymbolsWindow.h"
#include "Windows/main.h"

#include "Common/CommonWindows.h"
#include "Common/Data/Encoding/Utf8.h"
#include "ext/xxhash.h"
#include "Common/System/Display.h"

#include <CommDlg.h>
#include <tchar.h>
#include <set>

TCHAR CtrlDisAsmView::szClassName[] = _T("CtrlDisAsmView");

static constexpr UINT_PTR IDT_REDRAW = 0xC0DE0001;
static constexpr UINT REDRAW_DELAY = 1000 / 60;

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
		lmbDown = false;
		rmbDown = false;
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

	case WM_TIMER:
		if (wParam == IDT_REDRAW) {
			InvalidateRect(hwnd, nullptr, FALSE);
			UpdateWindow(hwnd);
			ccp->redrawScheduled_ = false;
			KillTimer(hwnd, wParam);
		}
		break;

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
	SetWindowLongPtr(wnd, GWLP_USERDATA, (LONG_PTR)this);
	SetWindowLong(wnd, GWL_STYLE, GetWindowLong(wnd,GWL_STYLE) | WS_VSCROLL);
	SetScrollRange(wnd, SB_VERT, -1, 1, TRUE);

	const float fontScale = 1.0f / g_display.dpi_scale_real_y;
	charWidth = g_Config.iFontWidth * fontScale;
	rowHeight = (g_Config.iFontHeight + 2) * fontScale;
	int scaledFontHeight = g_Config.iFontHeight * fontScale;
	font = CreateFont(scaledFontHeight, charWidth, 0, 0,
		FW_DONTCARE, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH,
		L"Lucida Console");
	boldfont = CreateFont(scaledFontHeight, charWidth, 0, 0,
		FW_DEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH,
		L"Lucida Console");

	curAddress = 0;
	showHex = false;
	hasFocus = false;
	dontRedraw = false;
	keyTaken = false;

	matchAddress = -1;
	searching = false;
	searchQuery.clear();
	windowStart = curAddress;
	whiteBackground = false;
	displaySymbols = true;
	calculatePixelPositions();
}


CtrlDisAsmView::~CtrlDisAsmView()
{
	DeleteObject(font);
	DeleteObject(boldfont);
	manager.clear();
}

static COLORREF scaleColor(COLORREF color, float factor)
{
	unsigned char r = color & 0xFF;
	unsigned char g = (color >> 8) & 0xFF;
	unsigned char b = (color >> 16) & 0xFF;

	r = std::min(255, std::max((int)(r * factor), 0));
	g = std::min(255, std::max((int)(g * factor), 0));
	b = std::min(255, std::max((int)(b * factor), 0));

	return (color & 0xFF000000) | (b << 16) | (g << 8) | r;
}

bool CtrlDisAsmView::getDisasmAddressText(u32 address, char* dest, bool abbreviateLabels, bool showData)
{
	if (!PSP_IsInited())
		return false;

	if (displaySymbols)
	{
		const std::string addressSymbol = g_symbolMap->GetLabelString(address);
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
		if (showData) {
			u32 encoding = Memory::IsValidAddress(address) ? Memory::Read_Instruction(address, true).encoding : 0;
			sprintf(dest, "%08X %08X", address, encoding);
		} else {
			sprintf(dest, "%08X", address);
		}
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

void CtrlDisAsmView::assembleOpcode(u32 address, const std::string &defaultText)
{
	auto memLock = Memory::Lock();
	if (!Core_IsStepping()) {
		MessageBox(wnd,L"Cannot change code while the core is running!",L"Error",MB_OK);
		return;
	}
	std::string op;
	bool result = InputBox_GetString(MainWindow::GetHInstance(), wnd, L"Assemble opcode", defaultText, op, InputBoxFlags::Default);
	if (!result) {
		return;
	}

	// check if it changes registers first
	auto separator = op.find('=');
	if (separator != std::string::npos)
	{
		std::string registerName = trimString(op.substr(0,separator));
		std::string expression = trimString(op.substr(separator+1));

		u32 value;
		if (parseExpression(expression.c_str(),debugger,value) == true)
		{
			for (int cat = 0; cat < debugger->GetNumCategories(); cat++)
			{
				for (int reg = 0; reg < debugger->GetNumRegsInCategory(cat); reg++)
				{
					if (strcasecmp(debugger->GetRegName(cat,reg).c_str(), registerName.c_str()) == 0)
					{
						debugger->SetRegValue(cat,reg,value);
						Reporting::NotifyDebugger();
						SendMessage(GetParent(wnd),WM_DEB_UPDATE,0,0);
						return;
					}
				}
			}
		}

		// try to assemble the input if it failed
	}

	result = MIPSAsm::MipsAssembleOpcode(op.c_str(), debugger, address);
	Reporting::NotifyDebugger();
	if (result == true)
	{
		scanFunctions();

		if (address == curAddress)
			gotoAddr(manager.getNthNextAddress(curAddress,1));

		redraw();
	} else {
		std::wstring error = ConvertUTF8ToWString(MIPSAsm::GetAssembleError());
		MessageBox(wnd,error.c_str(),L"Error",MB_OK);
	}
}

void CtrlDisAsmView::drawBranchLine(HDC hdc, std::map<u32,int> &addressPositions, const BranchLine &line) {
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
			args.emplace(line.params.substr(p, nextp - p));
			p = nextp + 1;
			nextp = line.params.find(',', p);
		}
		if (p < line.params.size()) {
			args.emplace(line.params.substr(p));
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
	auto memLock = Memory::Lock();
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
			int yOffset = std::max(-1, (rowHeight - 14 + 1) / 2);
			if (!enabled) yOffset++;
			DrawIconEx(hdc,2,rowY1+1+yOffset,enabled ? breakPoint : breakPointDisable,32,32,0,0,DI_NORMAL);
		}
		SetTextColor(hdc,textColor);

		char addressText[64];
		getDisasmAddressText(address,addressText,true,line.type == DISTYPE_OPCODE);
		TextOutA(hdc,pixelPositions.addressStart,rowY1+2,addressText,(int)strlen(addressText));
		
		if (isInInterval(address,line.totalSize,debugger->getPC()))
		{
			TextOut(hdc,pixelPositions.opcodeStart-8,rowY1,L"\x25A0",1);
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
			SendMessage(GetParent(wnd),WM_DEB_GOTOHEXEDIT,line.info.relevantAddress,0);
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
			CopyInstructions(selectRangeStart, selectRangeEnd, CopyInstructionsMode::DISASM);
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

	if (!redrawScheduled_) {
		SetTimer(wnd, IDT_REDRAW, REDRAW_DELAY, nullptr);
		redrawScheduled_ = true;
	}
}

void CtrlDisAsmView::toggleBreakpoint(bool toggleEnabled)
{
	bool enabled;
	if (CBreakPoints::IsAddressBreakPoint(curAddress, &enabled)) {
		if (!enabled) {
			// enable disabled breakpoints
			CBreakPoints::ChangeBreakPoint(curAddress, true);
		} else if (!toggleEnabled && CBreakPoints::GetBreakPointCondition(curAddress) != nullptr) {
			// don't just delete a breakpoint with a custom condition
			int ret = MessageBox(wnd,L"This breakpoint has a custom condition.\nDo you want to remove it?",L"Confirmation",MB_YESNO);
			if (ret == IDYES)
				CBreakPoints::RemoveBreakPoint(curAddress);
		} else if (toggleEnabled) {
			// disable breakpoint
			CBreakPoints::ChangeBreakPoint(curAddress, false);
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

void CtrlDisAsmView::CopyInstructions(u32 startAddr, u32 endAddr, CopyInstructionsMode mode) {
	_assert_msg_((startAddr & 3) == 0, "readMemory() can't handle unaligned reads");

	if (mode != CopyInstructionsMode::DISASM) {
		int instructionSize = debugger->getInstructionSize(0);
		int count = (endAddr - startAddr) / instructionSize;
		int space = count * 32;
		char *temp = new char[space];

		char *p = temp, *end = temp + space;
		for (u32 pos = startAddr; pos < endAddr && p < end; pos += instructionSize)
		{
			u32 data = mode == CopyInstructionsMode::OPCODES ? debugger->readMemory(pos) : pos;
			p += snprintf(p, end - p, "%08X", data);

			// Don't leave a trailing newline.
			if (pos + instructionSize < endAddr && p < end)
				p += snprintf(p, end - p, "\r\n");
		}
		W32Util::CopyTextToClipboard(wnd, temp);
		delete [] temp;
	} else {
		std::string disassembly = disassembleRange(startAddr,endAddr-startAddr);
		W32Util::CopyTextToClipboard(wnd, disassembly.c_str());
	}
}

void CtrlDisAsmView::NopInstructions(u32 selectRangeStart, u32 selectRangeEnd) {
	for (u32 addr = selectRangeStart; addr < selectRangeEnd; addr += 4) {
		Memory::Write_U32(0, addr);
	}

	if (currentMIPS) {
		currentMIPS->InvalidateICache(selectRangeStart, selectRangeEnd - selectRangeStart);
	}
}

void CtrlDisAsmView::onMouseUp(WPARAM wParam, LPARAM lParam, int button)
{
	if (button == 1)
	{
		int y = HIWORD(lParam);
		setCurAddress(yToAddress(y), KeyDownAsync(VK_SHIFT));
		redraw();
	}
	else if (button == 2)
	{
		// We don't want to let the users play with deallocated or uninitialized debugging objects
		GlobalUIState state = GetUIState();
		if (state != UISTATE_INGAME && state != UISTATE_PAUSEMENU) {
			return;
		}
		
		switch (TriggerContextMenu(ContextMenuID::DISASM, wnd, ContextPoint::FromEvent(lParam)))
		{
		case ID_DISASM_GOTOINMEMORYVIEW:
			SendMessage(GetParent(wnd),WM_DEB_GOTOHEXEDIT,curAddress,0);
			break;
		case ID_DISASM_TOGGLEBREAKPOINT:
			toggleBreakpoint();
			redraw();
			break;
		case ID_DISASM_ASSEMBLE:
			assembleOpcode(curAddress,"");
			break;
		case ID_DISASM_COPYINSTRUCTIONDISASM:
			CopyInstructions(selectRangeStart, selectRangeEnd, CopyInstructionsMode::DISASM);
			break;
		case ID_DISASM_COPYADDRESS:
			CopyInstructions(selectRangeStart, selectRangeEnd, CopyInstructionsMode::ADDRESSES);
			break;
		case ID_DISASM_COPYINSTRUCTIONHEX:
			CopyInstructions(selectRangeStart, selectRangeEnd, CopyInstructionsMode::OPCODES);
			break;
		case ID_DISASM_NOPINSTRUCTION:
			NopInstructions(selectRangeStart, selectRangeEnd);
			redraw();
			break;
		case ID_DISASM_EDITSYMBOLS:
			{
				EditSymbolsWindow esw(wnd, debugger);
				if (esw.exec()) {
					esw.eval();
					SendMessage(GetParent(wnd), WM_DEB_MAPLOADED, 0, 0);
					redraw();
				}
			}
			break;
		case ID_DISASM_SETPCTOHERE:
			debugger->setPC(curAddress);
			redraw();
			break;
		case ID_DISASM_FOLLOWBRANCH:
			followBranch();
			break;
		case ID_DISASM_RUNTOHERE:
			{
				SendMessage(GetParent(wnd), WM_COMMAND, ID_DEBUG_RUNTOLINE, 0);
				redraw();
			}
			break;
		case ID_DISASM_RENAMEFUNCTION:
			{
				u32 funcBegin = g_symbolMap->GetFunctionStart(curAddress);
				if (funcBegin != -1)
				{
					char name[256];
					std::string newname;
					truncate_cpy(name, g_symbolMap->GetLabelString(funcBegin).c_str());
					if (InputBox_GetString(MainWindow::GetHInstance(), MainWindow::GetHWND(), L"New function name", name, newname)) {
						g_symbolMap->SetLabelName(newname.c_str(), funcBegin);
						u32 funcSize = g_symbolMap->GetFunctionSize(funcBegin);
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
				u32 funcBegin = g_symbolMap->GetFunctionStart(curAddress);
				if (funcBegin != -1)
				{
					u32 prevBegin = g_symbolMap->GetFunctionStart(funcBegin-1);
					if (prevBegin != -1)
					{
						u32 expandedSize = g_symbolMap->GetFunctionSize(prevBegin) + g_symbolMap->GetFunctionSize(funcBegin);
						g_symbolMap->SetFunctionSize(prevBegin,expandedSize);
					}
					
					g_symbolMap->RemoveFunction(funcBegin,true);
					g_symbolMap->SortSymbols();
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
				u32 prevBegin = g_symbolMap->GetFunctionStart(curAddress);
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
						u32 prevSize = g_symbolMap->GetFunctionSize(prevBegin);
						u32 newSize = curAddress-prevBegin;
						g_symbolMap->SetFunctionSize(prevBegin,newSize);

						newSize = prevSize-newSize;
						snprintf(symname,128,"u_un_%08X",curAddress);
						g_symbolMap->AddFunction(symname,curAddress,newSize);
						g_symbolMap->SortSymbols();
						manager.clear();

						SendMessage(GetParent(wnd), WM_DEB_MAPLOADED, 0, 0);
					}
				}
				else
				{
					char symname[128];
					int newSize = selectRangeEnd - selectRangeStart;
					snprintf(symname, 128, "u_un_%08X", selectRangeStart);
					g_symbolMap->AddFunction(symname, selectRangeStart, newSize);
					g_symbolMap->SortSymbols();

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
		int y = HIWORD(lParam);
		setCurAddress(yToAddress(y), KeyDownAsync(VK_SHIFT));
		redraw();
	}
}	

void CtrlDisAsmView::updateStatusBarText()
{
	auto memLock = Memory::Lock();
	if (!PSP_IsInited())
		return;

	char text[512];
	DisassemblyLineInfo line;
	manager.getLine(curAddress,true,line);
	
	text[0] = 0;
	if (line.type == DISTYPE_OPCODE || line.type == DISTYPE_MACRO)
	{
		if (line.info.hasRelevantAddress && IsLikelyStringAt(line.info.relevantAddress)) {
			snprintf(text, sizeof(text), "[%08X] = \"%s\"", line.info.relevantAddress, Memory::GetCharPointer(line.info.relevantAddress));
		}

		if (line.info.isDataAccess) {
			if (!Memory::IsValidAddress(line.info.dataAddress)) {
				snprintf(text, sizeof(text), "Invalid address %08X",line.info.dataAddress);
			} else {
				bool isFloat = MIPSGetInfo(line.info.encodedOpcode) & (IS_FPU | IS_VFPU);
				switch (line.info.dataSize) {
				case 1:
					snprintf(text, sizeof(text), "[%08X] = %02X",line.info.dataAddress,Memory::Read_U8(line.info.dataAddress));
					break;
				case 2:
					snprintf(text, sizeof(text), "[%08X] = %04X",line.info.dataAddress,Memory::Read_U16(line.info.dataAddress));
					break;
				case 4:
					{
						u32 dataInt = Memory::Read_U32(line.info.dataAddress);
						u32 dataFloat = Memory::Read_Float(line.info.dataAddress);
						std::string dataString;
						if (isFloat)
							dataString = StringFromFormat("%08X / %f", dataInt, dataFloat);
						else
							dataString = StringFromFormat("%08X", dataInt);

						const std::string addressSymbol = g_symbolMap->GetLabelString(dataInt);
						if (!addressSymbol.empty()) {
							snprintf(text, sizeof(text), "[%08X] = %s (%s)", line.info.dataAddress, addressSymbol.c_str(), dataString.c_str());
						} else {
							snprintf(text, sizeof(text), "[%08X] = %s", line.info.dataAddress, dataString.c_str());
						}
						break;
					}
				case 16:
					{
						uint32_t dataInt[4];
						float dataFloat[4];
						for (int i = 0; i < 4; ++i) {
							dataInt[i] = Memory::Read_U32(line.info.dataAddress + i * 4);
							dataFloat[i] = Memory::Read_Float(line.info.dataAddress + i * 4);
						}
						std::string dataIntString = StringFromFormat("%08X,%08X,%08X,%08X", dataInt[0], dataInt[1], dataInt[2], dataInt[3]);
						std::string dataFloatString = StringFromFormat("%f,%f,%f,%f", dataFloat[0], dataFloat[1], dataFloat[2], dataFloat[3]);

						snprintf(text, sizeof(text), "[%08X] = %s / %s", line.info.dataAddress, dataIntString.c_str(), dataFloatString.c_str());
						break;
					}
				}
			}
		}

		if (line.info.isBranch)
		{
			const std::string addressSymbol = g_symbolMap->GetLabelString(line.info.branchTarget);
			if (addressSymbol.empty())
			{
				snprintf(text, sizeof(text), "%08X", line.info.branchTarget);
			} else {
				snprintf(text, sizeof(text), "%08X = %s",line.info.branchTarget,addressSymbol.c_str());
			}
		}
	} else if (line.type == DISTYPE_DATA) {
		u32 start = g_symbolMap->GetDataStart(curAddress);
		if (start == -1)
			start = curAddress;

		u32 diff = curAddress-start;
		const std::string label = g_symbolMap->GetLabelString(start);

		if (!label.empty()) {
			if (diff != 0)
				snprintf(text, sizeof(text), "%08X (%s) + %08X",start,label.c_str(),diff);
			else
				snprintf(text, sizeof(text), "%08X (%s)",start,label.c_str());
		} else {
			if (diff != 0)
				snprintf(text, sizeof(text), "%08X + %08X",start,diff);
			else
				snprintf(text, sizeof(text), "%08X",start);
		}
	}

	SendMessage(GetParent(wnd),WM_DEB_SETSTATUSBARTEXT,0,(LPARAM)text);

	const std::string label = g_symbolMap->GetLabelString(line.info.opcodeAddress);
	if (!label.empty()) {
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
	auto memLock = Memory::Lock();
	u32 searchAddress;

	if (continueSearch == false || searchQuery[0] == 0)
	{
		if (InputBox_GetString(MainWindow::GetHInstance(), MainWindow::GetHWND(), L"Search for:", searchQuery, searchQuery) == false
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
	auto memLock = Memory::Lock();
	std::string result;

	// gather all branch targets without labels
	std::set<u32> branchAddresses;
	for (u32 i = 0; i < size; i += debugger->getInstructionSize(0))
	{
		MIPSAnalyst::MipsOpcodeInfo info = MIPSAnalyst::GetOpcodeInfo(debugger,start+i);

		if (info.isBranch && g_symbolMap->GetLabelString(info.branchTarget).empty())
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
			&& g_symbolMap->GetLabelString(line.info.branchTarget).empty()
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

void CtrlDisAsmView::disassembleToFile() {
	// get size
	u32 size;
	if (executeExpressionWindow(wnd,debugger,size) == false)
		return;
	if (size == 0 || size > 10*1024*1024) {
		MessageBox(wnd,L"Invalid size!",L"Error",MB_OK);
		return;
	}

	std::string filename;
	if (W32Util::BrowseForFileName(false, nullptr, L"Save Disassembly As...", nullptr, L"All Files\0*.*\0\0", nullptr, filename)) {
		std::wstring fileName = ConvertUTF8ToWString(filename);
		FILE *output = _wfopen(fileName.c_str(), L"wb");
		if (output == nullptr) {
			MessageBox(wnd, L"Could not open file!", L"Error", MB_OK);
			return;
		}

		std::string disassembly = disassembleRange(curAddress, size);
		fprintf(output, "%s", disassembly.c_str());

		fclose(output);
		MessageBox(wnd, L"Finished!", L"Done", MB_OK);
	}
}

void CtrlDisAsmView::getOpcodeText(u32 address, char* dest, int bufsize)
{
	DisassemblyLineInfo line;
	address = manager.getStartAddress(address);
	manager.getLine(address,displaySymbols,line);
	snprintf(dest, bufsize, "%s  %s",line.name.c_str(),line.params.c_str());
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
