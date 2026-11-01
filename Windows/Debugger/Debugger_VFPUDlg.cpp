#include "Common/CommonWindows.h"
#include <windowsx.h>
#include <commctrl.h>
#include <algorithm>
#include <mutex>
#include "Windows/resource.h"
#include "Common/Data/Encoding/Utf8.h"
#include "Common/System/Display.h"

#include "Core/Config.h"
#include "Core/Core.h"
#include "Core/MIPS/MIPS.h"
#include "Core/MIPS/MIPSVFPUUtils.h"
#include "Windows/Debugger/Debugger_VFPUDlg.h"

CVFPUDlg::CVFPUDlg(HINSTANCE _hInstance, HWND _hParent, DebugInterface *cpu_) : Dialog((LPCSTR)IDD_VFPU, _hInstance,_hParent)
{
	cpu = cpu_;
	SetWindowText(m_hDlg, L"VFPU");

	ShowWindow(m_hDlg,SW_HIDE);

	// The same font the other debugger views use, scaled by the DPI.
	const float fontScale = 1.0f / g_display.dpi_scale_real_y;
	int fontHeight = (int)(g_Config.iFontHeight * fontScale);
	int fontWidth = std::max((int)(g_Config.iFontWidth * fontScale), 1);
	font = CreateFont(fontHeight, fontWidth, 0, 0, FW_DONTCARE, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH,
		L"Lucida Console");

	HWND tabs = GetDlgItem(m_hDlg, IDC_TABDATATYPE);

	TCITEM tcItem{};
	tcItem.mask = TCIF_TEXT;
	for (int i = 0; i < (int)VFPUDisplayMode::COUNT; i++) {
		std::wstring name = ConvertUTF8ToWString(g_vfpuDisplayModeNames[i]);
		tcItem.pszText = (LPTSTR)name.c_str();
		TabCtrl_InsertItem(tabs, i, &tcItem);
	}
	mode = 0;

	ComputeLayout();

	// The dialog template's size is in dialog units, which don't follow the font above, so size the
	// window to fit the grid instead.
	RECT rc{ 0, 0, layout_.xStart + layout_.columnWidth * 4 + layout_.pad, layout_.yStart + (int)(layout_.rowHeight * 5.5f * 8) };
	AdjustWindowRectEx(&rc, GetWindowLong(m_hDlg, GWL_STYLE), FALSE, GetWindowLong(m_hDlg, GWL_EXSTYLE));
	SetWindowPos(m_hDlg, nullptr, 0, 0, rc.right - rc.left, rc.bottom - rc.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

CVFPUDlg::~CVFPUDlg()
{
	DeleteObject(font);
}

void CVFPUDlg::ComputeLayout() {
	HDC hdc = GetDC(m_hDlg);
	HFONT oldFont = (HFONT)SelectObject(hdc, font);
	TEXTMETRIC tm{};
	GetTextMetrics(hdc, &tm);
	SelectObject(hdc, oldFont);
	ReleaseDC(m_hDlg, hdc);

	const int charWidth = std::max((int)tm.tmAveCharWidth, 1);
	layout_.pad = std::max(charWidth / 2, 2);
	layout_.rowHeight = tm.tmHeight + layout_.pad;
	// Labels are like "M000" and "R000". Cells fit the 11 characters of the Bytes and Ints modes,
	// longer values get clipped.
	layout_.xStart = charWidth * 4 + layout_.pad * 2;
	layout_.columnWidth = charWidth * 11 + layout_.pad * 2;

	RECT tabRect;
	GetWindowRect(GetDlgItem(m_hDlg, IDC_TABDATATYPE), &tabRect);
	MapWindowPoints(HWND_DESKTOP, m_hDlg, (LPPOINT)&tabRect, 2);
	layout_.yStart = tabRect.bottom + layout_.pad;
}

void CVFPUDlg::Update()
{
	if (m_hDlg != NULL)
	{
		InvalidateRect(m_hDlg, 0,0);
	}
}

void CVFPUDlg::Paint(HDC hdc) {
	u32 values[128];
	{
		// The registers belong to the CPU thread. See g_frameMutex in Core.h.
		std::lock_guard<std::mutex> frameGuard(g_frameMutex);
		CoreShutdownLock coreLock = Core_LockAgainstShutdown();
		for (int i = 0; i < 128; i++) {
			values[i] = currentMIPS->vi[voffset[i]];
		}
	}

	SetBkMode(hdc, TRANSPARENT);
	LOGBRUSH lbr{};
	lbr.lbColor = 0xffffff;
	HBRUSH whiteBrush = CreateBrushIndirect(&lbr);
	HPEN blackPen = CreatePen(0, 0, 0);
	HPEN oldPen = (HPEN)SelectObject(hdc, blackPen);
	HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, whiteBrush);
	HFONT oldFont = (HFONT)SelectObject(hdc, font);

	const int rowHeight = layout_.rowHeight;
	const int columnWidth = layout_.columnWidth;
	const int xStart = layout_.xStart;
	const int pad = layout_.pad;

	auto drawCell = [&](int x1, int y1, int x2, const char *text) {
		Rectangle(hdc, x1, y1, x2 + 1, y1 + rowHeight + 1);
		RECT clip{ x1 + 1, y1 + 1, x2, y1 + rowHeight };
		ExtTextOutA(hdc, x1 + pad, y1 + pad / 2 + 1, ETO_CLIPPED, &clip, text, (UINT)strlen(text), nullptr);
	};

	char temp[64];
	for (int matrix = 0; matrix < 8; matrix++) {
		int my = layout_.yStart + (int)(matrix * rowHeight * 5.5f);
		snprintf(temp, sizeof(temp), "M%i00", matrix);
		drawCell(0, my, xStart, temp);

		for (int column = 0; column < 4; column++) {
			int x = column * columnWidth + xStart;
			snprintf(temp, sizeof(temp), "R%i0%i", matrix, column);
			drawCell(x, my, x + columnWidth, temp);

			snprintf(temp, sizeof(temp), "C%i%i0", matrix, column);
			drawCell(0, my + rowHeight * (column + 1), xStart, temp);

			for (int row = 0; row < 4; row++) {
				FormatVFPUValue(temp, sizeof(temp), values[column * 32 + row + matrix * 4], (VFPUDisplayMode)mode);
				drawCell(x, my + rowHeight * (row + 1), x + columnWidth, temp);
			}
		}
	}

	SelectObject(hdc, oldFont);
	SelectObject(hdc, oldPen);
	SelectObject(hdc, oldBrush);
	DeleteObject(blackPen);
	DeleteObject(whiteBrush);
}

BOOL CVFPUDlg::DlgProc(UINT message, WPARAM wParam, LPARAM lParam)
{
	switch(message)
	{
	case WM_INITDIALOG:
		return TRUE;

	case WM_NOTIFY:
		{
			HWND tabs = GetDlgItem(m_hDlg, IDC_TABDATATYPE);
			NMHDR *pNotifyMessage = (LPNMHDR)lParam;
			if (pNotifyMessage->hwndFrom == tabs)
			{
				mode = TabCtrl_GetCurSel(tabs);
				InvalidateRect(m_hDlg,0,0);
			}
			break;
		}

	case WM_PAINT:
		{
			PAINTSTRUCT p;
			HDC hdc = BeginPaint(m_hDlg, &p);
			Paint(hdc);
			EndPaint(m_hDlg, &p);
		}
		break;

	case WM_CLOSE:
		Show(false);
		break;
	}

	return 0;
}
