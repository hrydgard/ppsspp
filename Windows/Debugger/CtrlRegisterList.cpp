#include <algorithm>
#include <cmath>

#include "Common/System/Display.h"
#include "Common/Data/Encoding/Utf8.h"
#include "Core/Config.h"
#include "Core/Core.h"
#include "Core/MemMap.h"
#include "Core/Reporting.h"
#include "Windows/W32Util/ContextMenu.h"
#include "Windows/W32Util/Misc.h"
#include "Windows/InputBox.h"
#include "Windows/resource.h"

#include "CtrlRegisterList.h"
#include "Debugger_MemoryDlg.h"

#include "Debugger_Disasm.h"
#include "DebuggerShared.h"

#include "Windows/main.h"

enum { REGISTER_PC = 32, REGISTER_HI, REGISTER_LO, REGISTERS_END };

constexpr const wchar_t *szClassName = L"CtrlRegisterList";

static constexpr UINT_PTR IDT_REDRAW = 0xC0DE0001;
static constexpr UINT REDRAW_DELAY = 1000 / 60;

void CtrlRegisterList::init()
{
	WNDCLASSEX wc;

	wc.cbSize = sizeof(wc);
	wc.lpszClassName = szClassName;
	wc.hInstance = GetModuleHandle(0);
	wc.lpfnWndProc = CtrlRegisterList::wndProc;
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	wc.hIcon = 0;
	wc.lpszMenuName = 0;
	wc.hbrBackground = (HBRUSH)GetSysColorBrush(COLOR_WINDOW);
	wc.style = CS_DBLCLKS;
	wc.cbClsExtra = 0;
	wc.cbWndExtra = sizeof(CtrlRegisterList *);
	wc.hIconSm = 0;

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
		if (ccp->cpu) ccp->scrollTo(ccp->scrollRow_);
		break;
	case WM_PAINT:
		ccp->onPaint(wParam,lParam);
		break;
	case WM_VSCROLL:
		ccp->onVScroll(wParam,lParam);
		break;
	case WM_MOUSEWHEEL:
		if (GET_WHEEL_DELTA_WPARAM(wParam) > 0) {
			ccp->scrollTo(ccp->scrollRow_ - 3);
		} else if (GET_WHEEL_DELTA_WPARAM(wParam) < 0) {
			ccp->scrollTo(ccp->scrollRow_ + 3);
		}
		break;
	case WM_ERASEBKGND:
		return FALSE;
	case WM_KEYDOWN:
		ccp->onKeyDown(wParam,lParam);
		return 0;
	case WM_KEYUP:
		if (wParam == VK_CONTROL) ccp->ctrlDown = false;
		return 0;
	case WM_LBUTTONDOWN: SetFocus(hwnd); lmbDown=true; ccp->onMouseDown(wParam,lParam,1); break;
	case WM_RBUTTONDOWN: rmbDown=true; ccp->onMouseDown(wParam,lParam,2); break;
	case WM_MOUSEMOVE:   ccp->onMouseMove(wParam,lParam,(lmbDown?1:0) | (rmbDown?2:0)); break;
	case WM_LBUTTONUP:   lmbDown=false; ccp->onMouseUp(wParam,lParam,1); break;
	case WM_RBUTTONUP:   rmbDown=false; ccp->onMouseUp(wParam,lParam,2); break;
	case WM_LBUTTONDBLCLK:	ccp->editRegisterValue(); break;
	case WM_SETFOCUS:
		SetFocus(hwnd);
		ccp->hasFocus=true;
		ccp->redraw();
		break;
	case WM_KILLFOCUS:
		ccp->hasFocus=false;
		ccp->redraw();
		break;
	case WM_GETDLGCODE:	// want chars so that we can return 0 on key press and supress the beeping sound
		return DLGC_WANTARROWS|DLGC_WANTCHARS;

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

CtrlRegisterList *CtrlRegisterList::getFrom(HWND hwnd)
{
	return (CtrlRegisterList *)GetWindowLongPtr(hwnd, GWLP_USERDATA);
}

CtrlRegisterList::CtrlRegisterList(HWND _wnd)
	: wnd(_wnd) {
	SetWindowLongPtr(wnd, GWLP_USERDATA, (LONG_PTR)this);
	SetWindowLong(wnd, GWL_STYLE, GetWindowLong(wnd,GWL_STYLE) | WS_VSCROLL);

	const float fontScale = 1.0f / g_display.dpi_scale_real_y;
	rowHeight = g_Config.iFontHeight * fontScale;
	charWidth = g_Config.iFontWidth * fontScale;
	if (charWidth < 1)
		charWidth = 1;
	font = CreateFont(rowHeight, charWidth, 0, 0,
		FW_DONTCARE, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH,
		L"Lucida Console");
}

CtrlRegisterList::~CtrlRegisterList()
{
	DeleteObject(font);
	delete [] lastCat0Values;
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
		int tabX = width * i / nc;
		Rectangle(hdc,tabX,0,width*(i+1)/nc,rowHeight);
		const char *name = cpu->GetCategoryName(i);
		// Clip to the tab so a narrow list doesn't get the labels running into each other.
		int tabChars = std::max((width * (i + 1) / nc - tabX) / charWidth, 1);
		TextOutA(hdc,tabX,1,name,std::min((int)strlen(name), tabChars));
	}

	int numRows=rect.bottom/rowHeight;

	// Column layout, in pixels. The font scales with the DPI and the vertical scrollbar takes a
	// bite out of the client width, while the control has a fixed width in the dialog - so derive
	// the value column from the client width instead of hardcoding it, or the values get cut off
	// at the right edge. kNameChars covers the widest register name in any category ("zero").
	constexpr int kNameChars = 5;
	const int nameX = 17;
	const int minValueX = nameX + kNameChars * charWidth;
	const int maxValueX = nameX + (kNameChars + 3) * charWidth;
	// Pull the column in far enough that a whole value fits - 8 digits for hex, more for floats.
	int valueX = width - 2 - (category == 0 ? 8 : 12) * charWidth;
	if (valueX > maxValueX)
		valueX = maxValueX;
	if (valueX < minValueX)
		valueX = minValueX;
	// How many characters actually fit. Anything longer gets clipped rather than spilling over.
	int valueChars = (width - 2 - valueX) / charWidth;
	if (valueChars < 1)
		valueChars = 1;

	SCROLLINFO si{ sizeof(si), SIF_RANGE | SIF_PAGE | SIF_POS | SIF_DISABLENOSCROLL };
	si.nMax = totalRows() - 1;
	si.nPage = visibleRows();
	si.nPos = scrollRow_;
	SetScrollInfo(wnd, SB_VERT, &si, TRUE);

	// Reading live CPU-thread-owned register state here on the GUI thread would otherwise race
	// with the CPU thread - hold g_frameMutex for the duration of the read, which NativeFrame()
	// also holds while it's actually touching that state. See g_frameMutex in Core.h.
	std::lock_guard<std::mutex> frameGuard(g_frameMutex);
	CoreShutdownLock coreLock = Core_LockAgainstShutdown();
	// The values are a moving target while the core is running - gray them out rather than trying
	// to highlight "changes" that are really just noise at that point.
	bool running = !Core_IsStepping();

	for (int i=scrollRow_; i<scrollRow_+numRows; i++)
	{
		int rowY1 = rowHeight*(i-scrollRow_+1);
		int rowY2 = rowY1+rowHeight;


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

			changedCat0Regs[REGISTER_PC] = cpu->GetPC() != lastCat0Values[REGISTER_PC];
			lastCat0Values[REGISTER_PC] = cpu->GetPC();
			changedCat0Regs[REGISTER_HI] = cpu->GetHi() != lastCat0Values[REGISTER_HI];
			lastCat0Values[REGISTER_HI] = cpu->GetHi();
			changedCat0Regs[REGISTER_LO] = cpu->GetLo() != lastCat0Values[REGISTER_LO];
			lastCat0Values[REGISTER_LO] = cpu->GetLo();

			lastPC = cpu->GetPC();
		}

		SelectObject(hdc,currentBrush);
		DeleteObject(mojsBrush);
		if (i<cpu->GetNumRegsInCategory(category))
		{
			char temp[256];
			int temp_len = snprintf(temp, sizeof(temp), "%s", cpu->GetRegName(category, i).c_str());
			SetTextColor(hdc, running ? 0x808080 : 0x600000);
			TextOutA(hdc,nameX,rowY1,temp,std::min(temp_len, kNameChars));

			cpu->PrintRegValue(category, i, temp, sizeof(temp));
			if (running)
				SetTextColor(hdc, 0x808080);
			else if (category == 0 && changedCat0Regs[i])
				SetTextColor(hdc, 0x0000FF);
			else
				SetTextColor(hdc,0x004000);
			TextOutA(hdc,valueX,rowY1,temp,std::min((int)strlen(temp), valueChars));
		} else if (category == 0 && i < REGISTERS_END)
		{
			char temp[256];
			int len;
			u32 value = -1;

			switch (i)
			{
			case REGISTER_PC:
				value = cpu->GetPC();
				len = snprintf(temp, sizeof(temp), "pc");
				break;
			case REGISTER_HI:
				value = cpu->GetHi();
				len = snprintf(temp, sizeof(temp), "hi");
				break;
			case REGISTER_LO:
				value = cpu->GetLo();
				len = snprintf(temp, sizeof(temp), "lo");
				break;
			default:
				temp[0] = '\0';
				len = 0;
				break;
			}

			SetTextColor(hdc, running ? 0x808080 : 0x600000);
			TextOutA(hdc,nameX,rowY1,temp,std::min(len, kNameChars));
			len = snprintf(temp, sizeof(temp), "%08X",value);
			if (running)
				SetTextColor(hdc, 0x808080);
			else if (changedCat0Regs[i])
				SetTextColor(hdc, 0x0000FF);
			else
				SetTextColor(hdc,0x004000);
			TextOutA(hdc,valueX,rowY1,temp,std::min(len, valueChars));
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
	
//	DestroyIcon(breakPoint);
//	DestroyIcon(breakPointDisable);
	
	EndPaint(wnd, &ps);
}



void CtrlRegisterList::onKeyDown(WPARAM wParam, LPARAM lParam)
{
	RECT rect;
	GetClientRect(this->wnd, &rect);

	if (ctrlDown && tolower(wParam) == 'c')
	{
		copyRegisterValue();
		return;
	}

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
	case VK_CONTROL:
		ctrlDown = true;
		break;
	default:
		return;
	}
	if (selection >= totalRows()) selection = totalRows() - 1;
	if (selection < 0) selection = 0;
	// Keep the selection in view.
	if (selection < scrollRow_)
		scrollTo(selection);
	else if (selection >= scrollRow_ + visibleRows())
		scrollTo(selection - visibleRows() + 1);
	redraw();
}

void CtrlRegisterList::onVScroll(WPARAM wParam, LPARAM lParam)
{
	switch (wParam & 0xFFFF)
	{
	case SB_LINEDOWN:
		scrollTo(scrollRow_ + 1);
		break;
	case SB_LINEUP:
		scrollTo(scrollRow_ - 1);
		break;
	case SB_PAGEDOWN:
		scrollTo(scrollRow_ + visibleRows());
		break;
	case SB_PAGEUP:
		scrollTo(scrollRow_ - visibleRows());
		break;
	case SB_THUMBTRACK:
	case SB_THUMBPOSITION:
		scrollTo(HIWORD(wParam));
		break;
	}
}

// Rows in the current category, including pc/hi/lo for the GPR tab.
int CtrlRegisterList::totalRows()
{
	return category == 0 ? REGISTERS_END : cpu->GetNumRegsInCategory(category);
}

// Rows that fit below the category header.
int CtrlRegisterList::visibleRows()
{
	GetClientRect(wnd, &rect);
	int rows = rect.bottom / rowHeight - 1;
	return rows < 1 ? 1 : rows;
}

void CtrlRegisterList::scrollTo(int row)
{
	int maxRow = totalRows() - visibleRows();
	if (row > maxRow) row = maxRow;
	if (row < 0) row = 0;
	scrollRow_ = row;
	redraw();
}


void CtrlRegisterList::redraw() {
	if (!redrawScheduled_) {
		SetTimer(wnd, IDT_REDRAW, REDRAW_DELAY, nullptr);
		redrawScheduled_ = true;
	}
}

u32 CtrlRegisterList::getSelectedRegValue(char *out, size_t size)
{
	int reg = selection;
	u32 val;

	if (selection >= cpu->GetNumRegsInCategory(category))
	{
		if (category != 0 || selection >= REGISTERS_END)
		{
			*out = '\0';
			return -1;
		}

		switch (selection)
		{
		case REGISTER_PC:
			val = cpu->GetPC();
			break;
		case REGISTER_HI:
			val = cpu->GetHi();
			break;
		case REGISTER_LO:
			val = cpu->GetLo();
			break;
		default:
			*out = '\0';
			return -1;
		}
	}
	else
		val = cpu->GetRegValue(category, reg);

	snprintf(out, size, "%08X", val);
	return val;
}

void CtrlRegisterList::copyRegisterValue()
{
	if (!Core_IsStepping())
	{
		MessageBox(wnd,L"Can't copy register values while the core is running.",L"Error",MB_OK);
		return;
	}

	char temp[24];
	getSelectedRegValue(temp, 24);
	W32Util::CopyTextToClipboard(wnd, temp);
}

void CtrlRegisterList::editRegisterValue()
{
	if (!Core_IsStepping())
	{
		MessageBox(wnd,L"Can't change registers while the core is running.",L"Error",MB_OK);
		return;
	}

	char temp[24];
	// Route the register read/mutation to the CPU thread instead of poking at it directly from
	// this GUI thread - see Core_RunOnCPUThread() in Core.h. InputBox_GetString() is modal, so it
	// must stay outside any queued callback, or we'd block the CPU thread on user input.
	u32 val = 0;
	Core_RunOnCPUThread([&] { val = getSelectedRegValue(temp, 24); });
	int reg = selection;

	std::string value = temp;
	if (InputBox_GetString(GetModuleHandle(NULL),wnd,L"Set new value",value,value)) {
		if (parseExpression(value.c_str(),cpu,val) == false) {
			displayExpressionError(wnd);
		} else {
			Core_RunOnCPUThread([&] {
				switch (reg)
				{
				case REGISTER_PC:
					cpu->SetPC(val);
					break;
				case REGISTER_HI:
					cpu->SetHi(val);
					break;
				case REGISTER_LO:
					cpu->SetLo(val);
					break;
				default:
					cpu->SetRegValue(category, reg, val);
					break;
				}
				Reporting::NotifyDebugger();
			});
			redraw();
			SendMessage(GetParent(wnd),WM_DEB_UPDATE,0,0);	// registers changed -> disassembly needs to be updated
		}
	}
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
			GetClientRect(wnd,&rc);
			int lastCat = category;
			category = (x*cpu->GetNumCategories())/(rc.right-rc.left);
			if (category<0) category=0;
			if (category>=cpu->GetNumCategories())
				category=cpu->GetNumCategories()-1;
			if (category!=lastCat)
				scrollTo(0);
		}
	}
	else
	{
		redraw();
	}
}

void CtrlRegisterList::onMouseUp(WPARAM wParam, LPARAM lParam, int button)
{
	int x = LOWORD(lParam);
	int y = HIWORD(lParam);

	if (button==2 && x>16)
	{
		//popup menu?
		int cat = category;
		int reg = selection;
		u32 val;
		if (selection < cpu->GetNumRegsInCategory(cat))
		{
			val = cpu->GetRegValue(cat, reg);
		}
		else if (cat == 0 && selection < REGISTERS_END)
		{
			switch (selection)
			{
			case REGISTER_PC:
				val = cpu->GetPC();
				break;
			case REGISTER_HI:
				val = cpu->GetHi();
				break;
			case REGISTER_LO:
				val = cpu->GetLo();
				break;
			default:
				return;
			}
		}
		else
		{
			return;
		}

		switch (TriggerContextMenu(ContextMenuID::REGLIST, wnd, ContextPoint::FromEvent(lParam)))
		{
		case ID_REGLIST_GOTOINMEMORYVIEW:
			SendMessage(GetParent(wnd),WM_DEB_GOTOHEXEDIT,val,0);
			break;
		case ID_REGLIST_GOTOINDISASM:
			if (disasmWindow)
				disasmWindow->Goto(val);
			break;
		case ID_REGLIST_COPYVALUE:
			copyRegisterValue();
			break;
		case ID_REGLIST_CHANGE:
			editRegisterValue();
			break;
		}
		return;
	}
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
//	int ydiff=y-rect.bottom/2-rowHeight_/2;
//	ydiff=(int)(floorf((float)ydiff / (float)rowHeight_))+1;
//	return curAddress + ydiff * align;
	int n = (y/rowHeight) - 1 + scrollRow_;
	if (n<0) n=0;
	return n;
}
