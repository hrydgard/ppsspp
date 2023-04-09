// Copyright (c) 2023- PPSSPP Project.

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

#include <string>
#include "Common/CommonWindows.h"
#include "Common/CommonTypes.h"
#include "Core/Debugger/DebugInterface.h"
#include "Windows/Debugger/Debugger_Lists.h"

class WatchItemWindow {
public:
	WatchItemWindow(HINSTANCE inst, HWND parent, DebugInterface *cpu) : parentHwnd_(parent), cpu_(cpu) {}

	void Init(const std::string &name, const std::string &expression, WatchFormat fmt) {
		name_ = name;
		expression_ = expression;
		format_ = fmt;
	}

	bool Exec();

	const std::string &GetName() const {
		return name_;
	}
	const std::string &GetExpression() const {
		return expression_;
	}
	WatchFormat GetFormat() const {
		return format_;
	}

private:
	static INT_PTR CALLBACK StaticDlgFunc(HWND hWnd, UINT iMsg, WPARAM wParam, LPARAM lParam);
	INT_PTR DlgFunc(HWND hWnd, UINT iMsg, WPARAM wParam, LPARAM lParam);
	bool FetchDialogData(HWND hwnd);

	HWND parentHwnd_;
	DebugInterface *cpu_;

	std::string name_;
	std::string expression_;
	WatchFormat format_ = WatchFormat::HEX;
};
