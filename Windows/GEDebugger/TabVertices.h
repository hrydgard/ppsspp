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

#pragma once

#include <vector>
#include "GPU/Common/GPUDebugInterface.h"
#include "Windows/W32Util/DialogManager.h"
#include "Windows/W32Util/Misc.h"

class VertexDecoder;

class CtrlVertexList: public GenericListControl {
public:
	CtrlVertexList(HWND hwnd);
	~CtrlVertexList();

	void SetRaw(bool raw) {
		raw_ = raw;
		Update();
	}

protected:
	bool WindowMessage(UINT msg, WPARAM wParam, LPARAM lParam, LRESULT &returnValue) override { return false; }
	void GetColumnText(wchar_t *dest, int row, int col) override;
	int GetRowCount() override;

private:
	void FormatVertCol(wchar_t *dest, const GPUDebugVertex &vert, int col);
	void FormatVertColRaw(wchar_t *dest, int row, int col);
	void FormatVertColRawType(wchar_t *dest, const void *data, int type, int offset);
	void FormatVertColRawColor(wchar_t *dest, const void *data, int type);

	std::vector<GPUDebugVertex> vertices;
	std::vector<u16> indices;
	int rowCount_;
	bool raw_;
	VertexDecoder *decoder;
};

class TabVertices : public Dialog {
public:
	TabVertices(HINSTANCE _hInstance, HWND _hParent);
	~TabVertices();

	void Update() override {
		values->Update();
	}

protected:
	BOOL DlgProc(UINT message, WPARAM wParam, LPARAM lParam) override;

private:
	void UpdateSize(WORD width, WORD height);

	CtrlVertexList *values;
};

class CtrlMatrixList: public GenericListControl {
public:
	CtrlMatrixList(HWND hwnd);

protected:
	bool WindowMessage(UINT msg, WPARAM wParam, LPARAM lParam, LRESULT &returnValue) override {
		return false;
	}
	void GetColumnText(wchar_t *dest, int row, int col) override;
	int GetRowCount() override;
	void OnDoubleClick(int row, int column) override;
	void OnRightClick(int row, int column, const POINT &point) override;

	bool ListenColPrePaint() override { return true; }
	bool OnColPrePaint(int row, int col, LPNMLVCUSTOMDRAW msg) override;

private:
	bool GetValue(const GPUgstate &state, int row, int col, float &val);
	bool ColChanged(const GPUgstate &lastState, const GPUgstate &state, int row, int col);
	void ToggleBreakpoint(int row);
	void PromptBreakpointCond(int row);
};

class TabMatrices : public Dialog {
public:
	TabMatrices(HINSTANCE _hInstance, HWND _hParent);
	~TabMatrices();

	void Update() override {
		values->Update();
	}

protected:
	BOOL DlgProc(UINT message, WPARAM wParam, LPARAM lParam) override;

private:
	void UpdateSize(WORD width, WORD height);

	CtrlMatrixList *values;
};
