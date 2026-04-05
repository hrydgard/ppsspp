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
#include "Common/CommonTypes.h"
#include "Common/Data/Encoding/Utf8.h"
#include "Common/StringUtils.h"
#include "Core/System.h"
#include "Windows/resource.h"
#include "Windows/InputBox.h"
#include "Windows/GEDebugger/GEDebugger.h"
#include "Windows/GEDebugger/TabVertices.h"
#include "Windows/W32Util/ContextMenu.h"
#include "GPU/Common/VertexDecoderCommon.h"
#include "GPU/GPUState.h"
#include "GPU/GeDisasm.h"
#include "GPU/Common/GPUDebugInterface.h"
#include "GPU/Debugger/Breakpoints.h"
#include "GPU/Debugger/Stepping.h"
#include "GPU/Debugger/State.h"

static const GenericListViewColumn vertexListCols[] = {
	{ L"X", 0.1f },
	{ L"Y", 0.1f },
	{ L"Z", 0.1f },
	{ L"U", 0.1f },
	{ L"V", 0.1f },
	{ L"Color", 0.1f },
	{ L"NX", 0.1f },
	{ L"NY", 0.1f },
	{ L"NZ", 0.1f },
	// TODO: weight, morph?
};

GenericListViewDef vertexListDef = {
	vertexListCols,	ARRAY_SIZE(vertexListCols),	NULL,	false
};

static const GenericListViewColumn matrixListCols[] = {
	{ L"", 0.03f },
	{ L"Name", 0.21f },
	{ L"0", 0.19f },
	{ L"1", 0.19f },
	{ L"2", 0.19f },
	{ L"3", 0.19f },
};

GenericListViewDef matrixListDef = {
	matrixListCols,	ARRAY_SIZE(matrixListCols),	NULL,	false
};

enum MatrixListCols {
	MATRIXLIST_COL_BREAKPOINT,
	MATRIXLIST_COL_NAME,
	MATRIXLIST_COL_0,
	MATRIXLIST_COL_1,
	MATRIXLIST_COL_2,
	MATRIXLIST_COL_3,

	MATRIXLIST_COL_COUNT,
};

enum MatrixListRows {
	MATRIXLIST_ROW_WORLD_0,
	MATRIXLIST_ROW_WORLD_1,
	MATRIXLIST_ROW_WORLD_2,
	MATRIXLIST_ROW_VIEW_0,
	MATRIXLIST_ROW_VIEW_1,
	MATRIXLIST_ROW_VIEW_2,
	MATRIXLIST_ROW_PROJ_0,
	MATRIXLIST_ROW_PROJ_1,
	MATRIXLIST_ROW_PROJ_2,
	MATRIXLIST_ROW_PROJ_3,
	MATRIXLIST_ROW_TGEN_0,
	MATRIXLIST_ROW_TGEN_1,
	MATRIXLIST_ROW_TGEN_2,
	MATRIXLIST_ROW_BONE_0_0,
	MATRIXLIST_ROW_BONE_0_1,
	MATRIXLIST_ROW_BONE_0_2,
	MATRIXLIST_ROW_BONE_1_0,
	MATRIXLIST_ROW_BONE_1_1,
	MATRIXLIST_ROW_BONE_1_2,
	MATRIXLIST_ROW_BONE_2_0,
	MATRIXLIST_ROW_BONE_2_1,
	MATRIXLIST_ROW_BONE_2_2,
	MATRIXLIST_ROW_BONE_3_0,
	MATRIXLIST_ROW_BONE_3_1,
	MATRIXLIST_ROW_BONE_3_2,
	MATRIXLIST_ROW_BONE_4_0,
	MATRIXLIST_ROW_BONE_4_1,
	MATRIXLIST_ROW_BONE_4_2,
	MATRIXLIST_ROW_BONE_5_0,
	MATRIXLIST_ROW_BONE_5_1,
	MATRIXLIST_ROW_BONE_5_2,
	MATRIXLIST_ROW_BONE_6_0,
	MATRIXLIST_ROW_BONE_6_1,
	MATRIXLIST_ROW_BONE_6_2,
	MATRIXLIST_ROW_BONE_7_0,
	MATRIXLIST_ROW_BONE_7_1,
	MATRIXLIST_ROW_BONE_7_2,

	MATRIXLIST_ROW_COUNT,
};

CtrlVertexList::CtrlVertexList(HWND hwnd)
	: GenericListControl(hwnd, vertexListDef), raw_(false) {
	decoder = new VertexDecoder();
	Update();
}

CtrlVertexList::~CtrlVertexList() {
	delete decoder;
}

void CtrlVertexList::GetColumnText(wchar_t *dest, size_t destSize, int row, int col) {
	if (row < 0 || row >= rowCount_ ) {
		wcscpy(dest, L"Invalid");
		return;
	}

	if (!indices.empty()) {
		if (row >= (int)indices.size()) {
			swprintf(dest, destSize, L"Invalid index %d", row);
			return;
		}
		row = indices[row];
	}

	char temp[256];
	if (raw_) {
		FormatVertColRaw(decoder, temp, sizeof(temp), row, col);
	} else {
		if (row >= (int)vertices.size()) {
			swprintf(dest, destSize, L"Invalid vertex %d", row);
			return;
		}

		FormatVertCol(temp, sizeof(temp), vertices[row], col);
	}
	ConvertUTF8ToWString(dest, destSize, temp);
}

int CtrlVertexList::GetRowCount() {
	auto memLock = Memory::Lock();
	if (!PSP_IsInited()) {
		return 0;
	}

	if (!gpuDebug || !Memory::IsValidAddress(gpuDebug->GetVertexAddress())) {
		rowCount_ = 0;
		return rowCount_;
	}

	// TODO: Maybe there are smarter ways?  Also, is this the best place to recalc?
	auto state = gpuDebug->GetGState();

	rowCount_ = gpuDebug->GetCurrentPrimCount();
	if (!gpuDebug->GetCurrentDrawAsDebugVertices(rowCount_, vertices, indices)) {
		rowCount_ = 0;
	}
	VertexDecoderOptions options{};
	// TODO: Maybe an option?
	u32 vertTypeID = GetVertTypeID(state.vertType, state.getUVGenMode(), true);
	decoder->SetVertexType(vertTypeID, options);
	return rowCount_;
}

TabVertices::TabVertices(HINSTANCE _hInstance, HWND _hParent)
	: Dialog((LPCSTR)IDD_GEDBG_TAB_VERTICES, _hInstance, _hParent) {
	values = new CtrlVertexList(GetDlgItem(m_hDlg, IDC_GEDBG_VERTICES));
}

TabVertices::~TabVertices() {
	delete values;
}

void TabVertices::UpdateSize(WORD width, WORD height) {
	struct Position {
		int x,y;
		int w,h;
	};

	Position position;
	static const int borderMargin = 5;
	static const int checkboxSpace = 22;

	position.x = borderMargin;
	position.y = borderMargin + checkboxSpace;
	position.w = width - 2 * borderMargin;
	position.h = height - 2 * borderMargin - checkboxSpace;

	HWND handle = GetDlgItem(m_hDlg, IDC_GEDBG_VERTICES);
	MoveWindow(handle, position.x, position.y, position.w, position.h, TRUE);
}

BOOL TabVertices::DlgProc(UINT message, WPARAM wParam, LPARAM lParam) {
	switch (message) {
	case WM_INITDIALOG:
		return TRUE;

	case WM_SIZE:
		UpdateSize(LOWORD(lParam), HIWORD(lParam));
		return TRUE;

	case WM_COMMAND:
		if (LOWORD(wParam) == IDC_GEDBG_RAWVERTS) {
			values->SetRaw(IsDlgButtonChecked(m_hDlg, IDC_GEDBG_RAWVERTS) == BST_CHECKED);
			values->Update();
		}
		return TRUE;

	case WM_NOTIFY:
		switch (wParam)
		{
		case IDC_GEDBG_VERTICES:
			SetWindowLongPtr(m_hDlg, DWLP_MSGRESULT, values->HandleNotify(lParam));
			return TRUE;
		}
		break;
	}

	return FALSE;
}

CtrlMatrixList::CtrlMatrixList(HWND hwnd)
	: GenericListControl(hwnd, matrixListDef) {
	SetIconList(12, 12, { (HICON)LoadIcon(GetModuleHandle(nullptr), (LPCWSTR)IDI_BREAKPOINT_SMALL) });
	Update();
}

bool CtrlMatrixList::OnColPrePaint(int row, int col, LPNMLVCUSTOMDRAW msg) {
	const auto state = gpuDebug->GetGState();
	const auto lastState = GPUStepping::LastState();

	bool changed = false;
	if (col < MATRIXLIST_COL_0) {
		for (int c = MATRIXLIST_COL_0; c <= MATRIXLIST_COL_3; ++c) {
			changed = changed || ColChanged(lastState, state, row, c);
		}
	} else {
		changed = ColChanged(lastState, state, row, col);
	}

	// At the column level, we have to reset the color back.
	static int lastRow = -1;
	static COLORREF rowDefaultText;
	if (lastRow != row) {
		rowDefaultText = msg->clrText;
		lastRow = row;
	}

	if (changed) {
		msg->clrText = RGB(255, 0, 0);
		return true;
	} else if (msg->clrText != rowDefaultText) {
		msg->clrText = rowDefaultText;
		return true;
	}

	return false;
}

bool CtrlMatrixList::ColChanged(const GPUgstate &lastState, const GPUgstate &state, int row, int col) {
	union {
		float f;
		uint32_t u;
	} newVal, oldVal;
	if (!GetValue(state, row, col, newVal.f) || !GetValue(lastState, row, col, oldVal.f))
		return false;

	// If there's any difference in bits, highlight.
	return newVal.u != oldVal.u;
}

bool CtrlMatrixList::GetValue(const GPUgstate &state, int row, int col, float &val) {
	if (!gpuDebug || row < 0 || row >= MATRIXLIST_ROW_COUNT || col < 0 || col >= MATRIXLIST_COL_COUNT)
		return false;

	if (col < MATRIXLIST_COL_0)
		col = MATRIXLIST_COL_0;

	if (row >= MATRIXLIST_ROW_BONE_0_0) {
		int b = (row - MATRIXLIST_ROW_BONE_0_0) / 3;
		int r = (row - MATRIXLIST_ROW_BONE_0_0) % 3;
		int offset = b * 12 + r + (col - MATRIXLIST_COL_0) * 3;

		val = state.boneMatrix[offset];
		return true;
	} else if (row >= MATRIXLIST_ROW_TGEN_0) {
		int r = row - MATRIXLIST_ROW_TGEN_0;
		int offset = r + (col - MATRIXLIST_COL_0) * 3;

		val = state.tgenMatrix[offset];
		return true;
	} else if (row >= MATRIXLIST_ROW_PROJ_0) {
		int r = row - MATRIXLIST_ROW_PROJ_0;
		int offset = r + (col - MATRIXLIST_COL_0) * 4;

		val = state.projMatrix[offset];
		return true;
	} else if (row >= MATRIXLIST_ROW_VIEW_0) {
		int r = row - MATRIXLIST_ROW_VIEW_0;
		int offset = r + (col - MATRIXLIST_COL_0) * 3;

		val = state.viewMatrix[offset];
		return true;
	}

	int r = row - MATRIXLIST_ROW_WORLD_0;
	int offset = r + (col - MATRIXLIST_COL_0) * 3;

	val = state.worldMatrix[offset];
	return true;
}

void CtrlMatrixList::GetColumnText(wchar_t *dest, size_t destSize, int row, int col) {
	if (col == MATRIXLIST_COL_BREAKPOINT) {
		wcscpy(dest, L" ");
		return;
	}

	float val;
	if (!GetValue(gpuDebug->GetGState(), row, col, val)) {
		wcscpy(dest, L"Invalid");
		return;
	}

	if (row >= MATRIXLIST_ROW_BONE_0_0) {
		int b = (row - MATRIXLIST_ROW_BONE_0_0) / 3;
		int r = (row - MATRIXLIST_ROW_BONE_0_0) % 3;
		switch (col) {
		case MATRIXLIST_COL_NAME:
			swprintf(dest, 255, L"Bone #%d row %d", b, r);
			break;

		default:
			swprintf(dest, 255, L"%f", val);
			break;
		}
	} else if (row >= MATRIXLIST_ROW_TGEN_0) {
		int r = row - MATRIXLIST_ROW_TGEN_0;
		switch (col) {
		case MATRIXLIST_COL_NAME:
			swprintf(dest, 255, L"Texgen %d", r);
			break;

		default:
			swprintf(dest, 255, L"%f", val);
			break;
		}
	} else if (row >= MATRIXLIST_ROW_PROJ_0) {
		int r = row - MATRIXLIST_ROW_PROJ_0;
		switch (col) {
		case MATRIXLIST_COL_NAME:
			swprintf(dest, 255, L"Proj %d", r);
			break;

		default:
			swprintf(dest, 255, L"%f", val);
			break;
		}
	} else if (row >= MATRIXLIST_ROW_VIEW_0) {
		int r = row - MATRIXLIST_ROW_VIEW_0;
		switch (col) {
		case MATRIXLIST_COL_NAME:
			swprintf(dest, 255, L"View %d", r);
			break;

		default:
			swprintf(dest, 255, L"%f", val);
			break;
		}
	} else {
		int r = row - MATRIXLIST_ROW_WORLD_0;
		switch (col) {
		case MATRIXLIST_COL_NAME:
			swprintf(dest, 255, L"World %d", r);
			break;

		default:
			swprintf(dest, 255, L"%f", val);
			break;
		}
	}
}

int CtrlMatrixList::GetRowCount() {
	if (!gpuDebug) {
		return 0;
	}

	return MATRIXLIST_ROW_COUNT;
}

struct MatrixCmdPair {
	MatrixListRows row;
	GECommand numCmd;
	GECommand cmd;
};
static constexpr MatrixCmdPair matrixCmds[] = {
	{ MATRIXLIST_ROW_WORLD_0, GE_CMD_WORLDMATRIXNUMBER, GE_CMD_WORLDMATRIXDATA },
	{ MATRIXLIST_ROW_VIEW_0, GE_CMD_VIEWMATRIXNUMBER, GE_CMD_VIEWMATRIXDATA },
	{ MATRIXLIST_ROW_PROJ_0, GE_CMD_PROJMATRIXNUMBER, GE_CMD_PROJMATRIXDATA },
	{ MATRIXLIST_ROW_TGEN_0, GE_CMD_TGENMATRIXNUMBER, GE_CMD_TGENMATRIXDATA },
	{ MATRIXLIST_ROW_BONE_0_0, GE_CMD_BONEMATRIXNUMBER, GE_CMD_BONEMATRIXDATA },
	{ MATRIXLIST_ROW_COUNT, GE_CMD_NOP },
};

static const MatrixCmdPair *FindCmdPair(int row) {
	for (int i = 0; i < ARRAY_SIZE(matrixCmds) - 1; ++i) {
		if (row < matrixCmds[i].row || row >= matrixCmds[i + 1].row)
			continue;

		return &matrixCmds[i];
	}
	return nullptr;
}

void CtrlMatrixList::ToggleBreakpoint(int row) {
	const MatrixCmdPair *info = FindCmdPair(row);
	if (!info)
		return;

	// Okay, this command is in range.  Toggle the actual breakpoint.
	bool state = !gpuDebug->GetBreakpoints()->IsCmdBreakpoint(info->cmd);
	if (state) {
		gpuDebug->GetBreakpoints()->AddCmdBreakpoint(info->cmd);
	} else {
		if (gpuDebug->GetBreakpoints()->GetCmdBreakpointCond(info->cmd, nullptr)) {
			int ret = MessageBox(GetHandle(), L"This breakpoint has a custom condition.\nDo you want to remove it?", L"Confirmation", MB_YESNO);
			if (ret != IDYES)
				return;
		}
		gpuDebug->GetBreakpoints()->RemoveCmdBreakpoint(info->cmd);
	}

	for (int r = info->row; r < (info + 1)->row; ++r) {
		SetItemState(r, state ? 1 : 0);
	}
}

void CtrlMatrixList::PromptBreakpointCond(int row) {
	const MatrixCmdPair *info = FindCmdPair(row);
	if (!info)
		return;

	std::string expression;
	gpuDebug->GetBreakpoints()->GetCmdBreakpointCond(info->cmd, &expression);
	if (!InputBox_GetString(GetModuleHandle(NULL), GetHandle(), L"Expression", expression, expression))
		return;

	std::string error;
	if (!gpuDebug->GetBreakpoints()->SetCmdBreakpointCond(info->cmd, expression, &error))
		MessageBox(GetHandle(), ConvertUTF8ToWString(error).c_str(), L"Invalid expression", MB_OK | MB_ICONEXCLAMATION);
}

void CtrlMatrixList::OnDoubleClick(int row, int column) {
	if (row >= GetRowCount())
		return;

	if (column == MATRIXLIST_COL_BREAKPOINT) {
		ToggleBreakpoint(row);
		return;
	}

	float val;
	if (!GetValue(gpuDebug->GetGState(), row, column, val))
		return;

	std::string strvalue = StringFromFormat("%f", val);
	bool res = InputBox_GetString(GetModuleHandle(NULL), GetHandle(), L"Column value", strvalue, strvalue);
	if (!res)
		return;

	if (sscanf(strvalue.c_str(), "%f", &val) == 1) {
		auto prevState = gpuDebug->GetGState();
		auto setCmdValue = [&](u32 op) {
			SendMessage(GetParent(GetParent(GetHandle())), WM_GEDBG_SETCMDWPARAM, op, 0);
		};

		union {
			float f;
			u32 u;
		} temp = { val };

		for (int i = 0; i < ARRAY_SIZE(matrixCmds) - 1; ++i) {
			if (row < matrixCmds[i].row || row >= matrixCmds[i + 1].row)
				continue;

			// Everything is 3 except the projection matrix, which is 4.
			int sz = matrixCmds[i + 1].row - matrixCmds[i].row == 4 ? 4 : 3;
			// Always zero except for bones.
			int b = (row - matrixCmds[i].row) / sz;
			int r = (row - matrixCmds[i].row) % sz;
			int c = column >= MATRIXLIST_COL_0 ? column - MATRIXLIST_COL_0 : 0;

			// Okay, now set the number, then data.
			int n = b * 12 + r + c * sz;
			setCmdValue((matrixCmds[i].numCmd << 24) | n);
			setCmdValue((matrixCmds[i].cmd << 24) | (temp.u >> 8));

			// Done, revert the number.
			setCmdValue(prevState.cmdmem[matrixCmds[i].numCmd]);
			Update();
		}
	}
}

void CtrlMatrixList::OnRightClick(int row, int column, const POINT &point) {
	if (row >= GetRowCount())
		return;
	const MatrixCmdPair *info = FindCmdPair(row);

	POINT screenPt(point);
	ClientToScreen(GetHandle(), &screenPt);

	HMENU subMenu = GetContextMenu(ContextMenuID::GEDBG_MATRIX);
	SetMenuDefaultItem(subMenu, ID_REGLIST_CHANGE, FALSE);
	EnableMenuItem(subMenu, ID_GEDBG_SETCOND, info && gpuDebug->GetBreakpoints()->IsCmdBreakpoint(info->cmd) ? MF_ENABLED : MF_GRAYED);

	switch (TriggerContextMenu(ContextMenuID::GEDBG_MATRIX, GetHandle(), ContextPoint::FromClient(point))) {
	case ID_DISASM_TOGGLEBREAKPOINT:
		ToggleBreakpoint(row);
		break;

	case ID_GEDBG_SETCOND:
		PromptBreakpointCond(row);
		break;

	case ID_DISASM_COPYINSTRUCTIONDISASM:
	{
		// Not really copy instruction, more like copy a float.
		float val;
		if (GetValue(gpuDebug->GetGState(), row, column, val)) {
			char dest[128];
			snprintf(dest, sizeof(dest), "%f", val);
			W32Util::CopyTextToClipboard(GetHandle(), dest);
		}
		break;
	}

	case ID_GEDBG_COPYALL:
		CopyRows(0, GetRowCount());
		break;

	case ID_REGLIST_CHANGE:
		OnDoubleClick(row, MATRIXLIST_COL_0);
		break;
	}
}

TabMatrices::TabMatrices(HINSTANCE _hInstance, HWND _hParent)
	: Dialog((LPCSTR)IDD_GEDBG_TAB_MATRICES, _hInstance, _hParent) {
	values = new CtrlMatrixList(GetDlgItem(m_hDlg, IDC_GEDBG_MATRICES));
}

TabMatrices::~TabMatrices() {
	delete values;
}

void TabMatrices::UpdateSize(WORD width, WORD height) {
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

	HWND handle = GetDlgItem(m_hDlg, IDC_GEDBG_MATRICES);
	MoveWindow(handle, position.x, position.y, position.w, position.h, TRUE);
}

BOOL TabMatrices::DlgProc(UINT message, WPARAM wParam, LPARAM lParam) {
	switch (message) {
	case WM_INITDIALOG:
		return TRUE;

	case WM_SIZE:
		UpdateSize(LOWORD(lParam), HIWORD(lParam));
		return TRUE;

	case WM_NOTIFY:
		switch (wParam)
		{
		case IDC_GEDBG_MATRICES:
			SetWindowLongPtr(m_hDlg, DWLP_MSGRESULT, values->HandleNotify(lParam));
			return TRUE;
		}
		break;
	}

	return FALSE;
}
