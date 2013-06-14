// NOTE: Apologies for the quality of this code, this is really from pre-opensource Dolphin - that is, 2003.

#include <math.h>
#include <tchar.h>

#include "../resource.h"
#include "../../Core/MemMap.h"
#include "../W32Util/Misc.h"
#include "../InputBox.h"

#include "CtrlRegisterList.h"
#include "Debugger_MemoryDlg.h"

#include "../../globals.h"
#include "Debugger_Disasm.h"

#include "../main.h"

//#include "DbgHelp.h"
extern HMENU g_hPopupMenus;


TCHAR CtrlRegisterList::szClassName[] = _T("CtrlRegisterList");

void CtrlRegisterList::init()
{
    WNDCLASSEX wc;
    
    wc.cbSize         = sizeof(wc);
    wc.lpszClassName  = szClassName;
    wc.hInstance      = GetModuleHandle(0);
    wc.lpfnWndProc    = CtrlRegisterList::wndProc;
    wc.hCursor        = LoadCursor (NULL, IDC_ARROW);
    wc.hIcon          = 0;
    wc.lpszMenuName   = 0;
    wc.hbrBackground  = (HBRUSH)GetSysColorBrush(COLOR_WINDOW);
    wc.style          = 0;
    wc.cbClsExtra     = 0;
	wc.cbWndExtra     = sizeof( CtrlRegisterList * );
    wc.hIconSm        = 0;
	
	
    RegisterClassEx(&wc);
}

void CtrlRegisterList::deinit()
{
	//UnregisterClass(szClassName, hInst)
}



LRESULT CALLBACK CtrlRegisterList::wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	CtrlRegisterList *ccp = CtrlRegisterList::getFrom(hwnd);
	static bool lmbDown=false,rmbDown=false;
    switch(msg)
    {
    case WM_NCCREATE:
        // Allocate a new CustCtrl structure for this window.
        ccp = new CtrlRegisterList(hwnd);
		
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
/*
	case WM_VSCROLL:
		ccp->onVScroll(wParam,lParam);
		break;*/
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


CtrlRegisterList *CtrlRegisterList::getFrom(HWND hwnd)
{
    return (CtrlRegisterList *)GetWindowLongPtr(hwnd, GWLP_USERDATA);
}




CtrlRegisterList::CtrlRegisterList(HWND _wnd)
{
	wnd=_wnd;
	SetWindowLongPtr(wnd, GWLP_USERDATA, (LONG_PTR)this);
	//SetWindowLong(wnd, GWL_STYLE, GetWindowLong(wnd,GWL_STYLE) | WS_VSCROLL);
	//SetScrollRange(wnd, SB_VERT, -1,1,TRUE);
	font = CreateFont(12,0,0,0,FW_DONTCARE,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,DEFAULT_QUALITY,DEFAULT_PITCH,
		"Lucida Console");
	rowHeight=12;
	selecting=false;
	selection=0;
	category=0;
	showHex=false;
	cpu=0;
	lastPC = 0;
	lastCat0Values = NULL;
	changedCat0Regs = NULL;
}


CtrlRegisterList::~CtrlRegisterList()
{
	DeleteObject(font);
	if (lastCat0Values != NULL)
		delete [] lastCat0Values;
	if (changedCat0Regs != NULL)
		delete [] changedCat0Regs;
}

void fillRect(HDC hdc, RECT *rect, COLORREF colour);



//Yeah this truly turned into a mess with the latest additions.. but it sure looks nice ;)
void CtrlRegisterList::onPaint(WPARAM wParam, LPARAM lParam)
{
	if (!cpu) 
		return;

	GetClientRect(wnd, &rect);
	PAINTSTRUCT ps;
	HDC hdc;
	
	hdc = BeginPaint(wnd, &ps);
	// TODO: Add any drawing code here...
	int width = rect.right;
	//numRows=(numRows&(~1)) + 1;
	SetBkMode(hdc, TRANSPARENT);
	DWORD bgColor = 0xffffff;
	HPEN nullPen=CreatePen(0,0,bgColor);
	HPEN currentPen=CreatePen(0,0,0);
	HPEN selPen=CreatePen(0,0,0x808080);

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
//	HICON breakPoint = (HICON)LoadIcon(GetModuleHandle(0),(LPCSTR)IDI_STOP);
//	HICON breakPointDisable = (HICON)LoadIcon(GetModuleHandle(0),(LPCSTR)IDI_STOPDISABLE);

	int nc = cpu->GetNumCategories();
	for (int i=0; i<nc; i++)
	{
		SelectObject(hdc,i==category?currentPen:nullPen);
		SelectObject(hdc,i==category?pcBrush:nullBrush);
		Rectangle(hdc,width*i/nc,0,width*(i+1)/nc,rowHeight);
		const TCHAR *name = cpu->GetCategoryName(i);
		TextOut(hdc,width*i/nc,1,name,(int)strlen(name));
	}

	int numRows=rect.bottom/rowHeight;

	for (int i=0; i<numRows; i++)
	{
		int rowY1 = rowHeight*(i+1);
		int rowY2 = rowHeight*(i+2);


		lbr.lbColor = i==selection?0xffeee0:0xffffff;

		SelectObject(hdc,currentBrush);
		SelectObject(hdc,nullPen);
		Rectangle(hdc,0,rowY1,16,rowY2);

		if (selecting && i == selection)
			SelectObject(hdc,selPen);
		else
			SelectObject(hdc,nullPen);

		HBRUSH mojsBrush=CreateBrushIndirect(&lbr);
		SelectObject(hdc,mojsBrush);

		//else
		//	SelectObject(hdc,i==0 ? currentBrush : nullBrush);

		Rectangle(hdc,16,rowY1,width,rowY2);

		// Check for any changes in the registers.
		if (lastPC != cpu->GetPC())
		{
			for (int j = 0, n = cpu->GetNumRegsInCategory(0); j < n; ++j)
			{
				u32 v = cpu->GetRegValue(0, j);
				changedCat0Regs[j] = v != lastCat0Values[j];
				lastCat0Values[j] = v;
			}
			lastPC = cpu->GetPC();
		}

		SelectObject(hdc,currentBrush);
		DeleteObject(mojsBrush);
		if (i<cpu->GetNumRegsInCategory(category))
		{
			char temp[256];
			int temp_len = sprintf(temp,"%s",cpu->GetRegName(category,i));
			SetTextColor(hdc,0x600000);
			TextOut(hdc,17,rowY1,temp,temp_len);
			SetTextColor(hdc,0x000000);

			cpu->PrintRegValue(category,i,temp);
			if (category == 0 && changedCat0Regs[i])
				SetTextColor(hdc, 0x0000FF);
			else
				SetTextColor(hdc,0x004000);
			TextOut(hdc,77,rowY1,temp,(int)strlen(temp));
		}


		/*
			}
			SetTextColor(hdc,0x007000);

			TextOut(hdc,70,rowY1,dis,strlen(dis));
			if (desc[0]==0)
				strcpy(desc,debugger->getDescription(address));
			SetTextColor(hdc,0x0000FF);
			//char temp[256];
			//UnDecorateSymbolName(desc,temp,255,UNDNAME_COMPLETE);
			if (strlen(desc))
				TextOut(hdc,280,rowY1,desc,strlen(desc));
			if (debugger->isBreakpoint(address))
			{
				DrawIconEx(hdc,2,rowY1,breakPoint,32,32,0,0,DI_NORMAL);
			}
		}*/
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
	
//	DestroyIcon(breakPoint);
//	DestroyIcon(breakPointDisable);
	
	EndPaint(wnd, &ps);
}



void CtrlRegisterList::onKeyDown(WPARAM wParam, LPARAM lParam)
{
	RECT rect;
	GetClientRect(this->wnd, &rect);
	int page=(rect.bottom/rowHeight)/2-1;

	switch (wParam & 0xFFFF)
	{
	case VK_DOWN:
		selection++;
		break;
	case VK_UP:
		selection--;
		break;
	case VK_NEXT:
		selection+=4;
		break;
	case VK_PRIOR:
		selection-=4;
		break;
	default:
		return;
	}
	redraw();
}


void CtrlRegisterList::redraw()
{
	InvalidateRect(wnd, NULL, FALSE);
	UpdateWindow(wnd); 
}


void CtrlRegisterList::onMouseDown(WPARAM wParam, LPARAM lParam, int button)
{
	int x = (s16)LOWORD(lParam); 
	int y = (s16)HIWORD(lParam); 
	if (x>16)
	{
		oldSelection=selection;

		if (y>rowHeight)
		{
			selection=yToIndex(y);
			SetCapture(wnd);
			bool oldselecting=selecting;
			selecting=true;
			if (!oldselecting || (selection!=oldSelection))
				redraw();
		}
		else
		{
			RECT rc;
			SetCapture(wnd);
			GetWindowRect(wnd,&rc);
			int lastCat = category;
			category = (x*cpu->GetNumCategories())/(rc.right-rc.left);
			if (category<0) category=0;
			if (category>=cpu->GetNumCategories())
				category=cpu->GetNumCategories()-1;
			if (category!=lastCat)
				redraw();
		}
	}
	else
	{
		redraw();
	}
}

void CtrlRegisterList::onMouseUp(WPARAM wParam, LPARAM lParam, int button)
{
	if (button==2)
	{
		//popup menu?
		int cat = category;
		int reg = selection;
		if (selection >= cpu->GetNumRegsInCategory(cat))
			return;
		POINT pt;
		GetCursorPos(&pt);
		u32 val = cpu->GetRegValue(cat,reg);			
		switch(TrackPopupMenuEx(GetSubMenu(g_hPopupMenus,3),TPM_RIGHTBUTTON|TPM_RETURNCMD,pt.x,pt.y,wnd,0))
		{
		case ID_REGLIST_GOTOINMEMORYVIEW:
			for (int i=0; i<numCPUs; i++)
				if (memoryWindow[i])
					memoryWindow[i]->Goto(val);
			break;
		case ID_REGLIST_GOTOINDISASM:
			for (int i=0; i<numCPUs; i++)
				if (disasmWindow[i])
					disasmWindow[i]->Goto(val);
			break;
		case ID_REGLIST_COPYVALUE:
			{
				char temp[24];
				sprintf(temp,"%08x",val);
				W32Util::CopyTextToClipboard(wnd,temp);
			}
			break;
		case ID_REGLIST_CHANGE:
			{
				if (InputBox_GetHex(GetModuleHandle(NULL),wnd,"Set new value",val,val))
				{
					cpu->SetRegValue(cat,reg,val);
					redraw();
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
		selection=yToIndex(y);
		selecting=false;
		ReleaseCapture();
		redraw();
	}
}

void CtrlRegisterList::onMouseMove(WPARAM wParam, LPARAM lParam, int button)
{
	if (button&1)
	{
		int x = LOWORD(lParam); 
		int y = (signed short)HIWORD(lParam); 
//		if (x>16)
		{
/*
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
			else*/
			onMouseDown(wParam,lParam,1);
		}
	}
}	


int CtrlRegisterList::yToIndex(int y)
{
//	int ydiff=y-rect.bottom/2-rowHeight/2;
//	ydiff=(int)(floorf((float)ydiff / (float)rowHeight))+1;
//	return curAddress + ydiff * align;
	int n = (y/rowHeight) - 1;
	if (n<0) n=0;
	return n;
}
