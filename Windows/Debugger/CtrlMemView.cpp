// NOTE: Apologies for the quality of this code, this is really from pre-opensource Dolphin - that is, 2003.

#include <tchar.h>
#include <math.h>

#include "Core/Config.h"
#include "../resource.h"
#include "../../Core/MemMap.h"
#include "../W32Util/Misc.h"
#include "Windows/InputBox.h"
#include "../Main.h"
#include "../../Core/Debugger/SymbolMap.h"

#include "Debugger_Disasm.h"
#include "DebuggerShared.h"
#include "CtrlMemView.h"
#include "DumpMemoryWindow.h"

wchar_t CtrlMemView::szClassName[] = L"CtrlMemView";
extern HMENU g_hPopupMenus;

CtrlMemView::CtrlMemView(HWND _wnd)
{
	wnd=_wnd;
	SetWindowLongPtr(wnd, GWLP_USERDATA, (LONG_PTR)this);
	SetWindowLong(wnd, GWL_STYLE, GetWindowLong(wnd,GWL_STYLE) | WS_VSCROLL);
	SetScrollRange(wnd, SB_VERT, -1,1,TRUE);

	rowHeight = g_Config.iFontHeight;
	charWidth = g_Config.iFontWidth;
	offsetPositionY = offsetLine*rowHeight;

	font =
		CreateFont(rowHeight,charWidth,0,0,FW_DONTCARE,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,
			CLIP_DEFAULT_PRECIS,DEFAULT_QUALITY,DEFAULT_PITCH,L"Lucida Console");
	underlineFont =
		CreateFont(rowHeight,charWidth,0,0,FW_DONTCARE,FALSE,TRUE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,
			CLIP_DEFAULT_PRECIS,DEFAULT_QUALITY,DEFAULT_PITCH,L"Lucida Console");
	curAddress=0;
	debugger = 0;
  
	searchQuery = "";
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
	SetTimer(wnd,1,1000,0);
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
		if (wParam == 1 && IsWindowVisible(ccp->wnd))
			ccp->redraw();
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




void CtrlMemView::onPaint(WPARAM wParam, LPARAM lParam)
{
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

	HPEN oldPen = (HPEN) SelectObject(hdc,standardPen);
	HBRUSH oldBrush = (HBRUSH) SelectObject(hdc,standardBrush);
   	HFONT oldFont = (HFONT) SelectObject(hdc,(HGDIOBJ)font);

	// white background
	SelectObject(hdc,standardPen);
	SelectObject(hdc,standardBrush);
	Rectangle(hdc,0,0,rect.right,rect.bottom);

	if (displayOffsetScale) 
		drawOffsetScale(hdc);
	

	// draw one extra row that may be partially visible
	for (int i = 0; i < visibleRows+1; i++)
	{
		char temp[32];

		unsigned int address=windowStart + i*rowSize;
		int rowY = rowHeight*i;

		if (displayOffsetScale) 
			rowY += rowHeight * offsetSpace; // skip the first X rows to make space for the offsets
		
		
		sprintf(temp,"%08X",address);
		SetTextColor(hdc,0x600000);
		TextOutA(hdc,addressStart,rowY,temp,(int)strlen(temp));

		SetTextColor(hdc,0x000000);

		u32 memory[4];
		bool valid = debugger != NULL && debugger->isAlive() && Memory::IsValidAddress(address);
		if (valid)
		{
			memory[0] = debugger->readMemory(address);
			memory[1] = debugger->readMemory(address+4);
			memory[2] = debugger->readMemory(address+8);
			memory[3] = debugger->readMemory(address+12);
		}

		u8* m = (u8*) memory;
		for (int j = 0; j < rowSize; j++)
		{
			if (valid) sprintf(temp,"%02X",m[j]);
			else strcpy(temp,"??");

			unsigned char c = m[j];
			if (c < 32 || c >= 128 || valid == false) c = '.';

			if (address+j == curAddress && searching == false)
			{
				COLORREF oldBkColor = GetBkColor(hdc);
				COLORREF oldTextColor = GetTextColor(hdc);

				if (hasFocus && !asciiSelected)
				{
					SetTextColor(hdc,0xFFFFFF);
					SetBkColor(hdc,0xFF9933);
					if (selectedNibble == 0) SelectObject(hdc,(HGDIOBJ)underlineFont);
				} else {
					SetTextColor(hdc,0);
					SetBkColor(hdc,0xC0C0C0);
				}
				TextOutA(hdc,hexStart+j*3*charWidth,rowY,&temp[0],1);
							
				if (hasFocus && !asciiSelected)
				{
					if (selectedNibble == 1) SelectObject(hdc,(HGDIOBJ)underlineFont);
					else SelectObject(hdc,(HGDIOBJ)font);
				}
				TextOutA(hdc,hexStart+j*3*charWidth+charWidth,rowY,&temp[1],1);

				if (hasFocus && asciiSelected)
				{
					SetTextColor(hdc,0xFFFFFF);
					SetBkColor(hdc,0xFF9933);
				} else {
					SetTextColor(hdc,0);
					SetBkColor(hdc,0xC0C0C0);
					SelectObject(hdc,(HGDIOBJ)font);
				}
				TextOutA(hdc,asciiStart+j*(charWidth+2),rowY,(char*)&c,1);

				SetTextColor(hdc,oldTextColor);
				SetBkColor(hdc,oldBkColor);
			} else {
				TextOutA(hdc,hexStart+j*3*charWidth,rowY,temp,2);
				TextOutA(hdc,asciiStart+j*(charWidth+2),rowY,(char*)&c,1);
			}
		}
	}

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
	if (active) Core_EnableStepping(true);

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

	if (active) Core_EnableStepping(false);
}

void CtrlMemView::redraw()
{
	GetClientRect(wnd, &rect);
	visibleRows = (rect.bottom/rowHeight);

	if (displayOffsetScale) {
		visibleRows -= offsetSpace; // visibleRows is calculated based on the size of the control, but X rows have already been used for the offsets and are no longer usable
	}

	InvalidateRect(wnd, NULL, FALSE);
	UpdateWindow(wnd); 
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
		//popup menu?
		POINT pt;
		GetCursorPos(&pt);

		bool enable16 = !asciiSelected && (curAddress % 2) == 0;
		bool enable32 = !asciiSelected && (curAddress % 4) == 0;

		HMENU menu = GetSubMenu(g_hPopupMenus,0);
		EnableMenuItem(menu,ID_MEMVIEW_COPYVALUE_16,enable16 ? MF_ENABLED : MF_GRAYED);
		EnableMenuItem(menu,ID_MEMVIEW_COPYVALUE_32,enable32 ? MF_ENABLED : MF_GRAYED);

		switch (TrackPopupMenuEx(menu,TPM_RIGHTBUTTON|TPM_RETURNCMD,pt.x,pt.y,wnd,0))
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

		case ID_MEMVIEW_COPYADDRESS:
			{
				char temp[24];
				sprintf(temp,"0x%08X",curAddress);
				W32Util::CopyTextToClipboard(wnd,temp);
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

void CtrlMemView::updateStatusBarText()
{
	char text[64];
	sprintf(text,"%08X",curAddress);
	SendMessage(GetParent(wnd),WM_DEB_SETSTATUSBARTEXT,0,(LPARAM)text);
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

void CtrlMemView::search(bool continueSearch)
{
	auto memLock = Memory::Lock();
	if (!PSP_IsInited())
		return;

	u32 searchAddress;
	if (continueSearch == false || searchQuery[0] == 0)
	{
		if (InputBox_GetString(GetModuleHandle(NULL),wnd,L"Search for", "",searchQuery) == false)
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
	if (asciiSelected)
	{
		for (size_t i = 0; i < searchQuery.length(); i++)
		{
			char c = searchQuery[i];
			searchData.push_back(c);
		}
	} else {
		size_t index = 0;
		while (index < searchQuery.size())
		{
			if (searchQuery[index] == ' ' || searchQuery[index] == '\t')
			{
				index++;
				continue;
			}

			u8 value = 0;
			for (int i = 0; i < 2; i++)
			{
				char c = tolower(searchQuery[index++]);
				if (c >= 'a' && c <= 'f')
				{
					value |= (c-'a'+10) << (1-i)*4;
				} else  if (c >= '0' && c <= '9')
				{
					value |= (c-'0') << (1-i)*4;
				} else {
					MessageBox(wnd,L"Invalid search text.",L"Error",MB_OK);
					return;
				}
			}

			searchData.push_back(value);
		}
	}

	std::vector<std::pair<u32,u32>> memoryAreas;
	memoryAreas.push_back(std::pair<u32,u32>(0x04000000,0x04200000));
	memoryAreas.push_back(std::pair<u32,u32>(0x08000000,0x0A000000));
	
	searching = true;
	redraw();	// so the cursor is disabled
	for (size_t i = 0; i < memoryAreas.size(); i++)
	{
		u32 segmentStart = memoryAreas[i].first;
		u32 segmentEnd = memoryAreas[i].second;
		u8* dataPointer = Memory::GetPointer(segmentStart);
		if (dataPointer == NULL) continue;		// better safe than sorry, I guess

		if (searchAddress < segmentStart) searchAddress = segmentStart;
		if (searchAddress >= segmentEnd) continue;

		int index = searchAddress-segmentStart;
		int endIndex = segmentEnd-segmentStart-(int)searchData.size();

		while (index < endIndex)
		{
			// cancel search
			if ((index % 256) == 0 && KeyDownAsync(VK_ESCAPE))
			{
				searching = false;
				return;
			}
		
			if (memcmp(&dataPointer[index],searchData.data(),searchData.size()) == 0)
			{
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

void CtrlMemView::toggleOffsetScale(OffsetToggles toggle)
{
	if (toggle == On) 
		displayOffsetScale = true;
	else if (toggle == Off)
		displayOffsetScale = false;

	updateStatusBarText();
	redraw();
}
