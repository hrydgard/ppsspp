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
#include "Core/System.h"
#include "Windows/resource.h"
#include "Windows/GEDebugger/GEDebugger.h"
#include "Windows/GEDebugger/TabVertices.h"
#include "GPU/Common/VertexDecoderCommon.h"
#include "GPU/GPUState.h"
#include "GPU/GeDisasm.h"
#include "GPU/Common/GPUDebugInterface.h"

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

enum VertexListCols {
	VERTEXLIST_COL_X,
	VERTEXLIST_COL_Y,
	VERTEXLIST_COL_Z,
	VERTEXLIST_COL_U,
	VERTEXLIST_COL_V,
	VERTEXLIST_COL_COLOR,
	VERTEXLIST_COL_NX,
	VERTEXLIST_COL_NY,
	VERTEXLIST_COL_NZ,
};

static const GenericListViewColumn matrixListCols[] = {
	{ L"Name", 0.24f },
	{ L"0", 0.19f },
	{ L"1", 0.19f },
	{ L"2", 0.19f },
	{ L"3", 0.19f },
};

GenericListViewDef matrixListDef = {
	matrixListCols,	ARRAY_SIZE(matrixListCols),	NULL,	false
};

enum MatrixListCols {
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

void CtrlVertexList::GetColumnText(wchar_t *dest, int row, int col) {
	if (row < 0 || row >= rowCount_ ) {
		wcscpy(dest, L"Invalid");
		return;
	}

	if (!indices.empty()) {
		if (row >= (int)indices.size()) {
			swprintf(dest, L"Invalid indice %d", row);
			return;
		}
		row = indices[row];
	}

	if (raw_) {
		FormatVertColRaw(dest, row, col);
	} else {
		if (row >= (int)vertices.size()) {
			swprintf(dest, L"Invalid vertex %d", row);
			return;
		}

		FormatVertCol(dest, vertices[row], col);
	}
}

void CtrlVertexList::FormatVertCol(wchar_t *dest, const GPUDebugVertex &vert, int col) {
	switch (col) {
	case VERTEXLIST_COL_X: swprintf(dest, L"%f", vert.x); break;
	case VERTEXLIST_COL_Y: swprintf(dest, L"%f", vert.y); break;
	case VERTEXLIST_COL_Z: swprintf(dest, L"%f", vert.z); break;
	case VERTEXLIST_COL_U: swprintf(dest, L"%f", vert.u); break;
	case VERTEXLIST_COL_V: swprintf(dest, L"%f", vert.v); break;
	case VERTEXLIST_COL_COLOR:
		swprintf(dest, L"%02x%02x%02x%02x", vert.c[0], vert.c[1], vert.c[2], vert.c[3]);
		break;
	case VERTEXLIST_COL_NX: swprintf(dest, L"%f", vert.nx); break;
	case VERTEXLIST_COL_NY: swprintf(dest, L"%f", vert.ny); break;
	case VERTEXLIST_COL_NZ: swprintf(dest, L"%f", vert.nz); break;

	default:
		wcscpy(dest, L"Invalid");
		break;
	}
}

void CtrlVertexList::FormatVertColRaw(wchar_t *dest, int row, int col) {
	auto memLock = Memory::Lock();
	if (!PSP_IsInited()) {
		wcscpy(dest, L"Invalid");
		return;
	}

	// We could use the vertex decoder and reader, but those already do some minor adjustments.
	// There's only a few values - let's just go after them directly.
	const u8 *vert = Memory::GetPointer(gpuDebug->GetVertexAddress()) + row * decoder->size;
	const u8 *pos = vert + decoder->posoff;
	const u8 *tc = vert + decoder->tcoff;
	const u8 *color = vert + decoder->coloff;

	switch (col) {
	case VERTEXLIST_COL_X:
		FormatVertColRawType(dest, pos, decoder->pos, 0);
		break;
	case VERTEXLIST_COL_Y:
		FormatVertColRawType(dest, pos, decoder->pos, 1);
		break;
	case VERTEXLIST_COL_Z:
		FormatVertColRawType(dest, pos, decoder->pos, 2);
		break;
	case VERTEXLIST_COL_U:
		FormatVertColRawType(dest, tc, decoder->tc, 0);
		break;
	case VERTEXLIST_COL_V:
		FormatVertColRawType(dest, tc, decoder->tc, 1);
		break;
	case VERTEXLIST_COL_COLOR:
		FormatVertColRawColor(dest, color, decoder->col);
		break;

	default:
		wcscpy(dest, L"Invalid");
		break;
	}
}

void CtrlVertexList::FormatVertColRawType(wchar_t *dest, const void *data, int type, int offset) {
	switch (type) {
	case 0:
		wcscpy(dest, L"-");
		break;

	case 1: // 8-bit
		swprintf(dest, L"%02x", ((const u8 *)data)[offset]);
		break;

	case 2: // 16-bit
		swprintf(dest, L"%04x", ((const u16_le *)data)[offset]);
		break;

	case 3: // float
		swprintf(dest, L"%f", ((const float *)data)[offset]);
		break;

	default:
		wcscpy(dest, L"Invalid");
		break;
	}
}

void CtrlVertexList::FormatVertColRawColor(wchar_t *dest, const void *data, int type) {
	switch (type) {
	case GE_VTYPE_COL_NONE >> GE_VTYPE_COL_SHIFT:
		wcscpy(dest, L"-");
		break;

	case GE_VTYPE_COL_565 >> GE_VTYPE_COL_SHIFT:
	case GE_VTYPE_COL_5551 >> GE_VTYPE_COL_SHIFT:
	case GE_VTYPE_COL_4444 >> GE_VTYPE_COL_SHIFT:
		swprintf(dest, L"%04x", *(const u16_le *)data);
		break;

	case GE_VTYPE_COL_8888 >> GE_VTYPE_COL_SHIFT:
		swprintf(dest, L"%08x", *(const u32_le *)data);
		break;

	default:
		wcscpy(dest, L"Invalid");
		break;
	}
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
	rowCount_ = state.prim & 0xFFFF;

	// Override if we're on a prim command.
	DisplayList list;
	if (gpuDebug->GetCurrentDisplayList(list)) {
		u32 cmd = Memory::Read_U32(list.pc);
		if ((cmd >> 24) == GE_CMD_PRIM) {
			rowCount_ = cmd & 0xFFFF;
		} else if ((cmd >> 24) == GE_CMD_BEZIER || (cmd >> 24) == GE_CMD_SPLINE) {
			u32 u = (cmd & 0x00FF) >> 0;
			u32 v = (cmd & 0xFF00) >> 8;
			rowCount_ = u * v;
		}
	}

	if (!gpuDebug->GetCurrentSimpleVertices(rowCount_, vertices, indices)) {
		rowCount_ = 0;
	}
	VertexDecoderOptions options{};
	decoder->SetVertexType(state.vertType, options);
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
			values->HandleNotify(lParam);
			break;
		}
		break;
	}

	return FALSE;
}

CtrlMatrixList::CtrlMatrixList(HWND hwnd)
	: GenericListControl(hwnd, matrixListDef) {
	Update();
}

void CtrlMatrixList::GetColumnText(wchar_t *dest, int row, int col) {
	if (!gpuDebug || row < 0 || row >= MATRIXLIST_ROW_COUNT || col < 0 || col >= MATRIXLIST_COL_COUNT) {
		wcscpy(dest, L"Invalid");
		return;
	}

	auto state = gpuDebug->GetGState();

	if (row >= MATRIXLIST_ROW_BONE_0_0) {
		int b = (row - MATRIXLIST_ROW_BONE_0_0) / 3;
		int r = (row - MATRIXLIST_ROW_BONE_0_0) % 3;
		int offset = (row - MATRIXLIST_ROW_BONE_0_0) * 4 + col - 1;

		switch (col) {
		case MATRIXLIST_COL_NAME:
			swprintf(dest, L"Bone #%d row %d", b, r);
			break;

		default:
			swprintf(dest, L"%f", state.boneMatrix[offset]);
			break;
		}
	} else if (row >= MATRIXLIST_ROW_TGEN_0) {
		int r = row - MATRIXLIST_ROW_TGEN_0;
		int offset = r * 4 + col - 1;

		switch (col) {
		case MATRIXLIST_COL_NAME:
			swprintf(dest, L"Texgen %d", r);
			break;

		default:
			swprintf(dest, L"%f", state.tgenMatrix[offset]);
			break;
		}
	} else if (row >= MATRIXLIST_ROW_PROJ_0) {
		int r = row - MATRIXLIST_ROW_PROJ_0;
		int offset = r * 4 + col - 1;

		switch (col) {
		case MATRIXLIST_COL_NAME:
			swprintf(dest, L"Proj %d", r);
			break;

		default:
			swprintf(dest, L"%f", state.projMatrix[offset]);
			break;
		}
	} else if (row >= MATRIXLIST_ROW_VIEW_0) {
		int r = row - MATRIXLIST_ROW_VIEW_0;
		int offset = r * 4 + col - 1;

		switch (col) {
		case MATRIXLIST_COL_NAME:
			swprintf(dest, L"View %d", r);
			break;

		default:
			swprintf(dest, L"%f", state.viewMatrix[offset]);
			break;
		}
	} else {
		int r = row - MATRIXLIST_ROW_WORLD_0;
		int offset = r * 4 + col - 1;

		switch (col) {
		case MATRIXLIST_COL_NAME:
			swprintf(dest, L"World %d", r);
			break;

		default:
			swprintf(dest, L"%f", state.worldMatrix[offset]);
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
			values->HandleNotify(lParam);
			break;
		}
		break;
	}

	return FALSE;
}
