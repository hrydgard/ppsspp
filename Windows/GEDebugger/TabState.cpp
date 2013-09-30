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

enum CmdFormatType {
	CMD_FMT_HEX = 0,
	CMD_FMT_NUM,
	CMD_FMT_FLOAT24,
	CMD_FMT_PTRWIDTH,
	CMD_FMT_XY,
	CMD_FMT_XYXY,
	CMD_FMT_XYZ,
};

struct TabStateRow {
	const TCHAR *title;
	u8 cmd;
	CmdFormatType fmt;
	u8 enableCmd;
	u8 otherCmd;
	u8 otherCmd2;
};

static const TabStateRow stateFlagsRows[] = {
	{ L"Lighting enable",      GE_CMD_LIGHTINGENABLE,          CMD_FMT_NUM },
	{ L"Light 0 enable",       GE_CMD_LIGHTENABLE0,            CMD_FMT_NUM },
	{ L"Light 1 enable",       GE_CMD_LIGHTENABLE1,            CMD_FMT_NUM },
	{ L"Light 2 enable",       GE_CMD_LIGHTENABLE2,            CMD_FMT_NUM },
	{ L"Light 3 enable",       GE_CMD_LIGHTENABLE3,            CMD_FMT_NUM },
	{ L"Clip enable",          GE_CMD_CLIPENABLE,              CMD_FMT_NUM },
	{ L"Cullface enable",      GE_CMD_CULLFACEENABLE,          CMD_FMT_NUM },
	{ L"Texture map enable",   GE_CMD_TEXTUREMAPENABLE,        CMD_FMT_NUM },
	{ L"Fog enable",           GE_CMD_FOGENABLE,               CMD_FMT_NUM },
	{ L"Dither enable",        GE_CMD_DITHERENABLE,            CMD_FMT_NUM },
	{ L"Alpha blend enable",   GE_CMD_ALPHABLENDENABLE,        CMD_FMT_NUM },
	{ L"Alpha test enable",    GE_CMD_ALPHATESTENABLE,         CMD_FMT_NUM },
	{ L"Depth test enable",    GE_CMD_ZTESTENABLE,             CMD_FMT_NUM },
	{ L"Stencil test enable",  GE_CMD_STENCILTESTENABLE,       CMD_FMT_NUM },
	{ L"Antialias enable",     GE_CMD_ANTIALIASENABLE,         CMD_FMT_NUM },
	{ L"Patch cull enable",    GE_CMD_PATCHCULLENABLE,         CMD_FMT_NUM },
	{ L"Color test enable",    GE_CMD_COLORTESTENABLE,         CMD_FMT_NUM },
	{ L"Logic Op Enable",      GE_CMD_LOGICOPENABLE,           CMD_FMT_NUM },
};

static const TabStateRow stateLightingRows[] = {
	{ L"Light mode",           GE_CMD_LIGHTMODE,               CMD_FMT_NUM, GE_CMD_LIGHTINGENABLE },
	{ L"Light type 0",         GE_CMD_LIGHTTYPE0,              CMD_FMT_NUM, GE_CMD_LIGHTENABLE0 },
	{ L"Light type 1",         GE_CMD_LIGHTTYPE1,              CMD_FMT_NUM, GE_CMD_LIGHTENABLE1 },
	{ L"Light type 2",         GE_CMD_LIGHTTYPE2,              CMD_FMT_NUM, GE_CMD_LIGHTENABLE2 },
	{ L"Light type 3",         GE_CMD_LIGHTTYPE3,              CMD_FMT_NUM, GE_CMD_LIGHTENABLE3 },
	{ L"Light pos 0",          GE_CMD_LX0,                     CMD_FMT_XYZ, GE_CMD_LIGHTENABLE0, GE_CMD_LY0, GE_CMD_LZ0 },
	{ L"Light pos 1",          GE_CMD_LX1,                     CMD_FMT_XYZ, GE_CMD_LIGHTENABLE1, GE_CMD_LY1, GE_CMD_LZ1 },
	{ L"Light pos 2",          GE_CMD_LX2,                     CMD_FMT_XYZ, GE_CMD_LIGHTENABLE2, GE_CMD_LY2, GE_CMD_LZ2 },
	{ L"Light pos 3",          GE_CMD_LX3,                     CMD_FMT_XYZ, GE_CMD_LIGHTENABLE3, GE_CMD_LY3, GE_CMD_LZ3 },
	// TODO: Others...
};

static const TabStateRow stateTextureRows[] = {
	{ L"CLUT",                 GE_CMD_CLUTADDR,                CMD_FMT_PTRWIDTH, GE_CMD_TEXTUREMAPENABLE, GE_CMD_CLUTADDRUPPER },
	{ L"Texture L0 addr",      GE_CMD_TEXADDR0,                CMD_FMT_PTRWIDTH, GE_CMD_TEXTUREMAPENABLE, GE_CMD_TEXBUFWIDTH0 },
	{ L"Texture L1 addr",      GE_CMD_TEXADDR1,                CMD_FMT_PTRWIDTH, GE_CMD_TEXTUREMAPENABLE, GE_CMD_TEXBUFWIDTH1 },
	{ L"Texture L2 addr",      GE_CMD_TEXADDR2,                CMD_FMT_PTRWIDTH, GE_CMD_TEXTUREMAPENABLE, GE_CMD_TEXBUFWIDTH2 },
	{ L"Texture L3 addr",      GE_CMD_TEXADDR3,                CMD_FMT_PTRWIDTH, GE_CMD_TEXTUREMAPENABLE, GE_CMD_TEXBUFWIDTH3 },
	{ L"Texture L4 addr",      GE_CMD_TEXADDR4,                CMD_FMT_PTRWIDTH, GE_CMD_TEXTUREMAPENABLE, GE_CMD_TEXBUFWIDTH4 },
	{ L"Texture L5 addr",      GE_CMD_TEXADDR5,                CMD_FMT_PTRWIDTH, GE_CMD_TEXTUREMAPENABLE, GE_CMD_TEXBUFWIDTH5 },
	{ L"Texture L6 addr",      GE_CMD_TEXADDR6,                CMD_FMT_PTRWIDTH, GE_CMD_TEXTUREMAPENABLE, GE_CMD_TEXBUFWIDTH6 },
	{ L"Texture L7 addr",      GE_CMD_TEXADDR7,                CMD_FMT_PTRWIDTH, GE_CMD_TEXTUREMAPENABLE, GE_CMD_TEXBUFWIDTH7 },
	// TODO: Others...
};

static const TabStateRow stateSettingsRows[] = {
	{ L"Framebuffer",          GE_CMD_FRAMEBUFPTR,             CMD_FMT_PTRWIDTH, 0, GE_CMD_FRAMEBUFWIDTH },
	{ L"Framebuffer format",   GE_CMD_FRAMEBUFPIXFORMAT,       CMD_FMT_NUM },
	{ L"Depthbuffer",          GE_CMD_ZBUFPTR,                 CMD_FMT_PTRWIDTH, 0, GE_CMD_ZBUFWIDTH },
	{ L"Region",               GE_CMD_REGION1,                 CMD_FMT_XYXY, 0, GE_CMD_REGION2 },
	// TODO: Right place?
	{ L"Morph Weight 0",       GE_CMD_MORPHWEIGHT0,            CMD_FMT_FLOAT24 },
	// TODO: Others...
};

CtrlStateValues::CtrlStateValues(const TabStateRow *rows, int rowCount, HWND hwnd)
	: GenericListControl(hwnd, stateValuesCols, ARRAY_SIZE(stateValuesCols)),
	  rows_(rows), rowCount_(rowCount) {
	Update();
}

void FormatStateRow(wchar_t *dest, const TabStateRow &info, u32 value, bool enabled, u32 otherValue, u32 otherValue2) {
	const wchar_t *fmtString;
	switch (info.fmt) {
	case CMD_FMT_HEX:
		fmtString = enabled ? L"%06x" : L"(%06x)";
		swprintf(dest, fmtString, value);
		break;

	case CMD_FMT_NUM:
		fmtString = enabled ? L"%d" : L"(%d)";
		swprintf(dest, fmtString, value);
		break;

	case CMD_FMT_FLOAT24:
		fmtString = enabled ? L"%f" : L"(%f)";
		swprintf(dest, fmtString, getFloat24(value));
		break;

	case CMD_FMT_PTRWIDTH:
		value |= (otherValue & 0x00FF0000) << 8;
		otherValue &= 0xFFFF;
		fmtString = enabled ? L"%08x, w=%d" : L"(%08x, w=%d)";
		swprintf(dest, fmtString, value, otherValue);
		break;

	case CMD_FMT_XY:
		{
			int x = value & 0x3FF;
			int y = value >> 10;
			fmtString = enabled ? L"%d,%d" : L"(%d,%d)";
			swprintf(dest, fmtString, x, y);
		}
		break;

	case CMD_FMT_XYXY:
		{
			int x1 = value & 0x3FF;
			int y1 = value >> 10;
			int x2 = otherValue & 0x3FF;
			int y2 = otherValue >> 10;
			fmtString = enabled ? L"%d,%d - %d,%d" : L"(%d,%d - %d,%d)";
			swprintf(dest, fmtString, x1, y1, x2, y2);
		}
		break;

	case CMD_FMT_XYZ:
		{
			float x = getFloat24(value);
			float y = getFloat24(otherValue);
			float z = getFloat24(otherValue2);
			fmtString = enabled ? L"%f, %f, %f" : L"(%f, %f, %f)";
			swprintf(dest, fmtString, x, y, z);
		}
		break;

	default:
		swprintf(dest, L"BAD FORMAT %08x (%d)", value, enabled);
	}
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
			const u32 value = state.cmdmem[info.cmd] & 0xFFFFFF;
			const u32 otherValue = state.cmdmem[info.otherCmd] & 0xFFFFFF;
			const u32 otherValue2 = state.cmdmem[info.otherCmd2] & 0xFFFFFF;

			FormatStateRow(dest, info, value, enabled, otherValue, otherValue2);
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

TabStateTexture::TabStateTexture(HINSTANCE _hInstance, HWND _hParent)
	: TabStateValues(stateTextureRows, ARRAY_SIZE(stateTextureRows), (LPCSTR)IDD_GEDBG_TAB_VALUES, _hInstance, _hParent) {
}
