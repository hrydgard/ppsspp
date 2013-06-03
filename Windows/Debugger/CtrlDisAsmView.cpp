// NOTE: Apologies for the quality of this code, this is really from pre-opensource Dolphin - that is, 2003.

#include "../resource.h"
#include "../../Core/MemMap.h"
#include "../../Core/MIPS/JitCommon/JitCommon.h"
#include "../W32Util/Misc.h"
#include "../WndMainWindow.h"
#include "../InputBox.h"

#include "CtrlDisAsmView.h"
#include "Debugger_MemoryDlg.h"
#include "../../Core/Debugger/SymbolMap.h"
#include "../../globals.h"
#include "../main.h"

#include <windows.h>
#include <tchar.h>

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
	font = CreateFont(11,0,0,0,FW_DONTCARE,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,DEFAULT_QUALITY,DEFAULT_PITCH,
		"Lucida Console");
	boldfont = CreateFont(11,0,0,0,FW_DEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,DEFAULT_QUALITY,DEFAULT_PITCH,
		"Lucida Console");
	curAddress=0;
	rowHeight=12;
	align=2;
	selecting=false;
	showHex=false;
}


CtrlDisAsmView::~CtrlDisAsmView()
{
	DeleteObject(font);
	DeleteObject(boldfont);
}

void fillRect(HDC hdc, RECT *rect, COLORREF colour)
{
    COLORREF oldcr = SetBkColor(hdc, colour);
    ExtTextOut(hdc, 0, 0, ETO_OPAQUE, rect, "", 0, 0);
    SetBkColor(hdc, oldcr);
}


u32 halfAndHalf(u32 a, u32 b)
{
	return ((a>>1)&0x7f7f7f7f) + ((b>>1)&0x7f7f7f7f);	
}


//Yeah this truly turned into a mess with the latest additions.. but it sure looks nice ;)
void CtrlDisAsmView::onPaint(WPARAM wParam, LPARAM lParam)
{
	struct branch
	{
		int src,dst,srcAddr;
		bool conditional;
	};
	branch branches[256];
	int numBranches=0;

	

	GetClientRect(wnd, &rect);
	PAINTSTRUCT ps;
	HDC hdc;
	
	hdc = BeginPaint(wnd, &ps);
	// TODO: Add any drawing code here...
	int width = rect.right;
	int numRows=(rect.bottom/rowHeight)/2+1;
	//numRows=(numRows&(~1)) + 1;
	SetBkMode(hdc, TRANSPARENT);
	DWORD bgColor = 0xffffff;
	HPEN nullPen=CreatePen(0,0,bgColor);
	HPEN currentPen=CreatePen(0,0,0);
	HPEN selPen=CreatePen(0,0,0x808080);
	HPEN condPen=CreatePen(0,0,0xFF3020);

	LOGBRUSH lbr;
	lbr.lbHatch=0; lbr.lbStyle=0; 
	lbr.lbColor=bgColor;
	HBRUSH nullBrush=CreateBrushIndirect(&lbr);
	lbr.lbColor=0xFFEfE8;
	HBRUSH currentBrush=CreateBrushIndirect(&lbr);
	lbr.lbColor=0x70FF70;
	HBRUSH pcBrush=CreateBrushIndirect(&lbr);

	HPEN oldPen=(HPEN)SelectObject(hdc,nullPen);
	HBRUSH oldBrush=(HBRUSH)SelectObject(hdc,nullBrush);

   
	HFONT oldFont = (HFONT)SelectObject(hdc,(HGDIOBJ)font);
	HICON breakPoint = (HICON)LoadIcon(GetModuleHandle(0),(LPCSTR)IDI_STOP);
	HICON breakPointDisable = (HICON)LoadIcon(GetModuleHandle(0),(LPCSTR)IDI_STOPDISABLE);
	int i;
	curAddress&=~(align-1);

	align=(debugger->getInstructionSize(0));
	for (i=-numRows; i<=numRows; i++)
	{
		unsigned int address=curAddress + i*align;

		int rowY1 = rect.bottom/2 + rowHeight*i - rowHeight/2;
		int rowY2 = rect.bottom/2 + rowHeight*i + rowHeight/2;
		char temp[256];
		int temp_len = sprintf(temp,"%08x",address);

		lbr.lbColor=marker==address?0xffeee0:debugger->getColor(address);
		u32 bg = lbr.lbColor;
		//SelectObject(hdc,currentBrush);
		SelectObject(hdc,nullPen);
		Rectangle(hdc,0,rowY1,16,rowY2);

		if (selecting && address == selection)
			SelectObject(hdc,selPen);
		else
			SelectObject(hdc,i==0 ? currentPen : nullPen);

		HBRUSH mojsBrush=CreateBrushIndirect(&lbr);
		SelectObject(hdc,mojsBrush);

		if (address == debugger->getPC())
			SelectObject(hdc,pcBrush);
		//else
		//	SelectObject(hdc,i==0 ? currentBrush : nullBrush);

		Rectangle(hdc,16,rowY1,width,rowY2);
		SelectObject(hdc,currentBrush);
		DeleteObject(mojsBrush);
		SetTextColor(hdc,halfAndHalf(bg,0));
		TextOut(hdc,17,rowY1,temp,temp_len);
		SetTextColor(hdc,0x000000);
		if (debugger->isAlive())
		{
			const TCHAR *dizz = debugger->disasm(address, align);
			char dis[512];
			strcpy(dis, dizz);
			TCHAR *dis2 = strchr(dis,'\t');
			TCHAR desc[256]="";
			if (dis2)
			{
				*dis2=0;
				dis2++;
				const char *mojs=strstr(dis2,"->$");
				if (mojs)
				{
					for (int j=0; j<8; ++j)
					{
						bool found=false;
						for (int k=0; k<22; ++k)
						{
							if (mojs[j+3]=="0123456789ABCDEFabcdef"[k])
								found=true;
						}
						if (!found)
						{
							mojs=0;
							break;
						}
					}
				}
				if (mojs)
				{
					int offs;
					sscanf(mojs+3,"%08x",&offs);
					branches[numBranches].src=rowY1 + rowHeight/2;
					branches[numBranches].srcAddr=address/align;
					branches[numBranches].dst=(int)(rowY1+((__int64)offs-(__int64)address)*rowHeight/align + rowHeight/2);
					branches[numBranches].conditional = (dis[1]!=0); //unconditional 'b' branch
					numBranches++;
					const char *t = debugger->getDescription(offs);
					if (memcmp(t,"z_",2)==0)
						t+=2;
					if (memcmp(t,"zz_",3)==0)
						t+=3;
					sprintf(desc,"-->%s", t);
					SetTextColor(hdc,0x600060);
				}
				else
					SetTextColor(hdc,0x000000);
				TextOut(hdc,149,rowY1,dis2,(int)strlen(dis2));
			}
			SetTextColor(hdc,0x007000);
			SelectObject(hdc,boldfont);
			TextOut(hdc,84,rowY1,dis,(int)strlen(dis));
			SelectObject(hdc,font);
			if (desc[0]==0)
			{
				const char *t = debugger->getDescription(address);
				if (memcmp(t,"z_",2)==0)
					t+=2;
				if (memcmp(t,"zz_",3)==0)
					t+=3;
				strcpy(desc,t);
			}
			if (memcmp(desc,"-->",3) == 0)
				SetTextColor(hdc,0x0000FF);
			else
				SetTextColor(hdc,halfAndHalf(halfAndHalf(bg,0),bg));
			//char temp[256];
			//UnDecorateSymbolName(desc,temp,255,UNDNAME_COMPLETE);
			if (strlen(desc))
				TextOut(hdc,max(280,width/3+190),rowY1,desc,(int)strlen(desc));
			if (debugger->isBreakpoint(address))
			{
				DrawIconEx(hdc,2,rowY1,breakPoint,32,32,0,0,DI_NORMAL);
			}
		}
	}
	for (i=0; i<numBranches; i++)
	{
		SelectObject(hdc,branches[i].conditional ? condPen : currentPen);
		int x=280+(branches[i].srcAddr%9)*8;
		MoveToEx(hdc,x-2,branches[i].src,0);

		if (branches[i].dst<rect.bottom+200 && branches[i].dst>-200)
		{
			LineTo(hdc,x+2,branches[i].src);
			LineTo(hdc,x+2,branches[i].dst);
			LineTo(hdc,x-4,branches[i].dst);
			
			MoveToEx(hdc,x,branches[i].dst-4,0);
			LineTo(hdc,x-4,branches[i].dst);
			LineTo(hdc,x+1,branches[i].dst+5);
		}
		else
		{
			LineTo(hdc,x+4,branches[i].src);
			//MoveToEx(hdc,x+2,branches[i].dst-4,0);
			//LineTo(hdc,x+6,branches[i].dst);
			//LineTo(hdc,x+1,branches[i].dst+5);
		}
		//LineTo(hdc,x,branches[i].dst+4);

		//LineTo(hdc,x-2,branches[i].dst);
	}

	SelectObject(hdc,oldFont);
	SelectObject(hdc,oldPen);
	SelectObject(hdc,oldBrush);
	
	DeleteObject(nullPen);
	DeleteObject(currentPen);
	DeleteObject(selPen);
	DeleteObject(condPen);

	DeleteObject(nullBrush);
	DeleteObject(pcBrush);
	DeleteObject(currentBrush);
	
	DestroyIcon(breakPoint);
	DestroyIcon(breakPointDisable);
	
	EndPaint(wnd, &ps);
}



void CtrlDisAsmView::onVScroll(WPARAM wParam, LPARAM lParam)
{
	RECT rect;
	GetClientRect(this->wnd, &rect);
	int page=(rect.bottom/rowHeight)/2-1;

	switch (wParam & 0xFFFF)
	{
	case SB_LINEDOWN:
		curAddress+=align;
		break;
	case SB_LINEUP:
		curAddress-=align;
		break;
	case SB_PAGEDOWN:
		curAddress+=page*align;
		break;
	case SB_PAGEUP:
		curAddress-=page*align;
		break;
	default:
		return;
	}
	redraw();
}

void CtrlDisAsmView::onKeyDown(WPARAM wParam, LPARAM lParam)
{
	RECT rect;
	GetClientRect(this->wnd, &rect);
	int page=(rect.bottom/rowHeight)/2-1;

	switch (wParam & 0xFFFF)
	{
	case VK_DOWN:
		curAddress+=align;
		break;
	case VK_UP:
		curAddress-=align;
		break;
	case VK_NEXT:
		curAddress+=page*align;
		break;
	case VK_PRIOR:
		curAddress-=page*align;
		break;
	default:
		return;
	}
	redraw();
}


void CtrlDisAsmView::redraw()
{
	InvalidateRect(wnd, NULL, FALSE);
	UpdateWindow(wnd); 
}


void CtrlDisAsmView::onMouseDown(WPARAM wParam, LPARAM lParam, int button)
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
	else
	{
		debugger->toggleBreakpoint(yToAddress(y));
		redraw();
	}
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
					memoryWindow[i]->Goto(selection);
			break;
		case ID_DISASM_ADDHLE:
			break;
		case ID_DISASM_TOGGLEBREAKPOINT:
			debugger->toggleBreakpoint(selection);
			redraw();
			break;
		case ID_DISASM_COPYINSTRUCTIONDISASM:
			W32Util::CopyTextToClipboard(wnd, debugger->disasm(selection,align));
			break;
		case ID_DISASM_COPYADDRESS:
			{
				char temp[16];
				sprintf(temp,"%08x",selection);
				W32Util::CopyTextToClipboard(wnd, temp);
			}
			break;
		case ID_DISASM_SETPCTOHERE:
			debugger->setPC(selection);
			redraw();
			break;
		case ID_DISASM_FOLLOWBRANCH:
			{
				const char *temp = debugger->disasm(selection,align);
				const char *mojs=strstr(temp,"->$");
				if (mojs)
				{
					u32 dest;
					sscanf(mojs+3,"%08x",&dest);
					if (dest)
					{
						marker = selection;
						gotoAddr(dest);
					}
				}
			}
			break;
		case ID_DISASM_COPYINSTRUCTIONHEX:
			{
				char temp[24];
				sprintf(temp,"%08x",debugger->readMemory(selection));
				W32Util::CopyTextToClipboard(wnd,temp);
			}
			break;
		case ID_DISASM_RUNTOHERE:
			{
				debugger->setBreakpoint(selection);
				debugger->runToBreakpoint();
				redraw();
			}
			break;
		case ID_DISASM_RENAMEFUNCTION:
			{
				int sym = symbolMap.GetSymbolNum(selection);
				if (sym != -1)
				{
					char name[256];
					char newname[256];
					strncpy_s(name, symbolMap.GetSymbolName(sym),_TRUNCATE);
					if (InputBox_GetString(MainWindow::GetHInstance(), MainWindow::GetHWND(), "New function name", name, newname))
					{
						symbolMap.SetSymbolName(sym,newname);
						redraw();
						SendMessage(GetParent(wnd),WM_USER+1,0,0);
					}
				}
				else
				{
					MessageBox(MainWindow::GetHWND(),"No symbol selected",0,0);
				}
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

void CtrlDisAsmView::onMouseMove(WPARAM wParam, LPARAM lParam, int button)
{
	if (button&1)
	{
		int x = LOWORD(lParam); 
		int y = (signed short)HIWORD(lParam); 
		if (x>16)
		{
			if (y<0)
			{
				curAddress-=align;
				redraw();
			}
			else if (y>rect.bottom)
			{
				curAddress+=align;
				redraw();
			}
			else
				onMouseDown(wParam,lParam,1);
		}
	}
}	


int CtrlDisAsmView::yToAddress(int y)
{
	int ydiff=y-rect.bottom/2-rowHeight/2;
	ydiff=(int)(floorf((float)ydiff / (float)rowHeight))+1;
	return curAddress + ydiff * align;
}
