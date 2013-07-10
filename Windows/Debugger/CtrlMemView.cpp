// NOTE: Apologies for the quality of this code, this is really from pre-opensource Dolphin - that is, 2003.

#include <tchar.h>
#include <math.h>

#include "../../globals.h"

#include "Core/Config.h"
#include "../resource.h"
#include "../../Core/MemMap.h"
#include "../W32Util/Misc.h"
#include "../Main.h"
#include "../../Core/Debugger/SymbolMap.h"

#include "Debugger_Disasm.h"
#include "DebuggerShared.h"
#include "CtrlMemView.h"

TCHAR CtrlMemView::szClassName[] = _T("CtrlMemView");
extern HMENU g_hPopupMenus;

CtrlMemView::CtrlMemView(HWND _wnd)
{
  wnd=_wnd;
  SetWindowLongPtr(wnd, GWLP_USERDATA, (LONG)this);
  SetWindowLong(wnd, GWL_STYLE, GetWindowLong(wnd,GWL_STYLE) | WS_VSCROLL);
  SetScrollRange(wnd, SB_VERT, -1,1,TRUE);

  rowHeight = g_Config.iFontHeight;
  charWidth = g_Config.iFontWidth;

  font =
	  CreateFont(rowHeight,charWidth,0,0,FW_DONTCARE,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,
		  CLIP_DEFAULT_PRECIS,DEFAULT_QUALITY,DEFAULT_PITCH,"Lucida Console");
  underlineFont =
	  CreateFont(rowHeight,charWidth,0,0,FW_DONTCARE,FALSE,TRUE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,
		  CLIP_DEFAULT_PRECIS,DEFAULT_QUALITY,DEFAULT_PITCH,"Lucida Console");
  curAddress=0;
  mode=MV_NORMAL;
  debugger = 0;
  
	ctrlDown = false;
	hasFocus = false;
	windowStart = curAddress;
	asciiSelected = false;

	selectedNibble = 0;
	rowSize = 16;
	addressStart = charWidth;
	hexStart = addressStart + 9*charWidth;
	asciiStart = hexStart + (rowSize*3+1)*charWidth;
}

CtrlMemView::~CtrlMemView()
{
  DeleteObject(font);
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
		if (wParam == VK_CONTROL) ccp->ctrlDown = false;
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
	if (!debugger) return;

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

	// draw one extra row that may be partially visible
	for (int i = 0; i < visibleRows+1; i++)
	{
		char temp[32];

		unsigned int address=windowStart + i*rowSize;
		int rowY = rowHeight*i;
		
		sprintf(temp,"%08X",address);
		SetTextColor(hdc,0x600000);
		TextOut(hdc,addressStart,rowY,temp,(int)strlen(temp));

		SetTextColor(hdc,0x000000);
		if (debugger->isAlive())
		{

			switch(mode) {
			case MV_NORMAL:
				{
					u32 memory[4];
					if (Memory::IsValidAddress(address))
					{
						memory[0] = debugger->readMemory(address);
						memory[1] = debugger->readMemory(address+4);
						memory[2] = debugger->readMemory(address+8);
						memory[3] = debugger->readMemory(address+12);
					}
					else
					{
						memory[0] = memory[1] = memory[2] = memory[3] = 0xFFFFFFFF;
					}

					u8* m = (u8*) memory;
					for (int j = 0; j < rowSize; j++)
					{
						sprintf(temp,"%02X",m[j]);
						unsigned char c = m[j];
						if (c < 32 || c >= 128) c = '.';

						if (address+j == curAddress)
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
							TextOut(hdc,hexStart+j*3*charWidth,rowY,&temp[0],1);
							
							if (hasFocus && !asciiSelected)
							{
								if (selectedNibble == 1) SelectObject(hdc,(HGDIOBJ)underlineFont);
								else SelectObject(hdc,(HGDIOBJ)font);
							}
							TextOut(hdc,hexStart+j*3*charWidth+charWidth,rowY,&temp[1],1);

							if (hasFocus && asciiSelected)
							{
								SetTextColor(hdc,0xFFFFFF);
								SetBkColor(hdc,0xFF9933);
							} else {
								SetTextColor(hdc,0);
								SetBkColor(hdc,0xC0C0C0);
								SelectObject(hdc,(HGDIOBJ)font);
							}
							TextOut(hdc,asciiStart+j*(charWidth+2),rowY,(char*)&c,1);

							SetTextColor(hdc,oldTextColor);
							SetBkColor(hdc,oldBkColor);
						} else {
							TextOut(hdc,hexStart+j*3*charWidth,rowY,temp,2);
							TextOut(hdc,asciiStart+j*(charWidth+2),rowY,(char*)&c,1);
						}
					}
				}
				break;
/*			case MV_SYMBOLS:
				{
					SetTextColor(hdc,0x0000FF);
					int fn = symbolMap.GetSymbolNum(address);
					if (fn==-1)
					{
						sprintf(temp, "%s (ns)", Memory::GetAddressName(address));
					}
					else
                        sprintf(temp, "%s (0x%x b)", symbolMap.GetSymbolName(fn),symbolMap.GetSymbolSize(fn));
					TextOut(hdc,200,rowY1,temp,(int)strlen(temp));

					SetTextColor(hdc,0x0000000);
					
					if (align==4)
					{
						u32 value = Memory::ReadUnchecked_U32(address);
						int num = symbolMap.GetSymbolNum(value);
						if (num != -1)
							sprintf(temp, "%08x [%s]", value, symbolMap.GetSymbolName(num));
						else
							sprintf(temp, "%08x", value);
					}
					else if (align==2)
					{
						u16 value = Memory::ReadUnchecked_U16(address);
						int num = symbolMap.GetSymbolNum(value);
						if (num != -1)
							sprintf(temp, "%04x [%s]", value, symbolMap.GetSymbolName(num));
						else
							sprintf(temp, "%04x", value);
					}

					TextOut(hdc,70,rowY1,temp,(int)strlen(temp));
					break;
				}*/
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
	if (ctrlDown && tolower(wParam & 0xFFFF) == 'g')
	{
		ctrlDown = false;
		u32 addr;
		if (executeExpressionWindow(wnd,debugger,addr) == false) return;
		gotoAddr(addr);
		return;
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
	case VK_CONTROL:
		ctrlDown = true;
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
	if (ctrlDown || wParam == VK_TAB) return;

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
		FILE* outputfile;
		switch (TrackPopupMenuEx(GetSubMenu(g_hPopupMenus,0),TPM_RIGHTBUTTON|TPM_RETURNCMD,pt.x,pt.y,wnd,0))
		{
		case ID_MEMVIEW_DUMP:
     
			if (!Core_IsStepping()) // If emulator isn't paused
			{
				MessageBox(wnd,"You have to pause the emulator first","Sorry",0);
				break;
			}
			else
			{
				outputfile = fopen("Ram.dump","wb");		// Could also dump Vram, but not useful for now.
				fwrite(Memory::GetPointer(0x08800000), 1, 0x1800000, outputfile); 
				fclose(outputfile);
				break;
			}

		case ID_MEMVIEW_COPYVALUE:
			{
				char temp[24];

				// it's admittedly not really useful like this
				if (asciiSelected)
				{
					unsigned char c = Memory::IsValidAddress(curAddress) ? Memory::ReadUnchecked_U8(curAddress) : '.';
					if (c < 32 || c >= 128) c = '.';
					sprintf(temp,"%c",c);
				} else {
					sprintf(temp,"%02X",Memory::IsValidAddress(curAddress) ? Memory::ReadUnchecked_U8(curAddress) : 0xFF);
				}
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


void CtrlMemView::gotoPoint(int x, int y)
{
	int line = y/rowHeight;
	int lineAddress = windowStart+line*rowSize;

	if (x >= asciiStart)
	{
		int col = (x-asciiStart) / (charWidth+2);
		if (col >= rowSize) return;
		
		asciiSelected = true;
		curAddress = lineAddress+col;
		selectedNibble = 0;
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

	redraw();
}