// Copyright (c) 2012- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

#include "base/basictypes.h"
#include "Windows/resource.h"
#include "Windows/GEDebugger/TabState.h"
#include "GPU/GPUState.h"
#include "GPU/Common/GPUDebugInterface.h"

static const GenericListViewColumn stateValuesCols[] = {
	{ L"Name", 0.50f },
	{ L"Value", 0.50f },
};

enum StateValuesCols {
	STATEVALUES_COL_NAME,
	STATEVALUES_COL_VALUE,
};

struct TabStateRow {
	const TCHAR *title;
	u8 cmd;
	u8 enableCmd;
	// TODO: Format type?
};

static const TabStateRow stateFlagsRows[] = {
	{ L"Lighting enable",      GE_CMD_LIGHTINGENABLE },
	{ L"Light 0 enable",       GE_CMD_LIGHTENABLE0 },
	{ L"Light 1 enable",       GE_CMD_LIGHTENABLE1 },
	{ L"Light 2 enable",       GE_CMD_LIGHTENABLE2 },
	{ L"Light 3 enable",       GE_CMD_LIGHTENABLE3 },
	{ L"Clip enable",          GE_CMD_CLIPENABLE },
	{ L"Cullface enable",      GE_CMD_CULLFACEENABLE },
	{ L"Texture map enable",   GE_CMD_TEXTUREMAPENABLE },
	{ L"Fog enable",           GE_CMD_FOGENABLE },
	{ L"Dither enable",        GE_CMD_DITHERENABLE },
	{ L"Alpha blend enable",   GE_CMD_ALPHABLENDENABLE },
	{ L"Alpha test enable",    GE_CMD_ALPHATESTENABLE },
	{ L"Depth test enable",    GE_CMD_ZTESTENABLE },
	{ L"Stencil test enable",  GE_CMD_STENCILTESTENABLE },
	{ L"Antialias enable",     GE_CMD_ANTIALIASENABLE },
	{ L"Patch cull enable",    GE_CMD_PATCHCULLENABLE },
	{ L"Color test enable",    GE_CMD_COLORTESTENABLE },
	{ L"Logic Op Enable",      GE_CMD_LOGICOPENABLE },
};

static const TabStateRow stateLightingRows[] = {
	{ L"Light mode",           GE_CMD_LIGHTMODE,               GE_CMD_LIGHTINGENABLE },
	{ L"Light type 0",         GE_CMD_LIGHTTYPE0,              GE_CMD_LIGHTENABLE0 },
	{ L"Light type 1",         GE_CMD_LIGHTTYPE1,              GE_CMD_LIGHTENABLE1 },
	{ L"Light type 2",         GE_CMD_LIGHTTYPE2,              GE_CMD_LIGHTENABLE2 },
	{ L"Light type 3",         GE_CMD_LIGHTTYPE3,              GE_CMD_LIGHTENABLE3 },
	// TODO: Others...
};

static const TabStateRow stateSettingsRows[] = {
	{ L"Region TL",            GE_CMD_REGION1 },
	{ L"Region BR",            GE_CMD_REGION2 },
	// TODO: Right place?
	{ L"Morph Weight 0",       GE_CMD_MORPHWEIGHT0 },
	// TODO: Others...
};

CtrlStateValues::CtrlStateValues(const TabStateRow *rows, int rowCount, HWND hwnd)
	: GenericListControl(hwnd, stateValuesCols, ARRAY_SIZE(stateValuesCols)),
	  rows_(rows), rowCount_(rowCount) {
	Update();
}

void CtrlStateValues::GetColumnText(wchar_t *dest, int row, int col) {
	switch (col) {
	case STATEVALUES_COL_NAME:
		wcscpy(dest, rows_[row].title);
		break;

	case STATEVALUES_COL_VALUE:
		{
			if (gpuDebug == NULL) {
				wcscpy(dest, L"N/A");
				break;
			}

			const auto info = rows_[row];
			const auto state = gpuDebug->GetGState();
			const bool enabled = info.enableCmd == 0 || (state.cmdmem[info.enableCmd] & 1) == 1;

			// TODO: Format better.
			if (enabled) {
				wsprintf(dest, L"%06x", state.cmdmem[info.cmd] & 0xFFFFFF);
			} else {
				wsprintf(dest, L"(%06x)", state.cmdmem[info.cmd] & 0xFFFFFF);
			}
			break;
		}
	}
	
}

TabStateValues::TabStateValues(const TabStateRow *rows, int rowCount, LPCSTR dialogID, HINSTANCE _hInstance, HWND _hParent)
	: Dialog(dialogID, _hInstance, _hParent) {
	values = new CtrlStateValues(rows, rowCount, GetDlgItem(m_hDlg, IDC_GEDBG_VALUES));
}

TabStateValues::~TabStateValues() {
	delete values;
}

void TabStateValues::UpdateSize(WORD width, WORD height) {
	struct Position {
		int x,y;
		int w,h;
	};

	Position position;
	static const int borderMargin = 5;

	position.x = borderMargin;
	position.y = borderMargin;
	position.w = width - 2 * borderMargin;
	position.h = height - 2 * borderMargin;

	HWND handle = GetDlgItem(m_hDlg,IDC_GEDBG_VALUES);
	MoveWindow(handle, position.x, position.y, position.w, position.h, TRUE);
}

BOOL TabStateValues::DlgProc(UINT message, WPARAM wParam, LPARAM lParam) {
	switch (message) {
	case WM_INITDIALOG:
		return TRUE;

	case WM_SIZE:
		UpdateSize(LOWORD(lParam), HIWORD(lParam));
		return TRUE;

	case WM_NOTIFY:
		switch (wParam)
		{
		case IDC_GEDBG_VALUES:
			values->HandleNotify(lParam);
			break;
		}
		break;
	}

	return FALSE;
}

TabStateFlags::TabStateFlags(HINSTANCE _hInstance, HWND _hParent)
	: TabStateValues(stateFlagsRows, ARRAY_SIZE(stateFlagsRows), (LPCSTR)IDD_GEDBG_TAB_VALUES, _hInstance, _hParent) {
}

TabStateLighting::TabStateLighting(HINSTANCE _hInstance, HWND _hParent)
	: TabStateValues(stateLightingRows, ARRAY_SIZE(stateLightingRows), (LPCSTR)IDD_GEDBG_TAB_VALUES, _hInstance, _hParent) {
}

TabStateSettings::TabStateSettings(HINSTANCE _hInstance, HWND _hParent)
	: TabStateValues(stateSettingsRows, ARRAY_SIZE(stateSettingsRows), (LPCSTR)IDD_GEDBG_TAB_VALUES, _hInstance, _hParent) {
}
