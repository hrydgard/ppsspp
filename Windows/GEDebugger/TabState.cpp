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

#include "Common/CommonWindows.h"
#include <commctrl.h>
#include <array>
#include "Common/CommonFuncs.h"
#include "Common/CommonTypes.h"
#include "Common/System/Request.h"
#include "Common/Data/Encoding/Utf8.h"
#include "Common/Data/Text/Parsers.h"
#include "Common/Log.h"
#include "Common/StringUtils.h"
#include "Windows/resource.h"
#include "Windows/InputBox.h"
#include "Windows/GEDebugger/GEDebugger.h"
#include "Windows/GEDebugger/TabState.h"
#include "Windows/W32Util/ContextMenu.h"
#include "GPU/GPUState.h"
#include "GPU/GeDisasm.h"
#include "GPU/Common/GPUDebugInterface.h"
#include "GPU/Debugger/Breakpoints.h"
#include "GPU/Debugger/Stepping.h"
#include "GPU/Debugger/State.h"

using namespace GPUBreakpoints;

// First column is the breakpoint icon.
static const GenericListViewColumn stateValuesCols[] = {
	{ L"", 0.03f },
	{ L"Name", 0.40f },
	{ L"Value", 0.57f },
};

GenericListViewDef stateValuesListDef = {
	stateValuesCols,
	ARRAY_SIZE(stateValuesCols),
	nullptr,
	false,
};

enum StateValuesCols {
	STATEVALUES_COL_BREAKPOINT,
	STATEVALUES_COL_NAME,
	STATEVALUES_COL_VALUE,
};

static std::vector<GECommand> watchList;

static void ToggleWatchList(const GECommand cmd) {
	for (size_t i = 0; i < watchList.size(); ++i) {
		if (watchList[i] == cmd) {
			watchList.erase(watchList.begin() + i);
			return;
		}
	}
	watchList.push_back(cmd);
}

bool PromptStateValue(const GECmdInfo &info, HWND hparent, const char *title, u32 &value) {
	wchar_t wtitle[1024];
	ConvertUTF8ToWString(wtitle, ARRAY_SIZE(wtitle), title);

	if (info.fmt == CMD_FMT_FLOAT24 || info.fmt == CMD_FMT_XYZ) {
		union {
			u32 u;
			float f;
		} temp = { value << 8 };

		std::string strvalue = StringFromFormat("%f", temp.f);
		bool res = InputBox_GetString(GetModuleHandle(NULL), hparent, wtitle, strvalue, strvalue);
		if (!res)
			return false;

		// Okay, the result could be a simple float, hex (0x...), or invalid.
		if (sscanf(strvalue.c_str(), "0x%08x", &value) == 1)
			return true;
		if (sscanf(strvalue.c_str(), "%f", &temp.f) == 1) {
			value = temp.u >> 8;
			return true;
		}
		return false;
	}
	return InputBox_GetHex(GetModuleHandle(NULL), hparent, wtitle, value, value);
}

CtrlStateValues::CtrlStateValues(const GECommand *rows, int rowCount, HWND hwnd)
	: GenericListControl(hwnd, stateValuesListDef),
	  rows_(rows), rowCount_(rowCount) {
	SetIconList(12, 12, { (HICON)LoadIcon(GetModuleHandle(nullptr), (LPCWSTR)IDI_BREAKPOINT_SMALL) });
	Update();
}

void CtrlStateValues::GetColumnText(wchar_t *dest, size_t destSize, int row, int col) {
	if (row < 0 || row >= rowCount_) {
		return;
	}

	switch (col) {
	case STATEVALUES_COL_BREAKPOINT:
		wcscpy(dest, L" ");
		break;

	case STATEVALUES_COL_NAME:
	{
		ConvertUTF8ToWString(dest, destSize, GECmdInfoByCmd(rows_[row]).uiName);
		break;
	}
		
	case STATEVALUES_COL_VALUE:
		{
			if (!gpuDebug) {
				wcscpy(dest, L"N/A");
				break;
			}

			const auto info = GECmdInfoByCmd(rows_[row]);
			const auto state = gpuDebug->GetGState();
			const bool enabled = info.enableCmd == 0 || (state.cmdmem[info.enableCmd] & 1) == 1;
			const u32 value = state.cmdmem[info.cmd] & 0xFFFFFF;
			const u32 otherValue = state.cmdmem[info.otherCmd] & 0xFFFFFF;
			const u32 otherValue2 = state.cmdmem[info.otherCmd2] & 0xFFFFFF;
			char temp[256];
			FormatStateRow(gpuDebug, temp, sizeof(temp), info.fmt, value, enabled, otherValue, otherValue2);
			ConvertUTF8ToWString(dest, destSize, temp);
			break;
		}
	}
}

void CtrlStateValues::OnDoubleClick(int row, int column) {
	if (gpuDebug == nullptr || row >= rowCount_) {
		return;
	}

	const GECmdInfo &info = GECmdInfoByCmd(rows_[row]);

	if (column == STATEVALUES_COL_BREAKPOINT) {
		bool proceed = true;
		if (GetCmdBreakpointCond(info.cmd, nullptr)) {
			int ret = MessageBox(GetHandle(), L"This breakpoint has a custom condition.\nDo you want to remove it?", L"Confirmation", MB_YESNO);
			proceed = ret == IDYES;
		}
		if (proceed)
			SetItemState(row, ToggleBreakpoint(info) ? 1 : 0);
		return;
	}

	switch (info.fmt) {
	case CMD_FMT_FLAG:
		{
			const auto state = gpuDebug->GetGState();
			u32 newValue = state.cmdmem[info.cmd] ^ 1;
			SetCmdValue(newValue);
		}
		break;

	default:
		{
			char title[1024];
			const auto state = gpuDebug->GetGState();

			u32 newValue = state.cmdmem[info.cmd] & 0x00FFFFFF;
			snprintf(title, sizeof(title), "New value for %.*s", (int)info.uiName.size(), info.uiName.data());
			if (PromptStateValue(info, GetHandle(), title, newValue)) {
				newValue |= state.cmdmem[info.cmd] & 0xFF000000;
				SetCmdValue(newValue);

				if (info.otherCmd) {
					newValue = state.cmdmem[info.otherCmd] & 0x00FFFFFF;
					snprintf(title, sizeof(title), "New value for %.*s (secondary)", (int)info.uiName.size(), info.uiName.data());
					if (PromptStateValue(info, GetHandle(), title, newValue)) {
						newValue |= state.cmdmem[info.otherCmd] & 0xFF000000;
						SetCmdValue(newValue);

						if (info.otherCmd2) {
							newValue = state.cmdmem[info.otherCmd2] & 0x00FFFFFF;
							snprintf(title, sizeof(title), "New value for %.*s (tertiary)", (int)info.uiName.size(), info.uiName.data());
							if (PromptStateValue(info, GetHandle(), title, newValue)) {
								newValue |= state.cmdmem[info.otherCmd2] & 0xFF000000;
								SetCmdValue(newValue);
							}
						}
					}
				}
			}
		}
		break;
	}
}

void CtrlStateValues::OnRightClick(int row, int column, const POINT &point) {
	if (gpuDebug == nullptr) {
		return;
	}

	const GECommand cmd = rows_[row];
	const GECmdInfo &info = GECmdInfoByCmd(cmd);

	const auto state = gpuDebug->GetGState();

	POINT screenPt(point);
	ClientToScreen(GetHandle(), &screenPt);

	HMENU subMenu = GetContextMenu(ContextMenuID::GEDBG_STATE);
	SetMenuDefaultItem(subMenu, ID_REGLIST_CHANGE, FALSE);
	EnableMenuItem(subMenu, ID_GEDBG_SETCOND, GPUBreakpoints::IsCmdBreakpoint(info.cmd) ? MF_ENABLED : MF_GRAYED);

	// Ehh, kinda ugly.
	if (!watchList.empty() && rows_ == &watchList[0]) {
		ModifyMenu(subMenu, ID_GEDBG_WATCH, MF_BYCOMMAND | MF_STRING, ID_GEDBG_WATCH, L"Remove Watch");
	} else {
		ModifyMenu(subMenu, ID_GEDBG_WATCH, MF_BYCOMMAND | MF_STRING, ID_GEDBG_WATCH, L"Add Watch");
	}
	if (info.fmt == CMD_FMT_FLAG) {
		ModifyMenu(subMenu, ID_REGLIST_CHANGE, MF_BYCOMMAND | MF_STRING, ID_REGLIST_CHANGE, L"Toggle Flag");
	} else {
		ModifyMenu(subMenu, ID_REGLIST_CHANGE, MF_BYCOMMAND | MF_STRING, ID_REGLIST_CHANGE, L"Change...");
	}

	switch (TriggerContextMenu(ContextMenuID::GEDBG_STATE, GetHandle(), ContextPoint::FromClient(point)))
	{
	case ID_DISASM_TOGGLEBREAKPOINT: {
		bool proceed = true;
		if (GetCmdBreakpointCond(info.cmd, nullptr)) {
			int ret = MessageBox(GetHandle(), L"This breakpoint has a custom condition.\nDo you want to remove it?", L"Confirmation", MB_YESNO);
			proceed = ret == IDYES;
		}
		if (proceed)
			SetItemState(row, ToggleBreakpoint(info) ? 1 : 0);
		break;
	}

	case ID_GEDBG_SETCOND:
		PromptBreakpointCond(info);
		break;

	case ID_DISASM_COPYINSTRUCTIONHEX: {
		char temp[16];
		snprintf(temp, sizeof(temp), "%08x", gstate.cmdmem[info.cmd] & 0x00FFFFFF);
		System_CopyStringToClipboard(temp);
		break;
	}

	case ID_DISASM_COPYINSTRUCTIONDISASM: {
		const bool enabled = info.enableCmd == 0 || (state.cmdmem[info.enableCmd] & 1) == 1;
		const u32 value = state.cmdmem[info.cmd] & 0xFFFFFF;
		const u32 otherValue = state.cmdmem[info.otherCmd] & 0xFFFFFF;
		const u32 otherValue2 = state.cmdmem[info.otherCmd2] & 0xFFFFFF;

		char dest[512];
		FormatStateRow(gpuDebug, dest, sizeof(dest), info.fmt, value, enabled, otherValue, otherValue2);
		System_CopyStringToClipboard(dest);
		break;
	}

	case ID_GEDBG_COPYALL:
		CopyRows(0, GetRowCount());
		break;

	case ID_REGLIST_CHANGE:
		OnDoubleClick(row, STATEVALUES_COL_VALUE);
		break;

	case ID_GEDBG_WATCH:
		ToggleWatchList(cmd);
		SendMessage(GetParent(GetParent(GetHandle())), WM_GEDBG_UPDATE_WATCH, 0, 0);
		break;
	}
}

bool CtrlStateValues::OnRowPrePaint(int row, LPNMLVCUSTOMDRAW msg) {
	if (gpuDebug && RowValuesChanged(row)) {
		msg->clrText = RGB(255, 0, 0);
		return true;
	}
	return false;
}

void CtrlStateValues::SetCmdValue(u32 op) {
	SendMessage(GetParent(GetParent(GetHandle())), WM_GEDBG_SETCMDWPARAM, op, NULL);
	Update();
}

bool CtrlStateValues::RowValuesChanged(int row) {
	_assert_(gpuDebug != nullptr && row >= 0 && row < rowCount_);

	const auto &info = GECmdInfoByCmd(rows_[row]);
	const auto state = gpuDebug->GetGState();
	const auto lastState = GPUStepping::LastState();

	if (state.cmdmem[info.cmd] != lastState.cmdmem[info.cmd])
		return true;
	if (info.otherCmd && state.cmdmem[info.otherCmd] != lastState.cmdmem[info.otherCmd])
		return true;
	if (info.otherCmd2 && state.cmdmem[info.otherCmd2] != lastState.cmdmem[info.otherCmd2])
		return true;

	return false;
}

void CtrlStateValues::PromptBreakpointCond(const GECmdInfo &info) {
	std::string expression;
	GPUBreakpoints::GetCmdBreakpointCond(info.cmd, &expression);
	if (!InputBox_GetString(GetModuleHandle(NULL), GetHandle(), L"Expression", expression, expression))
		return;

	std::string error;
	if (!GPUBreakpoints::SetCmdBreakpointCond(info.cmd, expression, &error)) {
		MessageBox(GetHandle(), ConvertUTF8ToWString(error).c_str(), L"Invalid expression", MB_OK | MB_ICONEXCLAMATION);
	} else {
		if (info.otherCmd)
			GPUBreakpoints::SetCmdBreakpointCond(info.otherCmd, expression, &error);
		if (info.otherCmd2)
			GPUBreakpoints::SetCmdBreakpointCond(info.otherCmd2, expression, &error);
	}

}

TabStateValues::TabStateValues(const GECommand *rows, size_t rowCount, LPCSTR dialogID, HINSTANCE _hInstance, HWND _hParent)
	: Dialog(dialogID, _hInstance, _hParent) {
	values = new CtrlStateValues(rows, (int)rowCount, GetDlgItem(m_hDlg, IDC_GEDBG_VALUES));
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
			SetWindowLongPtr(m_hDlg, DWLP_MSGRESULT, values->HandleNotify(lParam));
			return TRUE;
		}
		break;
	}

	return FALSE;
}

TabStateFlags::TabStateFlags(HINSTANCE _hInstance, HWND _hParent)
	: TabStateValues(g_stateFlagsRows, g_stateFlagsRowsSize, (LPCSTR)IDD_GEDBG_TAB_VALUES, _hInstance, _hParent) {
}

TabStateLighting::TabStateLighting(HINSTANCE _hInstance, HWND _hParent)
	: TabStateValues(g_stateLightingRows, g_stateLightingRowsSize, (LPCSTR)IDD_GEDBG_TAB_VALUES, _hInstance, _hParent) {
}

TabStateSettings::TabStateSettings(HINSTANCE _hInstance, HWND _hParent)
	: TabStateValues(g_stateSettingsRows, g_stateSettingsRowsSize, (LPCSTR)IDD_GEDBG_TAB_VALUES, _hInstance, _hParent) {
}

TabStateTexture::TabStateTexture(HINSTANCE _hInstance, HWND _hParent)
	: TabStateValues(g_stateTextureRows, g_stateTextureRowsSize, (LPCSTR)IDD_GEDBG_TAB_VALUES, _hInstance, _hParent) {
}

TabStateWatch::TabStateWatch(HINSTANCE _hInstance, HWND _hParent)
	: TabStateValues(nullptr, 0, (LPCSTR)IDD_GEDBG_TAB_VALUES, _hInstance, _hParent) {
}

void TabStateWatch::Update() {
	if (watchList.empty()) {
		values->UpdateRows(nullptr, 0);
	} else {
		values->UpdateRows(&watchList[0], (int)watchList.size());
	}
	TabStateValues::Update();
}
