// NOTE: Apologies for the quality of this code, this is really from pre-opensource Dolphin - that is, 2003.

#include <tchar.h>
#include <math.h>

#include "../../globals.h"

#include "../resource.h"
#include "../../Core/MemMap.h"
#include "../W32Util/Misc.h"
#include "../Main.h"
#include "../../Core/Debugger/SymbolMap.h"

#include "Debugger_Disasm.h"

#include "CtrlMemView.h"

TCHAR CtrlMemView::szClassName[] = _T("CtrlMemView");
extern HMENU g_hPopupMenus;

CtrlMemView::CtrlMemView(HWND _wnd)
{
  wnd=_wnd;
  SetWindowLongPtr(wnd, GWLP_USERDATA, (LONG)this);
  SetWindowLong(wnd, GWL_STYLE, GetWindowLong(wnd,GWL_STYLE) | WS_VSCROLL);
  SetScrollRange(wnd, SB_VERT, -1,1,TRUE);
  font = CreateFont(12,0,0,0,FW_DONTCARE,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,DEFAULT_QUALITY,DEFAULT_PITCH,
    "Lucida Console");
  curAddress=0;
  rowHeight=12;
  align=4;
  alignMul=4;
  selecting=false;
  mode=MV_NORMAL;
  debugger = 0;
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
	case WM_ERASEBKGND:
		return FALSE;
	case WM_KEYDOWN:
		ccp->onKeyDown(wParam,lParam);
		break;
	case WM_LBUTTONDOWN: SetFocus(hwnd); lmbDown=true; ccp->onMouseDown(wParam,lParam,1); break;
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
    default:
        break;
    }
	
    return DefWindowProc(hwnd, msg, wParam, lParam);
}


CtrlMemView *CtrlMemView::getFrom(HWND hwnd)
{
	return (CtrlMemView *)GetWindowLongPtr(hwnd, GWLP_USERDATA);
}




//Yeah this truly turned into a mess with the latest additions.. but it sure looks nice ;)
void CtrlMemView::onPaint(WPARAM wParam, LPARAM lParam)
{
  if (!debugger)
    return;
	GetClientRect(wnd, &rect);
	PAINTSTRUCT ps;
	HDC hdc;
	hdc = BeginPaint(wnd, &ps);
	int width = rect.right;
	int numRows=(rect.bottom/rowHeight)/2+1;
	SetBkMode(hdc, TRANSPARENT);
	HPEN nullPen=CreatePen(0,0,0xFFFFFF);
	HPEN currentPen=CreatePen(0,0,0);
	HPEN selPen=CreatePen(0,0,0x808080);
	LOGBRUSH lbr;
	lbr.lbHatch=0; lbr.lbStyle=0; 
	lbr.lbColor=0xFFFFFF;
	HBRUSH nullBrush=CreateBrushIndirect(&lbr);
	lbr.lbColor=0xFFEfE8;
	HBRUSH currentBrush=CreateBrushIndirect(&lbr);
	lbr.lbColor=0x70FF70;
	HBRUSH pcBrush=CreateBrushIndirect(&lbr);
	HPEN oldPen=(HPEN)SelectObject(hdc,nullPen);
	HBRUSH oldBrush=(HBRUSH)SelectObject(hdc,nullBrush);
   	HFONT oldFont = (HFONT)SelectObject(hdc,(HGDIOBJ)font);

	int i;
	curAddress&=~(align-1);
	for (i=-numRows; i<=numRows; i++)
	{
		unsigned int address=curAddress + i*align*alignMul;

		int rowY1 = rect.bottom/2 + rowHeight*i - rowHeight/2;
		int rowY2 = rect.bottom/2 + rowHeight*i + rowHeight/2;

		char temp[256];
		sprintf(temp,"%08x",address);

		SelectObject(hdc,currentBrush);

		if (selecting && address == selection)
			SelectObject(hdc,selPen);
		else
			SelectObject(hdc,i==0 ? currentPen : nullPen);
		Rectangle(hdc,0,rowY1,16,rowY2);

		Rectangle(hdc,16,rowY1,width,rowY2);
		SelectObject(hdc,nullBrush);
		SetTextColor(hdc,0x600000);
		TextOut(hdc,17,rowY1,temp,(int)strlen(temp));
		SetTextColor(hdc,0x000000);
		if (debugger->isAlive())
		{

			switch(mode) {
			case MV_NORMAL:
				{
					const char *m = "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0";
					if (Memory::IsValidAddress(address))
					{
					  u32 memory[4] = {
						  debugger->readMemory(address),
						  debugger->readMemory(address+4),
						  debugger->readMemory(address+8),
						  debugger->readMemory(address+12)
					  };
					  m = (const char*)memory;
					  sprintf(temp, "%08x %08x %08x %08x  ................", 
						  memory[0],memory[1],memory[2],memory[3]);
					}
					for (int j=0; j<16; ++j)
					{
						int c = (unsigned char)m[j];
						if (c>=32 && c<255)
							temp[j+37]=c;
					}
				}
//				if (align == 16)
//				else
//					sprintf(temp, "%04x %04x %04x %04x", ReadMem16Unchecked(address),ReadMem16Unchecked(address+2), ReadMem16Unchecked(address+4), ReadMem16Unchecked(address+6))
				TextOut(hdc,80,rowY1,temp,(int)strlen(temp));
				break;

			case MV_SYMBOLS:
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
				}
			}
		}
	}

	SelectObject(hdc,oldFont);
	SelectObject(hdc,oldPen);
	SelectObject(hdc,oldBrush);
	
	DeleteObject(nullPen);
	DeleteObject(currentPen);
	DeleteObject(selPen);

	DeleteObject(nullBrush);
	DeleteObject(pcBrush);
	DeleteObject(currentBrush);
	
	EndPaint(wnd, &ps);
}



void CtrlMemView::onVScroll(WPARAM wParam, LPARAM lParam)
{
	RECT rect;
	GetClientRect(this->wnd, &rect);
	int page=(rect.bottom/rowHeight)/2-1;

	switch (wParam & 0xFFFF)
	{
	case SB_LINEDOWN:
		curAddress+=align*alignMul;
		break;
	case SB_LINEUP:
		curAddress-=align*alignMul;
		break;
	case SB_PAGEDOWN:
		curAddress+=page*align*alignMul;
		break;
	case SB_PAGEUP:
		curAddress-=page*align*alignMul;
		break;
	default:
		return;
	}
	redraw();
}

void CtrlMemView::onKeyDown(WPARAM wParam, LPARAM lParam)
{
	RECT rect;
	GetClientRect(this->wnd, &rect);
	int page=(rect.bottom/rowHeight)/2-1;

	switch (wParam & 0xFFFF)
	{
	case VK_DOWN:
		curAddress+=align*alignMul;
		break;
	case VK_UP:
		curAddress-=align*alignMul;
		break;
	case VK_NEXT:
		curAddress+=page*align*alignMul;
		break;
	case VK_PRIOR:
		curAddress-=page*align*alignMul;
		break;
	default:
		return;
	}
	redraw();
}


void CtrlMemView::redraw()
{
	InvalidateRect(wnd, NULL, FALSE);
	UpdateWindow(wnd); 
}


void CtrlMemView::onMouseDown(WPARAM wParam, LPARAM lParam, int button)
{	
	int x = LOWORD(lParam); 
	int y = HIWORD(lParam); 
	if (x>16)
	{
		oldSelection=selection;
		selection=yToAddress(y);
		SetCapture(wnd);
		bool oldselecting=selecting;
		selecting=true;
		if (!oldselecting || (selection!=oldSelection))
			redraw();
	}
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
				sprintf(temp,"%08x",Memory::ReadUnchecked_U32(selection));
				W32Util::CopyTextToClipboard(wnd,temp);
			}
			break;
		}
		return;
	}
	int x = LOWORD(lParam); 
	int y = HIWORD(lParam); 
	if (x>16)
	{
		curAddress=yToAddress(y);
		selecting=false;
		ReleaseCapture();
		redraw();
	}
}

void CtrlMemView::onMouseMove(WPARAM wParam, LPARAM lParam, int button)
{
	if (button&1)
	{
		int x = LOWORD(lParam); 
		int y = (signed short)HIWORD(lParam); 
		if (x>16)
		{
			if (y<0)
			{
				curAddress-=align*alignMul;
				redraw();
			}
			else if (y>rect.bottom)
			{
				curAddress+=align*alignMul;
				redraw();
			}
			else
				onMouseDown(wParam,lParam,1);
		}
	}
}	


int CtrlMemView::yToAddress(int y)
{
	int ydiff=y-rect.bottom/2-rowHeight/2;
	ydiff=(int)(floorf((float)ydiff / (float)rowHeight))+1;
	return curAddress + ydiff * align*alignMul;
}
