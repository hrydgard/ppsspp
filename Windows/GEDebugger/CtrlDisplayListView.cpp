#include <algorithm>
#include "Common/Data/Encoding/Utf8.h"
#include "Common/StringUtils.h"
#include "Common/System/Display.h"
#include "Windows/GEDebugger/CtrlDisplayListView.h"
#include "Windows/GEDebugger/GEDebugger.h"
#include "Windows/MainWindow.h"
#include "Windows/InputBox.h"
#include "Windows/W32Util/ContextMenu.h"
#include "Windows/main.h"
#include "Core/Config.h"
#include "GPU/Debugger/Breakpoints.h"
#include "GPU/GPUState.h"

constexpr const wchar_t *szWindowClass = L"CtrlDisplayListView";

void CtrlDisplayListView::registerClass()
{
	WNDCLASSEX wndClass;

	wndClass.cbSize         = sizeof(wndClass);
	wndClass.lpszClassName  = szWindowClass;
	wndClass.hInstance      = GetModuleHandle(0);
	wndClass.lpfnWndProc    = wndProc;
	wndClass.hCursor        = LoadCursor (NULL, IDC_ARROW);
	wndClass.hIcon          = 0;
	wndClass.lpszMenuName   = 0;
	wndClass.hbrBackground  = (HBRUSH)GetSysColorBrush(COLOR_WINDOW);
	wndClass.style          = 0;
	wndClass.cbClsExtra     = 0;
	wndClass.cbWndExtra     = sizeof(CtrlDisplayListView*);
	wndClass.hIconSm        = 0;

	RegisterClassEx(&wndClass);
}

CtrlDisplayListView::CtrlDisplayListView(HWND _wnd)
	: wnd(_wnd)
{
	SetWindowLongPtr(wnd, GWLP_USERDATA, (LONG_PTR) this);
	SetWindowLong(wnd, GWL_STYLE, GetWindowLong(wnd,GWL_STYLE) | WS_VSCROLL);
	SetScrollRange(wnd, SB_VERT, -1,1,TRUE);
	
	instructionSize = 4;

	// In small window mode, g_dpi_scale may have been adjusted.
	const float fontScale = 1.0f / g_display.dpi_scale_real_y;
	int fontHeight = g_Config.iFontHeight * fontScale;
	int charWidth = g_Config.iFontWidth * fontScale;

	rowHeight = fontHeight + 2;

	font = CreateFont(fontHeight,charWidth,0,0,FW_DONTCARE,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,DEFAULT_QUALITY,DEFAULT_PITCH,
		L"Lucida Console");
	boldfont = CreateFont(fontHeight,charWidth,0,0,FW_DEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,DEFAULT_QUALITY,DEFAULT_PITCH,
		L"Lucida Console");

	pixelPositions.addressStart = 16;
	pixelPositions.opcodeStart = pixelPositions.addressStart + 19*charWidth;

	hasFocus = false;
	validDisplayList = false;
}

CtrlDisplayListView::~CtrlDisplayListView() {
	DeleteObject(font);
	DeleteObject(boldfont);
}

CtrlDisplayListView *CtrlDisplayListView::getFrom(HWND hwnd)
{
	return (CtrlDisplayListView*) GetWindowLongPtr(hwnd, GWLP_USERDATA);
}

LRESULT CALLBACK CtrlDisplayListView::wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	CtrlDisplayListView *win = CtrlDisplayListView::getFrom(hwnd);

	switch(msg) {
	case WM_NCCREATE:
		// Allocate a new CustCtrl structure for this window.
		win = new CtrlDisplayListView(hwnd);
		
		// Continue with window creation.
		return win != NULL;
	case WM_NCDESTROY:
		SetWindowLongPtr(hwnd, GWLP_USERDATA, 0);
		delete win;
		break;
	case WM_SIZE:
		win->redraw();
		break;
	case WM_PAINT:
		win->onPaint(wParam,lParam);
		break;
	case WM_SETFOCUS:
		SetFocus(hwnd);
		win->hasFocus=true;
		win->redraw();
		break;
	case WM_KILLFOCUS:
		win->hasFocus=false;
		win->redraw();
		break;
	case WM_VSCROLL:
		win->onVScroll(wParam,lParam);
		break;
	case WM_MOUSEWHEEL:
		if (GET_WHEEL_DELTA_WPARAM(wParam) > 0)
		{
			win->scrollWindow(-3);
		} else if (GET_WHEEL_DELTA_WPARAM(wParam) < 0) {
			win->scrollWindow(3);
		}
		break;
	case WM_LBUTTONDOWN:
		win->onMouseDown(wParam,lParam,1);
		break;
	case WM_RBUTTONDOWN:
		win->onMouseDown(wParam,lParam,2);
		break;
	case WM_LBUTTONUP:
		win->onMouseUp(wParam,lParam,1);
		break;
	case WM_RBUTTONUP:
		win->onMouseUp(wParam,lParam,2);
		break;
	case WM_KEYDOWN:
	case WM_SYSKEYDOWN:
		win->onKeyDown(wParam,lParam);
		return 0;
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
	}
	
	return DefWindowProc(hwnd, msg, wParam, lParam);
}

void CtrlDisplayListView::redraw()
{
	GetClientRect(wnd, &rect);
	visibleRows = rect.bottom/rowHeight;

	RedrawWindow(wnd, NULL, NULL, RDW_ERASE | RDW_FRAME | RDW_INVALIDATE | RDW_INTERNALPAINT | RDW_ALLCHILDREN);
}


void CtrlDisplayListView::onPaint(WPARAM wParam, LPARAM lParam)
{
	if (!validDisplayList || !gpuDebug)
		return;

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
	
	HICON breakPoint = (HICON)LoadIcon(GetModuleHandle(0),(LPCWSTR)IDI_STOP);

	auto disasm = gpuDebug->DisassembleOpRange(windowStart, windowStart + (visibleRows + 2) * instructionSize);

	for (int i = 0; i < visibleRows+2; i++)
	{
		unsigned int address=windowStart + i*instructionSize;
		bool stall = address == list.stall;

		int rowY1 = rowHeight*i;

		// draw background
		COLORREF backgroundColor = stall ? 0xCCCCFF : 0xFFFFFF;
		COLORREF textColor = 0x000000;
		
		if (address >= selectRangeStart && address < selectRangeEnd)
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
		if (gpuDebug->GetBreakpoints()->IsAddressBreakpoint(address))
		{
			textColor = 0x0000FF;
			int yOffset = std::max(-1,(rowHeight-14+1)/2);
			DrawIconEx(hdc,2,rowY1+1+yOffset,breakPoint,32,32,0,0,DI_NORMAL);
		}
		SetTextColor(hdc,textColor);

		GPUDebugOp op = i < (int)disasm.size() ? disasm[i] : GPUDebugOp();

		char addressText[64];
		snprintf(addressText,sizeof(addressText),"%08X %08X",op.pc,op.op);
		TextOutA(hdc,pixelPositions.addressStart,rowY1+2,addressText,(int)strlen(addressText));

		if (address == list.pc)
		{
			TextOut(hdc,pixelPositions.opcodeStart-8,rowY1,L"\x25A0",1);
		}

		const char* opcode = op.desc.c_str();
		SelectObject(hdc,stall ? boldfont : font);
		TextOutA(hdc,pixelPositions.opcodeStart,rowY1+2,opcode,(int)strlen(opcode));
		SelectObject(hdc,font);
	}

	SelectObject(hdc,oldFont);
	SelectObject(hdc,oldPen);
	SelectObject(hdc,oldBrush);

	// copy bitmap to the actual hdc
	BitBlt(actualHdc, 0, 0, rect.right, rect.bottom, hdc, 0, 0, SRCCOPY);
	DeleteObject(hBM);
	DeleteDC(hdc);

	DeleteObject(nullPen);
	DeleteObject(condPen);

	DeleteObject(nullBrush);
	DeleteObject(currentBrush);
	
	DestroyIcon(breakPoint);

	EndPaint(wnd, &ps);
}

void CtrlDisplayListView::toggleBreakpoint()
{
	SendMessage(GetParent(wnd),WM_GEDBG_TOGGLEPCBREAKPOINT,curAddress,0);
}

void CtrlDisplayListView::PromptBreakpointCond() {
	std::string expression;
	gpuDebug->GetBreakpoints()->GetAddressBreakpointCond(curAddress, &expression);
	if (!InputBox_GetString(GetModuleHandle(NULL), wnd, L"Expression", expression, expression))
		return;

	std::string error;
	if (!gpuDebug->GetBreakpoints()->SetAddressBreakpointCond(curAddress, expression, &error))
		MessageBox(wnd, ConvertUTF8ToWString(error).c_str(), L"Invalid expression", MB_OK | MB_ICONEXCLAMATION);
}

void CtrlDisplayListView::onMouseDown(WPARAM wParam, LPARAM lParam, int button)
{
	if (!validDisplayList || !gpuDebug) {
		return;
	}

	int y = HIWORD(lParam);

	int line = y/rowHeight;
	u32 newAddress = windowStart + line*instructionSize;
	
	bool extend = KeyDownAsync(VK_SHIFT);
	if (button == 1)
	{
		if (newAddress == curAddress && hasFocus)
		{
			toggleBreakpoint();
		}
	} else if (button == 2)
	{
		// Maintain the current selection if right clicking into it.
		if (newAddress >= selectRangeStart && newAddress < selectRangeEnd)
			extend = true;
	}

	setCurAddress(newAddress,extend);

	SetFocus(wnd);
	redraw();
}

void CtrlDisplayListView::onMouseUp(WPARAM wParam, LPARAM lParam, int button)
{
	if (!validDisplayList || !gpuDebug) {
		return;
	}

	if (button == 2)
	{
		HMENU menu = GetContextMenu(ContextMenuID::DISPLAYLISTVIEW);
		EnableMenuItem(menu, ID_GEDBG_SETCOND, gpuDebug->GetBreakpoints()->IsAddressBreakpoint(curAddress) ? MF_ENABLED : MF_GRAYED);

		switch (TriggerContextMenu(ContextMenuID::DISPLAYLISTVIEW, wnd, ContextPoint::FromEvent(lParam)))
		{
		case ID_DISASM_GOTOINMEMORYVIEW:
			if (memoryWindow)
				memoryWindow->Goto(curAddress);
			break;
		case ID_DISASM_TOGGLEBREAKPOINT:
			toggleBreakpoint();
			redraw();
			break;
		case ID_GEDBG_SETCOND:
			PromptBreakpointCond();
			break;
		case ID_DISASM_COPYINSTRUCTIONDISASM:
			{
				int space = 256 * (selectRangeEnd - selectRangeStart) / instructionSize;
				char *temp = new char[space];

				char *p = temp, *end = temp + space;
				for (u32 pos = selectRangeStart; pos < selectRangeEnd && p < end; pos += instructionSize)
				{
					u32 opcode = Memory::Read_U32(pos);
					GPUDebugOp op = gpuDebug->DisassembleOp(pos, opcode);
					p += snprintf(p, end - p, "%s\r\n", op.desc.c_str());
				}

				W32Util::CopyTextToClipboard(wnd, temp);
				delete [] temp;
			}
			break;
		case ID_DISASM_COPYADDRESS:
			{
				char temp[16];
				snprintf(temp,sizeof(temp),"%08X",curAddress);
				W32Util::CopyTextToClipboard(wnd, temp);
			}
			break;
		case ID_DISASM_SETPCTOHERE:
			{
				gpuDebug->ResetListPC(list.id,curAddress);
				list.pc = curAddress;
				redraw();
			}
			break;
		case ID_GEDBG_SETSTALLADDR:
			{
				gpuDebug->ResetListStall(list.id,curAddress);
				list.stall = curAddress;
				redraw();
			}
			break;
		case ID_DISASM_COPYINSTRUCTIONHEX:
			{
				int space = 24 * (selectRangeEnd - selectRangeStart) / instructionSize;
				char *temp = new char[space];

				char *p = temp, *end = temp + space;
				for (u32 pos = selectRangeStart; pos < selectRangeEnd && p < end; pos += instructionSize)
					p += snprintf(p, end - p, "%08X\r\n", Memory::ReadUnchecked_U32(pos));

				W32Util::CopyTextToClipboard(wnd, temp);
				delete [] temp;
			}
			break;
		case ID_DISASM_RUNTOHERE:
			{
				SendMessage(GetParent(wnd),WM_GEDBG_RUNTOWPARAM,curAddress,0);
				redraw();
			}
			break;
		case ID_GEDBG_GOTOPC:
			setCurAddress(list.pc);
			scrollAddressIntoView();
			redraw();
			break;
		case ID_GEDBG_GOTOADDR:
			{
				std::string expression = StringFromFormat("%08x", curAddress);
				if (!InputBox_GetString(GetModuleHandle(NULL), wnd, L"Address", expression, expression, InputBoxFlags::Selected)) {
					break;
				}
				uint32_t newAddress = curAddress;
				if (!GPUDebugExecExpression(gpuDebug, expression.c_str(), newAddress)) {
					MessageBox(wnd, ConvertUTF8ToWString(getExpressionError()).c_str(), L"Invalid expression", MB_OK | MB_ICONEXCLAMATION);
					break;
				}
				if (!Memory::IsValidAddress(newAddress)) {
					MessageBox(wnd, L"Address not in valid memory", L"Invalid address", MB_OK | MB_ICONEXCLAMATION);
					break;
				}

				setCurAddress(newAddress);
				scrollAddressIntoView();
				redraw();
			}
			break;
		}
		return;
	}

	redraw();
}

void CtrlDisplayListView::onVScroll(WPARAM wParam, LPARAM lParam)
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

void CtrlDisplayListView::onKeyDown(WPARAM wParam, LPARAM lParam)
{
	u32 windowEnd = windowStart+visibleRows*instructionSize;

	switch (wParam & 0xFFFF)
	{
	case VK_DOWN:
		setCurAddress(curAddress + instructionSize, KeyDownAsync(VK_SHIFT));
		scrollAddressIntoView();
		break;
	case VK_UP:
		setCurAddress(curAddress - instructionSize, KeyDownAsync(VK_SHIFT));
		scrollAddressIntoView();
		break;
	case VK_NEXT:
		if (curAddress != windowEnd - instructionSize && curAddressIsVisible()) {
			setCurAddress(windowEnd - instructionSize, KeyDownAsync(VK_SHIFT));
			scrollAddressIntoView();
		} else {
			setCurAddress(curAddress + visibleRows * instructionSize, KeyDownAsync(VK_SHIFT));
			scrollAddressIntoView();
		}
		break;
	case VK_PRIOR:
		if (curAddress != windowStart && curAddressIsVisible()) {
			setCurAddress(windowStart, KeyDownAsync(VK_SHIFT));
			scrollAddressIntoView();
		} else {
			setCurAddress(curAddress - visibleRows * instructionSize, KeyDownAsync(VK_SHIFT));
			scrollAddressIntoView();
		}
		break;
	case VK_LEFT:
		gotoAddr(list.pc);
		return;
	case VK_SPACE:
		toggleBreakpoint();
		break;
	case VK_F10:
	case VK_F11:
		SendMessage(GetParent(wnd),WM_GEDBG_STEPDISPLAYLIST,0,0);
		break;
	}
	redraw();
}

void CtrlDisplayListView::scrollAddressIntoView()
{
	u32 windowEnd = windowStart + visibleRows * instructionSize;

	if (curAddress < windowStart)
		windowStart = curAddress;
	else if (curAddress >= windowEnd)
		windowStart = curAddress - visibleRows * instructionSize + instructionSize;
}

bool CtrlDisplayListView::curAddressIsVisible()
{
	u32 windowEnd = windowStart + visibleRows * instructionSize;
	return curAddress >= windowStart && curAddress < windowEnd;
}
