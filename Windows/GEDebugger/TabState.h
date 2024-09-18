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

#include "Windows/W32Util/DialogManager.h"
#include "Windows/W32Util/Misc.h"

struct TabStateRow;

class CtrlStateValues: public GenericListControl {
public:
	CtrlStateValues(const TabStateRow *rows, int rowCount, HWND hwnd);

	// Used by watch.
	void UpdateRows(const TabStateRow *rows, int rowCount) {
		rows_ = rows;
		rowCount_ = rowCount;
	}

protected:
	bool WindowMessage(UINT msg, WPARAM wParam, LPARAM lParam, LRESULT& returnValue) override {
		return false;
	}
	void GetColumnText(wchar_t* dest, int row, int col) override;
	int GetRowCount() override { return rowCount_; }
	void OnDoubleClick(int row, int column) override;
	void OnRightClick(int row, int column, const POINT& point) override;

	bool ListenRowPrePaint() override { return true; }
	bool OnRowPrePaint(int row, LPNMLVCUSTOMDRAW msg) override;

private:
	bool RowValuesChanged(int row);
	void SetCmdValue(u32 op);
	void PromptBreakpointCond(const TabStateRow &info);

	const TabStateRow *rows_;
	int rowCount_;
};

class TabStateValues : public Dialog {
public:
	TabStateValues(const TabStateRow *rows, int rowCount, LPCSTR dialogID, HINSTANCE _hInstance, HWND _hParent);
	~TabStateValues();

	void Update() override {
		values->Update();
	}

protected:
	BOOL DlgProc(UINT message, WPARAM wParam, LPARAM lParam) override;

	CtrlStateValues *values;

private:
	void UpdateSize(WORD width, WORD height);
};

class TabStateFlags : public TabStateValues {
public:
	TabStateFlags(HINSTANCE _hInstance, HWND _hParent);
};

class TabStateLighting : public TabStateValues {
public:
	TabStateLighting(HINSTANCE _hInstance, HWND _hParent);
};

class TabStateSettings : public TabStateValues {
public:
	TabStateSettings(HINSTANCE _hInstance, HWND _hParent);
};

class TabStateTexture : public TabStateValues {
public:
	TabStateTexture(HINSTANCE _hInstance, HWND _hParent);
};

class TabStateWatch : public TabStateValues {
public:
	TabStateWatch(HINSTANCE _hInstance, HWND _hParent);

	void Update() override;
};
