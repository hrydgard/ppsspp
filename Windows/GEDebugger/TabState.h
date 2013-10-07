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

protected:
	virtual bool WindowMessage(UINT msg, WPARAM wParam, LPARAM lParam, LRESULT& returnValue) { return false; };
	virtual void GetColumnText(wchar_t* dest, int row, int col);
	virtual int GetRowCount() { return rowCount_; }
	virtual void OnDoubleClick(int row, int column);
	virtual void OnRightClick(int row, int column, const POINT& point);

private:
	void SetCmdValue(u32 op);

	const TabStateRow *rows_;
	int rowCount_;
};

class TabStateValues : public Dialog {
public:
	TabStateValues(const TabStateRow *rows, int rowCount, LPCSTR dialogID, HINSTANCE _hInstance, HWND _hParent);
	~TabStateValues();

	virtual void Update() {
		values->Update();
	}

protected:
	BOOL DlgProc(UINT message, WPARAM wParam, LPARAM lParam);

private:
	void UpdateSize(WORD width, WORD height);

	CtrlStateValues *values;
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
