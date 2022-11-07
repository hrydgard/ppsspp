// NOTE: Apologies for the quality of this code, this is really from pre-opensource Dolphin - that is, 2003.

#include <cctype>
#include <tchar.h>
#include <math.h>
#include <iomanip>
#include "ext/xxhash.h"
#include "Core/Config.h"
#include "Core/MemMap.h"
#include "Core/Reporting.h"
#include "Windows/W32Util/ContextMenu.h"
#include "Windows/W32Util/Misc.h"
#include "Windows/InputBox.h"
#include "Windows/main.h"
#include "Windows/resource.h"
#include "Common/System/Display.h"

#include "Debugger_Disasm.h"
#include "DebuggerShared.h"
#include "CtrlMemView.h"
#include "DumpMemoryWindow.h"

wchar_t CtrlMemView::szClassName[] = L"CtrlMemView";

static constexpr UINT_PTR IDT_REDRAW_DELAYED = 0xC0DE0001;
static constexpr UINT REDRAW_DELAY = 1000 / 60;
// We also redraw regularly, since data changes during runtime.
static constexpr UINT_PTR IDT_REDRAW_AUTO = 0xC0DE0002;
static constexpr UINT REDRAW_INTERVAL = 1000;

CtrlMemView::CtrlMemView(HWND _wnd)
{
	wnd=_wnd;
	SetWindowLongPtr(wnd, GWLP_USERDATA, (LONG_PTR)this);
	SetWindowLong(wnd, GWL_STYLE, GetWindowLong(wnd,GWL_STYLE) | WS_VSCROLL);
	SetScrollRange(wnd, SB_VERT, -1,1,TRUE);

	const float fontScale = 1.0f / g_dpi_scale_real_y;
	rowHeight = g_Config.iFontHeight * fontScale;
	charWidth = g_Config.iFontWidth * fontScale;
	offsetPositionY = offsetLine * rowHeight;

	font = CreateFont(rowHeight, charWidth, 0, 0,
		FW_DONTCARE, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH,
		L"Lucida Console");
	underlineFont = CreateFont(rowHeight, charWidth, 0, 0,
		FW_DONTCARE, FALSE, TRUE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH,
		L"Lucida Console");
	curAddress = 0;
	debugger = 0;
 
	searchQuery.clear();
	matchAddress = -1;
	searching = false;

	hasFocus = false;
	windowStart = curAddress;
	asciiSelected = false;

	selectedNibble = 0;
	rowSize = 16;
	addressStart = charWidth;
	hexStart = addressStart + 9*charWidth;
	asciiStart = hexStart + (rowSize*3+1)*charWidth;

	// set redraw timer
	SetTimer(wnd, IDT_REDRAW_AUTO, REDRAW_INTERVAL, nullptr);
}

CtrlMemView::~CtrlMemView()
{
	DeleteObject(font);
	DeleteObject(underlineFont);
}

void CtrlMemView::init()
{
    WNDCLASSEX wc;
    
    wc.cbSize         = sizeof(wc);
    wc.lpszClassName  = szClassName;
    wc.hInstance      = GetModuleHandle(0);
    wc.lpfnWndProc    = CtrlMemView::wndProc;
    wc.hCursor        = LoadCursor (NULL, IDC_ARROW);
    wc.hIcon          = 0;
    wc.lpszMenuName   = 0;
    wc.hbrBackground  = (HBRUSH)GetSysColorBrush(COLOR_WINDOW);
    wc.style          = 0;
    wc.cbClsExtra     = 0;
	wc.cbWndExtra     = sizeof( CtrlMemView * );
    wc.hIconSm        = 0;
	
	
    RegisterClassEx(&wc);
}

void CtrlMemView::deinit()
{
	//UnregisterClass(szClassName, hInst)
}


LRESULT CALLBACK CtrlMemView::wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	CtrlMemView *ccp = CtrlMemView::getFrom(hwnd);
	static bool lmbDown=false,rmbDown=false;

    switch(msg)
    {
    case WM_NCCREATE:
        // Allocate a new CustCtrl structure for this window.
        ccp = new CtrlMemView(hwnd);
		
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
	case WM_CHAR:
		ccp->onChar(wParam,lParam);
		return 0;
	case WM_KEYUP:
		return 0;
	case WM_LBUTTONDOWN: SetFocus(hwnd); lmbDown=true; ccp->onMouseDown(wParam,lParam,1); break;
	case WM_RBUTTONDOWN: SetFocus(hwnd); rmbDown=true; ccp->onMouseDown(wParam,lParam,2); break;
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
	case WM_GETDLGCODE:	// we want to process the arrow keys and all characters ourselves
		return DLGC_WANTARROWS|DLGC_WANTCHARS|DLGC_WANTTAB;
		break;
	case WM_TIMER:
		// This is actually delayed too, using another timer.  That way we won't update twice.
		if (wParam == IDT_REDRAW_AUTO && IsWindowVisible(ccp->wnd))
			ccp->redraw();

		if (wParam == IDT_REDRAW_DELAYED) {
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


CtrlMemView *CtrlMemView::getFrom(HWND hwnd)
{
	return (CtrlMemView *)GetWindowLongPtr(hwnd, GWLP_USERDATA);
}


void CtrlMemView::onPaint(WPARAM wParam, LPARAM lParam) {
	auto memLock = Memory::Lock();

	// draw to a bitmap for double buffering
	PAINTSTRUCT ps;	
	HDC actualHdc = BeginPaint(wnd, &ps);
	HDC hdc = CreateCompatibleDC(actualHdc);
	HBITMAP hBM = CreateCompatibleBitmap(actualHdc, rect.right-rect.left, rect.bottom-rect.top);
	SelectObject(hdc, hBM);

	SetBkMode(hdc,OPAQUE);
	HPEN standardPen = CreatePen(0,0,0xFFFFFF);
	HBRUSH standardBrush = CreateSolidBrush(0xFFFFFF);
	COLORREF standardBG = GetBkColor(hdc);

	HPEN oldPen = (HPEN) SelectObject(hdc,standardPen);
	HBRUSH oldBrush = (HBRUSH) SelectObject(hdc,standardBrush);
   	HFONT oldFont = (HFONT) SelectObject(hdc,(HGDIOBJ)font);

	// white background
	SelectObject(hdc,standardPen);
	SelectObject(hdc,standardBrush);
	Rectangle(hdc,0,0,rect.right,rect.bottom);

	if (displayOffsetScale) 
		drawOffsetScale(hdc);

	std::vector<MemBlockInfo> memRangeInfo = FindMemInfoByFlag(highlightFlags_, windowStart, (visibleRows + 1) * rowSize);

	COLORREF lastTextCol = 0x000000;
	COLORREF lastBGCol = standardBG;
	auto setTextColors = [&](COLORREF fg, COLORREF bg) {
		if (lastTextCol != fg) {
			SetTextColor(hdc, fg);
			lastTextCol = fg;
		}
		if (lastBGCol != bg) {
			SetBkColor(hdc, bg);
			lastBGCol = bg;
		}
	};

	// draw one extra row that may be partially visible
	for (int i = 0; i < visibleRows + 1; i++) {
		int rowY = rowHeight * i;
		// Skip the first X rows to make space for the offsets.
		if (displayOffsetScale) 
			rowY += rowHeight * offsetSpace;

		char temp[32];
		uint32_t address = windowStart + i * rowSize;
		sprintf(temp, "%08X", address);

		setTextColors(0x600000, standardBG);
		TextOutA(hdc, addressStart, rowY, temp, (int)strlen(temp));

		union {
			uint32_t words[4];
			uint8_t bytes[16];
		} memory;
		bool valid = debugger != nullptr && debugger->isAlive() && Memory::IsValidAddress(address);
		for (int i = 0; valid && i < 4; ++i) {
			memory.words[i] = debugger->readMemory(address + i * 4);
		}

		for (int j = 0; j < rowSize; j++) {
			const uint32_t byteAddress = (address + j) & ~0xC0000000;
			std::string tag;
			bool tagContinues = false;
			for (auto info : memRangeInfo) {
				if (info.start <= byteAddress && info.start + info.size > byteAddress) {
					tag = info.tag;
					tagContinues = byteAddress + 1 < info.start + info.size;
				}
			}

			int hexX = hexStart + j * 3 * charWidth;
			int hexLen = 2;
			int asciiX = asciiStart + j * (charWidth + 2);

			char c;
			if (valid) {
				sprintf(temp, "%02X ", memory.bytes[j]);
				c = (char)memory.bytes[j];
				if (memory.bytes[j] < 32 || memory.bytes[j] >= 128)
					c = '.';
			} else {
				strcpy(temp, "??");
				c = '.';
			}

			COLORREF hexBGCol = standardBG;
			COLORREF hexTextCol = 0x000000;
			COLORREF continueBGCol = standardBG;
			COLORREF asciiBGCol = standardBG;
			COLORREF asciiTextCol = 0x000000;
			int underline = -1;

			if (address + j == curAddress && searching == false) {
				if (asciiSelected) {
					hexBGCol = 0xC0C0C0;
					hexTextCol = 0x000000;
					asciiBGCol = hasFocus ? 0xFF9933 : 0xC0C0C0;
					asciiTextCol = hasFocus ? 0xFFFFFF : 0x000000;
				} else {
					hexBGCol = hasFocus ? 0xFF9933 : 0xC0C0C0;
					hexTextCol = hasFocus ? 0xFFFFFF : 0x000000;
					asciiBGCol = 0xC0C0C0;
					asciiTextCol = 0x000000;
					underline = selectedNibble;
				}
				if (!tag.empty() && tagContinues) {
					continueBGCol = pickTagColor(tag);
				}
			} else if (!tag.empty()) {
				hexBGCol = pickTagColor(tag);
				continueBGCol = hexBGCol;
				asciiBGCol = pickTagColor(tag);
				hexLen = tagContinues ? 3 : 2;
			}

			setTextColors(hexTextCol, hexBGCol);
			if (underline >= 0) {
				SelectObject(hdc, underline == 0 ? (HGDIOBJ)underlineFont : (HGDIOBJ)font);
				TextOutA(hdc, hexX, rowY, &temp[0], 1);
				SelectObject(hdc, underline == 0 ? (HGDIOBJ)font : (HGDIOBJ)underlineFont);
				TextOutA(hdc, hexX + charWidth, rowY, &temp[1], 1);
				SelectObject(hdc, (HGDIOBJ)font);

				// If the tag keeps going, draw the BG too.
				if (continueBGCol != standardBG) {
					setTextColors(0x000000, continueBGCol);
					TextOutA(hdc, hexX + charWidth * 2, rowY, &temp[2], 1);
				}
			} else {
				TextOutA(hdc, hexX, rowY, temp, hexLen);
			}

			setTextColors(asciiTextCol, asciiBGCol);
			TextOutA(hdc, asciiX, rowY, &c, 1);
		}
	}

	setTextColors(0x000000, standardBG);
	SelectObject(hdc,oldFont);
	SelectObject(hdc,oldPen);
	SelectObject(hdc,oldBrush);
	
	// copy bitmap to the actual hdc
	BitBlt(actualHdc,0,0,rect.right,rect.bottom,hdc,0,0,SRCCOPY);
	DeleteObject(hBM);
	DeleteDC(hdc);

	DeleteObject(standardPen);
	DeleteObject(standardBrush);
	
	EndPaint(wnd, &ps);
}

void CtrlMemView::onVScroll(WPARAM wParam, LPARAM lParam)
{
	switch (wParam & 0xFFFF)
	{
	case SB_LINEDOWN:
		scrollWindow(1);
		break;
	case SB_LINEUP:
		scrollWindow(-1);
		break;
	case SB_PAGEDOWN:
		scrollWindow(visibleRows);
		break;
	case SB_PAGEUP:
		scrollWindow(-visibleRows);
		break;
	default:
		return;
	}
}

void CtrlMemView::onKeyDown(WPARAM wParam, LPARAM lParam)
{
	if (KeyDownAsync(VK_CONTROL))
	{	
		switch (tolower(wParam & 0xFFFF))
		{
		case 'g':
			{
				u32 addr;
				if (executeExpressionWindow(wnd,debugger,addr) == false) return;
				gotoAddr(addr);
				return;
			}
			break;
		case 'f':
		case 's':
			search(false);
			return;
		case 'c':
			search(true);
			return;
		}
	}

	switch (wParam & 0xFFFF)
	{
	case VK_DOWN:
		scrollCursor(rowSize);
		break;
	case VK_UP:
		scrollCursor(-rowSize);
		break;
	case VK_LEFT:
		scrollCursor(-1);
		break;
	case VK_RIGHT:
		scrollCursor(1);
		break;
	case VK_NEXT:
		scrollWindow(visibleRows);
		break;
	case VK_PRIOR:
		scrollWindow(-visibleRows);
		break;
	case VK_TAB:
		SendMessage(GetParent(wnd),WM_DEB_TABPRESSED,0,0);
		break;
	default:
		return;
	}
}

void CtrlMemView::onChar(WPARAM wParam, LPARAM lParam)
{
	auto memLock = Memory::Lock();
	if (!PSP_IsInited())
		return;

	if (KeyDownAsync(VK_CONTROL) || wParam == VK_TAB) return;

	if (!Memory::IsValidAddress(curAddress))
	{
		scrollCursor(1);
		return;
	}

	bool active = Core_IsActive();
	if (active) Core_EnableStepping(true, "memory.access", curAddress);

	if (asciiSelected)
	{
		u8 newValue = wParam;
		Memory::WriteUnchecked_U8(newValue,curAddress);
		scrollCursor(1);
	} else {
		wParam = tolower(wParam);
		int inputValue = -1;

		if (wParam >= '0' && wParam <= '9') inputValue = wParam - '0';
		if (wParam >= 'a' && wParam <= 'f') inputValue = wParam -'a' + 10;

		if (inputValue >= 0)
		{
			int shiftAmount = (1-selectedNibble)*4;

			u8 oldValue = Memory::ReadUnchecked_U8(curAddress);
			oldValue &= ~(0xF << shiftAmount);
			u8 newValue = oldValue | (inputValue << shiftAmount);
			Memory::WriteUnchecked_U8(newValue,curAddress);
			scrollCursor(1);
		}
	}

	Reporting::NotifyDebugger();
	if (active) Core_EnableStepping(false);
}

void CtrlMemView::redraw()
{
	GetClientRect(wnd, &rect);
	visibleRows = (rect.bottom/rowHeight);

	if (displayOffsetScale) {
		visibleRows -= offsetSpace; // visibleRows is calculated based on the size of the control, but X rows have already been used for the offsets and are no longer usable
	}

	if (!redrawScheduled_) {
		SetTimer(wnd, IDT_REDRAW_DELAYED, REDRAW_DELAY, nullptr);
		redrawScheduled_ = true;
	}
}

void CtrlMemView::onMouseDown(WPARAM wParam, LPARAM lParam, int button)
{	
	int x = LOWORD(lParam); 
	int y = HIWORD(lParam);

	gotoPoint(x,y);
}

void CtrlMemView::onMouseUp(WPARAM wParam, LPARAM lParam, int button)
{
	if (button==2)
	{
		bool enable16 = !asciiSelected && (curAddress % 2) == 0;
		bool enable32 = !asciiSelected && (curAddress % 4) == 0;

		HMENU menu = GetContextMenu(ContextMenuID::MEMVIEW);
		EnableMenuItem(menu,ID_MEMVIEW_COPYVALUE_16,enable16 ? MF_ENABLED : MF_GRAYED);
		EnableMenuItem(menu,ID_MEMVIEW_COPYVALUE_32,enable32 ? MF_ENABLED : MF_GRAYED);

		switch (TriggerContextMenu(ContextMenuID::MEMVIEW, wnd, ContextPoint::FromEvent(lParam)))
		{
		case ID_MEMVIEW_DUMP:
			{
				DumpMemoryWindow dump(wnd, debugger);
				dump.exec();
				break;
			}
			
		case ID_MEMVIEW_COPYVALUE_8:
			{
				auto memLock = Memory::Lock();
				char temp[24];

				// it's admittedly not really useful like this
				if (asciiSelected)
				{
					unsigned char c = Memory::IsValidAddress(curAddress) ? Memory::Read_U8(curAddress) : '.';
					if (c < 32|| c >= 128) c = '.';
					sprintf(temp,"%c",c);
				} else {
					sprintf(temp,"%02X",Memory::IsValidAddress(curAddress) ? Memory::Read_U8(curAddress) : 0xFF);
				}
				W32Util::CopyTextToClipboard(wnd,temp);
			}
			break;
			
		case ID_MEMVIEW_COPYVALUE_16:
			{
				auto memLock = Memory::Lock();
				char temp[24];

				sprintf(temp,"%04X",Memory::IsValidAddress(curAddress) ? Memory::Read_U16(curAddress) : 0xFFFF);
				W32Util::CopyTextToClipboard(wnd,temp);
			}
			break;
			
		case ID_MEMVIEW_COPYVALUE_32:
			{
				auto memLock = Memory::Lock();
				char temp[24];

				sprintf(temp,"%08X",Memory::IsValidAddress(curAddress) ? Memory::Read_U32(curAddress) : 0xFFFFFFFF);
				W32Util::CopyTextToClipboard(wnd,temp);
			}
			break;

		case ID_MEMVIEW_EXTENTBEGIN:
		{
			std::vector<MemBlockInfo> memRangeInfo = FindMemInfoByFlag(highlightFlags_, curAddress, 1);
			uint32_t addr = curAddress;
			for (MemBlockInfo info : memRangeInfo) {
				addr = info.start;
			}
			gotoAddr(addr);
			break;
		}

		case ID_MEMVIEW_EXTENTEND:
		{
			std::vector<MemBlockInfo> memRangeInfo = FindMemInfoByFlag(highlightFlags_, curAddress, 1);
			uint32_t addr = curAddress;
			for (MemBlockInfo info : memRangeInfo) {
				addr = info.start + info.size - 1;
			}
			gotoAddr(addr);
			break;
		}

		case ID_MEMVIEW_COPYADDRESS:
			{
				char temp[24];
				sprintf(temp,"0x%08X",curAddress);
				W32Util::CopyTextToClipboard(wnd,temp);
			}
			break;

		case ID_MEMVIEW_GOTOINDISASM:
			if (disasmWindow) {
				disasmWindow->Goto(curAddress);
				disasmWindow->Show(true);
			}
			break;
		}
		return;
	}

	int x = LOWORD(lParam); 
	int y = HIWORD(lParam);
	ReleaseCapture();
	gotoPoint(x,y);
}

void CtrlMemView::onMouseMove(WPARAM wParam, LPARAM lParam, int button)
{

}	

void CtrlMemView::updateStatusBarText() {
	std::vector<MemBlockInfo> memRangeInfo = FindMemInfoByFlag(highlightFlags_, curAddress, 1);

	char text[512];
	snprintf(text, sizeof(text), "%08X", curAddress);
	// There should only be one.
	for (MemBlockInfo info : memRangeInfo) {
		snprintf(text, sizeof(text), "%08X - %s %08X-%08X (at PC %08X / %lld ticks)", curAddress, info.tag.c_str(), info.start, info.start + info.size, info.pc, info.ticks);
	}

	SendMessage(GetParent(wnd), WM_DEB_SETSTATUSBARTEXT, 0, (LPARAM)text);
}

void CtrlMemView::gotoPoint(int x, int y)
{
	int line = y/rowHeight;
	int lineAddress = windowStart+line*rowSize;

	if (displayOffsetScale)
	{
		if (line < offsetSpace) // ignore clicks on the offset space
		{
			updateStatusBarText();
			redraw();
			return;
		}
		lineAddress -= (rowSize * offsetSpace); // since each row has been written X rows down from where the window expected it to be written the target of the clicks must be adjusted
	}

	if (x >= asciiStart)
	{
		int col = (x-asciiStart) / (charWidth+2);
		if (col >= rowSize) return;
		
		asciiSelected = true;
		curAddress = lineAddress+col;
		selectedNibble = 0;
		updateStatusBarText();
		redraw();
	} else if (x >= hexStart)
	{
		int col = (x-hexStart) / charWidth;
		if ((col/3) >= rowSize) return;

		switch (col % 3)
		{
		case 0: selectedNibble = 0; break;
		case 1: selectedNibble = 1; break;
		case 2: return;		// don't change position when clicking on the space
		}

		asciiSelected = false;
		curAddress = lineAddress+col/3;
		updateStatusBarText();
		redraw();
	}
}

void CtrlMemView::gotoAddr(unsigned int addr)
{	
	int lines=(rect.bottom/rowHeight);
	u32 windowEnd = windowStart+lines*rowSize;

	curAddress = addr;
	selectedNibble = 0;

	if (curAddress < windowStart || curAddress >= windowEnd)
	{
		windowStart = curAddress & ~15;
	}

	updateStatusBarText();
	redraw();
}

void CtrlMemView::scrollWindow(int lines)
{
	windowStart += lines*rowSize;
	curAddress += lines*rowSize;
	updateStatusBarText();
	redraw();
}

void CtrlMemView::scrollCursor(int bytes)
{
	if (!asciiSelected && bytes == 1)
	{
		if (selectedNibble == 0)
		{
			selectedNibble = 1;
			bytes = 0;
		} else {
			selectedNibble = 0;
		}
	} else if (!asciiSelected && bytes == -1)
	{
		if (selectedNibble == 0)
		{
			selectedNibble = 1;
		} else {
			selectedNibble = 0;
			bytes = 0;
		}
	} 

	curAddress += bytes;
		
	u32 windowEnd = windowStart+visibleRows*rowSize;
	if (curAddress < windowStart)
	{
		windowStart = curAddress & ~15;
	} else if (curAddress >= windowEnd)
	{
		windowStart = (curAddress-(visibleRows-1)*rowSize) & ~15;
	}
	
	updateStatusBarText();
	redraw();
}

bool CtrlMemView::ParseSearchString(const std::string &query, bool asHex, std::vector<uint8_t> &data) {
	data.clear();
	if (!asHex) {
		for (size_t i = 0; i < query.length(); i++) {
			data.push_back(query[i]);
		}
		return true;
	}

	for (size_t index = 0; index < query.size(); ) {
		if (isspace(query[index])) {
			index++;
			continue;
		}

		u8 value = 0;
		for (int i = 0; i < 2 && index < query.size(); i++) {
			char c = tolower(query[index++]);
			if (c >= 'a' && c <= 'f') {
				value |= (c - 'a' + 10) << (1 - i) * 4;
			} else  if (c >= '0' && c <= '9') {
				value |= (c - '0') << (1 - i) * 4;
			} else {
				return false;
			}
		}

		data.push_back(value);
	}

	return true;
}

std::vector<u32> CtrlMemView::searchString(const std::string &searchQuery) {
	std::vector<u32> searchResAddrs;

	auto memLock = Memory::Lock();
	if (!PSP_IsInited())
		return searchResAddrs;

	std::vector<u8> searchData;
	if (!ParseSearchString(searchQuery, false, searchData))
		return searchResAddrs;

	if (searchData.empty())
		return searchResAddrs;

	std::vector<std::pair<u32, u32>> memoryAreas;
	memoryAreas.reserve(3);
	memoryAreas.emplace_back(PSP_GetScratchpadMemoryBase(), PSP_GetScratchpadMemoryEnd());
	// Ignore the video memory mirrors.
	memoryAreas.emplace_back(PSP_GetVidMemBase(), 0x04200000);
	memoryAreas.emplace_back(PSP_GetKernelMemoryBase(), PSP_GetUserMemoryEnd());

	for (const auto &area : memoryAreas) {
		const u32 segmentStart = area.first;
		const u32 segmentEnd = area.second - (u32)searchData.size();

		for (u32 pos = segmentStart; pos < segmentEnd; pos++) {
			if ((pos % 256) == 0 && KeyDownAsync(VK_ESCAPE)) {
				return searchResAddrs;
			}

			const u8 *ptr = Memory::GetPointerUnchecked(pos);
			if (memcmp(ptr, searchData.data(), searchData.size()) == 0) {
				searchResAddrs.push_back(pos);
			}
		}
	}

	return searchResAddrs;
};

void CtrlMemView::search(bool continueSearch)
{
	auto memLock = Memory::Lock();
	if (!PSP_IsInited())
		return;

	u32 searchAddress = 0;
	u32 segmentStart = 0;
	u32 segmentEnd = 0;
	const u8* dataPointer = 0;
	if (continueSearch == false || searchQuery.empty())
	{
		if (InputBox_GetString(GetModuleHandle(NULL), wnd, L"Search for", searchQuery, searchQuery) == false)
		{
			SetFocus(wnd);
			return;
		}
		SetFocus(wnd);
		searchAddress = curAddress+1;
	} else {
		searchAddress = matchAddress+1;
	}

	std::vector<u8> searchData;
	if (!ParseSearchString(searchQuery, !asciiSelected, searchData)) {
		MessageBox(wnd, L"Invalid search text.", L"Error", MB_OK);
		return;
	}

	std::vector<std::pair<u32, u32>> memoryAreas;
	memoryAreas.reserve(3);
	// Ignore the video memory mirrors.
	memoryAreas.emplace_back(PSP_GetVidMemBase(), 0x04200000);
	memoryAreas.emplace_back(PSP_GetKernelMemoryBase(), PSP_GetUserMemoryEnd());
	memoryAreas.emplace_back(PSP_GetScratchpadMemoryBase(), PSP_GetScratchpadMemoryEnd());
	
	searching = true;
	redraw();	// so the cursor is disabled

	for (size_t i = 0; i < memoryAreas.size(); i++) {
		segmentStart = memoryAreas[i].first;
		segmentEnd = memoryAreas[i].second;

		dataPointer = Memory::GetPointer(segmentStart);
		if (dataPointer == NULL) continue;		// better safe than sorry, I guess

		if (searchAddress < segmentStart) searchAddress = segmentStart;
		if (searchAddress >= segmentEnd) continue;

		int index = searchAddress-segmentStart;
		int endIndex = segmentEnd-segmentStart - (int)searchData.size();

		while (index < endIndex) {
			// cancel search
			if ((index % 256) == 0 && KeyDownAsync(VK_ESCAPE)) {
				searching = false;
				return;
			}
			if (memcmp(&dataPointer[index], searchData.data(), searchData.size()) == 0) {
				matchAddress = index+segmentStart;
				searching = false;
				gotoAddr(matchAddress);
				return;
			}
			index++;
		}
	}

	MessageBox(wnd,L"Not found",L"Search",MB_OK);
	searching = false;
	redraw();
}

void CtrlMemView::drawOffsetScale(HDC hdc)
{
	int currentX = addressStart;

	SetTextColor(hdc, 0x600000);
	TextOutA(hdc, currentX, offsetPositionY, "Offset", 6);

	currentX = addressStart + ((8 + 1)*charWidth); // the start offset, the size of the hex addresses and one space 
	
	char temp[64];
	for (int i = 0; i < 16; i++) 
	{
		sprintf(temp, "%02X", i);
		TextOutA(hdc, currentX, offsetPositionY, temp, 2);
		currentX += 3 * charWidth; // hex and space
	}
}

void CtrlMemView::toggleOffsetScale(CommonToggles toggle)
{
	if (toggle == On) 
		displayOffsetScale = true;
	else if (toggle == Off)
		displayOffsetScale = false;

	updateStatusBarText();
	redraw();
}

void CtrlMemView::setHighlightType(MemBlockFlags flags) {
	if (highlightFlags_ != flags) {
		highlightFlags_ = flags;
		updateStatusBarText();
		redraw();
	}
}

uint32_t CtrlMemView::pickTagColor(const std::string &tag) {
	int colors[6] = { 0xe0FFFF, 0xFFE0E0, 0xE8E8FF, 0xFFE0FF, 0xE0FFE0, 0xFFFFE0 };
	int which = XXH3_64bits(tag.c_str(), tag.length()) % ARRAY_SIZE(colors);
	return colors[which];
}
